#version 460

#extension GL_EXT_nonuniform_qualifier: require
#extension GL_ARB_separate_shader_objects: enable
#extension GL_ARB_shading_language_420pack: enable
#extension GL_GOOGLE_include_directive: require
#extension GL_EXT_shader_8bit_storage: require

struct Material
{
    uint d;
    uint n;
    uint a;
    uint r;
};

layout(set = 0, binding = 2) readonly buffer inMatBuffer { Material mats[]; };
layout(set = 0, binding = 4) uniform sampler2D textures[];

layout (location = 0) in vec2 inUV;
layout (location = 1) in flat uint inMatId;

// Return Output
layout (location = 0) out vec4 outAlbedo;

void main()
{
    Material m = mats[inMatId];

    outAlbedo = texture(textures[nonuniformEXT(m.d)], inUV);
    if(outAlbedo.a < 0.5)
        discard;
}