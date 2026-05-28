#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <stdexcept>
#include <cstring>
#include <libcbg/detail/register.inc>
#include <libcbg/error.hpp>
#include <libcbg/subregister_view.hpp>
#include <asm/ptrace.h>
#include <optional>

namespace cbg
{
    struct RegisterView
    {
        std::string name;
        const void *data;
        void *writable_data;
        size_t size;
        RegisterFormat format;
        int dwarf_id;

        template <typename T>
        T get() const
        {
            if (sizeof(T) != size)
            {
                cbg::Error::send_errno("Register size mismatch");
            }
            T value;
            std::memcpy(&value, data, size);
            return value;
        }

        template <typename T>
        void set(const T &value)
        {
            if (!writable_data)
            {
                cbg::Error::send_errno("Register is read-only");
            }
            if (sizeof(T) > size)
            {
                cbg::Error::send_errno("Register size incompatible");
            }
            std::memset(writable_data, 0, size);
            std::memcpy(writable_data, &value, size);
        }
    };

    // Lightweight cached handle for fast O(1) register access after one-time name resolution.
    // Obtained via Registers::lookup(). Indices are stable for the lifetime of the Registers object.
    struct RegisterDescriptor
    {
        size_t index;
    };

    

    class Registers
    {
    public:
        Registers(pid_t pid);
        void load();
        void save();
        const RegisterView &get_register(const std::string &name) const;
        RegisterView &get_register(const std::string &name);
        void set_register(const std::string &name, uint64_t value);

        // === Minimal fast-path descriptor API (additive, string API unchanged) ===
        RegisterDescriptor lookup(std::string_view name) const;
        const RegisterView &get_register(RegisterDescriptor d) const;
        RegisterView &get_register(RegisterDescriptor d);
        void set_register(RegisterDescriptor d, uint64_t value);

        std::optional<SubregisterView> make_subview_by_name(std::string_view name, bool zero_upper_fp = true);
        std::optional<uint64_t> read_sub_64(std::string_view name);
        bool write_sub_u32(std::string_view name, uint32_t value);
        bool write_sub_u64(std::string_view name, uint64_t value);

    private:
        pid_t pid;
        size_t break_debug_size;
        size_t watch_debug_size;
        struct user_pt_regs gpr{};
        struct user_fpsimd_state fpr{};
        struct user_hwdebug_state hw_break{};
        struct user_hwdebug_state hw_watch{};
        std::vector<RegisterView> views;

        void build_views();

        // Internal helper shared by string lookup and new descriptor lookup.
        size_t find_index(std::string_view name) const;
    };

}
