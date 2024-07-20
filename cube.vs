#version 320 es
layout (location = 0) in vec3 apos;
layout (location = 1) in vec3 acolor;

out vec4 vertex_color;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

void main()
{
    gl_Position = projection * view * model * vec4(apos, 1.0);
    vertex_color = vec4(acolor, 1.0);
}