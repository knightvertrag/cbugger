# cbugger Memory Context

**Project**: cbugger — A simple Linux aarch64 debugger for C/C++ programs  
**Mission**: Build a from-scratch ptrace-based debugger targeting Linux aarch64 (lp64 ABI), starting with solid process control and a rich register model, then layering higher-level debugging features.  
**Last updated**: 2026-05-29 (after promotion of RegisterHandle + unified descriptor-based subregister path, handle read/write, and direct make_subview(SubregisterDescriptor))  
**Purpose**: Persistent AI / agent memory priming. Future sessions should read `Readme.md` + this file first to avoid re-exploring or making contradictory assumptions.  
**Quick status**: Mid-stage. Core process lifecycle (launch/attach/resume/wait) and full AArch64 register access (GPR + FPR + HW debug + subregisters) are functional in the library. Subregister views (`wN`, `sN`/`dN`, `vN.4s[i]`/`vN.2d[i]`) with correct architectural write policies are complete and tested. A unified `RegisterHandle` (via `resolve(name)`) now provides a single entry point for both full registers and subregisters, with handle-based `read()`/`write()` and direct `make_subview(SubregisterDescriptor)`. The high-level CLI uses this model for data access. Memory access, breakpoints, and stepping remain unimplemented.

---

## Project Overview & Goals

- **Primary target**: Linux aarch64 (GNU/Linux, aarch64-linux-gnu, lp64) debugging of position-dependent and PIE ELF binaries.
- **Host dev reality**: macOS (Apple Silicon) development using Docker dev containers (or equivalent) that run aarch64-linux-gnu GCC 15 + vcpkg arm64-linux triplet.
- **Current scope**: ptrace-driven debugger library (`libcbg`) + minimal readline CLI (`cbg` / `tools/cbg`).
- **Explicit non-goals** (inferred from code and history): multi-architecture support, macOS/Darwin, Windows, remote debugging (GDB remote serial), GUI, source-level single stepping with DWARF yet, JIT, or language-specific runtime support.
- **Long-term ambition**: A capable native aarch64 debugger with register inspection (including sub-register views), hardware-assisted breakpoints/watchpoints, memory inspection, and eventually source correlation.

The project has evolved iteratively:
- Early: classic "debugger class" + fork/exec model.
- Mid: extraction to `libcbg::Process` + sophisticated `Registers` layer (recent focus).

---

## Architecture & Component Map

```mermaid
flowchart TD
    CLI["tools/cbg.cpp\n(readline 'cbg> ' loop + full register commands)"] 
    CLI -->|"Process::launch(path) or\nProcess::attach(pid)"| Process

    Process["libcbg::Process\n(pid, state, terminate_on_end, is_attached)"]
    Process -->|"fork + ptrace(TRACEME) + execlp\nor ptrace(PTRACE_ATTACH)"| Linux["Linux kernel (aarch64)"]
    Process -->|resume / wait_on_signal| Linux
    Process -->|"get_registers() → Registers"| Registers

    Registers["Registers (per-Process)\nload/save via regsets"]
    Registers -->|"PTRACE_GETREGSET / SETREGSET\nNT_PRSTATUS, NT_FPREGSET,\nNT_ARM_HW_BREAK, NT_ARM_HW_WATCH"| Regsets["Kernel regsets\n(user_pt_regs, user_fpsimd_state,\nuser_hwdebug_state)"]
    Registers -->|build_views() + REGISTER_LIST macro| Views["std::vector<RegisterView>\n+ optional<SubregisterView>"]

    Subregister["SubregisterView\n(bit_offset, bit_size, WritePolicy,\nparent pointer)"]
    Subregister -->|read_u64 / write_u64 + policies\n(ZeroExtend32To64, ZeroUpperVector128, PreserveParentBits)| BitOps["128-bit bit manipulation\nfor wN, sN, dN, vN.lane views"]

    Linux -->|signals (SIGTRAP from test asm, etc.)| StopReason["stop_reason\n(EXITED / TERMINATED / STOPPED)"]

    style Process fill:#e6f3ff
    style Registers fill:#e6f3ff
```

**Layers**:
- **Presentation**: `tools/cbg.cpp` (libedit readline, rich command dispatch including `register read`/`write`, `regs`, subregister formatting, and stop-reason printing).
- **Core Library** (`libcbg`): `Process`, `Registers`, `RegisterView`, `SubregisterView`, `Pipe`, `Error`.
- **OS Interaction**: Direct `ptrace(2)`, `waitpid`, `kill`, regset iovec calls. No higher abstractions (no `libunwind`, no `libdw` yet).

