#version 310 es

#extension GL_GOOGLE_include_directive : enable
#include "constants.h"

layout(input_attachment_index = 0, set = 0, binding = 0) uniform highp subpassInput in_color;

layout(set = 0, binding = 1) uniform sampler2D color_grading_lut_texture_sampler;

layout(location = 0) out highp vec4 out_color;

void main()
{
    highp ivec2 lut_tex_size = textureSize(color_grading_lut_texture_sampler, 0);
    highp float _COLORS = float(lut_tex_size.y); 
    highp float _NUM    = float(lut_tex_size.x / lut_tex_size.y);

    highp vec4 color = subpassLoad(in_color).rgba;
    highp vec3 rgb = clamp(color.rgb, 0.0, 1.0);

    highp float b_val = rgb.b * (_NUM - 1.0);
    highp float b_low = floor(b_val);
    highp float b_high = min(b_low + 1.0, _NUM - 1.0);
    highp float b_weight = fract(b_val);


    highp float scale = (_COLORS - 1.0) / float(lut_tex_size.x);
    highp float offset = 0.5 / float(lut_tex_size.x);

    highp float x1 = (rgb.r * scale) + (b_low / _NUM) + offset;
    highp float x2 = (rgb.r * scale) + (b_high / _NUM) + offset;
    highp float y  = (rgb.g * (_COLORS - 1.0) / _COLORS) + (0.5 / _COLORS);

    highp vec4 color_1 = texture(color_grading_lut_texture_sampler, vec2(x1, y));
    highp vec4 color_2 = texture(color_grading_lut_texture_sampler, vec2(x2, y));

    out_color = vec4(mix(color_1.rgb, color_2.rgb, b_weight), color.a);
}