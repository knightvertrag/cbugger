#include <libcbg/registers.hpp>
#include <libcbg/error.hpp>
#include <libcbg/detail/register_name.hpp>
#include <spdlog/spdlog.h>
#include <sys/ptrace.h>
#include <asm/ptrace.h>
#include <elf.h>

// =============================================================================
// registers.cpp - AArch64 register access via ptrace regsets + subregister views
// =============================================================================

using namespace cbg;

namespace
{
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    size_t calc_debug_size(size_t dbg_info)
    {
        unsigned num_slots = dbg_info & 0xff;
        size_t header = offsetof(struct user_hwdebug_state, dbg_regs);
        size_t entry = sizeof(user_hwdebug_state::dbg_regs[0]);
        return header + num_slots * entry;
    }
}

// -----------------------------------------------------------------------------
// Core lifecycle (ctor + view construction)
// -----------------------------------------------------------------------------

Registers::Registers(pid_t pid) : pid(pid)
{
    build_views();
}

// ============================================================================
// Sections (for navigation — definition order in this file):
//   - Core lifecycle + view build (ctor, load/save, build_views)
//   - View getters (get_register / get_subregister)
//   - Subview factories (make_subview_by_name / make_subview)   — used by getters + resolution
//   - Value setters (set_register / set_subregister)
//   - Name resolution (find_index / lookup / resolve)
//   - Handle ops (get_format / get_bit_size / read / write)
//   - Value parsing (parse_register_value)
// ============================================================================

// ============================================================================
// Core: register load/save + internal view construction
// ============================================================================

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

// ============================================================================
// Direct accessors: get the register/subregister *view* (for typed access)
// ============================================================================

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

const RegisterView &Registers::get_register(RegisterDescriptor d) const
{
    return views[d.index];
}

RegisterView &Registers::get_register(RegisterDescriptor d)
{
    return views[d.index];
}

// Get subregister view (for access to its bits, with policy etc.).
// Parallels get_register for full regs (returns the view, not raw value).
// Use RegisterHandle::read() (via resolve()) as the preferred unified path for raw bits value.
// (The old value-returning get_subregister was removed to avoid API confusion between
// "get the register/subregister" = its View vs. getting its value bits.)
SubregisterView Registers::get_subregister(const std::string &name) const
{
    auto h = resolve(name);
    if (auto* sd = std::get_if<SubregisterDescriptor>(&h)) {
        return get_subregister(*sd);
    }
    Error::send("Not a subregister: " + name);
    return SubregisterView{}; // unreachable
}

SubregisterView Registers::get_subregister(const SubregisterDescriptor &desc) const
{
    if (auto sv = make_subview(desc)) {
        return *sv;
    }
    Error::send("Invalid SubregisterDescriptor");
    return SubregisterView{}; // unreachable
}

// ============================================================================
// Subregister view factories (string-based and descriptor-based)
// (used by resolution, handle access, and sub get/set)
// ============================================================================

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

// ============================================================================
// Direct modification: set register/subregister *value*
// ============================================================================

void Registers::set_register(RegisterDescriptor d, __uint128_t value)
{
    // Size-dispatch; for 128-bit v regs use full value. For smaller regs,
    // high bits (if passed) are truncated to low bits.
    RegisterView &reg = views[d.index];
    switch (reg.size)
    {
    case 1:  reg.set<uint8_t>(static_cast<uint8_t>(value)); break;
    case 2:  reg.set<uint16_t>(static_cast<uint16_t>(value)); break;
    case 4:  reg.set<uint32_t>(static_cast<uint32_t>(value)); break;
    case 8:  reg.set<uint64_t>(static_cast<uint64_t>(value)); break;
    case 16: reg.set<__uint128_t>(value); break;
    default: Error::send("Unsupported register size for descriptor");
    }
}

// Encapsulated subregister writing (wN, sN/dN, vN lanes etc.).
// These are always <=64 bits. Use RegisterHandle::write() (via resolve())
// as the preferred unified path when possible. (get_subregister/set_subregister
// provide consistent "get/set the subregister" view access, parallel to full regs.)
void Registers::set_subregister(const SubregisterDescriptor &desc, uint64_t value)
{
    if (auto sv = make_subview(desc))
    {
        sv->write_u64(value);
        return;
    }
    Error::send("Invalid SubregisterDescriptor");
}

