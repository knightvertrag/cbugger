# cbugger Memory Context

**Project**: cbugger — A simple Linux aarch64 debugger for C/C++ programs  
**Mission**: Build a from-scratch ptrace-based debugger targeting Linux aarch64 (lp64 ABI), starting with solid process control and a rich register model, then layering higher-level debugging features.  
**Last updated**: 2026-06-04 (after API consistency cleanup: get_subregister now returns SubregisterView like get_register returns RegisterView (eliminating value-vs-view confusion); string-name set_register/set_subregister overloads removed; get/set now consistently descriptor-based or via unified handle; registers.cpp reorganized with clear section separators; get_subregister(string/desc) added for view access)  
**Purpose**: Persistent AI / agent memory priming. Future sessions should read `Readme.md` + this file first to avoid re-exploring or making contradictory assumptions.  
**Quick status**: Mid-stage. Core process lifecycle (launch/attach/resume/wait) and full AArch64 register access (GPR + FPR + HW debug + subregisters, with full 128-bit vN) are functional. Subregister views (`wN`, `sN`/`dN`, `vN.4s[i]`/`vN.2d[i]`) with policies complete and tested. Unified `RegisterHandle` (resolve + read/write) is recommended for values; `get_register`/`get_subregister` (name or desc) return Views for structured access (full 128-bit via __uint128_t on views or handle). String `get_register` for full remains (for convenience); string `set_*` removed. `make_subview(desc)` preferred over by_name. CLI uses handle + views. No memory access, sw breakpoints, or high-level HW debug API yet. registers.cpp reorganized for navigation.

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
    CLI["tools/cbg.cpp<br>(readline 'cbg> ' loop + register commands)"]
    Process["libcbg::Process<br>(owns Registers, lifetime & state)"]
    Registers["Registers<br>load/save + resolve/read/write(handle)"]
    RegisterHandle["RegisterHandle<br>variant&lt;Full, SubregisterDescriptor&gt;<br>resolve(name) → handle"]
    Regsets["Kernel regsets<br>(NT_PRSTATUS / FPREGSET / HW_BREAK / HW_WATCH)"]
    Views["std::vector&lt;RegisterView&gt;<br>(internal, from REGISTER_LIST)"]
    SubregisterView["SubregisterView<br>+ WritePolicy"]
    Pipe["Pipe<br>(pre-exec error reporting)"]
    Linux["Linux kernel (aarch64)"]
    StopReason["stop_reason<br>(EXITED / TERMINATED / STOPPED)"]

    CLI -->|"launch(path) / attach(pid)"| Process
    Process -->|Pipe channel| Linux
    Process -->|"fork + ptrace(TRACEME/ATTACH)"| Linux
    Process -->|resume / wait_on_signal| Linux
    Process -->|"get_registers() / write_back_registers()"| Registers

    Registers -->|PTRACE_GET/SETREGSET| Regsets
    Registers -->|"build_views() + REGISTER_LIST macro"| Views
    Registers -->|"resolve(name)"| RegisterHandle
    Registers -->|"read(handle) / write(handle)<br>parse_register_value() [recommended]"| RegisterHandle
    Registers -->|"make_subview(desc) [preferred]<br>make_subview_by_name() [legacy]"| SubregisterView

    SubregisterView -->|"read_u64/write_u64 + policies<br>(ZeroExtend32To64, ZeroUpperVector128, PreserveParentBits)"| BitOps["128-bit bit manipulation<br>for wN / sN/dN / vN.lanes"]

    Linux -->|"signals (SIGTRAP etc.)"| StopReason

    style Process fill:#e6f3ff
    style Registers fill:#e6f3ff
    style RegisterHandle fill:#fff8e1
