#pragma once

#include <array>

namespace simple_3dgs {

class Camera {
public:
    void Rotate(float deltaX, float deltaY) noexcept;
    void Zoom(float wheelSteps) noexcept;
    void Move(float forward, float right) noexcept;

    [[nodiscard]] std::array<float, 3> Position() const noexcept;
    [[nodiscard]] std::array<float, 2> FocalLengthPixels(float viewportWidth,
                                                         float viewportHeight) const;
    [[nodiscard]] std::array<float, 16> ViewProjection(float aspectRatio) const;
    [[nodiscard]] std::array<float, 3> Target() const noexcept;
    [[nodiscard]] float Distance() const noexcept { return distance_; }
    [[nodiscard]] float Pitch() const noexcept { return pitch_; }

private:
    std::array<float, 3> position_ {0.0F, 0.9F, 5.93F};
    float yaw_ = 0.0F;
    float pitch_ = -0.15F;
    float distance_ = 6.0F;
};

} // namespace simple_3dgs
