#pragma once

#include "simple_3dgs/gaussian.hpp"

#include <filesystem>
#include <vector>

namespace simple_3dgs {

class PlyLoader {
public:
    [[nodiscard]] static std::vector<Gaussian> Load(const std::filesystem::path& path);
};

} // namespace simple_3dgs
