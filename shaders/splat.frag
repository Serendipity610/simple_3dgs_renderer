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
    // localPosition reaches one at the three-sigma ellipse boundary.
    float gaussianWeight = exp(-4.5 * radiusSquared);
    float opacity = 1.0 / (1.0 + exp(-splatColor.a));
    float alpha = opacity * gaussianWeight;
    if (alpha < (1.0 / 255.0)) {
        discard;
    }
    outputColor = vec4(splatColor.rgb, alpha);
}
