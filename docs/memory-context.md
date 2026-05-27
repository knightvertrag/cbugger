# cbugger Memory Context

**Project**: cbugger — A simple Linux aarch64 debugger for C/C++ programs  
**Mission**: Build a from-scratch ptrace-based debugger targeting Linux aarch64 (lp64 ABI), starting with solid process control and a rich register model, then layering higher-level debugging features.  
**Last updated**: 2026-02-06 (after completion of full subregister support + passing tests)  
**Purpose**: Persistent AI / agent memory priming. Future sessions should read `Readme.md` + this file first to avoid re-exploring or making contradictory assumptions.  
**Quick status**: Early/Mid-stage. Core process lifecycle (launch/attach/resume/wait) and full AArch64 register access (GPR + FPR + HW debug + subregisters) are functional in the library. Subregister views (`wN`, `sN`/`dN`, `vN.4s[i]`/`vN.2d[i]`) with correct architectural write policies are now complete and tested. High-level CLI, memory access, breakpoints, and stepping remain unimplemented.

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
    CLI["tools/cbg.cpp\n(readline 'sdb> ' loop + handle_command)"] 
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
- **Presentation**: `tools/cbg.cpp` (libedit readline, command dispatch, very thin today).
- **Core Library** (`libcbg`): `Process`, `Registers`, `RegisterView`, `SubregisterView`, `Pipe`, `Error`.
- **OS Interaction**: Direct `ptrace(2)`, `waitpid`, `kill`, regset iovec calls. No higher abstractions (no `libunwind`, no `libdw` yet).

**Key data structures** (with locations):
- `process_state` + `stop_reason` — [include/libcbg/process.hpp:12](include/libcbg/process.hpp)
- `RegisterView` (name, data pointers, size, format, dwarf_id) — [include/libcbg/registers.hpp:29](include/libcbg/registers.hpp)
- `SubregisterView` + `WritePolicy` enum — [include/libcbg/subregister_view.hpp:8](include/libcbg/subregister_view.hpp)
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
| Basic CLI "continue" + help          | ✅        | `handle_command` [tools/cbg.cpp:77]                | Only two commands wired; register commands exist only in help text. |
| Memory peek/poke                     | ❌        | —                                                   | No `PTRACE_PEEKDATA`, `process_vm_readv`, etc. yet. |
| Software breakpoints / single-step   | ❌        | —                                                   | No `PTRACE_SINGLESTEP` or trap insertion. |
| High-level HW breakpoint/watchpoint API | ❌     | Raw reg access only [include/libcbg/detail/register.inc:82] | No helpers to arm `dbg_regs[].ctrl` (BAS, PMC, E, etc.). |
| Symbolication / DWARF / source correlation | ❌ | —                                              | No debug info parsing. Dwarf IDs are present in views as future hooks. |
| `write_back_registers()` on Process  | ❌        | Declared only [include/libcbg/process.hpp:55]      | Tests and CLI call `regs.save()` directly. |
| Proper handling of process exit in wait | 🟡     | Commented out [src/process.cpp:175]                | Currently leaves pid_ non-zero after exit. |

**Tests exercising the above**:
- Process lifecycle (launch, attach, resume, dtor) — [test/tests.cpp:30](test/tests.cpp)
- Register r/w round-trips via asm `trap` targets — [test/tests.cpp:82](test/tests.cpp), [test/targets/register_*.s](test/targets/)
- Subregister views, policies, and helpers (wN zero-extend, sN/dN zero-upper, lane preserve, error cases) — new dedicated test case in [test/tests.cpp](test/tests.cpp)

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

### Current gaps in the register layer (precise locations)
- Minor: `set_register` for 16-byte registers [src/registers.cpp:162](src/registers.cpp) takes a `uint64_t value` and casts it — loses the upper bits the caller probably wanted (unchanged from prior state).
- No higher-level helpers yet for convenient subregister access from the CLI or for SVE/SME registers (future work).

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
- [include/libcbg/process.hpp:55](include/libcbg/process.hpp) — `write_back_registers()` is now implemented (done as part of subregister work).
- [tools/cbg.cpp:73](tools/cbg.cpp) — `handle_command` only implements `continue` and `help`. All register help text is dead code.
- [include/debugger.hpp](include/debugger.hpp) — Old `cbugger::debugger` class and `execute_debugee` are completely unused (historical artifact from before Process extraction).

**Note**: The previous critical subregister gaps (`parse_index`, undefined factories, incomplete `make_subview_by_name`) have been fully resolved. Subregister support is now complete and tested.

**Behavioral / API gaps**:
- No memory access primitives exposed.
- HW debug registers are raw r/w only; no `enable_breakpoint(addr, size)` helper that correctly programs the control registers per ARM ARM.
- `stop_reason` printing for the user is commented out in the CLI.
- Destructor [src/process.cpp:105](src/process.cpp) has complex attached-vs-launched logic; easy to get wrong on double-detach or already-exited processes.
- After natural exit, `pid_` is not cleared (commented code at [src/process.cpp:175](src/process.cpp)).
- CLI prompt still says `sdb>` (leftover from an earlier name?).

