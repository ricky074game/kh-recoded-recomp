#include "hw_save.h"

// ============================================================================
// hw_save.cpp — Nintendo DS Save Chip SPI Emulation
// ============================================================================

// ============================================================================
// AUXSPICNT Register
// ============================================================================
void AUXSPIControl::Write(uint16_t val) {
    baudrate   = val & 0x3;
    cs_hold    = (val >> 6) & 1;
    // bit 7 (busy) is read-only
    slot_mode  = (val >> 13) & 1;
    irq_enable = (val >> 14) & 1;
    enable     = (val >> 15) & 1;
}

uint16_t AUXSPIControl::Read() const {
    uint16_t val = 0;
    val |= baudrate & 0x3;
    if (cs_hold)    val |= (1 << 6);
    if (busy)       val |= (1 << 7);
    if (slot_mode)  val |= (1 << 13);
    if (irq_enable) val |= (1 << 14);
    if (enable)     val |= (1 << 15);
    return val;
}

// ============================================================================
// SaveChip — Init
// ============================================================================
SaveChip::SaveChip() = default;

void SaveChip::SetType(SaveType save_type) {
    type = save_type;
    switch (type) {
        case SaveType::EEPROM_512B: capacity = 512; break;
        case SaveType::EEPROM_8KB:  capacity = 8192; break;
        case SaveType::EEPROM_64KB: capacity = 65536; break;
        case SaveType::Flash_256KB: capacity = 262144; break;
        case SaveType::Flash_512KB: capacity = 524288; break;
        case SaveType::FRAM_32KB:   capacity = 32768; break;
        default: capacity = 0; break;
    }
    data.resize(capacity, 0xFF); // Flash/EEPROM erased state = 0xFF
    address_byte_count = GetAddressByteCount();
}

void SaveChip::Initialize(SaveType save_type, const std::string& file_path) {
    SetType(save_type);
    if (!file_path.empty()) {
        save_path = file_path;
        LoadFromFile(file_path);
    }
}

int SaveChip::GetAddressByteCount() const {
    switch (type) {
        case SaveType::EEPROM_512B: return 1;  // 9-bit, but 1 byte + bit in cmd
        case SaveType::EEPROM_8KB:  return 2;
        case SaveType::EEPROM_64KB: return 2;  // 17-bit via hi/lo command
        case SaveType::FRAM_32KB:   return 2;
        case SaveType::Flash_256KB: return 3;
        case SaveType::Flash_512KB: return 3;
        default: return 2;
    }
}

uint32_t SaveChip::GetJEDECId() const {
    // Return manufacturer + device IDs per common save chips
    switch (type) {
        case SaveType::Flash_256KB: return 0x204012; // ST M45PE20 (256KB)
        case SaveType::Flash_512KB: return 0x204013; // ST M45PE40 (512KB)
        case SaveType::EEPROM_512B: return 0xFF0000;
        case SaveType::EEPROM_8KB:  return 0xFF0001;
        case SaveType::EEPROM_64KB: return 0xFF0002;
        case SaveType::FRAM_32KB:   return 0x040401; // Ramtron FM25L256
        default: return 0xFFFFFF;
    }
}

// ============================================================================
// SPI IO
// ============================================================================
void SaveChip::WriteSPICNT(uint16_t value) {
    bool was_enabled = spicnt.enable;
    bool old_cs_hold = spicnt.cs_hold;
    spicnt.Write(value);

    // CS released (cs_hold went from 1→0 or SPI disabled): end transaction
    if ((was_enabled && old_cs_hold && !spicnt.cs_hold) ||
        (was_enabled && !spicnt.enable)) {
        ResetState();
    }
}

uint16_t SaveChip::ReadSPICNT() const {
    return spicnt.Read();
}

