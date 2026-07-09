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

layout(std430, set = 0, binding = 1) readonly buffer SortedGaussianIndexBuffer {
    uint sortedGaussianIndices[];
};

layout(push_constant) uniform CameraState {
    mat4 viewProjection;
    vec4 viewportSizeAndFocalLength;
    vec4 cameraPosition;
    vec4 cameraTarget;
} camera;

layout(location = 0) out vec2 localPosition;
layout(location = 1) out vec4 splatColor;

const vec2 corners[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

mat3 quaternionRotation(vec4 quaternion)
{
    float normSquared = dot(quaternion, quaternion);
    if (normSquared <= 1.0e-12) {
        return mat3(1.0);
    }
    vec4 q = quaternion * inversesqrt(normSquared);
    float w = q.x;
    float x = q.y;
    float y = q.z;
    float z = q.w;
    return mat3(
        vec3(1.0 - 2.0 * (y * y + z * z),
             2.0 * (x * y + w * z),
             2.0 * (x * z - w * y)),
        vec3(2.0 * (x * y - w * z),
             1.0 - 2.0 * (x * x + z * z),
             2.0 * (y * z + w * x)),
        vec3(2.0 * (x * z + w * y),
             2.0 * (y * z - w * x),
             1.0 - 2.0 * (x * x + y * y)));
}

mat3 worldCovariance(Gaussian gaussian)
{
    vec3 scale = exp(clamp(gaussian.scale.xyz, vec3(-20.0), vec3(20.0)));
    mat3 scaleSquared = mat3(
        vec3(scale.x * scale.x, 0.0, 0.0),
        vec3(0.0, scale.y * scale.y, 0.0),
        vec3(0.0, 0.0, scale.z * scale.z));
    mat3 rotation = quaternionRotation(gaussian.rotation);
    return rotation * scaleSquared * transpose(rotation);
}

mat3 worldToCameraBasis()
{
    vec3 forward = normalize(camera.cameraTarget.xyz - camera.cameraPosition.xyz);
    vec3 side = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(side, forward);
    return transpose(mat3(side, up, forward));
}

vec3 projectedCovariance(mat3 covariance, vec3 cameraPosition)
{
    float depth = max(cameraPosition.z, 1.0e-4);
    float inverseDepth = 1.0 / depth;
    float inverseDepthSquared = inverseDepth * inverseDepth;
    float focalX = camera.viewportSizeAndFocalLength.z;
    float focalY = camera.viewportSizeAndFocalLength.w;

    // Gradients of pixel coordinates
    // u = fx * x / z and v = -fy * y / z.
    vec3 gradientX = vec3(focalX * inverseDepth, 0.0,
                          -focalX * cameraPosition.x * inverseDepthSquared);
    vec3 gradientY = vec3(0.0, -focalY * inverseDepth,
                          focalY * cameraPosition.y * inverseDepthSquared);
    float varianceX = dot(gradientX, covariance * gradientX);
    float covarianceXY = dot(gradientX, covariance * gradientY);
    float varianceY = dot(gradientY, covariance * gradientY);

    // A small pixel-space low-pass term keeps sub-pixel covariance invertible.
    return vec3(max(varianceX + 0.3, 0.3), covarianceXY,
                max(varianceY + 0.3, 0.3));
}

void ellipseAxes(vec3 covariance, out vec2 majorAxis, out vec2 minorAxis)
{
    float varianceX = covariance.x;
    float covarianceXY = covariance.y;
    float varianceY = covariance.z;
    float midpoint = 0.5 * (varianceX + varianceY);
    float radius = sqrt(max(0.0,
        0.25 * (varianceX - varianceY) * (varianceX - varianceY) +
        covarianceXY * covarianceXY));
    float majorEigenvalue = max(midpoint + radius, 0.3);
    float minorEigenvalue = max(midpoint - radius, 0.3);

    vec2 majorDirection;
    if (abs(covarianceXY) > 1.0e-6) {
        majorDirection = normalize(vec2(covarianceXY,
                                        majorEigenvalue - varianceX));
    } else {
        majorDirection = varianceX >= varianceY ? vec2(1.0, 0.0)
                                                 : vec2(0.0, 1.0);
    }
    vec2 minorDirection = vec2(-majorDirection.y, majorDirection.x);
    const float sigmaExtent = 3.0;
    majorAxis = majorDirection * sigmaExtent * sqrt(majorEigenvalue);
    minorAxis = minorDirection * sigmaExtent * sqrt(minorEigenvalue);

    const float maxAxisPixels = 512.0;
    float majorLength = length(majorAxis);
    if (majorLength > maxAxisPixels) {
        majorAxis *= maxAxisPixels / majorLength;
    }
    float minorLength = length(minorAxis);
    if (minorLength > maxAxisPixels) {
        minorAxis *= maxAxisPixels / minorLength;
    }
}

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
    uint gaussianIndex = sortedGaussianIndices[gl_InstanceIndex];
    Gaussian gaussian = gaussians[gaussianIndex];
    vec2 corner = corners[gl_VertexIndex];
    vec4 center = camera.viewProjection * vec4(gaussian.positionOpacity.xyz, 1.0);
    mat3 cameraBasis = worldToCameraBasis();
    vec3 centerInCamera = cameraBasis *
                          (gaussian.positionOpacity.xyz - camera.cameraPosition.xyz);
    if (centerInCamera.z <= 0.001 || center.w <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        localPosition = corner;
        splatColor = vec4(0.0);
        return;
    }

    mat3 covarianceInCamera = cameraBasis * worldCovariance(gaussian) *
                              transpose(cameraBasis);
    vec3 covariance2D = projectedCovariance(covarianceInCamera, centerInCamera);
    vec2 majorAxis;
    vec2 minorAxis;
    ellipseAxes(covariance2D, majorAxis, minorAxis);
    vec2 pixelOffset = corner.x * majorAxis + corner.y * minorAxis;
    vec2 ndcOffset = pixelOffset * 2.0 /
                     camera.viewportSizeAndFocalLength.xy;
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
        color = evaluateShColor(gaussianIndex, shDegree, viewDirection);
    }
    splatColor = vec4(color, gaussian.positionOpacity.w);
}
