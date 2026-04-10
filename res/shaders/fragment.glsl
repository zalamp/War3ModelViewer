#version 330 core
in vec2 v_uv;
out vec4 FragColor;

void main()
{
    // 【调试】亮绿色，非常显眼
    FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
