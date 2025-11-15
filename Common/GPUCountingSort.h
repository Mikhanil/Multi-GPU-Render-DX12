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
	using BufferPtr = std::shared_ptr<PEPEngine::Graphics::GBuffer>;
	
	struct EKernels
	{
		// this shit is so i could iterate easily not by casting non-stop like a madman
		enum : uint8_t 
		{
			ClearCounts,
			CalculateCounts,
			ScatterOutput,
			CopyBack,
			NumKernels
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

public:
	GPUCountingSort() = default;
	void Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device);

	void Run(
		const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList,
		const BufferPtr& itemsBuffer,
		const BufferPtr& keysBuffer,
		UINT maxValue);

public:
	bool bIsInitialized{false};

private:
	void CompileShaders();

	bool TryCreateBuffer(BufferPtr& buffer, UINT stride, UINT count);

	void InitializeDescriptors(const BufferPtr& itemsBuffer, const BufferPtr& keysBuffer);

private:
	GPUPrefixSum scan;

	bool m_bDescriptorsInitialized{false};
	
	using ComputePSOsMap = std::unordered_map<uint8_t, std::shared_ptr<PEPEngine::Graphics::ComputePSO>>;
	using ShadersMap = std::unordered_map<uint8_t, std::shared_ptr<PEPEngine::Graphics::GShader>>;
	PEPEngine::Graphics::GRootSignature m_SortSignature;
	ComputePSOsMap m_PSOs;
	ShadersMap m_Shaders;

	PEPEngine::Graphics::GDescriptor m_ComputeDescriptors;
	
	BufferPtr m_SortedItemsBuffer;
	BufferPtr m_SortedKeysBuffer;
	BufferPtr m_CountsBuffer;

	std::shared_ptr<PEPEngine::Graphics::GDevice> m_Device;
};
