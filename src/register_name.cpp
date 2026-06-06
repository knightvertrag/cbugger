#include <libcbg/detail/register_name.hpp>

// =============================================================================
// register_name.cpp - subregister name parser (wN, sN, dN, vN.4s[l], vN.2d[l])
// =============================================================================

namespace
{
    // -------------------------------------------------------------------------
    // Small helpers (local to this TU)
    // -------------------------------------------------------------------------

    int is_digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    int parse_index(std::string_view sv)
    {
        if (sv.empty())
            return -1;
        int v = 0;
        for (char c : sv)
        {
            if (!is_digit(c))
                return -1;
            v = v * 10 + (c - '0');
        }
        return v;
    }
}

namespace cbg::detail
{
    // -------------------------------------------------------------------------
    // Subregister name parser (public detail API)
    // -------------------------------------------------------------------------

    SubregisterSpec parse_subregister_name(std::string_view name)
    {
        if (name.empty())
            return {};

        // wN => lower 32 bits of xN (N=0..30)
        if (name.size() >= 2 && name[0] == 'w' && is_digit(name[1]))
        {
            int n = parse_index(name.substr(1));
            if (n >= 0 && n <= 30)
            {
                SubregisterSpec spec{};
                spec.kind = SubregisterSpec::Kind::Wn;
                spec.n = static_cast<uint8_t>(n);
                return spec;
            }
            return {};
        }

        // sN/dN => scalar FP views of vN (N=0..31)
        if (name.size() >= 2 && (name[0] == 's' || name[0] == 'd') && is_digit(name[1]))
        {
            int n = parse_index(name.substr(1));
            if (n >= 0 && n <= 31)
            {
                SubregisterSpec spec{};
                spec.kind = (name[0] == 's')
                                ? SubregisterSpec::Kind::Sn
                                : SubregisterSpec::Kind::Dn;
                spec.n = static_cast<uint8_t>(n);
                return spec;
            }
            return {};
        }

        // vN.4s[lane] or vN.2d[lane]  (N=0..31, lane bounds checked here)
        if (name.size() >= 2 && name[0] == 'v' && is_digit(name[1]))
        {
            size_t p = 1;
            while (p < name.size() && is_digit(name[p]))
                ++p;

            int n = parse_index(name.substr(1, p - 1));
            if (n < 0 || n > 31)
                return {};

            if (p >= name.size() || name[p] != '.')
                return {};
            ++p;

            bool is_4s = false;
            if (p + 1 < name.size() && name[p] == '4' && name[p + 1] == 's')
            {
                is_4s = true;
                p += 2;
            }
            else if (p + 1 < name.size() && name[p] == '2' && name[p + 1] == 'd')
            {
                is_4s = false;
                p += 2;
            }
            else
            {
                return {};
            }

            if (p >= name.size() || name[p] != '[')
                return {};
            ++p;

            size_t lane_start = p;
            while (p < name.size() && is_digit(name[p]))
                ++p;

            int lane = parse_index(name.substr(lane_start, p - lane_start));
            if (lane < 0)
                return {};

            if (p >= name.size() || name[p] != ']')
                return {};

            if (is_4s)
            {
                if (lane > 3)
                    return {};
                SubregisterSpec spec{};
                spec.kind = SubregisterSpec::Kind::V4sLane;
                spec.n = static_cast<uint8_t>(n);
                spec.lane = static_cast<uint8_t>(lane);
                return spec;
            }
            else
            {
                if (lane > 1)
                    return {};
                SubregisterSpec spec{};
                spec.kind = SubregisterSpec::Kind::V2dLane;
                spec.n = static_cast<uint8_t>(n);
                spec.lane = static_cast<uint8_t>(lane);
                return spec;
            }
        }

        return {};
    }

} // namespace cbg::detail
