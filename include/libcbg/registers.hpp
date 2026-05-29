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
#include <variant>

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

    // === Unified name resolution handle (std::variant based) ===
    // A RegisterHandle can represent either a full architectural register or
    // a subregister view (wN, sN, dN, vN.lane, etc.). This allows callers to
    // resolve a name once and then use the handle for format-aware operations
    // (parsing values, reading/writing, pretty printing) without repeated
    // make_subview_by_name checks.
    struct FullRegisterDescriptor { size_t index; };

    // Carries enough information to identify a subregister without
    // needing the original string name for most operations.
    struct SubregisterDescriptor {
        enum class Kind { None, Wn, Sn, Dn, V4sLane, V2dLane };
        Kind       kind = Kind::None;
        uint8_t    n    = 0;
        uint8_t    lane = 0;

        // Cached for fast access (populated at resolution time)
        RegisterFormat format   = RegisterFormat::U64;
        uint16_t       bit_size = 0;
    };

    using RegisterHandle = std::variant<FullRegisterDescriptor, SubregisterDescriptor>;

    class Registers
    {
    public:
        Registers(pid_t pid);
        void load();
        void save();
        const RegisterView &get_register(const std::string &name) const;
        RegisterView &get_register(const std::string &name);

        // Sets the value of a full register or subregister by name.
        // Full registers are resolved via the REGISTER_LIST (xN, vN, fpsr, pc,
        // brk_*, watch_*, etc.). Subregister forms (wN, sN/dN, vN.4s[k],
        // vN.2d[k]) are supported by delegating internally to
        // make_subview_by_name() + the appropriate WritePolicy.
        void set_register(const std::string &name, uint64_t value);

        // === Minimal fast-path descriptor API (additive, string API unchanged) ===
        RegisterDescriptor lookup(std::string_view name) const;
        const RegisterView &get_register(RegisterDescriptor d) const;
        RegisterView &get_register(RegisterDescriptor d);
        void set_register(RegisterDescriptor d, uint64_t value);

        // Unified name resolution returning a RegisterHandle (variant).
        // This is the recommended way to resolve register/subregister names
        // when you need format-aware behavior (read, write, parsing, etc.).
        RegisterHandle resolve(std::string_view name) const;

        // Metadata queries that work for both full registers and subregisters.
        RegisterFormat get_format(const RegisterHandle& h) const;
        size_t         get_bit_size(const RegisterHandle& h) const;

        // Read / write using a previously resolved handle.
        std::optional<uint64_t> read(const RegisterHandle& h) const;
        void                    write(const RegisterHandle& h, uint64_t value);

        // Single value parser driven purely by format + bit size.
        // The recommended way to turn user input into bits for a target register/subregister.
        static std::optional<uint64_t> parse_register_value(std::string_view text,
                                                            RegisterFormat     fmt,
                                                            size_t             bit_size);

        std::optional<SubregisterView> make_subview_by_name(std::string_view name, bool zero_upper_fp = true);

        // Build a SubregisterView directly from a SubregisterDescriptor (preferred when you
        // already have a resolved handle). Avoids string round-tripping.
        //
        // Uses the standard architectural write policy for the subregister kind:
        //   - wN         → ZeroExtend32To64
        //   - sN / dN    → ZeroUpperVector128   (normal scalar FP write behavior)
        //   - vN.4s[k] / vN.2d[k] → PreserveParentBits (lane writes must not disturb siblings)
        std::optional<SubregisterView> make_subview(const SubregisterDescriptor& desc) const;

        std::optional<uint64_t> read_sub_u64(std::string_view name);
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
