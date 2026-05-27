#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <asm/ptrace.h>

namespace cbg
{
    enum class RegisterFormat
    {
        U8,
        U16,
        U32,
        U64,
        F32,
        F64,
        Vec128,
    };

    enum class WritePolicy : uint8_t
    {
        ZeroExtend32To64,
        PreserveParentBits,
        ZeroUpperVector128,
    };

    struct SubregisterView
    {
        const char *name;
        RegisterFormat format;
        uint16_t bit_offset;
        uint16_t bit_size;
        WritePolicy write_policy;
        uint8_t *parent;
        size_t parent_bytes;

        /* Reads */
        uint64_t read_u64() const;
        uint32_t read_u32() const { return (uint32_t)read_u64(); }
        template <typename T>
        T read_subview_as() const
        {
            uint64_t val = read_u64();
            T f;
            std::memcpy(&f, &val, sizeof(f));
            return f;
        }
        float read_f32() const;
        double read_f64() const;
        /* Writes */
        void write_u64(uint64_t v);
        void write_u32(uint32_t v);
        void write_f32(float f);
        void write_f64(double d);
    };

    SubregisterView make_wn(uint8_t n, user_pt_regs &gpr);
    SubregisterView make_sn(uint8_t n, user_fpsimd_state &fpr, bool zero_upper = false);
    SubregisterView make_dn(uint8_t n, user_fpsimd_state &fpr, bool zero_upper = false);
    SubregisterView make_vn_lane_s(uint8_t n, uint8_t lane, user_fpsimd_state &fpr, bool zero_upper = false);
    SubregisterView make_vn_lane_d(uint8_t n, uint8_t lane, user_fpsimd_state &fpr, bool zero_upper = false);
}