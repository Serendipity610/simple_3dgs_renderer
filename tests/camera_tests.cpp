#include "simple_3dgs/camera.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestCameraControls()
{
    simple_3dgs::Camera camera;
    const float initialDistance = camera.Distance();
    camera.Zoom(2.0F);
    Require(camera.Distance() < initialDistance, "positive wheel must zoom in");
    camera.Zoom(-1000.0F);
    Require(camera.Distance() == 500.0F, "zoom out must be bounded");
    camera.Zoom(1000.0F);
    Require(camera.Distance() == 0.1F, "zoom in must be bounded");

    camera.Rotate(0.0F, -10000.0F);
    Require(camera.Pitch() < 1.5707964F, "upper pitch must avoid camera singularity");
    camera.Rotate(0.0F, 20000.0F);
    Require(camera.Pitch() > -1.5707964F, "lower pitch must avoid camera singularity");

    const auto before = camera.Target();
    camera.Move(2.0F, 3.0F);
    const auto after = camera.Target();
    Require(before != after, "WASD movement must translate the target");

    const auto position = camera.Position();
    Require(std::all_of(position.begin(), position.end(), [](float value) {
                return std::isfinite(value);
            }),
            "camera position must be finite");

    const auto focalLength = camera.FocalLengthPixels(1280.0F, 720.0F);
    Require(focalLength[0] > 0.0F && focalLength[1] > 0.0F,
            "pixel focal lengths must be positive");
    Require(std::abs(focalLength[0] - focalLength[1]) < 1.0e-3F,
            "square pixels must use matching focal lengths");
    bool rejectedViewport = false;
    try {
        static_cast<void>(camera.FocalLengthPixels(1280.0F, 0.0F));
    } catch (const std::runtime_error&) {
        rejectedViewport = true;
    }
    Require(rejectedViewport, "zero viewport height must be rejected");

    const auto matrix = camera.ViewProjection(16.0F / 9.0F);
    Require(std::all_of(matrix.begin(), matrix.end(), [](float value) {
                return std::isfinite(value);
            }),
            "view-projection matrix must be finite");
    bool rejectedAspect = false;
    try {
        static_cast<void>(camera.ViewProjection(0.0F));
    } catch (const std::runtime_error&) {
        rejectedAspect = true;
    }
    Require(rejectedAspect, "zero aspect ratio must be rejected");
}

} // namespace

int main()
{
    try {
        TestCameraControls();
        std::cout << "Camera tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Camera test failure: " << error.what() << '\n';
        return 1;
    }
}
