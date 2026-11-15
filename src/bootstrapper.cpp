#include "bootstrapper.h"

#include <windows.h>
#include <fstream>
#include <vector>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cstdint>

namespace fs = std::filesystem;

namespace {
    constexpr char MAGIC[16] = "HORRORPKG_V1\0\0\0";
    constexpr size_t FOOTER_SIZE = 8 + 16; // offset(8) + magic(16)

    std::string exeDir() {
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string p(buf, n);
        return fs::path(p).parent_path().string();
    }
}

bool Bootstrapper::extractIfNeeded() {
    char exePath[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    std::ifstream exe(exePath, std::ios::binary | std::ios::ate);
    if (!exe) return false;

    std::streamoff exeSize = exe.tellg();
    if (exeSize < (std::streamoff)FOOTER_SIZE) return true; // payload نداره

    // خوندن footer از انتهای فایل
    exe.seekg(exeSize - FOOTER_SIZE);
    uint64_t payloadOffset = 0;
    char magic[16];
    exe.read((char*)&payloadOffset, 8);
    exe.read(magic, 16);

    if (std::memcmp(magic, MAGIC, 16) != 0) return true; // payload نداره
    if (payloadOffset >= (uint64_t)exeSize) return false;

    exe.seekg(payloadOffset);

    std::string base = exeDir();
    uint32_t fileCount = 0;
    exe.read((char*)&fileCount, 4);
    if (!exe) return false;

    int extracted = 0;
    constexpr size_t CHUNK = 1u << 20; // 1 MB
    std::vector<char> buf(CHUNK);

    for (uint32_t i = 0; i < fileCount; ++i) {
        uint16_t pathLen = 0;
        exe.read((char*)&pathLen, 2);
        if (!exe || pathLen == 0 || pathLen > 4096) return false;

        std::string relPath(pathLen, '\0');
        exe.read(&relPath[0], pathLen);

        uint64_t dataSize = 0;
        exe.read((char*)&dataSize, 8);
        if (!exe) return false;

        // جلوگیری از path traversal
        if (relPath.find("..") != std::string::npos) {
            exe.seekg(dataSize, std::ios::cur);
            continue;
        }

        fs::path dest = fs::path(base) / relPath;

        bool needExtract = true;
        std::error_code ec;
        if (fs::exists(dest, ec)) {
            auto sz = fs::file_size(dest, ec);
            if (!ec && sz == dataSize) needExtract = false;
        }

        if (needExtract) {
            fs::create_directories(dest.parent_path(), ec);
            std::ofstream out(dest, std::ios::binary);
            if (!out) { exe.seekg(dataSize, std::ios::cur); continue; }

            uint64_t remaining = dataSize;
            while (remaining > 0) {
                size_t toRead = (size_t)std::min<uint64_t>(remaining, CHUNK);
                exe.read(buf.data(), toRead);
                if (!exe) return false;
                out.write(buf.data(), toRead);
                remaining -= toRead;
            }
            ++extracted;
            std::cout << "[Bootstrap] Extracted: " << relPath << "\n";
        } else {
            exe.seekg(dataSize, std::ios::cur);
        }
    }

    if (extracted > 0)
        std::cout << "[Bootstrap] " << extracted << " file(s) extracted.\n";

    return true;
}
