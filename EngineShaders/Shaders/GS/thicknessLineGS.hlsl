#include "../Globals.hlsli"

struct VertexToPixel 
{
	float4 pos	: SV_POSITION;
	float4 col	: COLOR;
};

void AddHalfCircle(inout TriangleStream<VertexToPixel> triangleStream, VertexToPixel input, int nCountTriangles, float4 linePointToConnect, float pos_w, float angle, float pixel_thicknessX, float pixel_thicknessY)
{
    // projection position : linePointToConnect

    VertexToPixel output = input;
    for (int nI = 0; nI < nCountTriangles; ++nI)
    {
        output.pos = linePointToConnect;
        output.pos.x += cos(angle + (PI / nCountTriangles * nI)) * pixel_thicknessX;
        output.pos.y += sin(angle + (PI / nCountTriangles * nI)) * pixel_thicknessY;
        //output.pos.z = 0.0f;
        //output.pos.w = 0.0f;
        //output.pos += linePointToConnect;
        output.pos *= pos_w;
        triangleStream.Append(output);

        output.pos = linePointToConnect * pos_w;
        triangleStream.Append(output);

        output.pos = linePointToConnect;
        output.pos.x += cos(angle + (PI / nCountTriangles * (nI + 1))) * pixel_thicknessX;
        output.pos.y += sin(angle + (PI / nCountTriangles * (nI + 1))) * pixel_thicknessY;
        //output.pos.z = 0.0f;
        //output.pos.w = 0.0f;
        //output.pos += linePointToConnect;
        output.pos *= pos_w;
        triangleStream.Append(output);

        triangleStream.RestartStrip();
    }
}

void MakeThickLinesTriStream(VertexToPixel input[2], inout TriangleStream<VertexToPixel> triangleStream)
{
    float4 positionPoint0Transformed = input[0].pos;
    float4 positionPoint1Transformed = input[1].pos;

    float pos0_w = positionPoint0Transformed.w;
    float pos1_w = positionPoint1Transformed.w;

    //calculate out the W parameter, because of usage of perspective rendering
    positionPoint0Transformed.xyz = positionPoint0Transformed.xyz / positionPoint0Transformed.w;
    positionPoint0Transformed.w = 1.0f;
    positionPoint1Transformed.xyz = positionPoint1Transformed.xyz / positionPoint1Transformed.w;
    positionPoint1Transformed.w = 1.0f;

    //calculate the angle between the 2 points on the screen
    float3 positionDifference = positionPoint0Transformed.xyz - positionPoint1Transformed.xyz;
    float3 coordinateSystem = float3(1.0f, 0.0f, 0.0f);

    positionDifference.z = 0.0f;
    coordinateSystem.z = 0.0f;

    float angle = acos(dot(positionDifference.xy, coordinateSystem.xy) / (length(positionDifference.xy) * length(coordinateSystem.xy)));

    if (cross(positionDifference, coordinateSystem).z < 0.0f)
    {
        angle = 2.0f * PI - angle;
    }

    angle *= -1.0f;
    angle -= PI * 0.5f;

    const float pixel_thicknessX = g_xThickness * (float)g_xCamera.cameras[0].internal_resolution_rcp.x;
    const float pixel_thicknessY = g_xThickness * (float)g_xCamera.cameras[0].internal_resolution_rcp.y;
    //first half circle of the line
    int nCountTriangles = 6;
    AddHalfCircle(triangleStream, input[0], nCountTriangles, positionPoint0Transformed, pos0_w, angle, pixel_thicknessX, pixel_thicknessY);
    AddHalfCircle(triangleStream, input[1], nCountTriangles, positionPoint1Transformed, pos1_w, angle + PI, pixel_thicknessX, pixel_thicknessY);

    //connection between the two circles
    //triangle1
    VertexToPixel output0 = input[0];
    VertexToPixel output1 = input[1];
    output0.pos = positionPoint0Transformed;
    output0.pos.x += cos(angle) * pixel_thicknessX;
    output0.pos.y += sin(angle) * pixel_thicknessY;
    output0.pos *= pos0_w; //undo calculate out the W parameter, because of usage of perspective rendering
    triangleStream.Append(output0);

    output0.pos = positionPoint0Transformed;
    output0.pos.x += cos(angle + (PI / nCountTriangles * (nCountTriangles))) * pixel_thicknessX;
    output0.pos.y += sin(angle + (PI / nCountTriangles * (nCountTriangles))) * pixel_thicknessY;
    output0.pos *= pos0_w;
    triangleStream.Append(output0);

    output1.pos = positionPoint1Transformed;
    output1.pos.x += cos(angle + (PI / nCountTriangles * (nCountTriangles))) * pixel_thicknessX;
    output1.pos.y += sin(angle + (PI / nCountTriangles * (nCountTriangles))) * pixel_thicknessY;
    output1.pos *= pos1_w;
    triangleStream.Append(output1);

    //triangle2
    output0.pos = positionPoint0Transformed;
    output0.pos.x += cos(angle) * pixel_thicknessX;
    output0.pos.y += sin(angle) * pixel_thicknessY;
    output0.pos *= pos0_w;
    triangleStream.Append(output0);

    output1.pos = positionPoint1Transformed;
    output1.pos.x += cos(angle) * pixel_thicknessX;
    output1.pos.y += sin(angle) * pixel_thicknessY;
    output1.pos *= pos1_w;
    triangleStream.Append(output1);

    output1.pos = positionPoint1Transformed;
    output1.pos.x += cos(angle + (PI / nCountTriangles * (nCountTriangles))) * pixel_thicknessX;
    output1.pos.y += sin(angle + (PI / nCountTriangles * (nCountTriangles))) * pixel_thicknessY;
    output1.pos *= pos1_w;
    triangleStream.Append(output1);
}

[maxvertexcount(42)]
void main(line VertexToPixel input[2], inout TriangleStream<VertexToPixel> triangleStream)
{
    MakeThickLinesTriStream(input, triangleStream);
}
