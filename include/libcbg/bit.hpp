#pragma once

#include <vector>
#include <string_view>

namespace cbg
{
    inline std::string_view to_string_view(const std::byte* data, std::size_t size)
    {
        return {reinterpret_cast<const char *>(data), size};
    }

    inline std::string_view to_string_view(const std::vector<std::byte>& data)
    {
        return to_string_view(data.data(), data.size());
    }

}