#include "Utils.h"

#include <fstream>

namespace Utils
{
    std::vector<uint8_t> ReadAllBytes(const std::string &path)
    {
        std::ifstream inStream(path, std::ios::binary | std::ios::ate);
        auto fileSize = static_cast<size_t>(inStream.tellg());
        if (fileSize == 0)
        {
            throw std::exception("File is empty");
        }

        auto fileBytes = std::vector<uint8_t>(fileSize);

        inStream.seekg(0, std::ios::beg);
        inStream.read(reinterpret_cast<char *>(&fileBytes[0]), fileSize);
        inStream.close();

        return fileBytes;
    }
}