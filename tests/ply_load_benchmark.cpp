#include "simple_3dgs/ply_loader.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 2) {
        std::cerr << "usage: ply_load_benchmark <point_cloud.ply>\n";
        return 2;
    }

    try {
        const std::filesystem::path path = arguments[1];
        const auto start = std::chrono::steady_clock::now();
        simple_3dgs::PlyLoadStatistics statistics;
        const auto gaussians = simple_3dgs::PlyLoader::Load(path, &statistics);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        std::array<float, 3> minimum {
            std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        std::array<float, 3> maximum {
            std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        float minimumScale = std::numeric_limits<float>::max();
        float maximumScale = std::numeric_limits<float>::lowest();
        float minimumOpacity = std::numeric_limits<float>::max();
        float maximumOpacity = std::numeric_limits<float>::lowest();
        for (const auto& gaussian : gaussians) {
            for (size_t axis = 0; axis < 3; ++axis) {
                minimum[axis] = std::min(minimum[axis], gaussian.position[axis]);
                maximum[axis] = std::max(maximum[axis], gaussian.position[axis]);
                minimumScale = std::min(minimumScale, gaussian.scale[axis]);
                maximumScale = std::max(maximumScale, gaussian.scale[axis]);
            }
            minimumOpacity = std::min(minimumOpacity, gaussian.opacity);
            maximumOpacity = std::max(maximumOpacity, gaussian.opacity);
        }

        std::cout << std::fixed << std::setprecision(3)
                  << "file_bytes=" << std::filesystem::file_size(path) << '\n'
                  << "gaussian_count=" << gaussians.size() << '\n'
                  << "loader_path=" << statistics.path << '\n'
                  << "batch_count=" << statistics.batchCount << '\n'
                  << "header_seconds=" << statistics.headerSeconds << '\n'
                  << "data_seconds=" << statistics.dataSeconds << '\n'
                  << "parse_seconds="
                  << std::chrono::duration<double>(elapsed).count() << '\n'
                  << "throughput_mib_per_second="
                  << (static_cast<double>(statistics.fileBytes) / (1024.0 * 1024.0)) /
                         std::max(statistics.dataSeconds, 1.0e-9) << '\n'
                  << "cpu_gaussian_mib="
                  << static_cast<double>(gaussians.size() * sizeof(simple_3dgs::Gaussian)) /
                         (1024.0 * 1024.0) << '\n';
        if (!gaussians.empty()) {
            std::cout << "position_min=" << minimum[0] << ',' << minimum[1] << ','
                      << minimum[2] << '\n'
                      << "position_max=" << maximum[0] << ',' << maximum[1] << ','
                      << maximum[2] << '\n'
                      << "scale_min_max=" << minimumScale << ',' << maximumScale << '\n'
                      << "opacity_min_max=" << minimumOpacity << ',' << maximumOpacity
                      << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "large PLY load failed: " << error.what() << '\n';
        return 1;
    }
}
