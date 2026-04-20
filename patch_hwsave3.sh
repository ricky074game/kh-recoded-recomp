#!/bin/bash
cat << 'HWSAVEH_EOF' > runtime/src/hw/hw_save.h
#pragma once

// ============================================================================
// hw_save.h — Nintendo DS Save Chip Emulation (SPI EEPROM/Flash)
//
// DS save data lives on SPI-connected EEPROM or Flash chips on the
// cartridge. The game communicates via AUXSPICNT (0x040001A0) and
// AUXSPIDATA (0x040001A2). This module emulates the SPI command protocol,
// stores save data in a RAM buffer, and persists it to a PC file.
//
// Reference: GBATEK §DS Cartridge Backup
//   AUXSPICNT:  0x040001A0 (16-bit, SPI bus control)
//   AUXSPIDATA: 0x040001A2 (8-bit data register)
//
// Supported chip types:
//   EEPROM  512B  — 1-byte address
//   EEPROM  8KB   — 2-byte address
//   EEPROM  64KB  — 2-byte address (17-bit via command bit)
//   Flash   256KB — 3-byte address, sector erase
//   Flash   512KB — 3-byte address, sector erase
//   FRAM    32KB  — 2-byte address (no write delay)
// ============================================================================

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>

// ============================================================================
// Save Chip Types
// ============================================================================
enum class SaveType : uint8_t {
    None       = 0,
    EEPROM_512B,   //   512 bytes, 1-byte address
    EEPROM_8KB,    //  8192 bytes, 2-byte address
    EEPROM_64KB,   // 65536 bytes, 2-byte address (bit17 in command)
    Flash_256KB,   // 262144 bytes, 3-byte address
    Flash_512KB,   // 524288 bytes, 3-byte address
    FRAM_32KB      // 32768 bytes, 2-byte address
};

// ============================================================================
// SPI Commands (shared across EEPROM/Flash)
// ============================================================================
namespace SPICmd {
    constexpr uint8_t WREN       = 0x06; // Write Enable
    constexpr uint8_t WRDI       = 0x04; // Write Disable
    constexpr uint8_t RDSR       = 0x05; // Read Status Register
    constexpr uint8_t WRSR       = 0x01; // Write Status Register
    constexpr uint8_t READ       = 0x03; // Read Data
    constexpr uint8_t READ_LO    = 0x03; // EEPROM 64KB: read from lower half
    constexpr uint8_t READ_HI    = 0x0B; // EEPROM 64KB: read from upper half
    constexpr uint8_t WRITE      = 0x02; // Write Data / Page Write
    constexpr uint8_t WRITE_LO   = 0x02; // EEPROM 64KB: write lower half
    constexpr uint8_t WRITE_HI   = 0x0A; // EEPROM 64KB: write upper half
    constexpr uint8_t RDID       = 0x9F; // Read JEDEC ID
    constexpr uint8_t SE_4K      = 0x20; // Sector Erase 4KB (Flash)
    constexpr uint8_t SE_64K     = 0xD8; // Sector Erase 64KB (Flash)
    constexpr uint8_t PE         = 0xDB; // Page Erase (Flash)
    constexpr uint8_t CE         = 0xC7; // Chip Erase (Flash)
    constexpr uint8_t DP         = 0xB9; // Deep Power Down (Flash)
    constexpr uint8_t RDP        = 0xAB; // Release Deep Power Down (Flash)
}

// ============================================================================
// SPI Transfer State Machine
// ============================================================================
enum class SPIState : uint8_t {
    Idle,           // Waiting for command byte
    Address,        // Collecting address bytes
    Data            // Reading or writing data
};

// ============================================================================
// AUXSPICNT Register (0x040001A0)
// ============================================================================
struct AUXSPIControl {
    uint8_t  baudrate = 0;     // Bits 0-1: clock speed
    bool     cs_hold = false;  // Bit 6: keep CS low after transfer
    bool     busy = false;     // Bit 7: transfer in progress (read-only)
    bool     slot_mode = false; // Bit 13: 0=touchscreen, 1=game card
    bool     irq_enable = false;// Bit 14: IRQ on transfer
    bool     enable = false;   // Bit 15: SPI enable

    void Write(uint16_t val);
    uint16_t Read() const;
};

// ============================================================================
// Save Chip — SPI Command State Machine + File Persistence
// ============================================================================
class SaveChip {
public:
    // ---- Configuration ----
    SaveType type = SaveType::None;
    uint32_t capacity = 0;  // Total bytes

    // ---- Save Data Buffer ----
    std::vector<uint8_t> data;

    // ---- SPI State ----
    AUXSPIControl spicnt;
    SPIState state = SPIState::Idle;
    uint8_t  current_cmd = 0;
    uint32_t address = 0;
    int      address_bytes_remaining = 0;
    int      address_byte_count = 0;  // Total address bytes for this chip
    bool     write_enabled = false;
    uint8_t  status_reg = 0;
    uint8_t  last_read = 0;           // Last byte read from AUXSPIDATA
    bool     dirty = false;

    // ---- File Persistence ----
    std::string save_path;

    // ---- Initialization ----
    SaveChip();
    void Initialize(SaveType save_type, const std::string& file_path = "");
    void SetType(SaveType save_type);

    // ---- SPI IO (called from memory map) ----
    void WriteSPICNT(uint16_t value);
    uint16_t ReadSPICNT() const;
    void WriteSPIData(uint8_t value);
    uint8_t ReadSPIData() const;

    // ---- File IO ----
    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;
    bool SaveToFile() const; // Uses save_path
    bool FlushIfDirty();

    // ---- Helpers ----
    uint32_t GetCapacity() const { return capacity; }
    bool IsDirty() const { return dirty; }

    void FlushToDisk();
    void LoadFromDisk();
    void EraseSector(uint32_t sector_addr);
    void ComputeSaveChecksum();
    void FormatSaveChip();

private:
    void ResetState();
    void ExecuteCommand(uint8_t cmd_byte);
    void ProcessAddressByte(uint8_t byte);
    void ProcessDataByte(uint8_t byte);
    int  GetAddressByteCount() const;
    uint32_t GetJEDECId() const;
};
HWSAVEH_EOF
