#version 450

struct Gaussian {
    vec4 positionOpacity;
    vec4 scale;
    vec4 rotation;
    vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer GaussianBuffer {
    Gaussian gaussians[];
};

layout(push_constant) uniform CameraState {
    mat4 viewProjection;
    vec2 viewportSize;
    vec2 padding;
} camera;

layout(location = 0) out vec2 localPosition;
layout(location = 1) out vec4 splatColor;

const vec2 corners[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

void main()
{
    Gaussian gaussian = gaussians[gl_InstanceIndex];
    vec2 corner = corners[gl_VertexIndex];
    vec4 center = camera.viewProjection * vec4(gaussian.positionOpacity.xyz, 1.0);
    float pixelRadius = clamp(exp(max(gaussian.scale.x, gaussian.scale.y)) *
                              700.0 / max(center.w, 0.01), 1.0, 180.0);
    vec2 ndcOffset = corner * pixelRadius * 2.0 / camera.viewportSize;
    gl_Position = center;
    gl_Position.xy += ndcOffset * center.w;
    localPosition = corner;
    splatColor = vec4(gaussian.color.rgb, gaussian.positionOpacity.w);
}
