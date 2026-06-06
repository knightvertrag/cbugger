# Breakpoint Implementation Progress

**Project**: cbugger — Linux aarch64 ptrace debugger  
**Purpose**: Track the current state, architecture, design decisions, and planned evolution of the breakpoint subsystem (software breakpoints, single-step, high-level hardware breakpoints, and their encapsulation).  
**Last updated**: 2026-06 (after full implementation + encapsulation into dedicated `Breakpoints` class using Pimpl)

---

## Overview

The breakpoint layer adds execution control on top of the solid process lifecycle and register model. It includes:

- Software breakpoints (unlimited, via `brk #0` trap insertion + transparent step-over).
- Single-step support (`PTRACE_SINGLESTEP`).
- High-level hardware breakpoint API (limited slots, using the pre-existing raw `NT_ARM_HW_BREAK` regset).
- Clean integration with `Process` (the control pump) while keeping policy encapsulated.

A major refactoring extracted the logic from `Process` into a dedicated internal `Breakpoints` class (with Pimpl) for better separation, testability, and to prevent `Process` from becoming a god class.

This document tracks progress, current design, and future directions. It complements the high-level status in `memory-context.md`.

---

## Current State

### Architecture & Integration

- **`Breakpoints`** (`include/libcbg/breakpoints.hpp` + `src/breakpoints.cpp`):
  - Internal class (not intended for direct external use beyond `Process`).
  - Owns all breakpoint state and policy:
    - Software breakpoint sites (address → original instruction word, id allocation).
    - Transparent step-over protocol (`prepare_for_transparent_step_over` / `rearm_and_rewind`).
    - High-level HW breakpoint/watchpoint arming (slot discovery, control bit construction for E/PMC/BAS/BT, enable/disable).
    - Best-effort restore for destructor hygiene.
  - Uses the **Pimpl idiom** (`private: class Impl; std::unique_ptr<Impl> impl_;`) so the public header only includes lightweight standard headers. Heavy details (`<unordered_map>`, ptrace headers, bit constants, maps) live only in the `.cpp`.
  - Takes a `Process&` backpointer at construction to access services (memory r/w, registers, `write_back_registers()`, pid) without duplicating low-level ptrace logic.
  - The step-over protocol keeps raw `PTRACE_SINGLESTEP` + `waitpid` coordination in `Process` while `Breakpoints` owns the "when" and "what to restore/rewind" policy.

- **`Process`** owns and integrates:
  - `std::unique_ptr<Breakpoints> breakpoints_;` (initialized in the private constructor after `Registers`).
  - Public stable facade methods (unchanged for callers in CLI/tests):
    - `add_breakpoint(uint64_t) -> int`
    - `remove_breakpoint(int)` / `(uint64_t)`
    - `get_breakpoints() const -> vector<pair<id, addr>>`
    - `step_instruction()`
    - `enable_hw_breakpoint(uint64_t) -> int`, `disable_hw_breakpoint(int)`, `num_hw_*_slots()`
  - Integration points:
    - `wait_on_signal()`: after `read_all_registers()`, calls the prepare hook; if needed, performs the inner single-step + wait, then calls rearm+rewind and re-reads registers (so callers see the rewound PC).
    - `step_instruction()`: uses prepare/rearm for temp-disable when the current PC is at a SW site.
    - `~Process()`: calls `breakpoints_->restore_all_sw_sites()` before detach/kill (prevents leaving traps in the debugee).
  - Memory primitives (`read_memory`/`write_memory`) remain on `Process` (generally useful; used by SW breakpoints for patching).

- **HW support** reuses the existing `Registers` infrastructure (raw `brk_addrN` / `brk_ctrlN` views via `resolve`/`read`/`write` + `write_back_registers()` and the dynamic-sized `NT_ARM_HW_BREAK` regset loaded on stops). `Breakpoints` performs slot scanning and constructs proper control values.

- **CLI** (`tools/cbg.cpp`): `b`/`break <addr>`, `d`/`delete <id>`, `info breakpoints`, `s`/`step`/`si`. Hit detection is reported via a small post-wait helper that checks current PC against `get_breakpoints()`.

- No changes were required to `Registers` core (minor `hw_*_info()` accessors were added during implementation to support slot counts).

### Key Protocols & Behaviors

