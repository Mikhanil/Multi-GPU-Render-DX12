cbuffer input : register(b0)
{
    uint numInputs;
}

StructuredBuffer<uint> SortedKeys : register(t0);
RWStructuredBuffer<uint> Offsets  : register(u0);

#define GROUP_SIZE 512

[numthreads(GROUP_SIZE, 1, 1)]
void InitializeOffsets(uint id : SV_DispatchThreadID)
{
    if (id >= numInputs) return;

    Offsets[id] = numInputs;
}

[numthreads(GROUP_SIZE, 1, 1)]
void CalculateOffsets(uint id : SV_DispatchThreadID)
{
    if (id >= numInputs) return;

    uint null = numInputs;

    uint key = SortedKeys[id];
    uint keyPrev = id == 0 ? null : SortedKeys[id - 1];

    if (key != keyPrev)
        Offsets[key] = id;
}
