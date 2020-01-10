#include "cartridge.h"
#include <array>
#include <iostream>
#include <fstream>
#include <vector>
#include "fmt/core.h"

namespace gb::cartridge {

namespace {
    std::array<uint8_t, 8192> externalRAM;
    bool externalRamEnabled = false;
    bool enableTracing = false;
    int currentRomBank = 1;
    std::vector<uint8_t> cartridgeData;
}

void SetTracing(const bool enabled)
{
    enableTracing = enabled;
}


void Load(const std::string& path)
{
    {
        std::ifstream ifs(path);
        ifs >> std::noskipws;
        std::copy(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>(), std::back_inserter(cartridgeData));
    }
    if (cartridgeData.size() < 16384)
        throw std::runtime_error("cartridge file too short");

    const auto cartridgeType = cartridgeData[0x147];
    const auto cartridgeROMSize = cartridgeData[0x148];
    const auto cartridgeRAMSize = cartridgeData[0x149];
    std::cout << fmt::format("cartridge type {:x} rom size {:x} ram size {:x}\n", cartridgeType, cartridgeROMSize, cartridgeRAMSize);
    if (cartridgeType != 1 && cartridgeType != 0)
        throw std::runtime_error("only MBC1 supported");
}

uint8_t Read_u8(const Address address)
{
    if (address >= 0x0000 && address <= 0x3fff)
        return cartridgeData[address];
    if (address >= 0x4000 && address <= 0x7fff) {
        const Address addr = (address - 0x4000) + currentRomBank * 16384;
        return cartridgeData[addr];
    }

    if (address >= 0xa000 && address <= 0xbfff) {
        if (!externalRamEnabled && enableTracing) {
            std::cout << fmt::format("cartridge: ignoring read from address {:04x}, extram disabled\n", address);
            return 0xff;
        }
        return externalRAM[address - 0xa000];
    }

    std::cout << fmt::format("cartridge: ignoring read from address {:04x}, unmapped\n", address);
    return 0xff;
}

void Write_u8(const Address address, uint8_t value)
{
    if (address >= 0x0000 && address <= 0x1fff) {
        externalRamEnabled = (value & 0xf) != 0;
        return;
    }

    if (address >= 0x2000 && address <= 0x3fff) {
        currentRomBank= value & 0x1f;
        if ((currentRomBank & 0xf) == 0)
            ++currentRomBank;
        if (enableTracing)
            std::cout << fmt::format("cartridge: rom bank {} selected (wrote {:02x} to {:04x})\n", currentRomBank, value, address);
        return;
    }

    if (address >= 0x4000 && address <= 0x5fff) {
        std::cout << fmt::format("cartridge: TODO: set ram/rom bank {:02x}\n", value);
        return;
    }

    if (address >= 0x6000 && address <= 0x7fff) {
        std::cout << fmt::format("cartridge: TODO: set rom/ram mode {:02x}\n", value);
        return;
    }

    if (address >= 0xa000 && address <= 0xbfff) {
        if (!externalRamEnabled) {
            std::cout << fmt::format("cartridge: ignoring write to address {:04x}, extram disabled\n", address);
            return;
        }
        externalRAM[address - 0xa000] = value;
        return;
    }

    std::cout << fmt::format("cartridge: ignoring write to address {:04x}, unmapped\n", address);
}

}