// ============================================================================
// Name resolution: string -> descriptor/handle (used by unified paths)
// ============================================================================

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

// ============================================================================
// Handle-based operations (metadata + unified read/write for full + sub)
// ============================================================================

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

std::optional<__uint128_t> Registers::read(const RegisterHandle& h) const
{
    return std::visit([this](auto&& desc) -> std::optional<__uint128_t> {
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
                return rv.template get<__uint128_t>(); // full 128-bit for vN etc.
            }
            default: return std::nullopt;
            }
        }
        else if constexpr (std::is_same_v<T, SubregisterDescriptor>)
        {
            // Encapsulated via get_subregister (the view; extract bits for uniform handle read).
            auto sv = get_subregister(desc);
            return static_cast<__uint128_t>(sv.read_u64());
        }
        return std::nullopt;
    }, h);
}

void Registers::write(const RegisterHandle& h, __uint128_t value)
{
    std::visit([this, value](auto&& desc) {
        using T = std::decay_t<decltype(desc)>;
        if constexpr (std::is_same_v<T, FullRegisterDescriptor>)
        {
            set_register(RegisterDescriptor{desc.index}, value);
        }
        else if constexpr (std::is_same_v<T, SubregisterDescriptor>)
        {
            // Encapsulated via set_subregister.
            set_subregister(desc, static_cast<uint64_t>(value));
        }
    }, h);
}

// ============================================================================
// Value parsing for user input (used by writes via handles)
// ============================================================================

/* static */
std::optional<__uint128_t> Registers::parse_register_value(std::string_view text,
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
            return static_cast<__uint128_t>(bits);
        }
        else
        {
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof(bits));
            return static_cast<__uint128_t>(bits);
        }
    }

    // Special-case all-ones for any width (supports "-1" for 128-bit too)
    {
        // minimal trim
        size_t p = 0;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        size_t e = text.size();
        while (e > p && (text[e-1] == ' ' || text[e-1] == '\t')) --e;
        std::string_view tv(text.data() + p, e - p);
        if (tv == "-1" || tv == "~0") {
            if (bit_size == 0 || bit_size >= 128) return ~__uint128_t(0);
            return ((__uint128_t(1) << bit_size) - 1);
        }
    }

    // Integer / bit pattern path — support full 128-bit hex (for Vec128 / vN)
    // Detect 0x... (hex) and parse up to 32 hex digits manually.
    bool looks_hex = false;
    size_t hex_start = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        looks_hex = true;
        hex_start = 2;
    }

    if (looks_hex) {
        size_t ndigits = text.size() - hex_start;
        size_t maxd = (bit_size == 0 || bit_size >= 128) ? 32u : ((bit_size + 3u) / 4u);
        if (ndigits > maxd) return std::nullopt;

        __uint128_t v = 0;
        bool any_digit = false;
        for (size_t i = hex_start; i < text.size(); ++i) {
            char c = text[i];
            int d = -1;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
            else {
                return std::nullopt; // invalid hex digit in 0x form
            }
            any_digit = true;
            // check would overflow 128 bits (push nonzero out top)
            if ((v >> (128 - 4)) != 0) {
                return std::nullopt;
            }
            v = (v << 4) | static_cast<__uint128_t>(d);
        }
        if (!any_digit) return std::nullopt;

        // mask to bit width if requested (<128)
        if (bit_size > 0 && bit_size < 128) {
            __uint128_t mask = ((__uint128_t(1) << bit_size) - 1);
            v &= mask;
        }
        return v;
    }

    // Fallback for decimal/octal/short-hex (compat with prior behavior, <=64 bits)
    char* end = nullptr;
    uint64_t v64 = std::strtoull(text.data(), &end, 0);
    if (end == text.data() || *end != '\0')
        return std::nullopt;

    __uint128_t v = static_cast<__uint128_t>(v64);

    // Mask to the target bit width for safety (works for 1..127; 64-bit shift ok in __uint128_t)
    if (bit_size > 0 && bit_size < 128) {
        __uint128_t mask = ((__uint128_t(1) << bit_size) - 1);
        v &= mask;
    }
    return v;
}