**Key data structures** (with locations):
- `process_state` + `stop_reason` — [include/libcbg/process.hpp:12](include/libcbg/process.hpp)
- `RegisterView` (name, data pointers, size, format, dwarf_id) — [include/libcbg/registers.hpp:29](include/libcbg/registers.hpp)
- `SubregisterView` + `WritePolicy` enum — [include/libcbg/subregister_view.hpp:8](include/libcbg/subregister_view.hpp)
- `SubregisterSpec` + `parse_subregister_name` (internal parser for wN/sN/dN/vN.* syntax) — [include/libcbg/detail/register_name.hpp](include/libcbg/detail/register_name.hpp)
- `RegisterHandle` (variant of `FullRegisterDescriptor` + `SubregisterDescriptor`) + unified `resolve()` / `read()` / `write()` API — [include/libcbg/registers.hpp](include/libcbg/registers.hpp)
- `REGISTER_LIST(...)` macro — [include/libcbg/detail/register.inc:9](include/libcbg/detail/register.inc)

---

## Current Capabilities (Feature Matrix)

| Feature                              | Status     | Key Code Paths                                      | Notes |
|--------------------------------------|------------|-----------------------------------------------------|-------|
| Launch debugee with tracing          | ✅        | `Process::launch` [src/process.cpp:42]             | Uses `Pipe` for pre-exec error reporting from child; optional stdout redirection. |
| Attach to existing PID               | ✅        | `Process::attach` [src/process.cpp:89]             | Sends `PTRACE_ATTACH`, waits for initial stop. |
| Resume / continue                    | ✅        | `resume()` [src/process.cpp:130]                   | `PTRACE_CONT`. Sets internal `RUNNING` state. |
| Wait for stop + reason classification| ✅        | `wait_on_signal()` + `stop_reason` ctor [src/process.cpp:164,145] | Correctly detects EXITED / TERMINATED / STOPPED via `WIF*` macros. |
| Full GPR access (x0–x30, sp, pc, pstate) | ✅     | `REGISTER_LIST` + `load`/`save` [src/registers.cpp:49] | Uses `NT_PRSTATUS` regset. |
| Full FPR/SIMD (v0–v31 + fpsr/fpcr)   | ✅        | `NT_FPREGSET` [src/registers.cpp:54]               | 128-bit `Vec128` format stored as `__uint128_t`. |
| Hardware debug registers (16 BRK + 16 WPT) | ✅   | `NT_ARM_HW_BREAK` / `NT_ARM_HW_WATCH` [src/registers.cpp:59,66] | Raw addr + ctrl loaded/saved (sizes computed from `dbg_info`). |
| Register read/write via views (`get_register`, `set_register`) | ✅ | [include/libcbg/registers.hpp:74](include/libcbg/registers.hpp), [src/registers.cpp:122] | Templated `get<T>()` / `set<T>()`. |
| Subregister views (wN, sN, dN, vN.lanes) | ✅        | `make_subview_by_name` + factories + `SubregisterView` [src/registers.cpp:172], [src/subregister_view.cpp:1] + dedicated test | Full support for `wN` (zero-extend), scalar `sN`/`dN` (zero-upper), and vector lanes (`vN.4s[i]`, `vN.2d[i]` with preserve). `read_sub_*`/`write_sub_*` helpers + `write_back_registers()` also implemented. Passing Catch2 coverage. |
| Full register CLI (read/write + subregisters + formatting) | ✅ | `handle_register_command` + helpers in [tools/cbg.cpp] | `register read [name|all]`, `register write <name> <val>`, `regs`, `info registers`. Supports all subregister forms. Value-syntax driven float vs integer writes (GDB-like). Pretty printing with bit patterns + float interp for scalars. Stop reason printed on every continue. Prompt is now `cbg>`. |
| Memory peek/poke                     | ❌        | —                                                   | No `PTRACE_PEEKDATA`, `process_vm_readv`, etc. yet. |
| Software breakpoints / single-step   | ❌        | —                                                   | No `PTRACE_SINGLESTEP` or trap insertion. |
| High-level HW breakpoint/watchpoint API | ❌     | Raw reg access only [include/libcbg/detail/register.inc:82] | No helpers to arm `dbg_regs[].ctrl` (BAS, PMC, E, etc.). |
| Symbolication / DWARF / source correlation | ❌ | —                                              | No debug info parsing. Dwarf IDs are present in views as future hooks. |
| `write_back_registers()` on Process  | ✅        | Implemented [src/process.cpp:195] + used by CLI    | Convenience wrapper over `Registers::save()`. |
| Proper handling of process exit in wait | ✅      | [src/process.cpp:175]                              | `pid_` is cleared and "Process exited" is printed on EXITED/TERMINATED. |

