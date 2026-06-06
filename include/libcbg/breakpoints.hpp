#pragma once

#include <cstdint>
#include <vector>
#include <utility>
#include <optional>
#include <memory>

namespace cbg
{
    class Process;

    // Internal class owning all breakpoint state and policy (SW sites + step-over coordination,
    // high-level HW breakpoint arming). Process owns an instance and uses it as a delegate
    // while remaining the owner of the overall debug session (wait pump, ptrace primitives,
    // registers, memory primitives as a service, dtor sequencing).
    //
    // The public methods on Process for breakpoints remain the stable facade; callers
    // (CLI, tests) are not required to change.
    class Breakpoints
    {
    public:
        explicit Breakpoints(Process& owner);
        ~Breakpoints();

        // Software breakpoints
        int  add_breakpoint(uint64_t addr);
        bool remove_breakpoint(int id);
        bool remove_breakpoint(uint64_t addr);
        std::vector<std::pair<int, uint64_t>> get_breakpoints() const;

        // Called by Process at the right moments in its control flow.
        // Returns the bp address (bp_loc) if a transparent step-over was prepared
        // (original instruction restored). The caller is then responsible for
        // performing the SINGLESTEP + waitpid and later calling rearm_and_rewind.
        std::optional<uint64_t> prepare_for_transparent_step_over(uint64_t current_pc);

        // Completes the step-over: re-insert BRK at bp_loc and rewind PC (so the
        // presentation to the user is the classic "stopped at breakpoint").
        void rearm_and_rewind(uint64_t bp_loc);

        // High-level HW breakpoint support (reuses Registers' name-based views for
        // the raw brk_addrN / brk_ctrlN + write_back via owner).
        int  enable_hw_breakpoint(uint64_t addr);
        void disable_hw_breakpoint(int slot);

        // Best-effort restore of any remaining SW sites (called from ~Process).
        void restore_all_sw_sites();

        // Number of implemented slots (thin forward to Registers via owner after a load).
        int num_hw_breakpoint_slots() const;
        int num_hw_watchpoint_slots() const;

        Breakpoints(const Breakpoints&) = delete;
        Breakpoints& operator=(const Breakpoints&) = delete;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace cbg
