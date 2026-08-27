#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace simple_3dgs {

inline constexpr size_t kShCoefficientsPerChannel = 16;
inline constexpr size_t kShChannelCount = 3;
inline constexpr size_t kShCoefficientCount =
    kShCoefficientsPerChannel * kShChannelCount;
inline constexpr size_t kOpacityShCoefficientCount = 16;

struct Gaussian {
    std::array<float, 3> position {0.0F, 0.0F, 0.0F};
    std::array<float, 3> scale {0.0F, 0.0F, 0.0F};
    std::array<float, 4> rotation {1.0F, 0.0F, 0.0F, 0.0F};
    float opacity = 1.0F;
    std::array<float, 3> color {1.0F, 1.0F, 1.0F};
    // Coefficient-major RGB values: coefficient * 3 + channel.
    std::array<float, kShCoefficientCount> shCoefficients {};
    std::array<float, kOpacityShCoefficientCount> opacityShCoefficients {};
    // -1 selects direct RGB; 0..3 selects the available SH degree.
    int32_t shDegree = -1;
};

} // namespace simple_3dgs
