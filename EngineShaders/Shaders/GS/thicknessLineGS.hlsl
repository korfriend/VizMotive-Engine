#include "../Globals.hlsli"

struct VertexToPixel
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

// Creates a half-circle cap at line endpoints using triangle primitives
void AddHalfCircle(inout TriangleStream<VertexToPixel> triangleStream, VertexToPixel input,
    int segments, float4 center, float w, float angle,
    float radiusX, float radiusY)
{
    VertexToPixel output = input;

    for (int i = 0; i < segments; ++i)
    {
        // First vertex: point on circle
        float currentAngle = angle + (PI / segments * i);
        output.pos = center;
        output.pos.x += cos(currentAngle) * radiusX;
        output.pos.y += sin(currentAngle) * radiusY;
        output.pos *= w; // Restore perspective w component
        triangleStream.Append(output);

        // Second vertex: center point
        output.pos = center * w;
        triangleStream.Append(output);

        // Third vertex: next point on circle
        float nextAngle = angle + (PI / segments * (i + 1));
        output.pos = center;
        output.pos.x += cos(nextAngle) * radiusX;
        output.pos.y += sin(nextAngle) * radiusY;
        output.pos *= w;
        triangleStream.Append(output);

        triangleStream.RestartStrip();
    }
}

void MakeThickLinesTriStream(VertexToPixel input[2], inout TriangleStream<VertexToPixel> triangleStream)
{
    // Get the line endpoints in screen space
    float4 p0 = input[0].pos;
    float4 p1 = input[1].pos;

    // Store original w components for later restoration
    float w0 = p0.w;
    float w1 = p1.w;

    // Convert to normalized device coordinates by dividing by w
    p0.xyz /= p0.w;
    p0.w = 1.0f;
    p1.xyz /= p1.w;
    p1.w = 1.0f;

    // Calculate the angle between the line and positive x-axis
    //float3 lineDirection = p0.xyz - p1.xyz;
    //float3 xAxis = float3(1.0f, 0.0f, 0.0f);
    //
    //lineDirection.z = 0.0f; // Ensure we're working in 2D space
    //
    //float lineLength = length(lineDirection.xy);
    //float angle = acos(dot(lineDirection.xy, xAxis.xy) / lineLength);
    //
    //// Determine if angle is clockwise or counter-clockwise
    //if (cross(lineDirection, xAxis).z < 0.0f)
    //{
    //    angle = 2.0f * PI - angle;
    //}
    //
    //// Adjust angle to be perpendicular to line direction
    //angle *= -1.0f;
    //angle -= PI * 0.5f;

    float2 d = p0.xy - p1.xy;
    float  ls = dot(d, d);
    if (ls < 1e-10) return;           // skip zero-length

    float  cosA = clamp(d.x * rsqrt(ls), -1.0, 1.0);
    float  angle = acos(cosA);
    if (d.y > 0) angle = 2.0f * PI - angle;
    angle = -(angle + PI * 0.5f);

    // Calculate thickness in screen space
    const float thicknessX = g_xThickness * g_xCamera.cameras[0].internal_resolution_rcp.x;
    const float thicknessY = g_xThickness * g_xCamera.cameras[0].internal_resolution_rcp.y;

    // Number of segments for rounded caps (higher = smoother circles)
    const int segments = 6;

    // Generate half-circle caps at both endpoints
    AddHalfCircle(triangleStream, input[0], segments, p0, w0, angle, thicknessX, thicknessY);
    AddHalfCircle(triangleStream, input[1], segments, p1, w1, angle + PI, thicknessX, thicknessY);

    // Connect the two half-circles with a quad (2 triangles)
    VertexToPixel v0 = input[0];
    VertexToPixel v1 = input[1];

    // First triangle
    // Vertex 1: first point of first cap
    v0.pos = p0;
    v0.pos.x += cos(angle) * thicknessX;
    v0.pos.y += sin(angle) * thicknessY;
    v0.pos *= w0;
    triangleStream.Append(v0);

    // Vertex 2: last point of first cap
    v0.pos = p0;
    v0.pos.x += cos(angle + PI) * thicknessX;
    v0.pos.y += sin(angle + PI) * thicknessY;
    v0.pos *= w0;
    triangleStream.Append(v0);

    // Vertex 3: last point of second cap
    v1.pos = p1;
    v1.pos.x += cos(angle + PI) * thicknessX;
    v1.pos.y += sin(angle + PI) * thicknessY;
    v1.pos *= w1;
    triangleStream.Append(v1);

    // Second triangle
    // Vertex 1: first point of first cap
    v0.pos = p0;
    v0.pos.x += cos(angle) * thicknessX;
    v0.pos.y += sin(angle) * thicknessY;
    v0.pos *= w0;
    triangleStream.Append(v0);

    // Vertex 2: first point of second cap
    v1.pos = p1;
    v1.pos.x += cos(angle) * thicknessX;
    v1.pos.y += sin(angle) * thicknessY;
    v1.pos *= w1;
    triangleStream.Append(v1);

    // Vertex 3: last point of second cap
    v1.pos = p1;
    v1.pos.x += cos(angle + PI) * thicknessX;
    v1.pos.y += sin(angle + PI) * thicknessY;
    v1.pos *= w1;
    triangleStream.Append(v1);
}

[maxvertexcount(42)]
void main(line VertexToPixel input[2], inout TriangleStream<VertexToPixel> triangleStream)
{
    MakeThickLinesTriStream(input, triangleStream);
}