void SaveChip::WriteSPIData(uint8_t value) {
    if (!spicnt.enable || type == SaveType::None) {
        last_read = 0;
        return;
    }

    switch (state) {
        case SPIState::Idle:
            ExecuteCommand(value);
            break;
        case SPIState::Address:
            ProcessAddressByte(value);
            break;
        case SPIState::Data:
            ProcessDataByte(value);
            break;
    }

    // If CS hold is not set, this is the last byte — reset
    if (!spicnt.cs_hold) {
        ResetState();
    }
}

uint8_t SaveChip::ReadSPIData() const {
    return last_read;
}

void SaveChip::ResetState() {
    state = SPIState::Idle;
    current_cmd = 0;
    address = 0;
    address_bytes_remaining = 0;
}

// ============================================================================
// Command Dispatch
// ============================================================================
void SaveChip::ExecuteCommand(uint8_t cmd_byte) {
    current_cmd = cmd_byte;
    last_read = 0;

    switch (cmd_byte) {
        case SPICmd::WREN:
            write_enabled = true;
            last_read = 0;
            break;

        case SPICmd::WRDI:
            write_enabled = false;
            last_read = 0;
            break;

        case SPICmd::RDSR:
            // Status register: bit 0 = WIP (write in progress, always 0 for us)
            //                  bit 1 = WEL (write enable latch)
            last_read = status_reg;
            if (write_enabled) last_read |= 0x02;
            state = SPIState::Data;
            break;

        case SPICmd::WRSR:
            state = SPIState::Data;
            break;

        case SPICmd::READ:
        case SPICmd::READ_HI:
            address = 0;
            // For EEPROM_64KB: READ_HI (0x0B) sets bit 16 of address
            if (type == SaveType::EEPROM_64KB && cmd_byte == SPICmd::READ_HI) {
                address = 0x10000;
            }
            address_bytes_remaining = address_byte_count;
            state = SPIState::Address;
            break;

        case SPICmd::WRITE:
        case SPICmd::WRITE_HI:
            if (!write_enabled) { last_read = 0; break; }
            address = 0;
            if (type == SaveType::EEPROM_64KB && cmd_byte == SPICmd::WRITE_HI) {
                address = 0x10000;
            }
            address_bytes_remaining = address_byte_count;
            state = SPIState::Address;
            break;

        case SPICmd::RDID:
            // Return JEDEC ID bytes in sequence
            address = 0; // Use as byte counter
            last_read = (GetJEDECId() >> 16) & 0xFF;
            state = SPIState::Data;
            break;

        case SPICmd::SE_4K:
        case SPICmd::SE_64K:
        case SPICmd::PE:
            if (!write_enabled) break;
            address = 0;
            address_bytes_remaining = address_byte_count;
            state = SPIState::Address;
            break;

        case SPICmd::CE:
            if (write_enabled) {
                std::memset(data.data(), 0xFF, data.size());
                dirty = true;
                write_enabled = false;
            }
            break;

        case SPICmd::DP:
        case SPICmd::RDP:
            last_read = 0;
            break;

        default:
            last_read = 0xFF;
            break;
    }
}

// ============================================================================
// Address Byte Collection
// ============================================================================
void SaveChip::ProcessAddressByte(uint8_t byte) {
    address = (address << 8) | byte;
    address_bytes_remaining--;
    last_read = 0;

    if (address_bytes_remaining == 0) {
        // Address fully received — start data phase
        // For erase commands, execute immediately
        if (current_cmd == SPICmd::SE_4K) {
            uint32_t base = address & ~0xFFFu;
            uint32_t end = base + 4096;
            if (end > capacity) end = capacity;
            if (base < capacity) {
                std::memset(data.data() + base, 0xFF, end - base);
                dirty = true;
            }
            write_enabled = false;
            state = SPIState::Idle;
        } else if (current_cmd == SPICmd::SE_64K) {
            uint32_t base = address & ~0xFFFFu;
            uint32_t end = base + 65536;
            if (end > capacity) end = capacity;
            if (base < capacity) {
                std::memset(data.data() + base, 0xFF, end - base);
                dirty = true;
            }
            write_enabled = false;
            state = SPIState::Idle;
        } else if (current_cmd == SPICmd::PE) {
            uint32_t base = address & ~0xFFu;
            uint32_t end = base + 256;
            if (end > capacity) end = capacity;
            if (base < capacity) {
                std::memset(data.data() + base, 0xFF, end - base);
                dirty = true;
            }
            write_enabled = false;
            state = SPIState::Idle;
        } else {
            state = SPIState::Data;
        }
    }
}

