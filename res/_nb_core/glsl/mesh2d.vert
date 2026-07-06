#version 450

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;   // unused in this shader
layout(location = 2) in vec4 a_color;

layout(set = 1, binding = 0) uniform UBO {
    mat4 viewproj;
};

layout(location = 0) out vec4 v_color;

void main()
{
    gl_Position = viewproj * vec4(a_pos, 0.0, 1.0);
    v_color = a_color;
}
