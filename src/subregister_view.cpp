#include <libcbg/subregister_view.hpp>
#include <libcbg/error.hpp>

// =============================================================================
// subregister_view.cpp - non-owning views + read/write for wN/sN/dN/vN lanes
// =============================================================================

namespace cbg
{

// -----------------------------------------------------------------------------
// Factory functions (non-owning views into live reg structs)
// -----------------------------------------------------------------------------

SubregisterView make_wn(uint8_t n, user_pt_regs &gpr)
{
    SubregisterView sv{};
    sv.name = nullptr;
    sv.format = RegisterFormat::U32;
    sv.bit_offset = 0;
    sv.bit_size = 32;
    sv.write_policy = WritePolicy::ZeroExtend32To64;
    sv.parent = reinterpret_cast<uint8_t *>(&gpr.regs[n]);
    sv.parent_bytes = sizeof(gpr.regs[0]);
    return sv;
}

SubregisterView make_sn(uint8_t n, user_fpsimd_state &fpr, bool zero_upper)
{
    SubregisterView sv{};
    sv.name = nullptr;
    sv.format = RegisterFormat::F32;
    sv.bit_offset = 0;
    sv.bit_size = 32;
    sv.write_policy = zero_upper ? WritePolicy::ZeroUpperVector128 : WritePolicy::PreserveParentBits;
    sv.parent = reinterpret_cast<uint8_t *>(&fpr.vregs[n]);
    sv.parent_bytes = sizeof(fpr.vregs[0]);
    return sv;
}

SubregisterView make_dn(uint8_t n, user_fpsimd_state &fpr, bool zero_upper)
{
    SubregisterView sv{};
    sv.name = nullptr;
    sv.format = RegisterFormat::F64;
    sv.bit_offset = 0;
    sv.bit_size = 64;
    sv.write_policy = zero_upper ? WritePolicy::ZeroUpperVector128 : WritePolicy::PreserveParentBits;
    sv.parent = reinterpret_cast<uint8_t *>(&fpr.vregs[n]);
    sv.parent_bytes = sizeof(fpr.vregs[0]);
    return sv;
}

SubregisterView make_vn_lane_s(uint8_t n, uint8_t lane, user_fpsimd_state &fpr)
{
    SubregisterView sv{};
    sv.name = nullptr;
    sv.format = RegisterFormat::F32;
    sv.bit_offset = static_cast<uint16_t>(lane) * 32;
    sv.bit_size = 32;
    sv.write_policy = WritePolicy::PreserveParentBits;   // Lanes must never disturb siblings
    sv.parent = reinterpret_cast<uint8_t *>(&fpr.vregs[n]);
    sv.parent_bytes = sizeof(fpr.vregs[0]);
    return sv;
}

SubregisterView make_vn_lane_d(uint8_t n, uint8_t lane, user_fpsimd_state &fpr)
{
    SubregisterView sv{};
    sv.name = nullptr;
    sv.format = RegisterFormat::F64;
    sv.bit_offset = static_cast<uint16_t>(lane) * 64;
    sv.bit_size = 64;
    sv.write_policy = WritePolicy::PreserveParentBits;   // Lanes must never disturb siblings
    sv.parent = reinterpret_cast<uint8_t *>(&fpr.vregs[n]);
    sv.parent_bytes = sizeof(fpr.vregs[0]);
    return sv;
}

// -----------------------------------------------------------------------------
// Reads (u64/f32/f64)
// -----------------------------------------------------------------------------

uint64_t SubregisterView::read_u64() const
{
    uint8_t buffer[16] = {0};
    const size_t n = std::min(parent_bytes, sizeof(buffer));
    std::memcpy(buffer, parent, n);

    uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, buffer, std::min<size_t>(8, n));
    if (n > 8)
        std::memcpy(&hi, buffer + 8, std::min<size_t>(8, n - 8));

    const unsigned o = bit_offset;
    __uint128_t whole = (__uint128_t(hi) << 64) | lo;
    whole >>= o;

    uint64_t mask = (bit_size >= 64) ? ~uint64_t(0) : ((uint64_t(1) << bit_size) - 1);
    return (uint64_t)whole & mask;
}

float SubregisterView::read_f32() const
{
    uint32_t val = read_u32();
    float f;
    std::memcpy(&f, &val, sizeof(f));
    return f;
}

double SubregisterView::read_f64() const
{
    if (bit_size < 64)
    {
        Error::send_errno("Subregister too small to read as f64");
    }
    uint64_t val = read_u64();
    double f;
    std::memcpy(&f, &val, sizeof(f));
    return f;
}

// -----------------------------------------------------------------------------
// Writes (core write_u64 + convenience u32/f32/f64 + policy handling)
// -----------------------------------------------------------------------------

void SubregisterView::write_u64(uint64_t value)
{
    if (bit_size == 0 || bit_size > 64)
    {
        Error::send_errno("Subregister write size invalid");
    }

    uint8_t buffer[16] = {0};
    const size_t n = std::min(parent_bytes, sizeof(buffer));
    std::memcpy(buffer, parent, n);

    uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, buffer, std::min<size_t>(8, n));
    if (n > 8)
        std::memcpy(&hi, buffer + 8, std::min<size_t>(8, n - 8));

    __uint128_t whole = ((__uint128_t(hi) << 64) | lo);

    const unsigned o = bit_offset;
    __uint128_t slot_mask = (bit_size >= 128) ? ~__uint128_t(0) : ((__uint128_t(1) << bit_size) - 1);
    __uint128_t clear_mask = ~(slot_mask << o);

    // Preserve everything except the target slot, then insert the (masked) value.
    whole = (whole & clear_mask) | ((__uint128_t(value) & slot_mask) << o);

    // Apply architecture-mandated zeroing *after* the value has been written into its slot.
    switch (write_policy)
    {
    case WritePolicy::ZeroExtend32To64:
        // wN write: zero bits [32,63] of the 64-bit GPR (upper half of Xn).
        whole &= (__uint128_t(0x0000'0000'FFFF'FFFFULL));
        break;
    case WritePolicy::ZeroUpperVector128:
        // sN/dN scalar write: zero everything above bit_size in the 128-bit Vn.
        {
            __uint128_t keep = (bit_size >= 128) ? ~__uint128_t(0) : ((__uint128_t(1) << bit_size) - 1);
            whole &= keep;
        }
        break;
    case WritePolicy::PreserveParentBits:
        // Lane writes etc.: only the target slot was modified.
        break;
    }

    uint64_t new_lo = (uint64_t)whole;
    uint64_t new_hi = (uint64_t)(whole >> 64);
    std::memcpy(buffer, &new_lo, 8);
    if (n > 8)
        std::memcpy(buffer + 8, &new_hi, std::min<size_t>(8, n - 8));

    std::memcpy(parent, buffer, n);
}

void SubregisterView::write_u32(uint32_t value)
{
    write_u64(value);
}

void SubregisterView::write_f32(float value)
{
    uint32_t val;
    std::memcpy(&val, &value, sizeof(val));
    write_u32(val);
}

void SubregisterView::write_f64(double value)
{
    uint64_t val;
    std::memcpy(&val, &value, sizeof(val));
    write_u64(val);
}

} // namespace cbg