# Register Implementation Extension Progress

**Project**: cbugger — Linux aarch64 ptrace debugger  
**Purpose**: Track the current state and planned evolution of the register subsystem.  
**Last updated**: 2026-06-04 (after API consistency: get_subregister now returns SubregisterView (parallel to get_register); string set_* overloads removed; get/set use descriptor or unified handle; registers.cpp reorganized with section separators for navigation)

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
| String lookup (`get_register(name)`) | ✅ Mature | Linear scan on first use. Still supported for full registers (convenience in tests/CLI summaries); descriptor preferred for hot paths. |
| Subregister syntax (`make_subview_by_name`) | ✅ Mature | Supports `wN`, `sN`/`dN`, `vN.4s[k]`, `vN.2d[k]`. Correct policies. `get_subregister(name/desc)` preferred for "getting the subregister" (returns SubregisterView, parallels get_register returning RegisterView). (Legacy sub string helpers + value-only get_sub removed.) |
| Unified `RegisterHandle` (via `resolve(name)`) | ✅ Mature | `std::variant<FullRegisterDescriptor, SubregisterDescriptor>`. Recommended entry for values (read/write return __uint128_t bits). Also powers get_format/get_bit_size/parse. |
| Direct descriptor `make_subview(SubregisterDescriptor)` + `get_subregister(desc)` | ✅ Mature | `get_subregister(desc)` returns SubregisterView (consistent naming). `make_subview(desc)` is the builder (used internally). Avoids string roundtrips. |
| `RegisterDescriptor` fast path + `set_register(desc)` | ✅ Mature | `lookup(name)` + `get_register(d)` / `set_register(d, __u128)` for full. O(1) after resolution. |
| Numeric / Dwarf ID access | Partial | `dwarf_id` stored but unused for lookup (future for DWARF). |

### Recent Milestones
- **June 2026**: API consistency pass. `get_subregister(name/desc)` now returns SubregisterView (fully parallel to get_register returning RegisterView; value access is via the View or handle.read for uniformity across full/subs). All string-name `set_register`/`set_subregister` overloads removed (use resolve+write(h) or desc+set_*). `get_subregister`/`set_subregister` encapsulate sub logic. registers.cpp reorganized with clear //==== section separators (core, views/getters, sets, resolution, handle, parse, factories) for navigation. Updated CLI + all context docs.
- **late May 2026**: Major unification step. Promoted `RegisterHandle` (variant of `FullRegisterDescriptor` + `SubregisterDescriptor`) out of experimental. Added `resolve(name)`, handle-based `read()`/`write()`, `get_format()`/`get_bit_size()`, and static `parse_register_value()`. Introduced direct `make_subview(const SubregisterDescriptor&)` to eliminate string-synthesis hacks in hot paths. Cleaned factory signatures (`make_wn` and lane factories no longer take `zero_upper`; only scalar FP retain it as escape hatch for legacy string API). Migrated CLI (`tools/cbg.cpp`) to the new unified handle paths. Comprehensive test expansion (78 assertions).
- **May 2026**: Added minimal `RegisterDescriptor` + `lookup()` / fast overloads. This directly addressed the long-standing "linear scan on repeated lookups" gap noted in `memory-context.md`.

### Limitations (Current)
- Subregister syntax parsing (`wN`/`sN`/`dN`/`vN.*`) lives in dedicated internal unit (`detail/register_name.hpp` + `.cpp`); not yet public typed API (via resolve + get_subregister or legacy make_subview_by_name).
- String `get_register(name)` for full still supported (convenience/tests); all string `set_*` removed (recommended: resolve+write(h) or desc+set_*). `get_register`/`get_subregister` consistently return the View (value via View methods or handle.read for uniformity). 
- Initial `lookup(name)` for full registers still linear scan (descriptors help only after).
- No SVE, SME, or other architectural extensions.
- (done) High-level HW breakpoint arming (enable/disable + proper control bits) added on top of the raw fields during breakpoint implementation. Raw access remains available.
- No register groups/categories for better CLI organization/presentation.
- `dwarf_id` unused (future for DWARF).

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
- (implemented) `Process::enable_hw_breakpoint(addr)` / disable + ctrl construction (E/PMC/BAS/BT) + slot allocation using the pre-existing raw hw regset load/save.
- Watchpoint helpers are stubbed at similar level; full value-compare latching is future.
- BAS/PMC/etc. constants are defined in the impl.

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

- [memory-context.md](memory-context.md) — Overall project state and roadmap (including latest get_subregister as View, string set removal, .cpp reorg)
- `include/libcbg/detail/register.inc` — Current register table
- `include/libcbg/registers.hpp` — Public register API (get_register/get_subregister (Views), resolve, handle read/write, set_*(desc), parse_register_value, make_subview*, etc.)
- `include/libcbg/detail/register_name.hpp` — Internal subregister name parser (SubregisterSpec)
- `src/registers.cpp` — Implementation (reorganized with sections: core, views, sets, resolution, handle, parse, factories)

---

*This document should be updated whenever significant register-related work is completed or planned.*