- **Transparent SW breakpoint step-over** (for "continue" hitting a bp):
  1. On stop, if PC matches a SW site and it was a TRAP: restore original instruction.
  2. Perform `PTRACE_SINGLESTEP` + inner `waitpid` (executes the real instruction).
  3. Re-insert `brk #0`.
  4. Rewind the PC register (via `RegisterHandle` + `write_back`) so the presented state is "stopped at the breakpoint address" (classic debugger UX; the instruction has not yet executed from the user's perspective).
  5. Re-load registers for callers.

- Similar temp-disable + re-arm logic is used inside explicit `step_instruction()` when sitting on a bp site.

- **Destructor hygiene**: SW sites are restored (best-effort poke of originals) before `PTRACE_DETACH` or kill. HW breakpoints are implicitly cleared by the kernel on trace end.

- **Idempotent add**, remove by id or address, stable ids for CLI listing.

- HW breakpoints do not patch memory; they program the debug registers (persisted via the existing regset save on `write_back_registers()`).

### Current Capabilities Summary

| Area                        | Status     | Notes |
|-----------------------------|------------|-------|
| SW breakpoints + step-over  | ✅ Complete | Transparent handling, PC rewind, dtor restore, CLI + tests. |
| Single-step                 | ✅ Complete | `step_instruction()`, respects SW sites. |
| High-level HW breakpoints   | ✅ Complete | `enable_hw_breakpoint(addr)`, disable by slot, control bits, slot counts from `dbg_info`. |
| Memory primitives           | ✅ Complete | Word-based `read_memory`/`write_memory` (sufficient for patching + general use). |
| Encapsulation               | ✅ Complete | Dedicated `Breakpoints` class + Pimpl; `Process` delegates + integrates at control points. Public `Process` API unchanged. |
| CLI support                 | ✅ Complete | `b`/`break`, `d`/`delete`, `info breakpoints`, `step`. Hit notes on stop. |
| Tests                       | ✅ Complete | ~15 assertions in dedicated breakpoint cases; full suite ~110 assertions. |

### Recent Milestones

- Implementation of memory access, SW bp insertion/step-over/rewind, HW high-level API, CLI commands, and test coverage.
- Refactoring to extract into `Breakpoints` (Pimpl chosen for compilation firewall, strong hiding of maps/policy, and future evolvability).
- Deletion of orphaned `include/debugger.hpp`.
- Updates to `memory-context.md` and creation of this `breakpoint-progress.md`.

### Limitations (Current)

- Watchpoint high-level support is stub/minimal (raw registers available; value-based or richer watchpoint latching is future).
- Breakpoints are address-based only (no symbols/DWARF yet; "break main" not possible).
- Single-threaded assumption (no thread-specific bps or `PTRACE_O_TRACECLONE` handling yet).
- The `Process&` backpointer in `Breakpoints` is pragmatic but couples the classes; a narrower interface could be introduced later.
- No hit counts, temporary ("one-shot") breakpoints, or "until" commands yet.
- Inner single-step + waitpid for transparent step-over is coordinated in `Process` (via the prepare/rearm protocol) rather than fully hidden inside `Breakpoints`.

---

## Future Scope & Roadmap

1. **Symbolication & Source-Level Features** (high priority for usability)
   - DWARF parsing (libdw) for function/line breakpoints.
   - "break main", "break <file>:<line>", etc.
   - Source stepping correlation.

2. **Richer Watchpoints**
   - High-level `enable_watchpoint(addr, size, access_type)`.
   - Value-based / data watchpoints with latching on hit.

3. **Threading / Multi-Process Robustness**
   - Thread-specific breakpoints.
   - Handling of forks/clones (`PTRACE_O_*` options).
   - Per-thread debug register state if needed.

4. **Polish on the Breakpoint Layer**
   - Expose a `Breakpoints& get_breakpoints()` (or richer info) from `Process` if useful, while keeping the current convenient facade.
   - Evolve owner coupling (narrower service object instead of full `Process&`).
   - Temporary/one-shot breakpoints, hit counts, "info breakpoints" with more detail (HW slot, type).
   - Better error messages for "no free HW slots", "not stopped", invalid addresses.

5. **SVE/SME Interactions** (when register layer expands)
   - Breakpoint/watchpoint behavior with scalable vectors or SME state.

6. **Testing & CLI Improvements**
   - More automated coverage of edge cases (bp at current PC, multiple overlapping sites, attach/detach with live bps).
   - Tab completion for breakpoint-related commands.

---

## Design Decisions & Rationale

- **Pimpl for `Breakpoints`**: Chosen to achieve strong encapsulation after the initial "put it in Process" implementation. Benefits: compilation firewall (keeps `<unordered_map>`, ptrace headers, etc. out of `breakpoints.hpp` and thus out of `process.hpp` and its includers), implementation hiding (future changes to maps, protocols, or HW logic don't affect header users), and ABI stability of the `Breakpoints` object itself. See also the justification discussion in session history. Cost: one allocation + indirection (accepted as worthwhile for cleanliness).

- **Delegation + stable facade on `Process`**: Callers (CLI, tests) continue to use `process->add_breakpoint(...)` etc. No source changes were needed outside the core library during encapsulation. This keeps the public surface of the debugger simple.

- **Prepare/rearm protocol for step-over cooperation**: `Breakpoints` owns policy and state ("is this a bp site? what was the original word?"). `Process` owns the raw control primitives (`PTRACE_SINGLESTEP`, manual inner waitpid, register access for PC rewind). This avoids duplicating ptrace/wait logic in the breakpoint class while still giving it full ownership of the "what to do" decisions. It also makes the transparent continue path and explicit step paths share the same helpers.

- **HW built on existing raw regsets**: No duplication of debug register storage or load/save. `Breakpoints` uses the name-based `Registers` API (`"brk_addrN"`, `"brk_ctrlN"`) + `write_back_registers()`. This kept the change additive and consistent with how registers were already exposed.

- **Memory primitives stay on `Process`**: They were introduced for SW bps but are generally useful (peeking/poking data, etc.). `Breakpoints` consumes them via the owner.

- **Best-effort dtor restore for SW sites**: Critical for not corrupting the debugee when the debugger exits while breakpoints are armed. HW sites are kernel-cleared on trace end.

---

## Related Documents

- [memory-context.md](memory-context.md) — Overall project state, feature matrix (now showing breakpoints + memory as complete), architecture diagram (includes `Breakpoints`), roadmap, and history.
- Original planning document (in session plan.md) — Contains the initial phased plan for memory + breakpoints and the explicit recommendation for a separate `breakpoint.hpp` + `.cpp`.
- `include/libcbg/process.hpp`, `include/libcbg/breakpoints.hpp`, `src/process.cpp`, `src/breakpoints.cpp` — Primary implementation.
- `tools/cbg.cpp` and `test/tests.cpp` — Usage and black-box tests of the public facade.

---

*This document should be updated whenever significant breakpoint-related work (new features, refactoring, design changes) is completed or planned.*