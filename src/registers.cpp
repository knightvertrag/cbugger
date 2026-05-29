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

RegisterHandle Registers::resolve(std::string_view name) const
{
    // Try full register first (fast path + stable index)
    for (size_t i = 0; i < views.size(); ++i)
    {
        if (views[i].name == name)
        {
            return FullRegisterDescriptor{i};
        }
    }

    // Subregister
    auto spec = detail::parse_subregister_name(name);
    if (spec.kind != detail::SubregisterSpec::Kind::None)
    {
        SubregisterDescriptor sd;
        using K = SubregisterDescriptor::Kind;

        switch (spec.kind)
        {
        case detail::SubregisterSpec::Kind::Wn:      sd.kind = K::Wn;      break;
        case detail::SubregisterSpec::Kind::Sn:      sd.kind = K::Sn;      break;
        case detail::SubregisterSpec::Kind::Dn:      sd.kind = K::Dn;      break;
        case detail::SubregisterSpec::Kind::V4sLane: sd.kind = K::V4sLane; break;
        case detail::SubregisterSpec::Kind::V2dLane: sd.kind = K::V2dLane; break;
        default:                                     sd.kind = K::None;    break;
        }
        sd.n    = spec.n;
        sd.lane = spec.lane;

        // Populate cached format + bit_size using the descriptor directly
        // (no string reconstruction / round-trip through make_subview_by_name).
        if (auto sv = make_subview(sd))
        {
            sd.format   = sv->format;
            sd.bit_size = sv->bit_size;
        }

        return sd;
    }

    Error::send_errno("Register or subregister not found: " + std::string(name));
}

RegisterFormat Registers::get_format(const RegisterHandle& h) const
{
    return std::visit([this](auto&& desc) -> RegisterFormat {
        using T = std::decay_t<decltype(desc)>;
        if constexpr (std::is_same_v<T, FullRegisterDescriptor>)
        {
            return views[desc.index].format;
        }
        else if constexpr (std::is_same_v<T, SubregisterDescriptor>)
        {
            return desc.format;   // cached at resolve time
        }
        return RegisterFormat::U64;
    }, h);
}

size_t Registers::get_bit_size(const RegisterHandle& h) const
{
    return std::visit([this](auto&& desc) -> size_t {
        using T = std::decay_t<decltype(desc)>;
        if constexpr (std::is_same_v<T, FullRegisterDescriptor>)
        {
            return views[desc.index].size * 8;
        }
        else if constexpr (std::is_same_v<T, SubregisterDescriptor>)
        {
            return desc.bit_size;   // cached at resolve time
        }
        return 64;
    }, h);
}

std::optional<uint64_t> Registers::read(const RegisterHandle& h) const
{
    return std::visit([this](auto&& desc) -> std::optional<uint64_t> {
        using T = std::decay_t<decltype(desc)>;
        if constexpr (std::is_same_v<T, FullRegisterDescriptor>)
        {
            const auto& rv = views[desc.index];
            switch (rv.size)
            {
            case 1:  return rv.template get<uint8_t>();
            case 2:  return rv.template get<uint16_t>();
            case 4:  return rv.template get<uint32_t>();
            case 8:  return rv.template get<uint64_t>();
            case 16: {
                __uint128_t v = rv.template get<__uint128_t>();
                return static_cast<uint64_t>(v); // lower 64 bits for now
            }
            default: return std::nullopt;
            }
        }
        else if constexpr (std::is_same_v<T, SubregisterDescriptor>)
        {
            // Use direct descriptor-based construction (no string allocation/parsing).
            if (auto sv = make_subview(desc))
            {
                return sv->read_u64();
            }
            return std::nullopt;
        }
        return std::nullopt;
    }, h);
}

void Registers::write(const RegisterHandle& h, uint64_t value)
{
    std::visit([this, value](auto&& desc) {
        using T = std::decay_t<decltype(desc)>;
        if constexpr (std::is_same_v<T, FullRegisterDescriptor>)
        {
            set_register(RegisterDescriptor{desc.index}, value);
        }
        else if constexpr (std::is_same_v<T, SubregisterDescriptor>)
        {
            // Use direct descriptor-based construction.
            if (auto sv = make_subview(desc))
            {
                sv->write_u64(value);
            }
        }
    }, h);
}

/* static */
std::optional<uint64_t> Registers::parse_register_value(std::string_view text,
                                                        RegisterFormat     fmt,
                                                        size_t             bit_size)
{
    if (text.empty())
        return std::nullopt;

    if (fmt == RegisterFormat::F32 || fmt == RegisterFormat::F64)
    {
        char* end = nullptr;
        double d = std::strtod(text.data(), &end);
        if (end == text.data() || *end != '\0')
            return std::nullopt;

        if (fmt == RegisterFormat::F32)
        {
            float f = static_cast<float>(d);
            uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            return bits;
        }
        else
        {
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof(bits));
            return bits;
        }
    }

    // Integer / bit pattern path
    char* end = nullptr;
    uint64_t v = std::strtoull(text.data(), &end, 0);
    if (end == text.data() || *end != '\0')
        return std::nullopt;

    // Mask to the target bit width for safety
    if (bit_size > 0 && bit_size < 64)
    {
        uint64_t mask = (1ULL << bit_size) - 1;
        v &= mask;
    }
    return v;
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
        return make_vn_lane_s(spec.n, spec.lane, fpr);

    case detail::SubregisterSpec::Kind::V2dLane:
        return make_vn_lane_d(spec.n, spec.lane, fpr);

    case detail::SubregisterSpec::Kind::None:
    default:
        return std::nullopt;
    }
}

// Direct subview construction from descriptor (avoids string round-trip).
std::optional<SubregisterView> Registers::make_subview(const SubregisterDescriptor& desc) const
{
    // We cast away const on the parent structs because the factories need non-const
    // references (they store mutable pointers for later writes). Creating the view
    // itself does not mutate register state.
    auto& mut_gpr = const_cast<user_pt_regs&>(gpr);
    auto& mut_fpr = const_cast<user_fpsimd_state&>(fpr);

    switch (desc.kind)
    {
    case SubregisterDescriptor::Kind::Wn:
        return make_wn(desc.n, mut_gpr);

    case SubregisterDescriptor::Kind::Sn:
        // Standard scalar FP write behavior: zero the upper bits of the parent vector.
        return make_sn(desc.n, mut_fpr, /*zero_upper*/ true);

    case SubregisterDescriptor::Kind::Dn:
        return make_dn(desc.n, mut_fpr, /*zero_upper*/ true);

    case SubregisterDescriptor::Kind::V4sLane:
        // Lane writes must preserve other lanes (and upper bits).
        return make_vn_lane_s(desc.n, desc.lane, mut_fpr);

    case SubregisterDescriptor::Kind::V2dLane:
        return make_vn_lane_d(desc.n, desc.lane, mut_fpr);

    case SubregisterDescriptor::Kind::None:
    default:
        return std::nullopt;
    }
}

std::optional<uint64_t> Registers::read_sub_u64(std::string_view name)
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
