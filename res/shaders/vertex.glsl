#version 330 core
// 【关键】把所有输入都注释掉，我们不用 C++ 的数据
// in vec3 position;
// in vec2 texCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec2 v_uv;

void main()
{
    // 【调试】硬编码三个顶点，组成一个三角形
    // 这三个点是标准化设备坐标 (NDC)，直接在屏幕中间
    vec3 positions[3];
    positions[0] = vec3(-0.5, -0.5, 0.0); // 左下
    positions[1] = vec3( 0.5, -0.5, 0.0); // 右下
    positions[2] = vec3( 0.0,  0.5, 0.0); // 中上

    // 【关键】用 gl_VertexID 来取第几个顶点
    int idx = gl_VertexID % 3;
    vec3 pos = positions[idx];

    // 【关键】直接输出，不乘任何矩阵！
    gl_Position = vec4(pos, 1.0);

    v_uv = vec2(0.0, 0.0); // 随便给个 UV
}
