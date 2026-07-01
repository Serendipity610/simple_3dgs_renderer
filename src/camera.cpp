#include "simple_3dgs/camera.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace simple_3dgs {
namespace {

using Vec3 = std::array<float, 3>;
using Mat4 = std::array<float, 16>;

[[nodiscard]] Vec3 Subtract(const Vec3& left, const Vec3& right)
{
    return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

[[nodiscard]] float Dot(const Vec3& left, const Vec3& right)
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

[[nodiscard]] Vec3 Cross(const Vec3& left, const Vec3& right)
{
    return {left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}

[[nodiscard]] Vec3 Normalize(const Vec3& value)
{
    const float length = std::sqrt(Dot(value, value));
    if (length <= 1.0e-7F) {
        throw std::runtime_error("cannot normalize a zero-length camera vector");
    }
    return {value[0] / length, value[1] / length, value[2] / length};
}

[[nodiscard]] Mat4 LookAt(const Vec3& eye, const Vec3& center)
{
    const Vec3 forward = Normalize(Subtract(center, eye));
    const Vec3 side = Normalize(Cross(forward, {0.0F, 1.0F, 0.0F}));
    const Vec3 up = Cross(side, forward);
    return {side[0], up[0], -forward[0], 0.0F,
            side[1], up[1], -forward[1], 0.0F,
            side[2], up[2], -forward[2], 0.0F,
            -Dot(side, eye), -Dot(up, eye), Dot(forward, eye), 1.0F};
}

[[nodiscard]] Mat4 Perspective(float aspectRatio)
{
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0.0F) {
        throw std::runtime_error("camera aspect ratio must be positive");
    }
    constexpr float kVerticalFieldOfView = 0.7853981633974483F;
    constexpr float kNear = 0.05F;
    constexpr float kFar = 1000.0F;
    const float focalLength = 1.0F / std::tan(kVerticalFieldOfView * 0.5F);
    Mat4 result {};
    result[0] = focalLength / aspectRatio;
    result[5] = -focalLength;
    result[10] = kFar / (kNear - kFar);
    result[11] = -1.0F;
    result[14] = (kFar * kNear) / (kNear - kFar);
    return result;
}

[[nodiscard]] Mat4 Multiply(const Mat4& left, const Mat4& right)
{
    Mat4 result {};
    for (size_t column = 0; column < 4; ++column) {
        for (size_t row = 0; row < 4; ++row) {
            for (size_t index = 0; index < 4; ++index) {
                result[column * 4 + row] +=
                    left[index * 4 + row] * right[column * 4 + index];
            }
        }
    }
    return result;
}

} // namespace

void Camera::Rotate(float deltaX, float deltaY) noexcept
{
    constexpr float kSensitivity = 0.005F;
    constexpr float kPitchLimit = 1.5533430342749532F;
    yaw_ += deltaX * kSensitivity;
    pitch_ = std::clamp(pitch_ - deltaY * kSensitivity, -kPitchLimit, kPitchLimit);
}

void Camera::Zoom(float wheelSteps) noexcept
{
    distance_ = std::clamp(distance_ * std::exp(-wheelSteps * 0.12F), 0.1F, 500.0F);
}

void Camera::Move(float forward, float right) noexcept
{
    const Vec3 forwardDirection {-std::sin(yaw_), 0.0F, -std::cos(yaw_)};
    const Vec3 rightDirection {std::cos(yaw_), 0.0F, -std::sin(yaw_)};
    for (size_t index = 0; index < target_.size(); ++index) {
        target_[index] += forwardDirection[index] * forward +
                          rightDirection[index] * right;
    }
}

std::array<float, 3> Camera::Position() const noexcept
{
    const float horizontalDistance = distance_ * std::cos(pitch_);
    return {target_[0] + horizontalDistance * std::sin(yaw_),
            target_[1] + distance_ * std::sin(pitch_),
            target_[2] + horizontalDistance * std::cos(yaw_)};
}

std::array<float, 16> Camera::ViewProjection(float aspectRatio) const
{
    return Multiply(Perspective(aspectRatio), LookAt(Position(), target_));
}

} // namespace simple_3dgs
