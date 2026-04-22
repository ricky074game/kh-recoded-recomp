#include "lz_decompress.h"

#include "memory_map.h"

namespace LZDecompress {
namespace {

enum class Format : uint8_t {
    LZ10 = 0x10,
    LZ11 = 0x11,
};

struct Stream {
    std::function<bool(uint8_t&)> read_byte;
    std::function<bool(uint8_t)> write_byte;
};

bool ReadByte(Stream& stream, uint8_t& value) {
    return stream.read_byte(value);
}

bool WriteByte(Stream& stream, uint8_t value) {
    return stream.write_byte(value);
}

bool ReadLengthAndDispLZ10(Stream& stream, uint32_t& length, uint32_t& disp) {
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    if (!ReadByte(stream, b1) || !ReadByte(stream, b2)) {
        return false;
    }

    length = static_cast<uint32_t>((b1 >> 4) + 3);
    disp = static_cast<uint32_t>(((b1 & 0x0F) << 8) | b2) + 1u;
    return true;
}

bool ReadLengthAndDispLZ11(Stream& stream, uint32_t& length, uint32_t& disp) {
    uint8_t b0 = 0;
    if (!ReadByte(stream, b0)) {
        return false;
    }

    const uint8_t indicator = static_cast<uint8_t>((b0 >> 4) & 0x0F);
    if (indicator == 0) {
        uint8_t b1 = 0;
        uint8_t b2 = 0;
        if (!ReadByte(stream, b1) || !ReadByte(stream, b2)) {
            return false;
        }
        length = static_cast<uint32_t>(((b0 & 0x0F) << 4) | (b1 >> 4)) + 0x11u;
        disp = static_cast<uint32_t>(((b1 & 0x0F) << 8) | b2) + 1u;
        return true;
    }

    if (indicator == 1) {
        uint8_t b1 = 0;
        uint8_t b2 = 0;
        uint8_t b3 = 0;
        if (!ReadByte(stream, b1) || !ReadByte(stream, b2) || !ReadByte(stream, b3)) {
            return false;
        }
        length = static_cast<uint32_t>(((b0 & 0x0F) << 12) | (b1 << 4) | (b2 >> 4)) + 0x111u;
        disp = static_cast<uint32_t>(((b2 & 0x0F) << 8) | b3) + 1u;
        return true;
    }

    uint8_t b1 = 0;
    if (!ReadByte(stream, b1)) {
        return false;
    }
    length = static_cast<uint32_t>(indicator) + 1u;
    disp = static_cast<uint32_t>(((b0 & 0x0F) << 8) | b1) + 1u;
    return true;
}

Result DecompressImpl(Stream& stream) {
    uint8_t type = 0;
    uint8_t size_lo = 0;
    uint8_t size_mid = 0;
    uint8_t size_hi = 0;

    if (!ReadByte(stream, type) || !ReadByte(stream, size_lo) ||
        !ReadByte(stream, size_mid) || !ReadByte(stream, size_hi)) {
        return {};
    }

    const Format format = static_cast<Format>(type);
    if (format != Format::LZ10 && format != Format::LZ11) {
        return {};
    }

    uint32_t output_size = static_cast<uint32_t>(size_lo)
                         | (static_cast<uint32_t>(size_mid) << 8)
                         | (static_cast<uint32_t>(size_hi) << 16);
    if (output_size == 0) {
        uint8_t e0 = 0, e1 = 0, e2 = 0, e3 = 0;
        if (!ReadByte(stream, e0) || !ReadByte(stream, e1) ||
            !ReadByte(stream, e2) || !ReadByte(stream, e3)) {
            return {};
        }
        output_size = static_cast<uint32_t>(e0)
                    | (static_cast<uint32_t>(e1) << 8)
                    | (static_cast<uint32_t>(e2) << 16)
                    | (static_cast<uint32_t>(e3) << 24);
    }

    std::vector<uint8_t> history;
    history.reserve(output_size);

    while (history.size() < output_size) {
        uint8_t flags = 0;
        if (!ReadByte(stream, flags)) {
            return {};
        }

        for (int bit = 0; bit < 8 && history.size() < output_size; ++bit) {
            const bool compressed = (flags & (0x80 >> bit)) != 0;
            if (!compressed) {
                uint8_t literal = 0;
                if (!ReadByte(stream, literal)) {
                    return {};
                }
                if (!WriteByte(stream, literal)) {
                    return {};
                }
                history.push_back(literal);
                continue;
            }

            uint32_t length = 0;
            uint32_t disp = 0;
            const bool ok = (format == Format::LZ10)
                          ? ReadLengthAndDispLZ10(stream, length, disp)
                          : ReadLengthAndDispLZ11(stream, length, disp);
            if (!ok || disp == 0) {
                return {};
            }

            for (uint32_t i = 0; i < length && history.size() < output_size; ++i) {
                uint8_t value = 0;
                if (disp <= history.size()) {
                    value = history[history.size() - disp];
                }
                if (!WriteByte(stream, value)) {
                    return {};
                }
                history.push_back(value);
            }
        }
    }

    return {true, static_cast<uint32_t>(history.size())};
}

} // namespace

Result DecompressFromBuffer(const uint8_t* data,
                            size_t size,
                            std::vector<uint8_t>& out) {
    out.clear();
    if (data == nullptr || size < 4) {
        return {};
    }

    size_t read_pos = 0;
    Stream stream;
    stream.read_byte = [&](uint8_t& value) {
        if (read_pos >= size) {
            return false;
        }
        value = data[read_pos++];
        return true;
    };
    stream.write_byte = [&](uint8_t value) {
        out.push_back(value);
        return true;
    };

    Result result = DecompressImpl(stream);
    if (!result.ok) {
        out.clear();
    }
    return result;
}

Result DecompressFromMemory(NDSMemory& mem,
                            uint32_t source_addr,
                            uint32_t dest_addr,
                            WriteMode write_mode) {
    uint32_t read_addr = source_addr;
    uint32_t write_addr = dest_addr;
    bool has_pending = false;
    uint8_t pending_lo = 0;

    Stream stream;
    stream.read_byte = [&](uint8_t& value) {
        try {
            value = mem.Read8(read_addr++);
            return true;
        } catch (...) {
            return false;
        }
    };

    stream.write_byte = [&](uint8_t value) {
        try {
            if (write_mode == WriteMode::Byte) {
                mem.Write8(write_addr++, value);
                return true;
            }

            if (!has_pending) {
                pending_lo = value;
                has_pending = true;
                return true;
            }

            const uint16_t half = static_cast<uint16_t>(pending_lo)
                                | (static_cast<uint16_t>(value) << 8);
            mem.Write16(write_addr, half);
            write_addr += 2;
            has_pending = false;
            return true;
        } catch (...) {
            return false;
        }
    };

    Result result = DecompressImpl(stream);
    if (!result.ok) {
        return result;
    }

    if (write_mode == WriteMode::Halfword && has_pending) {
        try {
            mem.Write16(write_addr, static_cast<uint16_t>(pending_lo));
        } catch (...) {
            return {};
        }
    }

    return result;
}

} // namespace LZDecompress
