#pragma once

#include <cstdint>
#include <filesystem>

namespace lw::web2android {

struct IconInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class IconGenerator {
public:
    static IconInfo Inspect(const std::filesystem::path& source);
    static void Generate(const std::filesystem::path& source,
                         const std::filesystem::path& resourceDirectory);
};

}  // namespace lw::web2android
