#version 450

layout(location = 0) in vec2 localPosition;
layout(location = 1) in vec4 splatColor;

layout(location = 0) out vec4 outputColor;

void main()
{
    float radiusSquared = dot(localPosition, localPosition);
    if (radiusSquared > 1.0) {
        discard;
    }
    float gaussianWeight = exp(-4.0 * radiusSquared);
    float opacity = 1.0 / (1.0 + exp(-splatColor.a));
    outputColor = vec4(splatColor.rgb, opacity * gaussianWeight);
}
