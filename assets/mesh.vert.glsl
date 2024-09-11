#version 460

#extension GL_ARB_separate_shader_objects: enable
#extension GL_ARB_shading_language_420pack: enable
#extension GL_GOOGLE_include_directive: require
#extension GL_EXT_shader_8bit_storage: require

struct Instance
{
    mat4 model;
    uint meshId;
    uint skinId;
};

struct Command
{
    uint countIndex;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint baseInstance;
    uint drawId;
};

struct UBO
{
    mat4 proj;
    mat4 view;
};

// Attributes
layout (location = 0) in vec3 inPos;
layout (location = 3) in vec3 inUV;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inTangent;

layout(set = 0, binding = 0) uniform inPushBuffer { UBO pc; };
layout(set = 0, binding = 1) readonly buffer inInstBuffer { Instance props[]; };
layout(set = 0, binding = 3) readonly buffer inDrawBuffer { Command draws[]; };

layout (location = 0) out vec2 outUV;
layout (location = 1) out uint outMatId;

void main()
{
    uint drawIndex = draws[gl_DrawID].drawId;
    Instance inst = props[gl_DrawID];
    vec4 tmpPos = vec4(inPos.xyz, 1.0);

    gl_Position = pc.proj * pc.view * inst.model * tmpPos;

    outMatId = inst.skinId;

    outUV = inUV.xy;
}