// ============================================================================
// Data Phase — Read/Write
// ============================================================================
void SaveChip::ProcessDataByte(uint8_t byte) {
    switch (current_cmd) {
        case SPICmd::READ:
        case SPICmd::READ_HI: {
            // Read sequential bytes from save data
            uint32_t addr = address % capacity;
            last_read = (addr < data.size()) ? data[addr] : 0xFF;
            address++;
            break;
        }

        case SPICmd::WRITE:
        case SPICmd::WRITE_HI: {
            // Write a byte to save data
            uint32_t addr = address % capacity;
            if (addr < data.size()) {
                data[addr] = byte;
                dirty = true;
            }
            address++;
            last_read = 0;
            break;
        }

        case SPICmd::RDSR:
            // Keep returning status register
            last_read = status_reg;
            if (write_enabled) last_read |= 0x02;
            break;

        case SPICmd::WRSR:
            status_reg = byte & 0x0C; // Only block protect bits writable
            last_read = 0;
            break;

        case SPICmd::RDID: {
            // Return sequential JEDEC ID bytes
            uint32_t jedec = GetJEDECId();
            int byte_idx = static_cast<int>(address) + 1;
            if (byte_idx == 1) last_read = (jedec >> 8) & 0xFF;
            else if (byte_idx == 2) last_read = jedec & 0xFF;
            else last_read = 0;
            address++;
            break;
        }

        default:
            last_read = 0xFF;
            break;
    }
}

// ============================================================================
// File Persistence
// ============================================================================
bool SaveChip::LoadFromFile(const std::string& path) {
    if (path.empty() || capacity == 0) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        // File doesn't exist yet — that's OK, start with erased data
        return false;
    }

    file.seekg(0, std::ios::end);
    auto file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read up to capacity
    uint32_t read_size = static_cast<uint32_t>(
        std::min(static_cast<std::streamoff>(capacity),
                 static_cast<std::streamoff>(file_size)));
    file.read(reinterpret_cast<char*>(data.data()), read_size);
    dirty = false;
    return true;
}

bool SaveChip::SaveToFile(const std::string& path) const {
    if (path.empty() || capacity == 0) return false;

    // Create parent directories if needed
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    // Backup existing save
    if (std::filesystem::exists(path)) {
        std::string backup = path + ".bak";
        std::filesystem::copy_file(path, backup,
            std::filesystem::copy_options::overwrite_existing);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file.write(reinterpret_cast<const char*>(data.data()), capacity);
    return file.good();
}

bool SaveChip::SaveToFile() const {
    return SaveToFile(save_path);
}

bool SaveChip::FlushIfDirty() {
    if (dirty && !save_path.empty()) {
        if (SaveToFile()) {
            dirty = false;
            return true;
        }
    }
    return false;
}

void SaveChip::FlushToDisk() {
    FlushIfDirty();
}

void SaveChip::LoadFromDisk() {
    LoadFromFile(save_path);
}

void SaveChip::EraseSector(uint32_t sector_addr) {
    if (type != SaveType::Flash_256KB && type != SaveType::Flash_512KB) return;

    uint32_t base = sector_addr & ~0xFFFu;
    uint32_t end = base + 4096;
    if (end > capacity) end = capacity;
    if (base < capacity) {
        std::memset(data.data() + base, 0xFF, end - base);
        dirty = true;
    }
}

void SaveChip::ComputeSaveChecksum() {
    // Basic NDSSave signature checks/checksum routines would be placed here.
        // Keep no-op for unsupported deep power-down command while preserving command flow.
}

void SaveChip::FormatSaveChip() {
    std::memset(data.data(), 0xFF, data.size());
    dirty = true;
}
