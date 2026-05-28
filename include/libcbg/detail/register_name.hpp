#pragma once

#include <cstdint>
#include <string_view>

namespace cbg::detail
{
    // Lightweight result of parsing a subregister name (wN, sN, dN, vN.4s[k], vN.2d[k]).
    // Does not perform any register access; only decodes the syntactic form.
    // Used by Registers::make_subview_by_name to dispatch to the typed factories.
    struct SubregisterSpec
    {
        enum class Kind
        {
            None,
            Wn,         // w0..w30  (lower 32 bits of xN)
            Sn,         // s0..s31  (scalar single in vN)
            Dn,         // d0..d31  (scalar double in vN)
            V4sLane,    // vN.4s[lane]  lane 0..3
            V2dLane     // vN.2d[lane]  lane 0..1
        };

        Kind kind = Kind::None;
        uint8_t n = 0;      // register number (0-31)
        uint8_t lane = 0;   // lane index (only meaningful for V*Lane kinds)
    };

    // Parse a subregister name using the exact syntax rules currently implemented
    // inside Registers::make_subview_by_name. Returns Kind::None for any invalid
    // or unsupported form (including out-of-range indices).
    //
    // This function performs *only* syntactic analysis; it does not validate
    // against architectural limits or produce SubregisterViews.
    SubregisterSpec parse_subregister_name(std::string_view name);
}
