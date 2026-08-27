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

struct PlyModelMetadata {
    bool supportsSortFree = false;
    float weightBackground = 0.0F;
    float sigma = 0.0F;
    std::string sortFreeDiagnostic;
};

class PlyLoader {
public:
    [[nodiscard]] static std::vector<Gaussian> Load(const std::filesystem::path& path);
    [[nodiscard]] static std::vector<Gaussian> Load(
        const std::filesystem::path& path, PlyLoadStatistics* statistics);
    [[nodiscard]] static std::vector<Gaussian> Load(
        const std::filesystem::path& path, PlyLoadStatistics* statistics,
        PlyModelMetadata* metadata);

    using BatchCallback = std::function<void(const Gaussian*, size_t)>;
    static PlyLoadStatistics LoadBatches(const std::filesystem::path& path,
                                         size_t batchSize,
                                         const BatchCallback& callback,
                                         PlyModelMetadata* metadata = nullptr);
};

} // namespace simple_3dgs
