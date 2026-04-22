#pragma once

#include <cstdint>
#include <functional>
#include <vector>

class NDSMemory;

namespace LZDecompress {

enum class WriteMode {
    Byte,
    Halfword,
};

struct Result {
    bool ok = false;
    uint32_t bytes_written = 0;
};

Result DecompressFromBuffer(const uint8_t* data,
                            size_t size,
                            std::vector<uint8_t>& out);

Result DecompressFromMemory(NDSMemory& mem,
                            uint32_t source_addr,
                            uint32_t dest_addr,
                            WriteMode write_mode);

} // namespace LZDecompress
