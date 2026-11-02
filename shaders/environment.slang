static const float PI = 3.14159265;

float2 environmentUV(float3 direction)
{
    return float2(atan2(direction.z, direction.x) / (2.0 * PI) + 0.5,
                  acos(clamp(direction.y, -1.0, 1.0)) / PI);
}

float3 environmentDirection(float2 uv)
{
    const float phi = (uv.x - 0.5) * 2.0 * PI;
    const float theta = uv.y * PI;
    return float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));
}
