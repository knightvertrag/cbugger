# Register Implementation Extension Progress

**Project**: cbugger — Linux aarch64 ptrace debugger  
**Purpose**: Track the current state and planned evolution of the register subsystem.  
**Last updated**: 2026-05-28 (after introduction of `RegisterDescriptor` fast path)

---

## Overview

The register layer is one of the most important and carefully designed parts of libcbg. It is built around a single source of truth (`REGISTER_LIST` macro) and provides two parallel access styles:

- **String-based API** (primary for users and the CLI)
- **Fast descriptor-based API** (for performance-sensitive paths)

This document tracks how far we have come and what the major extension areas are for the future.

---

## Current State (May 2026)

### Core Architecture
- Single source of truth: `REGISTER_LIST` macro in `include/libcbg/detail/register.inc`.
- Currently defines ~132 registers:
  - 34 GPRs (`x0`–`x30`, `sp`, `pc`, `pstate`)
  - 34 FPR/SIMD (`v0`–`v31` + `fpsr`, `fpcr`)
  - 32 Hardware Breakpoint registers (raw `brk_addr`/`brk_ctrl`)
  - 32 Hardware Watchpoint registers (raw `watch_addr`/`watch_ctrl`)
- `RegisterView` provides non-owning access into live `user_pt_regs`, `user_fpsimd_state`, and `user_hwdebug_state` structures.
- Full support for `PTRACE_GETREGSET` / `SETREGSET` (NT_PRSTATUS, NT_FPREGSET, NT_ARM_HW_BREAK, NT_ARM_HW_WATCH).
- Correct AArch64 subregister write policies via `SubregisterView` + `WritePolicy` enum.

### Access Methods
| Access Style              | Status     | Notes |
|---------------------------|------------|-------|
| String lookup (`get_register(name)`) | ✅ Mature | Linear scan on first use. Used everywhere in CLI and tests. |
| Subregister syntax (`make_subview_by_name`) | ✅ Mature | Supports `wN`, `sN`/`dN`, `vN.4s[k]`, `vN.2d[k]`. Correct policies implemented and tested. |
| `RegisterDescriptor` fast path | ✅ New (May 2026) | `lookup(name)` returns a stable handle. Subsequent access is O(1) via index. Additive — string API unchanged. |
| Numeric / Dwarf ID access | Partial | `dwarf_id` is stored but not used for lookup. |

### Recent Milestone
- **May 2026**: Added minimal `RegisterDescriptor` + `lookup()` / fast overloads. This directly addressed the long-standing "linear scan on repeated lookups" gap noted in `memory-context.md`.

### Limitations (Current)
- Subregisters are still purely string-driven (by design — they are parametric).
- Initial `lookup(name)` still does a linear scan (descriptors only help *after* resolution).
- No SVE, SME, or other architectural extensions.
- HW debug registers are only available as raw fields (no high-level arming API).
- No register groups/categories for better organization or CLI presentation.
- `dwarf_id` is unused (future hook for debug info).

---

## Future Scope & Roadmap

### 1. SVE / SVE2 Support (High Priority Long-term)
- Add `z0`–`z31` (256-bit scalable vectors)
- Add `p0`–`p15` predicate registers + `ffr`
- Handle variable vector length (`vl`, `vscale`)
- New `RegisterFormat` variants (or dynamic sizing)
- Subregister views for SVE (`.b`, `.h`, `.s`, `.d`, `.q` slices, etc.)

This is the largest and most complex future extension. The macro-driven design was explicitly created with this in mind.

### 2. SME / SME2 Support
- `za` array + streaming mode registers
- `zt0` tile register
- Interaction with SVE state

### 3. Richer Typed / Enum Access (On Top of Descriptors)
Possible directions:
- Category enums (`GprReg`, `FprReg`, `SveReg`, etc.)
- `RegisterDescriptor` enriched with more metadata
- `lookup()` returning richer handles
- Generated `to_string()` / `from_string()` tables

The goal is **not** to replace the string API, but to provide compile-time safe and faster paths for internal / advanced use.

### 4. High-Level Hardware Debug API
- Proper helpers to arm/disable breakpoints and watchpoints
- BAS (byte address select), PMC (privilege mode control), etc.
- Value watchpoints with data value comparison

### 5. Register Organization & Presentation
- Register groups (General, Floating Point, Vector, Debug, etc.)
- Better support for "info registers" style grouping in the CLI
- Improved pretty-printing for vectors (lane views, float/int interpretations)

### 6. Performance & Lookup Improvements
- Build a name → index map (or perfect hash) during `build_views()` so even the first `lookup(name)` is fast.
- Optional case-insensitive or alias-aware lookup.
- Pre-resolve common registers (used by the `regs` command) at `Registers` construction time.

### 7. Better Subregister Coverage
- Additional arrangement specifiers (`.b`, `.h`, `.q`, etc.)
- More flexible lane access
- Public typed factory functions (`make_wn()`, `make_vn_lane_*()`) for advanced users who want to avoid strings

### 8. Debug Information Integration
- Make `dwarf_id` actually useful
- Support for reading register values from DWARF expressions / location lists
- Better interaction with future source-level debugging features

### 9. Target Description / Extensibility
- Move toward a more declarative register description (similar to GDB target descriptions)
- Make it easier to add new architectures or extensions without heavy macro changes

---

## Design Principles for Future Extensions

1. **String API stays primary** — especially for the CLI and anything involving subregisters or user input.
2. **Macro remains the single source of truth** — any new registers should be added in one place (`register.inc`).
3. **Additive, never breaking** — new access methods must coexist with existing ones.
4. **Follow real debugger precedent** — GDB/LLDB use strings + cached descriptors/handles + numeric IDs. We should continue aligning with that model.
5. **Subregisters are special** — they are derived views, not independent registers. Their API will likely remain name/parser-based.

---

## Related Documents

- [memory-context.md](memory-context.md) — Overall project state and roadmap
- `include/libcbg/detail/register.inc` — Current register table
- `include/libcbg/registers.hpp` — Public register API
- `src/registers.cpp` — Implementation of lookup and descriptor logic

---

*This document should be updated whenever significant register-related work is completed or planned.*