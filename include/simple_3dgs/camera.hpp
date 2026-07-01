#pragma once

#include <array>

namespace simple_3dgs {

class Camera {
public:
    void Rotate(float deltaX, float deltaY) noexcept;
    void Zoom(float wheelSteps) noexcept;
    void Move(float forward, float right) noexcept;

    [[nodiscard]] std::array<float, 3> Position() const noexcept;
    [[nodiscard]] std::array<float, 16> ViewProjection(float aspectRatio) const;
    [[nodiscard]] const std::array<float, 3>& Target() const noexcept { return target_; }
    [[nodiscard]] float Distance() const noexcept { return distance_; }
    [[nodiscard]] float Pitch() const noexcept { return pitch_; }

private:
    std::array<float, 3> target_ {0.0F, 0.0F, 0.0F};
    float yaw_ = 0.0F;
    float pitch_ = 0.15F;
    float distance_ = 6.0F;
};

} // namespace simple_3dgs
