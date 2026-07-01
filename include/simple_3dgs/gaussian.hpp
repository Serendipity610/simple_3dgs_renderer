#pragma once

#include <array>

namespace simple_3dgs {

struct Gaussian {
    std::array<float, 3> position {0.0F, 0.0F, 0.0F};
    std::array<float, 3> scale {0.0F, 0.0F, 0.0F};
    std::array<float, 4> rotation {1.0F, 0.0F, 0.0F, 0.0F};
    float opacity = 1.0F;
    std::array<float, 3> color {1.0F, 1.0F, 1.0F};
};

} // namespace simple_3dgs