**Tests exercising the above**:
- Process lifecycle (launch, attach, resume, dtor) — [test/tests.cpp:30](test/tests.cpp)
- Register r/w round-trips via asm `trap` targets — [test/tests.cpp:82](test/tests.cpp), [test/targets/register_*.s](test/targets/)
- Subregister views, policies, and helpers (wN zero-extend, sN/dN zero-upper, lane preserve, error cases) — dedicated test case in [test/tests.cpp](test/tests.cpp)
- CLI register commands are exercised manually against the trap targets (full automated CLI testing is still thin).

---

## Register Model Deep Dive (Most Important Technical Area)

This is the most sophisticated part of the current codebase (heavy focus of commits 6f90a1b and d295904).

### How it works
1. `REGISTER_LIST(GPR, FPR, HWBRK, HWWATCH)` macro in [include/libcbg/detail/register.inc:9](include/libcbg/detail/register.inc) enumerates:
   - 34 GPR entries (x0–x30, sp, pc, pstate) with dwarf IDs 0–33.
   - 34 FPR entries (v0–v31 + fpsr/fpcr) with dwarf IDs 64–97.
   - 32 HW debug entries (brk_addr/ctrl 0–15, watch_addr/ctrl 0–15) — dwarf_id = -1.
2. `Registers::build_views()` [src/registers.cpp:98](src/registers.cpp) expands the macro into `std::vector<RegisterView>`.
3. `load()` / `save()` use `PTRACE_GETREGSET` / `SETREGSET` with `struct iovec` + the four NT_* constants. HW debug sizes are dynamic (computed from `dbg_info` field).
4. `RegisterView` holds **two pointers** (const data + mutable writable_data) into the live `gpr` / `fpr` / `hw_*` structs.
5. Subregister support (`wN`, `sN`/`dN`, `vN.4s[i]` / `vN.2d[i]`) is fully implemented in `SubregisterView` [include/libcbg/subregister_view.hpp](include/libcbg/subregister_view.hpp) + [src/subregister_view.cpp](src/subregister_view.cpp). It performs 128-bit math over the parent storage and applies the correct `WritePolicy` on write (zero-extend for wN, zero-upper for scalar FP, preserve for lanes).

### Why this design
- aarch64 has far more state than the old `user_regs_struct`; regsets are the modern, extensible interface.
- Subregister views solve the common debugger problem of "when user says `w20` or `s5`, what bits actually move and what gets zeroed in the parent register?"
- Write policies (`ZeroExtend32To64`, `ZeroUpperVector128`, `PreserveParentBits`) encode ARM architectural rules cleanly.

A lightweight `RegisterDescriptor` (obtained once via `Registers::lookup(name)`) provides O(1) access for full registers. A first-class `RegisterHandle` (obtained via the unified `Registers::resolve(name)`) now represents *both* full registers and subregisters, enabling handle-based `read()` / `write()` and direct `make_subview(const SubregisterDescriptor&)` construction. This unifies the previous dual string-based paths while preserving the original `RegisterView` / `SubregisterView` implementations.

### Current gaps in the register layer (precise locations)
- Minor: `set_register` for 16-byte registers [src/registers.cpp:162](src/registers.cpp) takes a `uint64_t value` and casts it — loses the upper bits the caller probably wanted (unchanged from prior state).
- No higher-level helpers yet for convenient subregister access from the CLI or for SVE/SME registers (future work).
- The `RegisterHandle` + descriptor path for subregisters is still relatively new; higher-level conveniences (e.g. typed `read<T>()` on handles, richer pretty-printing integration) are still evolving.

---

## Build, Test & Development Workflow

- **Build system**: CMake 3.19+ (`project(... LANGUAGES CXX ASM)`), C++17, vcpkg manifest mode.
- **Dependencies** (vcpkg.json): libedit (readline), Catch2, spdlog (logging to `logs/debug_logs.log`).
- **Preset**: `CMakePresets.json` "cbg" — sets vcpkg toolchain for arm64-linux.
- **Docker**: `dockerfile` (gcc:latest base + vcpkg bootstrap). Container runs as aarch64-linux-gnu target.
- **Typical commands** (from README + CMake):
  ```bash
  # inside container or aarch64 env
  cmake --preset=cbg
  cmake --build build/cbg
  ./build/cbg/tools/cbg <program>          # or -p <pid>
  ctest --test-dir build/cbg/test
  ```
