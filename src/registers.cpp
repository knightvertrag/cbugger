#include <libcbg/registers.hpp>
#include <libcbg/error.hpp>
#include <spdlog/spdlog.h>
#include <sys/ptrace.h>
#include <asm/ptrace.h>
#include <elf.h>

using namespace cbg;

namespace
{
    size_t calc_debug_size(size_t dbg_info)
    {
        unsigned num_slots = dbg_info & 0xff;
        size_t header = offsetof(struct user_hwdebug_state, dbg_regs);
        size_t entry = sizeof(user_hwdebug_state::dbg_regs[0]);
        return header + num_slots * entry;
    }

    int is_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    int parse_index(std::string_view sv)
    {
        if (sv.empty())
            return -1;
        int v = 0;
        for (char c : sv)
        {
            if (!is_digit(c))
                return -1;
            v = v * 10 + (c - '0');
        }
        return v;
    }
}

Registers::Registers(pid_t pid) : pid(pid)
{
    build_views();
}

void Registers::load()
{
    spdlog::debug("Loading registers for PID {}", pid);

    struct iovec iov;
    iov.iov_base = &gpr;
    iov.iov_len = sizeof(gpr);
    if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &iov) == -1)
        Error::send_errno("Failed to get GPRs");

    iov.iov_base = &fpr;
    iov.iov_len = sizeof(fpr);
    if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_FPREGSET, &iov) == -1)
        Error::send_errno("Failed to get FPRs");

    iov.iov_base = &hw_break;
    iov.iov_len = sizeof(hw_break);
    if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_ARM_HW_BREAK, &iov) == -1)
        Error::send_errno("Failed to get HW breakpoints");

    break_debug_size = calc_debug_size(hw_break.dbg_info);

    iov.iov_base = &hw_watch;
    iov.iov_len = sizeof(hw_watch);
    if (ptrace(PTRACE_GETREGSET, pid, (void *)NT_ARM_HW_WATCH, &iov) == -1)
        Error::send_errno("Failed to get HW watchpoints");

    watch_debug_size = calc_debug_size(hw_watch.dbg_info);
}

void Registers::save()
{
    spdlog::debug("Saving registers for PID {}", pid);

    struct iovec iov;
    iov.iov_base = &gpr;
    iov.iov_len = sizeof(gpr);
    if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_PRSTATUS, &iov) == -1)
        Error::send_errno("Failed to set GPRs");

    iov.iov_base = &fpr;
    iov.iov_len = sizeof(fpr);
    if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_FPREGSET, &iov) == -1)
        Error::send_errno("Failed to set FPRs");

    iov.iov_base = &hw_break;
    iov.iov_len = break_debug_size;
    if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_ARM_HW_BREAK, &iov) == -1)
        Error::send_errno("Failed to set HW breakpoints");

    iov.iov_base = &hw_watch;
    iov.iov_len = watch_debug_size;
    if (ptrace(PTRACE_SETREGSET, pid, (void *)NT_ARM_HW_WATCH, &iov) == -1)
        Error::send_errno("Failed to set HW watchpoints");
}

void Registers::build_views()
{
    views.clear();

#define ADD_GPR(name, struct_name, field, fmt, dwarf_id) \
    views.push_back({name, &struct_name.field, &struct_name.field, sizeof(struct_name.field), RegisterFormat::fmt, dwarf_id});

#define ADD_FPR(name, struct_name, field, fmt, dwarf_id) \
    views.push_back({name, &struct_name.field, &struct_name.field, sizeof(struct_name.field), RegisterFormat::fmt, dwarf_id});

#define ADD_HWBRK(name, struct_name, field, fmt, dwarf_id) \
    views.push_back({name, &struct_name.field, &struct_name.field, sizeof(struct_name.field), RegisterFormat::fmt, dwarf_id});

#define ADD_HWWATCH(name, struct_name, field, fmt, dwarf_id) \
    views.push_back({name, &struct_name.field, &struct_name.field, sizeof(struct_name.field), RegisterFormat::fmt, dwarf_id});

    REGISTER_LIST(ADD_GPR, ADD_FPR, ADD_HWBRK, ADD_HWWATCH)

#undef ADD_GPR
#undef ADD_FPR
#undef ADD_HWBRK
#undef ADD_HWWATCH
}

const RegisterView &Registers::get_register(const std::string &name) const
{
    for (auto &r : views)
    {
        if (r.name == name)
            return r;
    }
    Error::send_errno("Register not found: " + name);
}

