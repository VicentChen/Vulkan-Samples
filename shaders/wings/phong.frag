#version 460

#include "lighting.h"

precision highp float;

#ifdef HAS_BASE_COLOR_TEXTURE
layout(set = 0, binding = 0) uniform sampler2D base_color_texture;
#endif

#ifdef HAS_NORMAL_TEXTURE
layout(set = 0, binding = 2) uniform sampler2D normal_texture;
#endif

#ifdef HAS_METALLIC_ROUGHNESS_TEXTURE
layout(set = 0, binding = 3) uniform sampler2D metallic_roughness_texture;
#endif

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 1) uniform GlobalUniform
{
    mat4 model;
    mat4 view_proj;
    vec3 camera_position;
}
global_uniform;

// Push constants come with a limitation in the size of data.
// The standard requires at least 128 bytes
layout(push_constant, std430) uniform PBRMaterialUniform
{
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
}
pbr_material_uniform;

layout(set = 0, binding = 4) uniform LightsInfo
{
    Light directional_lights[MAX_LIGHT_COUNT];
    Light point_lights[MAX_LIGHT_COUNT];
    Light spot_lights[MAX_LIGHT_COUNT];
}
lights_info;

layout(constant_id = 0) const uint DIRECTIONAL_LIGHT_COUNT = 0U;
layout(constant_id = 1) const uint POINT_LIGHT_COUNT       = 0U;
layout(constant_id = 2) const uint SPOT_LIGHT_COUNT        = 0U;

void main(void)
{
    vec3 normal = normalize(in_normal);

    vec3 light_contribution = vec3(0.0);

    for (uint i = 0U; i < DIRECTIONAL_LIGHT_COUNT; ++i)
    {
        light_contribution += apply_directional_light(lights_info.directional_lights[i], normal);
    }

    for (uint i = 0U; i < POINT_LIGHT_COUNT; ++i)
    {
        light_contribution += apply_point_light(lights_info.point_lights[i], in_pos.xyz, normal);
    }

    for (uint i = 0U; i < SPOT_LIGHT_COUNT; ++i)
    {
        light_contribution += apply_spot_light(lights_info.spot_lights[i], in_pos.xyz, normal);
    }

    vec4 base_color = vec4(1.0, 0.0, 0.0, 1.0);

    #ifdef HAS_BASE_COLOR_TEXTURE
    base_color = texture(base_color_texture, in_uv);
    #else
    base_color = vec4(normal, 1.0f);//pbr_material_uniform.base_color_factor;
    #endif

    vec3 ambient_color = vec3(0.2) * base_color.xyz;

    o_color = vec4(ambient_color + light_contribution * base_color.xyz, base_color.w);
}
