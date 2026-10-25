struct NestedData
{
    float3 Position;
    float Weight;
    float2 UV;
};

cbuffer LayoutFixture : register(b0)
{
    float Scalar;
    float3 Vector;
    row_major float2x3 RowMatrix;
    column_major float3x2 ColumnMatrix;
    float Values[3];
    NestedData Nested[2];
};

float4 VSMain(float2 position : POSITION) : SV_POSITION
{
    return float4(position, 0.5, 1);
}

float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    uint index = uint(position.x) % 2;
    return float4(Vector * Scalar + RowMatrix[index] + ColumnMatrix[index].xyx
                  + Nested[index].Position * Nested[index].Weight
                  + Nested[index].UV.xyx + Values[uint(position.y) % 3], 1);
}
