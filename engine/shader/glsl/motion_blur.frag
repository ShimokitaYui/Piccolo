#version 310 es

#extension GL_GOOGLE_include_directive : enable
#include "constants.h"

precision highp float;

layout(binding = 0) uniform sampler2D in_ColorTex;

layout(binding = 1) uniform sampler2D in_DepthTex;

layout(binding = 2) uniform MotionBlurUBO {
    mat4 inv_CurrentVP;
    mat4 prev_VP;
    vec4 viewportRect;
    vec4 targetSize;
    float blurScale;
} ubo;

layout(location = 0) in vec2 out_UV;
layout(location = 0) out highp vec4 out_color;

vec2 sceneUVToFramebufferUV(vec2 sceneUV)
{
    vec2 framebufferSize = max(ubo.targetSize.xy, vec2(1.0));
    vec2 viewportOrigin = ubo.viewportRect.xy;
    vec2 viewportSize = max(ubo.viewportRect.zw, vec2(1.0));
    return clamp((viewportOrigin + sceneUV * viewportSize) / framebufferSize, vec2(0.0), vec2(1.0));
}

void main()
{
    vec2 uv = out_UV;
    vec2 framebufferUV = sceneUVToFramebufferUV(uv);

    float depth = texture(in_DepthTex, framebufferUV).r;
    if (depth >= 0.999) {
        out_color = texture(in_ColorTex, framebufferUV);
        return;
    }

    float x_ndc = uv.x * 2.0 - 1.0;
    float y_ndc = uv.y * 2.0 - 1.0;
    float z_ndc = depth;

    vec4 ndc_position = vec4(x_ndc, y_ndc, z_ndc, 1.0);
    vec4 curPosRaw = ubo.inv_CurrentVP * ndc_position;
    vec3 curPos = curPosRaw.xyz / curPosRaw.w;
    vec4 preNdcPos = ubo.prev_VP * vec4(curPos, 1.0);
    vec3 ndcPrev = preNdcPos.xyz / preNdcPos.w;
    vec2 uvPrev = ndcPrev.xy * 0.5 + 0.5;

    vec2 velocity = uv - uvPrev;
    velocity *= ubo.blurScale;

    float maxVelocity = 0.05;
    if (length(velocity) > maxVelocity) {
        velocity = normalize(velocity) * maxVelocity;
    }

    vec4 colorAccum = vec4(0.0);
    int numSamples = 12;

    for (int i = 0; i < numSamples; ++i) {
        float t = float(i) / float(numSamples - 1);
        vec2 sampleSceneUV = clamp(uv - velocity * t, vec2(0.0), vec2(1.0));
        colorAccum += texture(in_ColorTex, sceneUVToFramebufferUV(sampleSceneUV));
    }

    out_color = colorAccum / float(numSamples);
}