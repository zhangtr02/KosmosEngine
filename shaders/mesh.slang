#include "scene.slang"
#include "lighting.slang"
#include "material.slang"
#include "skinning.slang"
#include "morph.slang"
#include "transmission.slang"

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 baseUV : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
    [[vk::location(3)]] float3 normal : NORMAL;
    [[vk::location(4)]] float4 tangent : TANGENT;
    [[vk::location(5)]] float4 normalMetalUV : TEXCOORD1;
    [[vk::location(6)]] float4 occlusionEmissiveUV : TEXCOORD2;
    [[vk::location(7)]] uint4 joints : BLENDINDICES0;
    [[vk::location(8)]] float4 weights : BLENDWEIGHT0;
    [[vk::location(9)]] uint2 morph : TEXCOORD3;
    [[vk::location(10)]] uint advancedUv : TEXCOORD4;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 baseUV : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
    float4 normalMetalUV : TEXCOORD4;
    float4 occlusionEmissiveUV : TEXCOORD5;
    float4 color : COLOR0;
    float4 advancedUv[AdvancedUvPairs] : TEXCOORD6;
    float3 inverseX : TEXCOORD14;
    float3 inverseY : TEXCOORD15;
    float3 inverseZ : TEXCOORD16;
};

[[vk::binding(2, 0)]] Sampler2D normalMap;
[[vk::binding(3, 0)]] Sampler2D metallicRoughnessMap;
[[vk::binding(5, 0)]] Sampler2D irradianceMap;
[[vk::binding(8, 0)]] Sampler2D occlusionMap;
[[vk::binding(9, 0)]] Sampler2D shadowMap;
[[vk::binding(10, 0)]] Sampler2D emissiveMap;
[[vk::binding(11, 0)]] SamplerCube pointShadowMap;
[[vk::binding(12, 0)]] Sampler2D spotShadowMap;
[[vk::binding(14, 0)]] Sampler2D specularMap;
[[vk::binding(15, 0)]] Sampler2D specularColorMap;
[[vk::binding(16, 0)]] Sampler2D clearcoatMap;
[[vk::binding(17, 0)]] Sampler2D clearcoatRoughnessMap;
[[vk::binding(18, 0)]] Sampler2D clearcoatNormalMap;
[[vk::binding(19, 0)]] Sampler2D sheenColorMap;
[[vk::binding(20, 0)]] Sampler2D sheenRoughnessMap;
[[vk::binding(21, 0)]] Sampler2D anisotropyMap;
[[vk::binding(22, 0)]] Sampler2D iridescenceMap;
[[vk::binding(23, 0)]] Sampler2D iridescenceThicknessMap;
[[vk::binding(26, 0)]] Sampler2D diffuseTransmissionMap;
[[vk::binding(27, 0)]] Sampler2D diffuseTransmissionColorMap;
[[vk::binding(28, 0)]] Sampler2D retroreflectionMap;

float localShadowCompare(float3 direction, float depth)
{
    float closest;
    if (scene.localShadowProjection.x != 0.0)
    {
        // A direction (w = 0) removes view translation; XY/W gives its shadow-map UV.
        const float4 projected = mul(scene.spotViewProjection, float4(direction, 0.0));
        if (projected.w <= 0.0)
            return 1.0;
        const float2 uv = projected.xy / projected.w * 0.5 + 0.5;
        if (any(uv < 0.0) || any(uv > 1.0))
            return 1.0;
        closest = spotShadowMap.SampleLevel(uv, 0.0).r;
    }
    else
        closest = pointShadowMap.SampleLevel(direction, 0.0).r;
    closest *= scene.lights[uint(scene.localShadowParams.x)].positionRange.w;
    return depth <= closest ? 1.0 : 0.0;
}