```

**Layers**:
- **Presentation**: `tools/cbg.cpp` (libedit readline, rich command dispatch including `register read`/`write`, `regs`, subregister formatting, and stop-reason printing).
- **Core Library** (`libcbg`): `Process`, `Registers`, `RegisterView`, `SubregisterView`, `Pipe`, `Error`.
- **OS Interaction**: Direct `ptrace(2)`, `waitpid`, `kill`, regset iovec calls. No higher abstractions (no `libunwind`, no `libdw` yet).

**Key data structures** (with locations):
- `process_state` + `stop_reason` — [include/libcbg/process.hpp:12](include/libcbg/process.hpp)
- `RegisterView` (name, data pointers, size, format, dwarf_id) — [include/libcbg/registers.hpp:29](include/libcbg/registers.hpp)
- `SubregisterView` + `WritePolicy` enum — [include/libcbg/subregister_view.hpp:8](include/libcbg/subregister_view.hpp)
- `SubregisterSpec` + `parse_subregister_name` (internal parser) — [include/libcbg/detail/register_name.hpp](include/libcbg/detail/register_name.hpp)
- `RegisterHandle` (variant&lt;FullRegisterDescriptor, SubregisterDescriptor&gt;) + `resolve()` / handle `read()` / `write()` / `parse_register_value()` + `get_subregister` (for SubregisterView) — [include/libcbg/registers.hpp](include/libcbg/registers.hpp)
- `RegisterDescriptor` (fast O(1) path for full registers) + `get_register(desc)` — [include/libcbg/registers.hpp](include/libcbg/registers.hpp)
- `get_subregister` (string/desc, returns SubregisterView for access, parallel to get_register) — [include/libcbg/registers.hpp](include/libcbg/registers.hpp)
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
| Register read/write via views (`get_register`, `set_register`) | ✅ | [include/libcbg/registers.hpp:92](include/libcbg/registers.hpp), [src/registers.cpp:128] | `get_register(name/desc)` returns RegisterView& (templated `.get<T>()` / `.set<T>()`). String name form still supported for full regs (convenience + tests); descriptor fast path preferred. set_register only via desc (string overloads removed). |
| Subregister views (wN, sN, dN, vN.lanes) | ✅        | `make_subview_by_name` + `make_subview(desc)` + `SubregisterView` + `get_subregister` [src/registers.cpp:163], [src/subregister_view.cpp:1] + dedicated test | Full support for `wN` (zero-extend), scalar `sN`/`dN` (zero-upper), vector lanes (`vN.4s[i]`, `vN.2d[i]` with preserve). `get_subregister(name/desc)` returns SubregisterView (consistent with get_register returning View; value via .read_u64() etc. or handle.read). `make_subview(desc)` preferred. (Legacy read/write_sub_* helpers + value-only get_subregister removed.) write_back_registers() implemented. Passing Catch2 coverage. |
| Full register CLI (read/write + subregisters + formatting) | ✅ | `handle_register_read` / `write` + helpers in [tools/cbg.cpp:318] | Fully on `resolve()` + `RegisterHandle` read/write + `get_subregister`/`get_register(d)` for display. `register read [name|all]`, `register write <name> <val>`, `regs`, `info registers`. Supports all sub forms + full 128-bit vN hex. Value-syntax (float/int). Rich printing via views. Uses RegisterDescriptor for hot paths. Stop reason on continue. Prompt `cbg>`. |
| Memory peek/poke                     | ✅        | `Process::read_memory` / `write_memory` (PTRACE_PEEKDATA/POKEDATA word API) | Word-based; sufficient for SW bp + general use. |
| Software breakpoints / single-step   | ✅        | `add_breakpoint` + transparent step-over in `wait_on_signal`, `step_instruction`, `PTRACE_SINGLESTEP`, brk #0 + orig restore + PC rewind | SW sites + auto step-over for clean presentation; dtor restores memory. |
| High-level HW breakpoint/watchpoint API | ✅     | `enable_hw_breakpoint(addr)` / `disable_hw_breakpoint(slot)`, num_*_slots(), ctrl bit builders (E/PMC/BAS/BT) | Built on already-loaded hw_break/hw_watch regsets + RegisterHandle write + write_back. Watchpoints similar but minimal impl. |
| Symbolication / DWARF / source correlation | ❌ | —                                              | No debug info parsing. Dwarf IDs are present in views as future hooks. |
| `write_back_registers()` on Process  | ✅        | Implemented [src/process.cpp:195] + used by CLI    | Convenience wrapper over `Registers::save()`. |
| Proper handling of process exit in wait | ✅      | [src/process.cpp:175]                              | `pid_` is cleared and "Process exited" is printed on EXITED/TERMINATED. |

**Tests exercising the above**:
- Process lifecycle (launch, attach, resume, dtor) — [test/tests.cpp:31](test/tests.cpp)
- Register r/w round-trips via asm `trap` targets — [test/tests.cpp:83](test/tests.cpp), [test/targets/register_*.s](test/targets/)
- Subregister views, policies, and helpers (wN zero-extend, sN/dN zero-upper, lane preserve, error cases) + get_subregister — dedicated test case in [test/tests.cpp](test/tests.cpp) (~30 assertions in sub test alone)
- Handle-based read/write, resolve, parse, 128-bit vN full access — [test/tests.cpp:306](test/tests.cpp) (RegisterHandle test)
- CLI register commands exercised manually against trap targets (full automated CLI testing thin). Total ~95 assertions across 12 cases.

---

## Register Model Deep Dive (Most Important Technical Area)

This is the most sophisticated part of the current codebase (heavy focus of commits 6f90a1b and d295904).

### How it works
1. `REGISTER_LIST(GPR, FPR, HWBRK, HWWATCH)` macro in [include/libcbg/detail/register.inc:9](include/libcbg/detail/register.inc) enumerates:
   - 34 GPR entries (x0–x30, sp, pc, pstate) with dwarf IDs 0–33.
   - 34 FPR entries (v0–v31 + fpsr/fpcr) with dwarf IDs 64–97.
   - 32 HW debug entries (brk_addr/ctrl 0–15, watch_addr/ctrl 0–15) — dwarf_id = -1.
2. `Registers::build_views()` [src/registers.cpp:100](src/registers.cpp) expands the macro into `std::vector<RegisterView>`.
3. `load()` / `save()` use `PTRACE_GETREGSET` / `SETREGSET` with `struct iovec` + the four NT_* constants. HW debug sizes are dynamic (computed from `dbg_info` field).
4. `RegisterView` holds **two pointers** (const data + mutable writable_data) into the live `gpr` / `fpr` / `hw_*` structs.
5. Subregister support (`wN`, `sN`/`dN`, `vN.4s[i]` / `vN.2d[i]`) is fully implemented in `SubregisterView` [include/libcbg/subregister_view.hpp](include/libcbg/subregister_view.hpp) + [src/subregister_view.cpp](src/subregister_view.cpp). It performs 128-bit math over the parent storage and applies the correct `WritePolicy` on write (zero-extend for wN, zero-upper for scalar FP, preserve for lanes). `get_subregister` (name/desc) returns SubregisterView for access (consistent naming with get_register).

### Why this design
- aarch64 has far more state than the old `user_regs_struct`; regsets are the modern, extensible interface.
- Subregister views solve the common debugger problem of "when user says `w20` or `s5`, what bits actually move and what gets zeroed in the parent register?"
- Write policies (`ZeroExtend32To64`, `ZeroUpperVector128`, `PreserveParentBits`) encode ARM architectural rules cleanly.

A lightweight `RegisterDescriptor` (obtained once via `Registers::lookup(name)`) provides O(1) access for full registers. A first-class `RegisterHandle` (obtained via the unified `Registers::resolve(name)`) now represents *both* full registers and subregisters, enabling handle-based `read()` / `write()` and direct `make_subview(const SubregisterDescriptor&)` construction. This unifies the previous dual string-based paths while preserving the original `RegisterView` / `SubregisterView` implementations.

### Current gaps in the register layer (precise locations)
- No string-name overloads for `set_register`/`set_subregister` (removed as unnecessary; use resolve(name) + handle write, or lookup + set_*(desc) for full/sub). `get_register` (string) still available for full regs convenience. `get_subregister(name/desc)` consistently returns SubregisterView (like get_register returns View); raw values via View methods or handle.read(h). Internal legacy `make_subview_by_name` uses for vN minimized (prefer resolve + make_subview(desc)). Legacy read/write_sub_* helpers removed.
- Initial `lookup(name)` for full registers still linear scan (descriptors help only after).
- No higher-level helpers yet for convenient subregister access from the CLI or for SVE/SME registers (future work).
- Higher-level conveniences on top of `RegisterHandle` (typed `read<T>()`, richer pretty-printing integration, ergonomic handle construction) still evolving. Core unification + 128-bit Vn + consistent get/set views is complete. registers.cpp reorganized with section separators.

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
- (legacy include/debugger.hpp was deleted as part of breakpoint implementation + review feedback; it was fully orphaned with zero includes or build references.)

**Note**: Previous critical subregister gaps (parse_index, factories, make_subview_by_name) fully resolved. String set_* overloads removed. `get_subregister` (string/desc) now returns SubregisterView (consistent with get_register returning View; raw value via view or handle.read). Legacy uint64 set overloads + read/write_sub_* removed. registers.cpp reorganized. Sub support (handle + views + string compat) complete and tested.

**Behavioral / API gaps**:
- No memory access primitives exposed.
- HW debug registers are raw r/w only; no `enable_breakpoint(addr, size)` helper that correctly programs the control registers per ARM ARM.
- Destructor [src/process.cpp:105](src/process.cpp) has complex attached-vs-launched logic; easy to get wrong on double-detach or already-exited processes.
- CLI has no persistent command history across sessions or tab completion for register names yet.

**Minor / polish**:
- No string `set_*` (by design; use handle for string names). `get_register(string)` for full remains (linear scan on first use; CLI uses desc fast path for hot summaries).
- No `const` correctness or caching around repeated `get_register(string)` lookups (linear scan). (Interactive CLI demonstrates `RegisterDescriptor` for "regs"/summaries.)
- Error messages sometimes use `send_errno` even when errno is not the issue.
- CLI could benefit from better formatting for 128-bit vectors and pstate bit decoding. No persistent history or tab completion for register names yet.

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

- **API consistency + get_subregister as View + registers.cpp reorg (2026-06)**: `get_subregister(name/desc)` finalized to return SubregisterView (fully consistent with `get_register` returning RegisterView; eliminates value-vs-view confusion). String-name overloads for `set_register`/`set_subregister` removed (handle + desc only; string gets for full remain for convenience). `get_subregister`/`set_subregister` encapsulate sub view logic (parallel to full). registers.cpp reorganized with distinct //==== separators (core, getters/views, setters, resolution, handle ops, parse, factories) for navigation. CLI uses get_subregister for sub display. Updated all context docs. register model now very mature and consistent.
- **RegisterHandle unification (late May 2026)**: Promoted `RegisterHandle` (std::variant<FullRegisterDescriptor, SubregisterDescriptor>), `resolve(name)`, handle-based `read()`/`write()`, `get_format()`, `get_bit_size()`, and static `parse_register_value()` out of experimental. Added direct `make_subview(const SubregisterDescriptor&)` (no more synthetic "wN" strings). Cleaned `SubregisterView` factory signatures (`make_wn` + lane factories no longer take `zero_upper`). Migrated the entire CLI (`tools/cbg.cpp`) to the new unified path + `RegisterDescriptor` fast paths for printing. Extracted name parsing to `detail::`. Expanded tests to 78 assertions. This is the completion of the long-running register/subregister unification effort.
- **128-bit Vn handling + set API cleanup (2026-06)**: Addressed truncation limitations for full vector registers (v0–v31) in the recommended unified path. `read(RegisterHandle)` / `write(RegisterHandle, __uint128_t)` / `parse_register_value` now use and preserve full 128-bit values for Vec128 (long 0x hex input supported for writes). Removed string name overloads for `set_register`/`set_subregister` (not necessary; use resolve+write or desc+set_*). `set_register` (desc) only for full regs. Sub writes via set_subregister(desc,u64). CLI updated to use get_subregister for subs. Legacy make_subview_by_name uses minimized internally; old read/write_sub_* and value-only get_sub removed. Updated tests/docs. The "minor gap" for 128-bit set_register is resolved for the RegisterHandle API.
- **Register CLI wiring (2026-05)**: Full `register read` / `register write` commands wired in `tools/cbg.cpp`, including all subregister forms (`wN`, `sN`/`dN`, `vN.4s[k]`, `vN.2d[k]`). Value-syntax-based float vs raw-bit write decisions (GDB-like). Rich pretty-printing (raw hex + float interpretation for scalars). `regs` / `info registers` convenience commands. Stop-reason printing on every `continue` / `c`. Prompt changed to `cbg>`. `write_back_registers()` integrated. Extensive manual testing against trap targets.
- **CLI + register layer cleanup (late May 2026)**: Split monolithic `handle_register_command` into focused read/write handlers. (Later cleanups removed uint64_t set_* overloads and all string-name set_register/set_subregister overloads in favor of handle write via resolve + descriptor set_*.) Made the hottest CLI paths (`regs`, summary printers) use the `RegisterDescriptor` fast path. Extracted subregister name parsing (`wN`/`sN`/`dN`/`vN.*` grammar) into a dedicated `detail/register_name` unit, dramatically shrinking `registers.cpp`.
- Subregister completion (2026-02): Full implementation of `wN`, `sN`/`dN`, and `vN.4s[i]`/`vN.2d[i]` support. Fixed `parse_index`, implemented all factories (`make_wn` et al.), completed `make_subview_by_name` parser, added `read_sub_*`/`write_sub_*` helpers, refactored `SubregisterView` bit math for correct policy application (especially after-insert zeroing), added comprehensive Catch2 coverage, and implemented `Process::write_back_registers()`. All subregister assertions now pass.
- `d295904` — "refactoring register reads" (big register.inc + registers.cpp + test target + tools/cbg updates; 328 insertions).
- `ce65d81` — register reading/writing tests added.
- `6f90a1b` — added support for reading and writing registers (core of the current model).
- `5c69cc7` — tests for all process functionality.
- `667a560` — pipes for error communication from debugee (important robustness win).
- Earlier: logging, Process class extraction, basic CLI + continue.

The project has moved from "get a process stopped" to "I can see and mutate every architecturally visible register reliably via ptrace regsets (full + subs, with consistent View-based get_* + unified handle read/write for values) — and a usable CLI (`register read`/`write`, `regs`) to drive it." registers.cpp reorganized with clear sections.

Experiments/ (adrp.s, ldr.s, ...) show ongoing investigation of aarch64 addressing modes and PIC codegen — useful when the debugger later needs to set breakpoints in shared libraries or PIE binaries.

---

## Recommended Next Steps / Roadmap (Prioritized)

**Subregister support, 128-bit full vN register support via the unified handle API, and basic-to-good register CLI are now complete** (including factories, parser, helpers, policy-correct writes, value-syntax float/int handling for lanes, rich printing, stop-reason output, and `write_back_registers()` integration). The highest-leverage remaining items (as of post-breakpoint implementation) are:

1. **(done)** Memory access primitives + full SW + HW breakpoint support (including transparent step-over for SW bps, PC rewind presentation, dtor hygiene for SW sites, high-level enable/disable on the raw debug regsets, CLI integration, and tests) are now complete.

2. Symbolication / DWARF / source correlation (libdw) for "break main", source stepping, etc.

3. SVE/SME register extensions, richer watchpoint support (value match, etc.), and robustness for threads/forks.

4. **Polish & robustness**:
   - Clean up orphaned `debugger.hpp`.
   - No more legacy uint64 set paths (string sets removed; __uint128_t + handle fully address vN). 
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
