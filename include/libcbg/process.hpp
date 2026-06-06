#pragma once

#include <filesystem>
#include <memory>
#include <sys/types.h>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>
#include <utility>
#include <libcbg/registers.hpp>
#include <libcbg/breakpoints.hpp>

namespace cbg
{
    enum class process_state
    {
        RUNNING,
        STOPPED,
        EXITED,
        TERMINATED,
    };

    struct stop_reason
    {
        stop_reason(int wait_status);
        process_state reason;
        uint8_t info;
    };

    class Process
    {
    private:
        pid_t pid_ = 0;
        bool terminate_on_end_ = true;
        bool is_attached = true;
        process_state state_ = process_state::STOPPED;
        std::unique_ptr<Registers> registers_ = nullptr;
        std::unique_ptr<Breakpoints> breakpoints_ = nullptr;

        Process(pid_t pid, bool terminate_on_end, bool is_attached);
        
        void read_all_registers();

    public:
        static std::unique_ptr<Process> launch(const std::filesystem::path &path,
                                               bool debug = true,
                                               std::optional<int> stdout_replacement = std::nullopt);

        static std::unique_ptr<Process> attach(pid_t pid);

        void resume();
        stop_reason wait_on_signal();
        pid_t pid() const { return pid_; }
        process_state state() const { return state_; }

        Registers &get_registers() { return *registers_; }
        const Registers &get_registers() const { return *registers_; }
        void write_back_registers();

        // Memory access (word-based via PTRACE_PEEKDATA/POKEDATA). Prerequisite for
        // software breakpoints. Addresses are the tracee's virtual addresses.
        // For 4-byte aarch64 instructions, callers typically read a full 8-byte word
        // containing the target address (aligned or use read-modify-write).
        uint64_t read_memory(uint64_t addr);
        void write_memory(uint64_t addr, uint64_t value);

        // Breakpoints (software by default; HW support added in later phase).
        // add_breakpoint installs immediately (best called while STOPPED) and returns
        // a stable id for later remove/info. remove accepts id or addr.
        int add_breakpoint(uint64_t addr);
        bool remove_breakpoint(int id);
        bool remove_breakpoint(uint64_t addr);
        std::vector<std::pair<int, uint64_t>> get_breakpoints() const;

        // Single-step one instruction (PTRACE_SINGLESTEP). Handles temp disable/re-arm
        // if the current PC is at a software breakpoint site.
        stop_reason step_instruction();

        // High-level hardware breakpoint API (code address match). Allocates a free slot
        // from the available debug registers (reported by hw_breakpoint_info), programs
        // address + control bits (E, PMC=EL0+EL1, BAS, BT=insn addr match), and writes back.
        // Returns the allocated slot index (0..N-1) on success.
        // The raw brk_addrN / brk_ctrlN registers remain accessible for inspection.
        int enable_hw_breakpoint(uint64_t addr);
        void disable_hw_breakpoint(int slot);

        // Number of implemented HW breakpoint / watchpoint slots (from dbg_info after load).
        // Returns 0 until the first stop/load has occurred.
        int num_hw_breakpoint_slots() const;
        int num_hw_watchpoint_slots() const;

        Process() = delete;
        Process(const Process &) = delete;
        Process &operator=(const Process &) = delete;

        ~Process();
    };

} // namespace cbg