float localShadowVisibility(float3 fromLight, float distance, float nDotL, float3 geometricNormal)
{
    const float bias = scene.localShadowParams.y * (2.0 - nDotL);
    const float depth = distance - bias;
    if (scene.localShadowParams.z == 0.0 || scene.localShadowParams.w <= 0.0)
        return localShadowCompare(fromLight, depth);
    const float3 direction = fromLight / distance;
    const float3 up = abs(direction.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    const float3 tangent = normalize(cross(up, direction));
    const float3 bitangent = cross(direction, tangent);
    uint width, height, levels;
    if (scene.localShadowProjection.x != 0.0)
        spotShadowMap.GetDimensions(0, width, height, levels);
    else
        pointShadowMap.GetDimensions(0, width, height, levels);
    const float radius = 2.0 * scene.localShadowParams.w / float(width) *
                         (scene.localShadowProjection.x != 0.0 ? scene.localShadowProjection.y : 1.0);
    // Center + eight disk directions; sample vectors can cross cube face boundaries.
    const float2 offsets[9] = {float2(0, 0), float2(1, 0), float2(-1, 0),
        float2(0, 1), float2(0, -1), float2(0.70710678, 0.70710678),
        float2(-0.70710678, 0.70710678), float2(0.70710678, -0.70710678),
        float2(-0.70710678, -0.70710678)};
    float visibility = 0.0;
    const float plane = dot(geometricNormal, direction);
    for (uint i = 0; i < 9; ++i)
    {
        const float3 sampleDirection =
            direction + radius * (tangent * offsets[i].x + bitangent * offsets[i].y);
        // Compare against the local receiver plane, avoiding filter-induced slope acne.
        const float projected = dot(geometricNormal, normalize(sampleDirection));
        const float sampleDistance =
            plane * projected > 1e-6 ? distance * plane / projected : distance;
        visibility += localShadowCompare(sampleDirection, sampleDistance - bias);
    }
    return visibility / 9.0;
}

float shadowVisibility(float3 worldPosition, float nDotL)
{
    const float4 lightPosition = mul(scene.lightViewProjection, float4(worldPosition, 1.0));
    if (lightPosition.w <= 0.0)
        return 1.0;
    const float3 position = lightPosition.xyz / lightPosition.w;
    const float2 uv = position.xy * 0.5 + 0.5;
    if (position.z < 0.0 || position.z > 1.0 || any(uv < 0.0) || any(uv > 1.0))
        return 1.0;
    uint width, height, levels;
    shadowMap.GetDimensions(0, width, height, levels);
    const float2 texel = 1.0 / float2(width, height);
    const float depth = position.z - scene.shadowParams.y * (2.0 - nDotL);
    const int radius = scene.shadowParams.z != 0.0 ? 1 : 0;
    float visibility = 0.0;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
        {
            const float closest = shadowMap.SampleLevel(uv + float2(x, y) * texel, 0.0).r;
            visibility += depth <= closest ? 1.0 : 0.0;
        }
    return visibility / float((2 * radius + 1) * (2 * radius + 1));
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint instance : SV_VulkanInstanceID,
                        out float pointSize : SV_PointSize)
{
    pointSize = 1.0;
    VertexOutput output;
    const MorphDelta morph = morphVertex(input.morph, constants.morphWeightOffset);
    const float4x4 model = instancedTransform(constants.model, instance);
    const float4x4 transform = skinnedTransform(model, constants.jointOffset, input.joints, input.weights);
    const float4 worldPosition = mul(transform, float4(input.position + morph.position.xyz, 1.0));
    output.position = mul(scene.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.baseUV = transformUv(input.baseUV, BaseColorSlot);
    output.normalMetalUV = float4(transformUv(input.normalMetalUV.xy, NormalSlot),
                                  transformUv(input.normalMetalUV.zw, MetallicRoughnessSlot));
    output.occlusionEmissiveUV = float4(transformUv(input.occlusionEmissiveUV.xy, OcclusionSlot),
                                        transformUv(input.occlusionEmissiveUV.zw, EmissiveSlot));
    [unroll]
    for (uint pair = 0; pair < AdvancedUvPairs; ++pair)
    {
        const uint slot = BaseTextureCount + pair * 2;
        float4 uv = 0.0;
        if (input.advancedUv != 0)
            uv = geometryData[input.advancedUv - 1 + pair];
        output.advancedUv[pair] = float4(transformUv(uv.xy, slot),
            slot + 1 < MaterialTextureCount ? transformUv(uv.zw, slot + 1) : 0.0);
    }
    output.color = input.color;
    const float3x3 linear = (float3x3)transform;
    const float3 x = mul(linear, float3(1, 0, 0));
    const float3 y = mul(linear, float3(0, 1, 0));
    const float3 z = mul(linear, float3(0, 0, 1));
    const float3 cofactorX = cross(y, z), cofactorY = cross(z, x), cofactorZ = cross(x, y);
    const float determinant = dot(x, cofactorX);
    const float orientation = determinant < 0.0 ? -1.0 : 1.0;
    const float inverseDeterminant = determinant != 0.0 ? 1.0 / determinant : 0.0;
    output.inverseX = cofactorX * inverseDeterminant;
    output.inverseY = cofactorY * inverseDeterminant;
    output.inverseZ = cofactorZ * inverseDeterminant;
    // Discard the magnitude of the determinant but keep its sign in the inverse transpose.
    const float3 normal = input.normal + morph.normal.xyz;
    output.worldNormal = orientation * (cofactorX * normal.x + cofactorY * normal.y +
                                        cofactorZ * normal.z);
    output.worldTangent = float4(mul(linear, input.tangent.xyz + morph.tangent.xyz),
                                 input.tangent.w * orientation);
    return output;
}

[shader("fragment")]
float4 selectionMain(VertexOutput input) : SV_Target0
{
    if (constants.alphaCutoff >= 0.0)
        alphaTest(sampleBaseColor(input.baseUV, input.color).a);
    return 1.0;
}

float2 extraUv(VertexOutput input, uint slot)
{
    const uint index = slot - BaseTextureCount;
    const float4 uv = input.advancedUv[index / 2];
    return index % 2 == 0 ? uv.xy : uv.zw;
}

struct TangentFrame
{
    float3 tangent;
    float3 bitangent;
};

TangentFrame tangentFrame(float3 normal, float4 authored, float3 dx, float3 dy,
                          float2 du, float2 dv, bool derive)
{
    float3 tangent = authored.xyz;
    float handedness = authored.w == 0.0 ? 1.0 : authored.w;
    if (derive)
    {
        const float handed = sign(du.x * dv.y - du.y * dv.x);
        const float3 derived = (dx * dv.y - dy * du.y) * handed;
        if (dot(derived, derived) > 1e-20)
        {
            tangent = derived;
            const float3 bitangent = (dy * du.x - dx * dv.x) * handed;
            handedness = dot(cross(normal, tangent), bitangent) < 0.0 ? -1.0 : 1.0;
        }
    }
    const float3 axis = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    const float3 orthogonal = tangent - normal * dot(normal, tangent);
    tangent = safeNormal(orthogonal, normalize(cross(axis, normal)));
    TangentFrame frame = {tangent, cross(normal, tangent) * handedness};
    return frame;
}

float3 mappedNormal(float3 normal, TangentFrame frame, float3 sample, float scale)
{
    const float3 map = sample * 2.0 - 1.0;
    return safeNormal(scale * (frame.tangent * map.x + frame.bitangent * map.y) + normal * map.z,
                      normal);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target0
{
    const float4 color = sampleBaseColor(input.baseUV, input.color);
    alphaTest(color.a);
    const float outputAlpha = (constants.flags & BlendFlag) != 0 ? color.a : 1.0;
    if ((constants.flags & UnlitFlag) != 0)
        return float4(color.rgb, outputAlpha);
    const float3 emissive = any(constants.emissiveFactor != 0.0)
        ? emissiveMap.Sample(input.occlusionEmissiveUV.zw).rgb * constants.emissiveFactor : 0.0;
    if ((constants.flags & NoNormalsFlag) != 0)
        return float4(color.rgb + emissive, outputAlpha);
    const bool triangles = (constants.flags & NonTriangleFlag) == 0;
    const bool useNormalTextures = triangles || input.worldTangent.w != 0.0;
    const float faceSign = !triangles || frontFacing ? 1.0 : -1.0;
    const float3 dx = ddx(input.worldPosition), dy = ddy(input.worldPosition);
    float3 geometricNormal = normalize(input.worldNormal);
    if ((constants.flags & FlatMorphNormalsFlag) != 0)
    {
        const float3 faceNormal = cross(dy, dx);
        if (dot(faceNormal, faceNormal) > 0.0)
            geometricNormal = normalize(faceNormal) * faceSign;
    }
    Surface surface = (Surface)0;
    surface.color = color.rgb;
    surface.normal = geometricNormal;
    if (useNormalTextures && constants.materialParams.x != 0.0)
    {
        const float2 uv = input.normalMetalUV.xy;
        const TangentFrame frame = tangentFrame(geometricNormal, input.worldTangent, dx, dy,
                                               ddx(uv), ddy(uv),
                                               (constants.flags & DeriveTangentFlag) != 0);
        float3 texel = normalMap.Sample(uv).xyz;
        if ((constants.flags & Bc5NormalFlag) != 0)
        {
            const float2 xy = texel.xy * 2.0 - 1.0;
            texel = float3(texel.xy,
                           sqrt(saturate(1.0 - dot(xy, xy))) * 0.5 + 0.5);
        }
        surface.normal = mappedNormal(geometricNormal, frame, texel, constants.materialParams.x);
    }
    const AdvancedMaterial material = readAdvancedMaterial();
    const float4 packedPbr = metallicRoughnessMap.Sample(input.normalMetalUV.zw);
    surface.legacy = material.workflow.z != 0.0;
    surface.dielectricF90 = 1.0;
    if (surface.legacy)
    {
        surface.dielectricF0 = saturate(material.specular.rgb * packedPbr.rgb);
        surface.roughness = clamp(1.0 - material.workflow.y * packedPbr.a, 0.045, 1.0);
    }
    else
    {
        surface.metallic = saturate(packedPbr.b * constants.materialParams.y);
        surface.roughness = clamp(packedPbr.g * constants.materialParams.z, 0.045, 1.0);
        const float ior = max(material.workflow.x, 0.0);
        const float ratio = (ior - 1.0) / (ior + 1.0);
        float3 specularColor = material.specular.rgb;
        float weight = material.specular.a;
        if ((constants.flags >> MaterialIndexShift) != 0)
        {
            specularColor *= specularColorMap.Sample(extraUv(input, SpecularColorSlot)).rgb;
            weight *= specularMap.Sample(extraUv(input, SpecularSlot)).a;
        }
        surface.dielectricF90 = saturate(weight);
        surface.dielectricF0 = saturate(ratio * ratio * specularColor) * surface.dielectricF90;
    }
    surface.coatNormal = geometricNormal;
    if (material.clearcoat.x > 0.0)
    {
        surface.coat = saturate(material.clearcoat.x * clearcoatMap.Sample(
                                extraUv(input, ClearcoatSlot)).r);
        surface.coatRoughness = clamp(material.clearcoat.y * clearcoatRoughnessMap.Sample(
                                      extraUv(input, ClearcoatRoughnessSlot)).g, 0.045, 1.0);
        if (useNormalTextures && material.clearcoat.z != 0.0)
        {
            const float2 uv = extraUv(input, ClearcoatNormalSlot);
            const TangentFrame frame = tangentFrame(geometricNormal, input.worldTangent, dx, dy,
                                                   ddx(uv), ddy(uv), triangles);
            surface.coatNormal = mappedNormal(geometricNormal, frame, clearcoatNormalMap.Sample(uv).xyz,
                                              material.clearcoat.z);
        }
    }
    if (maxComponent(material.sheen.rgb) > 0.0)
    {
        surface.sheen = saturate(material.sheen.rgb *
                        sheenColorMap.Sample(extraUv(input, SheenColorSlot)).rgb);
        surface.sheenRoughness = clamp(material.sheen.a *
            sheenRoughnessMap.Sample(extraUv(input, SheenRoughnessSlot)).a, 0.045, 1.0);
    }
    if (material.anisotropy.x > 0.0)
    {
        const float2 uv = extraUv(input, AnisotropySlot);
        const bool textured = material.anisotropy.w != 0.0;
        const float3 sample = textured ? anisotropyMap.Sample(uv).rgb : float3(1, 0.5, 1);
        const float2 direction = sample.rg * 2.0 - 1.0;
        const float2 rotation = material.anisotropy.yz;
        const float2 oriented = float2(dot(direction, float2(rotation.x, -rotation.y)),
                                       dot(direction, rotation.yx));
        const TangentFrame frame = tangentFrame(surface.normal, input.worldTangent, dx, dy,
                                               ddx(uv), ddy(uv),
                                               triangles && (textured || input.worldTangent.w == 0.0));
        surface.tangent = safeNormal(frame.tangent * oriented.x + frame.bitangent * oriented.y,
                                     frame.tangent);
        surface.bitangent = cross(surface.normal, surface.tangent);
        surface.anisotropy = saturate(sample.b * material.anisotropy.x);
    }
    if (material.iridescence.x > 0.0)
    {
        const float thickness = lerp(material.iridescence.z, material.iridescence.w,
            iridescenceThicknessMap.Sample(extraUv(input, IridescenceThicknessSlot)).g);
        if (thickness > 0.0)
            surface.film = float3(saturate(material.iridescence.x *
                iridescenceMap.Sample(extraUv(input, IridescenceSlot)).r),
                max(material.iridescence.y, 1.0), thickness);
    }
    // Reverse the entire mapped normal on back faces, including tangent-space detail.
    surface.normal *= faceSign;
    surface.coatNormal *= faceSign;
    surface.bitangent *= faceSign;
    const float3 toCamera = scene.cameraPosition.xyz - input.worldPosition;
    surface.view = safeNormal(toCamera, surface.normal);
    if (material.workflow.w > 0.0)
        surface.retroreflection = saturate(material.workflow.w *
            retroreflectionMap.Sample(extraUv(input, RetroreflectionSlot)).r);
    if (material.transmission.x > 0.0)
        surface.transmission = saturate(material.transmission.x *
            transmissionMap.Sample(extraUv(input, TransmissionSlot)).r);
    if (material.diffuseTransmission.a > 0.0)
    {
        surface.diffuseTransmission = (1.0 - surface.transmission) *
            saturate(material.diffuseTransmission.a *
                     diffuseTransmissionMap.Sample(extraUv(input, DiffuseTransmissionSlot)).a);
        surface.diffuseTransmissionColor = material.diffuseTransmission.rgb *
            diffuseTransmissionColorMap.Sample(extraUv(input, DiffuseTransmissionColorSlot)).rgb;
    }
    surface.scatterColor = saturate(material.scattering.rgb);
    float3 refraction = 0.0;
    const float scatter = maxComponent(surface.scatterColor);
    if ((surface.transmission > 0.0 || surface.diffuseTransmission > 0.0 || scatter > 0.0) &&
        surface.metallic < 1.0)
    {
        const float thickness = material.transmission.y > 0.0 ? material.transmission.y *
            thicknessMap.Sample(extraUv(input, ThicknessSlot)).g : 0.0;
        const float ior = material.workflow.x == 0.0 ? 1e6 : max(material.workflow.x, 1.0);
        const float3x3 meshFromWorld = float3x3(input.inverseX, input.inverseY, input.inverseZ);
        const float distance = length(volumeRay(surface, meshFromWorld, thickness, ior));
        const float3 attenuation = volumeAttenuation(material.attenuation, distance);
        if (scatter > 0.0 && material.attenuation.w > 0.0 && distance > 0.0)
        {
            surface.scatterDistance = distance;
            surface.extinction = -log(clamp(material.attenuation.rgb, 1e-6, 1.0)) / material.attenuation.w;
        }
        surface.diffuseTransmissionColor *= attenuation;
        if (surface.transmission > 0.0)
        {
            surface.transmissionRoughness = transmissionRoughness(surface.roughness, ior);
            surface.transmissionColor = surface.color * attenuation;
            const float dispersion = material.workflow.x == 0.0 ? 0.0 : material.transmission.z;
            const float nDotV = saturate(dot(surface.normal, surface.view));
            float diffuseWeight;
            surfaceFresnel(surface, nDotV, diffuseWeight);
            refraction = transmittedScene(surface, input.worldPosition, input.position.xy, meshFromWorld,
                                          float3(thickness, ior, dispersion), material.attenuation) *
                         surface.color * (surface.transmission * diffuseWeight *
                                          baseLayerWeight(surface, nDotV));
        }
    }
    float3 radiance = 0.0;
    if (any(scene.lighting.xz > 0.0))
    {
        const float3 backIrradiance = surface.diffuseTransmission > 0.0 || surface.scatterDistance > 0.0
            ? irradianceMap.Sample(environmentUV(-surface.normal)).rgb : 0.0;
        radiance = environmentSurface(surface, irradianceMap.Sample(environmentUV(surface.normal)).rgb,
                                      backIrradiance, scene.lighting.xz);
        // Baked AO attenuates indirect lighting, before adding the direct light.
        if (constants.materialParams.w > 0.0)
            radiance *= lerp(1.0, occlusionMap.Sample(input.occlusionEmissiveUV.xy).r,
                             constants.materialParams.w);
    }
    radiance += refraction;
    const float3 faceNormal = geometricNormal * faceSign;
    for (uint i = 0; i < scene.renderOptions.x; ++i)
    {
        const LightData light = scene.lights[i];
        if (light.colorIntensity.w <= 0.0)
            continue;
        const float3 color = light.colorIntensity.rgb * light.colorIntensity.w;
        if (light.directionType.w == DirectionalLight)
        {
            const float3 direction = -light.directionType.xyz;
            const float visibility = float(i) == scene.shadowParams.x ?
                shadowVisibility(input.worldPosition, saturate(dot(faceNormal, direction))) : 1.0;
            radiance += directSurface(surface, direction) * color * visibility;
            continue;
        }
        const float3 toLight = light.positionRange.xyz - input.worldPosition;
        const float distanceSquared = dot(toLight, toLight);
        const float range = light.positionRange.w;
        if (distanceSquared <= 1e-8 || (range > 0.0 && distanceSquared >= range * range))
            continue;
        const float ratioSquared = range > 0.0 ? distanceSquared / (range * range) : 0.0;
        const float attenuation = saturate(1.0 - ratioSquared * ratioSquared) /
                                  max(distanceSquared, light.cone.z);
        const float3 pointDirection = toLight * rsqrt(distanceSquared);
        float angular = 1.0;
        if (light.directionType.w == SpotLight)
        {
            const float cosine = dot(light.directionType.xyz, -pointDirection);
            angular = saturate((cosine - light.cone.y) /
                               max(light.cone.x - light.cone.y, 1e-6));
            angular *= angular;
        }
        if (angular > 0.0)
        {
            const float pointNDotL = saturate(dot(faceNormal, pointDirection));
            const float visibility = float(i) == scene.localShadowParams.x ?
                localShadowVisibility(-toLight, sqrt(distanceSquared), pointNDotL, faceNormal) : 1.0;
            radiance += directSurface(surface, pointDirection) * color *
                        (attenuation * visibility * angular);
        }
    }
    // Emission is independent of reflected lighting, AO and shadow visibility.
    radiance += emissive * (1.0 - coatCoverage(surface));
    // Keep the RGBA16F target finite for nearby bright lights.
    return float4(min(radiance, 65504.0), outputAlpha);
}
