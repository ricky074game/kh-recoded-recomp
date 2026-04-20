#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <fstream>
#include <filesystem>

// The Virtual File System (VFS) for Re:coded Recompilation
// Mimics the DS game's FAT/FNT (File ID vs Path) lookup mechanisms, but proxies
// the physical disk reads to the extracted PC 'data/' directory.
class VFS {
public:
    struct FileRecord {
        uint32_t fileId;
        std::string filePath;
        uint32_t romOffsetStart;
        uint32_t romOffsetEnd;
    };

    VFS(const std::string& dataDirectory) : dataDir(std::filesystem::path(dataDirectory)) {}

    // Parses the FAT/FNT from an NDS ROM to build the File ID -> Path mapping
    bool LoadFromRom(const std::string& romPath);

    // Read a file by its integer ID (intercepts game logic reading from the ROM)
    std::vector<uint8_t> ReadFileById(uint32_t fileId);

    // Read a file by its string path
    std::vector<uint8_t> ReadFileByPath(const std::string& path);

    // Look up ID by path, or Path by ID
    std::string GetPathById(uint32_t fileId) const;
    uint32_t GetIdByPath(const std::string& path) const;

    void MountRomFs();
    void ReadBanner();
    void DecryptSecureArea();
    void CalculateRomHash();

private:
    std::filesystem::path dataDir;
    std::map<uint32_t, FileRecord> idToFile;
    std::map<std::string, uint32_t> pathToId;

    void ParseFAT(std::ifstream& rom, uint32_t fatOffset, uint32_t fatSize);
    void ParseFNT(std::ifstream& rom, uint32_t fntOffset, uint32_t fntSize);
    static std::string NormalizePath(const std::string& path);
};