**Minor / polish**:
- `set_register` 128-bit path truncates the input value.
- No `const` correctness or caching around repeated `get_register` string lookups (linear scan).
- Error messages sometimes use `send_errno` even when errno is not the issue.

---

## Design Decisions & Rationale

1. **Regsets instead of classic PTRACE_GETREGS** — Required for aarch64's larger register file and the separate HW debug / FPR banks. Also future-proofs for more regsets (TLS, SVE, SME, etc.).

2. **Pipe for child-to-parent error reporting** [src/process.cpp:44](src/process.cpp) — Classic fork/exec race problem. Child writes error string to pipe then exits; parent reads before returning the Process object. Avoids "zombie that we can't diagnose".

3. **SubregisterView as non-owning facade with explicit policies** — Keeps the hot register structs (gpr/fpr) as simple C arrays while still giving ergonomic `w20`, `d5`, `v3.4s[2]` access. The three `WritePolicy` values correctly encode AArch64 architectural rules. The layer is now complete and tested.

4. **terminate_on_end_ + careful dtor** — Distinguishes "I launched this, I own its life" vs "I attached, I must detach cleanly and not kill it". Running-state handling (SIGSTOP before detach) prevents the debugee from continuing after debugger death.

5. **Macro-driven register table** — Single source of truth for names, struct fields, formats, and dwarf IDs. Easy to add SVE Z registers later.

---

## History & Momentum

Recent commit timeline (most recent first):

- Subregister completion (2026-02): Full implementation of `wN`, `sN`/`dN`, and `vN.4s[i]`/`vN.2d[i]` support. Fixed `parse_index`, implemented all factories (`make_wn` et al.), completed `make_subview_by_name` parser, added `read_sub_*`/`write_sub_*` helpers, refactored `SubregisterView` bit math for correct policy application (especially after-insert zeroing), added comprehensive Catch2 coverage, and implemented `Process::write_back_registers()`. All subregister assertions now pass.
- `d295904` — "refactoring register reads" (big register.inc + registers.cpp + test target + tools/cbg updates; 328 insertions).
- `ce65d81` — register reading/writing tests added.
- `6f90a1b` — added support for reading and writing registers (core of the current model).
- `5c69cc7` — tests for all process functionality.
- `667a560` — pipes for error communication from debugee (important robustness win).
- Earlier: logging, Process class extraction, basic CLI + continue.

The project has moved from "get a process stopped" to "I can see and mutate every architecturally visible register reliably via ptrace regsets, **including ergonomic subregister views with correct AArch64 zeroing semantics**."

Experiments/ (adrp.s, ldr.s, ...) show ongoing investigation of aarch64 addressing modes and PIC codegen — useful when the debugger later needs to set breakpoints in shared libraries or PIE binaries.

---

## Recommended Next Steps / Roadmap (Prioritized)

**Subregister support is now complete** (including factories, parser, helpers, policy-correct writes, and passing tests). The highest-leverage remaining items are:

1. **Wire the CLI for registers** (highest immediate usability win):
   - Implement `register read [name|all]`, `register write <name> <value>` (now that subregisters are fully functional).
   - Print stop reason on every wait (uncomment + improve).
   - Add `registers` or `info registers` command.

2. **Memory access primitives** (`Process::read_memory`, `write_memory`) using `PTRACE_PEEKDATA`/`POKEDATA` or `process_vm_readv` for larger transfers.

3. **Software single-step + breakpoints**:
   - `PTRACE_SINGLESTEP`.
   - Trap instruction insertion (aarch64 `brk #0` or `svc` variants) + restore.

4. **High-level HW debug API** on top of the already-loaded `hw_break`/`hw_watch` structs (mask, BAS, PMC, enable bits per ARM debug v8.0+).

5. **Polish**:
   - Clean up orphaned `debugger.hpp`.
   - Fix 128-bit `set_register` path (truncates input).
   - Better error handling after natural exit.
   - Optional: expose subregister access more ergonomically from the public API.

6. **Later** (when the above feels solid): DWARF line info + symbols (libdw), source stepping, watchpoint value latching, SVE/SME register support, etc.

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
include/libcbg/error.hpp
include/libcbg/pipe.hpp
include/libcbg/process.hpp
include/libcbg/registers.hpp
include/libcbg/subregister_view.hpp
Readme.md
src/CMakeLists.txt
src/pipe.cpp
src/process.cpp
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
- After any large register or process change, the **Register Model Deep Dive** and **Gaps** sections should be re-validated first.

---

*This file exists so that context survives compaction, session switches, and handoff to other agents or humans. Keep it honest and precise.*
