#pragma once

#include "simple_3dgs/gaussian.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace simple_3dgs {

struct PlyLoadStatistics {
    size_t fileBytes = 0;
    size_t gaussianCount = 0;
    size_t batchCount = 0;
    double headerSeconds = 0.0;
    double dataSeconds = 0.0;
    std::string path;
};

class PlyLoader {
public:
    [[nodiscard]] static std::vector<Gaussian> Load(const std::filesystem::path& path);
    [[nodiscard]] static std::vector<Gaussian> Load(
        const std::filesystem::path& path, PlyLoadStatistics* statistics);

    using BatchCallback = std::function<void(const Gaussian*, size_t)>;
    static PlyLoadStatistics LoadBatches(const std::filesystem::path& path,
                                         size_t batchSize,
                                         const BatchCallback& callback);
};

} // namespace simple_3dgs
