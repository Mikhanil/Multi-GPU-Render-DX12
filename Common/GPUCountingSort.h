#pragma once
#include <memory>
#include <unordered_map>

#include "GPUPrefixSum.h"
#include "GRootSignature.h"

namespace PEPEngine::Graphics
{
    class GCommandList;
}

namespace PEPEngine::Graphics
{
    class ComputePSO;
    class GBuffer;
    class GShader;
}

class GPUCountingSort
{

#pragma region Enums
    using BufferPtr = std::shared_ptr<PEPEngine::Graphics::GBuffer>;
    
    struct EKernels
    {
#define EKERNELS_LIST                           \
X(ClearCounts)                                  \
X(CalculateCounts)                              \
X(ScatterOutput)                                \
X(CopyBack)
        enum Enum : uint8_t 
        {
#define X(name) name,
            EKERNELS_LIST
#undef X	
            NumKernels
        };

        static const char* ToString(Enum kernel)
        {
            switch(kernel)
            {
#define X(name) case name: return "CountingSort::" #name;
                EKERNELS_LIST
#undef X
            default: return "";
            }
        }
        
        static std::wstring ToWide(Enum e)
        {
            const char* s = ToString(e);
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
            std::wstring ws(size_needed, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s, -1, ws.data(), size_needed);
            return ws;
        }

        static constexpr Enum All[] =
            {
#define X(name) name,
            EKERNELS_LIST
#undef X
            };
    };

    struct ESortSlots
    {
        enum Slots : uint8_t
        {
            NumInputsSlot,
            InputItemsSlot,
            InputKeysSlot,
            SortedItemsSlot,
            SortedKeysSlot,
            CountsSlot,
            Count
        };
    };

    enum EBufferOffsets
    {
        ItemsBuffer,
        KeysBuffer,
        SortedItemsBuffer,
        SortedKeysBuffer,
        CountsBuffer
    };
#pragma endregion Enums

public:
    GPUCountingSort() = default;
    void Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device, size_t count);

    void Run(
        const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList,
        const BufferPtr& itemsBuffer,
        const BufferPtr& keysBuffer,
        UINT maxValue);

public:
    bool bIsInitialized{false};

private:
    void CompileShaders();

    bool TryCreateBuffer(BufferPtr& buffer, UINT stride, UINT count, const std::wstring& name);

    void InitializeDescriptors(const BufferPtr& itemsBuffer, const BufferPtr& keysBuffer);

private:
    GPUPrefixSum scan;

    bool m_bDescriptorsInitialized{false};
    
    using ComputePSOsMap = std::unordered_map<EKernels::Enum, std::shared_ptr<PEPEngine::Graphics::ComputePSO>>;
    using ShadersMap = std::unordered_map<EKernels::Enum, std::shared_ptr<PEPEngine::Graphics::GShader>>;
    PEPEngine::Graphics::GRootSignature m_SortSignature;
    ComputePSOsMap m_PSOs;
    ShadersMap m_Shaders;

    PEPEngine::Graphics::GDescriptor m_ComputeDescriptors;
    
    BufferPtr m_SortedItemsBuffer;
    BufferPtr m_SortedKeysBuffer;
    BufferPtr m_CountsBuffer;

    std::shared_ptr<PEPEngine::Graphics::GDevice> m_Device;
};
