#version 120

#if @useUBO
    #extension GL_ARB_uniform_buffer_object : require
#endif

#if @useGPUShader4
    #extension GL_EXT_gpu_shader4: require
#endif

#define GROUNDCOVER

#include "water_waves.glsl"

#if @diffuseMap
uniform sampler2D diffuseMap;
varying vec2 diffuseMapUV;
#endif

#if @normalMap
uniform sampler2D normalMap;
varying vec2 normalMapUV;
varying vec4 passTangent;
#endif

// Other shaders respect forcePPL, but legacy groundcover mods were designed to work with vertex lighting.
// They may do not look as intended with per-pixel lighting, so ignore this setting for now.
#define PER_PIXEL_LIGHTING @normalMap

varying float euclideanDepth;
varying float linearDepth;
varying vec3 passWorldPos;

uniform float osg_SimulationTime;
uniform mat4 osg_ViewMatrixInverse;

const float MAX_CAUSTICS_DISTANCE = 2500.0;
const float CAUSTICS_FADE_START = 1500.0;

#if PER_PIXEL_LIGHTING
varying vec3 passViewPos;
varying vec3 passNormal;
#else
centroid varying vec3 passLighting;
centroid varying vec3 shadowDiffuseLighting;
#endif

// --- Inlined HDR/tonemap helpers (formerly HDR.glsl / helpsettings.glsl) ---
vec3 aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 preLight(vec3 x) {
    return pow(x, vec3(2.2));
}
vec3 extractBrightness(vec3 color, float threshold) {
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float soft = brightness - threshold + 0.5;
    soft = clamp(soft, 0.0, 1.0);
    soft = soft * soft * (3.0 - 2.0 * soft);
    float contribution = max(soft, brightness - threshold);
    contribution /= max(brightness, 0.00001);
    return color * contribution * 0.4;
}
vec3 toneMap(vec3 x) {
    vec3 bloom = extractBrightness(x, 1.2);
    x = x + bloom;
    vec3 col = aces(x);
    return pow(col, vec3(1.0 / 2.2));
}
// --- end inlined HDR helpers ---

#include "shadows_fragment.glsl"
#include "lighting.glsl"
#include "alpha.glsl"

void main()
{
#if @normalMap
    vec4 normalTex = texture2D(normalMap, normalMapUV);

    vec3 normalizedNormal = normalize(passNormal);
    vec3 normalizedTangent = normalize(passTangent.xyz);
    vec3 binormal = cross(normalizedTangent, normalizedNormal) * passTangent.w;
    mat3 tbnTranspose = mat3(normalizedTangent, binormal, normalizedNormal);

    vec3 viewNormal = gl_NormalMatrix * normalize(tbnTranspose * (normalTex.xyz * 2.0 - 1.0));
#endif

#if @diffuseMap
    gl_FragData[0] = texture2D(diffuseMap, diffuseMapUV);
#else
    gl_FragData[0] = vec4(1.0);
#endif

    if (euclideanDepth > @groundcoverFadeStart)
        gl_FragData[0].a *= 1.0-smoothstep(@groundcoverFadeStart, @groundcoverFadeEnd, euclideanDepth);

    alphaTest();

    // Convert to linear space for lighting calculations
    gl_FragData[0].xyz = preLight(gl_FragData[0].xyz);

    float shadowing = unshadowedLightRatio(linearDepth);

    vec3 lighting;
#if !PER_PIXEL_LIGHTING
    lighting = passLighting + shadowDiffuseLighting * shadowing;
#else
    vec3 diffuseLight, ambientLight;
    doLighting(passViewPos, normalize(viewNormal), shadowing, diffuseLight, ambientLight);
    lighting = diffuseLight + ambientLight;
    clampLightingResult(lighting);
#endif

    gl_FragData[0].xyz *= lighting;

    // Apply tonemapping after all lighting calculations
    gl_FragData[0].xyz = toneMap(gl_FragData[0].xyz);


    vec3 cameraPos = (osg_ViewMatrixInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    float cameraWaterH = zDoWaveSimple(cameraPos.xy, osg_SimulationTime);
    bool cameraUnderwater = cameraPos.z < cameraWaterH;

    vec3 sunPos = lcalcPosition(0);
    vec3 sunDir = normalize(sunPos);
    vec3 sunWDir = (osg_ViewMatrixInverse * vec4(sunDir, 0.0)).xyz;
    bool isInterior = sunWDir.y > 0.0;

    float waterH = zDoWaveSimple(passWorldPos.xy, osg_SimulationTime);
    float waterDepth = max(-passWorldPos.z + waterH, 0.0);

    float distanceToFragment = length(passWorldPos.xy - cameraPos.xy);
    float causticsFade = 1.0;
    if (distanceToFragment > CAUSTICS_FADE_START)
        causticsFade = 1.0 - smoothstep(CAUSTICS_FADE_START, MAX_CAUSTICS_DISTANCE, distanceToFragment);

    if (!isInterior && passWorldPos.z < waterH && waterDepth > 2.0 && distanceToFragment < MAX_CAUSTICS_DISTANCE) {
        float causticsIntensity = zcaustics(passWorldPos.xy * 0.012, osg_SimulationTime * 0.5) * 1.70;
        float causticsBlend = clamp(waterDepth * 0.018, 0.0, 0.72) / (1.0 + waterDepth / 700.0);
        causticsBlend *= causticsFade;
        gl_FragData[0].xyz *= mix(1.0, 0.65 + causticsIntensity, causticsBlend);
    }

    if (cameraUnderwater && !isInterior && waterDepth > 0.0)
        gl_FragData[0].xyz = applyUnderwaterMedium(gl_FragData[0].xyz, waterDepth, isInterior);

#if @radialFog
    float fogValue = clamp((euclideanDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#else
    float fogValue = clamp((linearDepth - gl_Fog.start) * gl_Fog.scale, 0.0, 1.0);
#endif
    gl_FragData[0].xyz = mix(gl_FragData[0].xyz, gl_Fog.color.xyz, fogValue);

    applyShadowDebugOverlay();
}