- **Logging**: spdlog file logger at debug level (see `log_setup()` in tools/cbg.cpp).
- **Test strategy**: Black-box via `Process` API + hand-written asm targets that deliberately `trap` (via `getpid` + `kill(SIGTRAP)`) so the debugger can inspect/modify state between stops.
- **Git**: `experiments/` is intentionally gitignored (local instruction exploration). `build/` ignored.

---

## Known Gaps, Bugs & Sharp Edges

**Critical (will cause compile/runtime failure or wrong behavior if touched)**:
- [include/debugger.hpp](include/debugger.hpp) — Old `cbugger::debugger` class and `execute_debugee` are completely unused (historical artifact from before Process extraction).

**Note**: The previous critical subregister gaps (`parse_index`, undefined factories, incomplete `make_subview_by_name`) have been fully resolved. Subregister support is now complete and tested.

**Behavioral / API gaps**:
- No memory access primitives exposed.
- HW debug registers are raw r/w only; no `enable_breakpoint(addr, size)` helper that correctly programs the control registers per ARM ARM.
- Destructor [src/process.cpp:105](src/process.cpp) has complex attached-vs-launched logic; easy to get wrong on double-detach or already-exited processes.
- CLI has no persistent command history across sessions or tab completion for register names yet.

**Minor / polish**:
- `set_register` 128-bit path truncates the input value (still present).
- No `const` correctness or caching around repeated `get_register` string lookups (linear scan). (The interactive CLI now demonstrates the `RegisterDescriptor` fast path for its hottest "regs" / summary printers.)
- Error messages sometimes use `send_errno` even when errno is not the issue.
- CLI could benefit from better formatting for 128-bit vectors and pstate bit decoding.

---

## Design Decisions & Rationale

1. **Regsets instead of classic PTRACE_GETREGS** — Required for aarch64's larger register file and the separate HW debug / FPR banks. Also future-proofs for more regsets (TLS, SVE, SME, etc.).

2. **Pipe for child-to-parent error reporting** [src/process.cpp:44](src/process.cpp) — Classic fork/exec race problem. Child writes error string to pipe then exits; parent reads before returning the Process object. Avoids "zombie that we can't diagnose".

3. **SubregisterView as non-owning facade with explicit policies** — Keeps the hot register structs (gpr/fpr) as simple C arrays while still giving ergonomic `w20`, `d5`, `v3.4s[2]` access. The three `WritePolicy` values correctly encode AArch64 architectural rules. The layer is now complete and tested.

4. **terminate_on_end_ + careful dtor** — Distinguishes "I launched this, I own its life" vs "I attached, I must detach cleanly and not kill it". Running-state handling (SIGSTOP before detach) prevents the debugee from continuing after debugger death.

5. **Macro-driven register table** — Single source of truth for names, struct fields, formats, and dwarf IDs. Easy to add SVE Z registers later.

---

## History & Momentum

Recent work (most recent first; some changes may still be uncommitted working-tree state):

- **RegisterHandle unification (late May 2026)**: Promoted `RegisterHandle` (std::variant of `FullRegisterDescriptor` + `SubregisterDescriptor`), `resolve(name)`, handle-based `read()`/`write()`, and direct `make_subview(const SubregisterDescriptor&)` out of `experimental`. `set_register(std::string)` now fully supports subs via the descriptor path. CLI migrated to primarily use the unified handle for data movement. Added `parse_register_value()` helper and comprehensive tests. This completes the move from dual string-based paths to a single descriptor-driven model for both full registers and subregisters.
- **Register CLI wiring (2026-05)**: Full `register read` / `register write` commands wired in `tools/cbg.cpp`, including all subregister forms (`wN`, `sN`/`dN`, `vN.4s[k]`, `vN.2d[k]`). Value-syntax-based float vs raw-bit write decisions (GDB-like). Rich pretty-printing (raw hex + float interpretation for scalars). `regs` / `info registers` convenience commands. Stop-reason printing on every `continue` / `c`. Prompt changed to `cbg>`. `write_back_registers()` integrated. Extensive manual testing against trap targets.
- **CLI + register layer cleanup (late May 2026)**: Split monolithic `handle_register_command` into focused read/write handlers. Unified `Registers::set_register(std::string, uint64_t)` to handle both full registers and all subregister forms. Made the hottest CLI paths (`regs`, summary printers) use the `RegisterDescriptor` fast path. Extracted subregister name parsing (`wN`/`sN`/`dN`/`vN.*` grammar) into a dedicated `detail/register_name` unit, dramatically shrinking `registers.cpp`.
- Subregister completion (2026-02): Full implementation of `wN`, `sN`/`dN`, and `vN.4s[i]`/`vN.2d[i]` support. Fixed `parse_index`, implemented all factories (`make_wn` et al.), completed `make_subview_by_name` parser, added `read_sub_*`/`write_sub_*` helpers, refactored `SubregisterView` bit math for correct policy application (especially after-insert zeroing), added comprehensive Catch2 coverage, and implemented `Process::write_back_registers()`. All subregister assertions now pass.
- `d295904` — "refactoring register reads" (big register.inc + registers.cpp + test target + tools/cbg updates; 328 insertions).
- `ce65d81` — register reading/writing tests added.
- `6f90a1b` — added support for reading and writing registers (core of the current model).
- `5c69cc7` — tests for all process functionality.
- `667a560` — pipes for error communication from debugee (important robustness win).
- Earlier: logging, Process class extraction, basic CLI + continue.

