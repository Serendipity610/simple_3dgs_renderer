#version 450

struct Gaussian {
    vec4 positionOpacity;
    vec4 scale;
    vec4 rotation;
    vec4 color;
    vec4 sphericalHarmonics[12];
};

layout(std430, set = 0, binding = 0) readonly buffer GaussianBuffer {
    Gaussian gaussians[];
};

layout(push_constant) uniform CameraState {
    mat4 viewProjection;
    vec2 viewportSize;
    vec2 padding;
    vec4 cameraPosition;
} camera;

layout(location = 0) out vec2 localPosition;
layout(location = 1) out vec4 splatColor;

const vec2 corners[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

float readShCoefficient(uint gaussianIndex, int valueIndex)
{
    return gaussians[gaussianIndex]
        .sphericalHarmonics[valueIndex / 4][valueIndex % 4];
}

vec3 readShRgb(uint gaussianIndex, int coefficient)
{
    int firstValue = coefficient * 3;
    return vec3(readShCoefficient(gaussianIndex, firstValue),
                readShCoefficient(gaussianIndex, firstValue + 1),
                readShCoefficient(gaussianIndex, firstValue + 2));
}

vec3 evaluateShColor(uint gaussianIndex, int degree, vec3 direction)
{
    const float C0 = 0.28209479177387814;
    const float C1 = 0.4886025119029199;
    const float C2_0 = 1.0925484305920792;
    const float C2_1 = -1.0925484305920792;
    const float C2_2 = 0.31539156525252005;
    const float C2_3 = -1.0925484305920792;
    const float C2_4 = 0.5462742152960396;
    const float C3_0 = -0.5900435899266435;
    const float C3_1 = 2.890611442640554;
    const float C3_2 = -0.4570457994644658;
    const float C3_3 = 0.3731763325901154;
    const float C3_4 = -0.4570457994644658;
    const float C3_5 = 1.445305721320277;
    const float C3_6 = -0.5900435899266435;

    float x = direction.x;
    float y = direction.y;
    float z = direction.z;
    vec3 result = C0 * readShRgb(gaussianIndex, 0);
    if (degree > 0) {
        result += -C1 * y * readShRgb(gaussianIndex, 1) +
                  C1 * z * readShRgb(gaussianIndex, 2) -
                  C1 * x * readShRgb(gaussianIndex, 3);
    }
    if (degree > 1) {
        float xx = x * x;
        float yy = y * y;
        float zz = z * z;
        result += C2_0 * x * y * readShRgb(gaussianIndex, 4) +
                  C2_1 * y * z * readShRgb(gaussianIndex, 5) +
                  C2_2 * (2.0 * zz - xx - yy) * readShRgb(gaussianIndex, 6) +
                  C2_3 * x * z * readShRgb(gaussianIndex, 7) +
                  C2_4 * (xx - yy) * readShRgb(gaussianIndex, 8);
    }
    if (degree > 2) {
        float xx = x * x;
        float yy = y * y;
        float zz = z * z;
        result += C3_0 * y * (3.0 * xx - yy) * readShRgb(gaussianIndex, 9) +
                  C3_1 * x * y * z * readShRgb(gaussianIndex, 10) +
                  C3_2 * y * (4.0 * zz - xx - yy) * readShRgb(gaussianIndex, 11) +
                  C3_3 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy) *
                      readShRgb(gaussianIndex, 12) +
                  C3_4 * x * (4.0 * zz - xx - yy) * readShRgb(gaussianIndex, 13) +
                  C3_5 * z * (xx - yy) * readShRgb(gaussianIndex, 14) +
                  C3_6 * x * (xx - 3.0 * yy) * readShRgb(gaussianIndex, 15);
    }
    return max(result + vec3(0.5), vec3(0.0));
}

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
    int shDegree = int(round(gaussian.color.a));
    vec3 color = gaussian.color.rgb;
    if (shDegree >= 0) {
        vec3 cameraToGaussian = gaussian.positionOpacity.xyz -
                                camera.cameraPosition.xyz;
        vec3 viewDirection = cameraToGaussian /
                             max(length(cameraToGaussian), 1.0e-6);
        color = evaluateShColor(gl_InstanceIndex, shDegree, viewDirection);
    }
    splatColor = vec4(color, gaussian.positionOpacity.w);
}
