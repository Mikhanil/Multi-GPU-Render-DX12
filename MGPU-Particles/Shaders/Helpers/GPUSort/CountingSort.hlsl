cbuffer inputBuffer                     : register(b0)
{
    uint numInputs;
}

RWStructuredBuffer<uint> InputItems     : register(u0);
RWStructuredBuffer<uint> InputKeys      : register(u1);
RWStructuredBuffer<uint> SortedItems    : register(u2);
RWStructuredBuffer<uint> SortedKeys     : register(u3);

RWStructuredBuffer<uint> Counts         : register(u4);

static const int GroupSize = 256;

[numthreads(GroupSize, 1, 1)]
void ClearCounts(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numInputs) return;

    Counts[id.x] = 0;
    InputItems[id.x] = id.x;
}

[numthreads(GroupSize, 1, 1)]
void CalculateCounts(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numInputs) return;

    uint key = InputKeys[id.x];
    InterlockedAdd(Counts[key], 1);
}

[numthreads(GroupSize, 1, 1)]
void ScatterOutput(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numInputs) return;

    uint key = InputKeys[id.x];

    uint sortedIndex;
    InterlockedAdd(Counts[key], 1, sortedIndex);

    SortedItems[sortedIndex] = InputItems[id.x];
    SortedKeys[sortedIndex] = key;
}

[numthreads(GroupSize, 1, 1)]
void CopyBack(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numInputs) return;

    InputItems[id.x] = SortedItems[id.x];
    InputKeys[id.x] = SortedKeys[id.x];
}