The project has moved from "get a process stopped" to "I can see and mutate every architecturally visible register reliably via ptrace regsets, **including ergonomic subregister views with correct AArch64 zeroing semantics** — and a usable CLI (`register read`/`write`, `regs`) to drive it."

Experiments/ (adrp.s, ldr.s, ...) show ongoing investigation of aarch64 addressing modes and PIC codegen — useful when the debugger later needs to set breakpoints in shared libraries or PIE binaries.

---

## Recommended Next Steps / Roadmap (Prioritized)

**Subregister support and basic-to-good register CLI are now complete** (including factories, parser, helpers, policy-correct writes, value-syntax float/int handling for lanes, rich printing, stop-reason output, and `write_back_registers()` integration). The highest-leverage remaining items are:

1. **Memory access primitives** (`Process::read_memory`, `write_memory`) using `PTRACE_PEEKDATA`/`POKEDATA` or `process_vm_readv` for larger transfers. This is the clear next major capability.

2. **Software single-step + breakpoints**:
   - `PTRACE_SINGLESTEP`.
   - Trap instruction insertion (aarch64 `brk #0` or `svc` variants) + restore.

3. **High-level HW debug API** on top of the already-loaded `hw_break`/`hw_watch` structs (mask, BAS, PMC, enable bits per ARM debug v8.0+).

4. **Polish & robustness**:
   - Clean up orphaned `debugger.hpp`.
   - Fix 128-bit `set_register` path (truncates input).
   - Better error handling after natural exit.
   - Improved CLI (tab completion for register names, better vector formatting, command history persistence).
   - Optional: expose subregister access more ergonomically from the public API.

5. **Later** (when the above feels solid): DWARF line info + symbols (libdw), source stepping, watchpoint value latching, SVE/SME register support, etc.

---

## Appendix

### All non-build files in the repository (as of generation)
```
.dockerignore
.gitignore
CMakeLists.txt
CMakePresets.json
dockerfile
examples/hello_world.cpp
include/debugger.hpp
include/libcbg/bit.hpp
include/libcbg/detail/register.inc
include/libcbg/detail/register_name.hpp
include/libcbg/error.hpp
include/libcbg/pipe.hpp
include/libcbg/process.hpp
include/libcbg/registers.hpp
include/libcbg/subregister_view.hpp
Readme.md
src/CMakeLists.txt
src/pipe.cpp
src/process.cpp
src/register_name.cpp
src/registers.cpp
src/subregister_view.cpp
test/CMakeLists.txt
test/targets/CMakeLists.txt
test/targets/end_immediately.cpp
test/targets/register_read.s
test/targets/register_write.s
test/targets/run_endlessly.cpp
test/tests.cpp
tools/cbg.cpp
tools/CMakeLists.txt
vcpkg.json
```

(Note: `experiments/` exists locally but is gitignored and contains only transient asm exploration artifacts.)

### How to refresh this document
- Re-run the "summarize current state into memory-context" task (or ask a fresh agent to do so after reading this file + the plan.md that produced it).
- Or manually update the "Last updated" date + the specific sections that have changed.
- After any large register, process, or CLI change, the **Quick status**, **Feature Matrix**, **Known Gaps**, and **Recommended Next Steps** sections should be re-validated first.

---

*This file exists so that context survives compaction, session switches, and handoff to other agents or humans. Keep it honest and precise.*
