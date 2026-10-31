#pragma once
#include "LockThreadQueue.h"
#include <filesystem>

class FileQueueWriter
{
    PEPEngine::Utils::LockThreadQueue<std::wstring> queue;

    std::filesystem::path filePath;
public:
    FileQueueWriter(std::filesystem::path filePath): filePath(std::move(filePath)) {  }
    
    void PushMessage(const std::wstring& message);

    bool WriteAllLog();
};
