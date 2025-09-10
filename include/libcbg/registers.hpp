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
#include <asm/ptrace.h>

namespace cbg
{
    enum class RegisterFormat
    {
        UInt32,
        UInt64,
        UInt128
    };

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
            if (sizeof(T) != size)
            {
                cbg::Error::send_errno("Register size mismatch");
            }
            std::memcpy(writable_data, &value, size);
        }
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

    private:
        pid_t pid;
        struct user_pt_regs gpr{};
        struct user_fpsimd_state fpr{};
        struct user_hwdebug_state hw_break{};
        struct user_hwdebug_state hw_watch{};
        std::vector<RegisterView> views;

        void build_views();
    };

}
