#include <libcbg/registers.hpp>
#include <libcbg/error.hpp>
#include <libcbg/detail/register_name.hpp>
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

    // Subregister path (wN, sN, dN, vN.4s[k], vN.2d[k]) — unified entry point
    if (auto sub = make_subview_by_name(name))
    {
        sub->write_u64(value);
        return;
    }

    // Full register path (xN, vN, pc, fpsr, brk_*, watch_*, etc.)
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

size_t Registers::find_index(std::string_view name) const
{
    for (size_t i = 0; i < views.size(); ++i)
    {
        if (views[i].name == name)
            return i;
    }
    Error::send_errno("Register not found: " + std::string(name));
}

RegisterDescriptor Registers::lookup(std::string_view name) const
{
    return { find_index(name) };
}

const RegisterView &Registers::get_register(RegisterDescriptor d) const
{
    return views[d.index];
}

RegisterView &Registers::get_register(RegisterDescriptor d)
{
    return views[d.index];
}

void Registers::set_register(RegisterDescriptor d, uint64_t value)
{
    // Reuse the existing size-dispatch logic by temporarily getting a reference
    RegisterView &reg = views[d.index];
    switch (reg.size)
    {
    case 1:  reg.set<uint8_t>(static_cast<uint8_t>(value)); break;
    case 2:  reg.set<uint16_t>(static_cast<uint16_t>(value)); break;
    case 4:  reg.set<uint32_t>(static_cast<uint32_t>(value)); break;
    case 8:  reg.set<uint64_t>(value); break;
    case 16: reg.set<__uint128_t>(static_cast<__uint128_t>(value)); break;
    default: Error::send("Unsupported register size for descriptor");
    }
}

std::optional<SubregisterView> Registers::make_subview_by_name(std::string_view name, bool zero_upper_fp)
{
    auto spec = detail::parse_subregister_name(name);
    switch (spec.kind)
    {
    case detail::SubregisterSpec::Kind::Wn:
        return make_wn(spec.n, gpr);

    case detail::SubregisterSpec::Kind::Sn:
        return make_sn(spec.n, fpr, zero_upper_fp);

    case detail::SubregisterSpec::Kind::Dn:
        return make_dn(spec.n, fpr, zero_upper_fp);

    case detail::SubregisterSpec::Kind::V4sLane:
        // Lane writes intentionally ignore zero_upper_fp (they use PreserveParentBits).
        return make_vn_lane_s(spec.n, spec.lane, fpr, /*zero_upper*/ false);

    case detail::SubregisterSpec::Kind::V2dLane:
        return make_vn_lane_d(spec.n, spec.lane, fpr, /*zero_upper*/ false);

    case detail::SubregisterSpec::Kind::None:
    default:
        return std::nullopt;
    }
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