RegisterView &Registers::get_register(const std::string &name)
{
    for (auto &r : views)
    {
        if (r.name == name)
            return r;
    }
    Error::send_errno("Register not found: " + name);
}

void Registers::set_register(const std::string &name, uint64_t value)
{
    spdlog::debug("Setting register {} to value {:#x}", name, value);
    RegisterView &reg = get_register(name);
    switch (reg.size)
    {
    case 1:
        reg.set<uint8_t>(static_cast<uint8_t>(value));
        break;
    case 2:
        reg.set<uint16_t>(static_cast<uint16_t>(value));
        break;
    case 4:
        reg.set<uint32_t>(static_cast<uint32_t>(value));
        break;
    case 8:
        reg.set<uint64_t>(value);
        break;
    case 16:
    {
        __uint128_t wide = value;
        reg.set<__uint128_t>(static_cast<__uint128_t>(value));
        break;
    }

    default:
        Error::send("Unsupported register size for: " + name);
    }
}

std::optional<SubregisterView> Registers::make_subview_by_name(std::string_view name, bool zero_upper_fp)
{
    if (name.empty())
        return std::nullopt;

    // wN => lower 32 bits of xN (N=0..30). Always uses ZeroExtend32To64.
    if (name.size() >= 2 && name[0] == 'w' && is_digit(name[1]))
    {
        int n = parse_index(name.substr(1));
        if (n >= 0 && n <= 30)
            return make_wn(static_cast<uint8_t>(n), gpr);
        return std::nullopt;
    }

    // sN/dN => scalar FP views of vN. Use zero_upper_fp (default true) for correct scalar write semantics.
    if (name.size() >= 2 && (name[0] == 's' || name[0] == 'd') && is_digit(name[1]))
    {
        int n = parse_index(name.substr(1));
        if (n >= 0 && n <= 31)
        {
            if (name[0] == 's')
                return make_sn(static_cast<uint8_t>(n), fpr, zero_upper_fp);
            else
                return make_dn(static_cast<uint8_t>(n), fpr, zero_upper_fp);
        }
        return std::nullopt;
    }

    // vN.4s[lane] or vN.2d[lane]  (lanes 0-based, bounds checked in factories)
    if (name.size() >= 2 && name[0] == 'v' && is_digit(name[1]))
    {
        size_t p = 1;
        while (p < name.size() && is_digit(name[p]))
            ++p;

        int n = parse_index(name.substr(1, p - 1));
        if (n < 0 || n > 31)
            return std::nullopt;

        if (p >= name.size() || name[p] != '.')
            return std::nullopt;
        ++p;

        bool is_4s = false;
        if (p + 1 < name.size() && name[p] == '4' && name[p + 1] == 's')
        {
            is_4s = true;
            p += 2;
        }
        else if (p + 1 < name.size() && name[p] == '2' && name[p + 1] == 'd')
        {
            is_4s = false;
            p += 2;
        }
        else
        {
            return std::nullopt;
        }

        if (p >= name.size() || name[p] != '[')
            return std::nullopt;
        ++p;

        size_t lane_start = p;
        while (p < name.size() && is_digit(name[p]))
            ++p;

        int lane = parse_index(name.substr(lane_start, p - lane_start));
        if (lane < 0)
            return std::nullopt;

        if (p >= name.size() || name[p] != ']')
            return std::nullopt;

        if (is_4s)
        {
            if (lane > 3)
                return std::nullopt;
            // Lane writes should preserve the rest of the vector (other lanes + upper bits).
            // The zero_upper_fp flag is intended for scalar sN/dN names.
            return make_vn_lane_s(static_cast<uint8_t>(n), static_cast<uint8_t>(lane), fpr, /*zero_upper*/ false);
        }
        else
        {
            if (lane > 1)
                return std::nullopt;
            return make_vn_lane_d(static_cast<uint8_t>(n), static_cast<uint8_t>(lane), fpr, /*zero_upper*/ false);
        }
    }

    return std::nullopt;
}

std::optional<uint64_t> Registers::read_sub_64(std::string_view name)
{
    auto sub = make_subview_by_name(name);
    if (!sub.has_value())
        return std::nullopt;
    return sub->read_u64();
}

bool Registers::write_sub_u32(std::string_view name, uint32_t value)
{
    auto sub = make_subview_by_name(name);
    if (!sub.has_value())
        return false;
    sub->write_u32(value);
    return true;
}

bool Registers::write_sub_u64(std::string_view name, uint64_t value)
{
    auto sub = make_subview_by_name(name);
    if (!sub.has_value())
        return false;
    sub->write_u64(value);
    return true;
}
