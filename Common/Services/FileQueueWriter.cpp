#include "pch.h"
#include "FileQueueWriter.h"

#include <fstream>

#include "../AssetsLoader.h"

void FileQueueWriter::PushMessage(const std::wstring& message)
{
    OutputDebugStringW(message.c_str());
    queue.Push(message);
}

bool FileQueueWriter::WriteAllLog()
{
    OutputDebugStringW(filePath.c_str());

    std::wofstream fileSteam;
    fileSteam.open(filePath.c_str(), std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if (fileSteam.is_open())
    {
        std::wstring line;

        while (queue.Size() > 0)
        {
            while (queue.TryPop(line))
            {
                fileSteam << line << std::endl;
            }
        }

        fileSteam.flush();
        fileSteam.close();
        return true;
    }
    return false;
}
