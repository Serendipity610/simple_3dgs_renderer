#version 450

layout(location = 0) in vec2 localPosition;
layout(location = 1) in vec4 splatColor;
layout(location = 2) in float lcWeight;

layout(location = 0) out vec4 accumulation;

void main()
{
    float radiusSquared = dot(localPosition, localPosition);
    if (radiusSquared > 1.0) {
        discard;
    }
    float gaussianWeight = exp(-4.5 * radiusSquared);
    float opacity = 1.0 / (1.0 + exp(-splatColor.a));
    float alpha = opacity * gaussianWeight;
    if (alpha < (1.0 / 255.0) || lcWeight <= 0.0) {
        discard;
    }
    float contribution = alpha * lcWeight;
    accumulation = vec4(splatColor.rgb * contribution, contribution);
}
