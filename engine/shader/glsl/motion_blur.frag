#version 310 es

#extension GL_GOOGLE_include_directive : enable
#include "constants.h"

precision highp float;

layout(binding = 0) uniform sampler2D in_ColorTex;

layout(binding = 1) uniform sampler2D in_DepthTex;

layout(binding = 2) uniform MotionBlurUBO {
    mat4 inv_CurrentVP;
    mat4 prev_VP;
    float blurScale;
} ubo;

layout(location = 0) in vec2 out_UV;
layout(location = 0) out highp vec4 out_color;

void main() 
{
    vec2 uv = out_UV;
    float depth = texture(in_DepthTex, out_UV).r;
    float x_ndc = uv.x * 2.0 - 1.0;
    float y_ndc = uv.y * 2.0 - 1.0;
    float z_ndc = depth;


    vec4 ndc_position = vec4(x_ndc, y_ndc, z_ndc, 1.0);
    vec4 curPosRaw = ubo.inv_CurrentVP * ndc_position;
    vec3 curPos = curPosRaw.xyz / curPosRaw.w;
    vec4 preNdcPos = ubo.prev_VP * vec4(curPos, 1.0);
    vec3 ndcPrev = preNdcPos.xyz / preNdcPos.w;
    vec2 uvPrev = ndcPrev.xy * 0.5 + 0.5;

    vec2 velocity = out_UV - uvPrev;
    velocity *= ubo.blurScale;
    float maxVelocity = 0.05; 
    if (length(velocity) > maxVelocity) {
        velocity = normalize(velocity) * maxVelocity;
    }
    vec4 colorAccum = vec4(0.0);
    int numSamples = 12;
    
    for (int i = 0; i < numSamples; ++i) {
        float t = float(i) / float(numSamples - 1);
        
        vec2 sampleUV = out_UV - velocity * t;
        
        colorAccum += texture(in_ColorTex, sampleUV);
    }
    out_color = colorAccum / float(numSamples);
}