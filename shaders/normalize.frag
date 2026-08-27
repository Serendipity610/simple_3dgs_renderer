#version 450

layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput inputAccumulation;
layout(location = 0) out vec4 outputColor;

const vec3 backgroundColor = vec3(0.025, 0.035, 0.055);

void main()
{
    vec4 accumulated = subpassLoad(inputAccumulation);
    vec3 color = backgroundColor;
    if (abs(accumulated.a) > 1.0e-7) {
        color = accumulated.rgb / accumulated.a;
    }
    outputColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
