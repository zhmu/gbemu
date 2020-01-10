#include "memory.h"
#include <iostream>
#include <string>
#include "io.h"

#include "fmt/core.h"

namespace gb {
    namespace {
        std::string IORegisterToString(const Address address) {
            switch(address) {
                case io::P1: return "P1";
                case io::SB: return "SB";
                case io::SC: return "SC";
                case io::DIV: return "DIV";
                case io::TIMA: return "TIMA";
                case io::TMA: return "TMA";
                case io::TAC: return "TAC";
                case io::IF: return "IF";
                case io::NR10: return "NR10";
                case io::NR11: return "NR11";
                case io::NR12: return "NR12";
                case io::NR13: return "NR13";
                case io::NR14: return "NR14";
                case io::NR21: return "NR21";
                case io::NR22: return "NR22";
                case io::NR23: return "NR23";
                case io::NR24: return "NR24";
                case io::NR30: return "NR30";
                case io::NR31: return "NR31";
                case io::NR32: return "NR32";
                case io::NR33: return "NR33";
                case io::NR34: return "NR34";
                case io::NR41: return "NR41";
                case io::NR42: return "NR42";
                case io::NR43: return "NR43";
                case io::NR44: return "NR44";
                case io::NR50: return "NR50";
                case io::NR51: return "NR51";
                case io::NR52: return "NR52";
                case io::AUD3WAVERAM+0: return "AUD3WAVERAM+0";
                case io::AUD3WAVERAM+1: return "AUD3WAVERAM+1";
                case io::AUD3WAVERAM+2: return "AUD3WAVERAM+2";
                case io::AUD3WAVERAM+3: return "AUD3WAVERAM+3";
                case io::AUD3WAVERAM+4: return "AUD3WAVERAM+4";
                case io::AUD3WAVERAM+5: return "AUD3WAVERAM+5";
                case io::AUD3WAVERAM+6: return "AUD3WAVERAM+6";
                case io::AUD3WAVERAM+7: return "AUD3WAVERAM+7";
                case io::AUD3WAVERAM+8: return "AUD3WAVERAM+8";
                case io::AUD3WAVERAM+9: return "AUD3WAVERAM+9";
                case io::AUD3WAVERAM+10: return "AUD3WAVERAM+a";
                case io::AUD3WAVERAM+11: return "AUD3WAVERAM+b";
                case io::AUD3WAVERAM+12: return "AUD3WAVERAM+c";
                case io::AUD3WAVERAM+13: return "AUD3WAVERAM+d";
                case io::AUD3WAVERAM+14: return "AUD3WAVERAM+e";
                case io::AUD3WAVERAM+15: return "AUD3WAVERAM+f";
                case io::LCDC: return "LCDC";
                case io::STAT: return "STAT";
                case io::SCY: return "SCY";
                case io::SCX: return "SCX";
                case io::LY: return "LY";
                case io::LYC: return "LYC";
                case io::DMA: return "DMA";
                case io::BGP: return "BGP";
                case io::OBP0: return "OBP0";
                case io::OBP1: return "OBP1";
                case io::WY: return "WY";
                case io::WX: return "WX";
                case io::IE: return "IE";
            }
            return fmt::format("{:x}", address);
        }

        constexpr bool IsInRange(const Address address, const Address start, const Address end) {
            return address >= start && address <= end;
        }

        constexpr bool IsRAM(const Address address)
        {
            return
                IsInRange(address, memory_map::VRAMStart, memory_map::VRAMEnd) ||
                IsInRange(address, memory_map::WRAM0Start, memory_map::WRAM0End) ||
                IsInRange(address, memory_map::WRAM1Start, memory_map::WRAM1End) ||
                IsInRange(address, memory_map::MirrorStart, memory_map::MirrorEnd) ||
                IsInRange(address, memory_map::HRAMStart, memory_map::HRAMEnd) ||
                IsInRange(address, memory_map::OAMStart, memory_map::OAMEnd);
        }

        constexpr bool IsIO(const Address address)
        {
            return
                IsInRange(address, memory_map::IOStart, memory_map::IOEnd) ||
                address == memory_map::IE;
        }

        constexpr bool IsCartridge(const Address address)
        {
            return
                IsInRange(address, memory_map::Cartridge0Start, memory_map::Cartridge0End) ||
                IsInRange(address, memory_map::Cartridge1Start, memory_map::Cartridge1End);
        }
    }

    namespace cartridge {
        uint8_t Read_u8(const Address);
        void Write_u8(const Address, uint8_t);
    }

    uint8_t Memory::Read_u8(Address address) {
        if (IsIO(address)) {
            const auto value = io.Read(address);
            if (enableTracing)
                std::cout << fmt::format("*** read (i/o): {} ({:x}) -> {:x}\n", IORegisterToString(address), address, value);
            return value;
        }

        if (IsCartridge(address)) {
            return cartridge::Read_u8(address);
        }

        if (IsRAM(address)) {
            if (enableTracing)
                std::cout << fmt::format("*** read: ram @ {:x} -> {:x}\n", address, data[address]);
            if (IsInRange(address, memory_map::MirrorStart, memory_map::MirrorEnd))
                address = (address - memory_map::MirrorStart) + memory_map::WRAM0Start;
            return data[address];
        }

        std::cout << fmt::format("*** read: invalid address {:x}\n", address);
        return 0xff;
    }

    uint16_t Memory::Read_u16(const Address address) {
        const uint16_t lo = Read_u8(address + 0);
        const uint16_t hi = Read_u8(address + 1);
        return (hi << 8) | lo;
    }

    void Memory::Write_u8(Address address, const uint8_t value) {
        // XXX This shouldn't be here
        if (address == io::DMA) {
            // XXX We need to properly delay, block everything except HRAM etc...
            const uint16_t sourceAddress = value << 8;
            const uint16_t destAddress = 0xfe00;
            for(unsigned int n = 0; n < 0xa0; ++n) {
                Write_u8(destAddress + n, Read_u8(sourceAddress + n));
            }
        }

        if (IsCartridge(address)) {
            return cartridge::Write_u8(address, value);
        }

        if (IsRAM(address)) {
            if (enableTracing)
                std::cout << fmt::format("*** write: ram write @ {:x} <- {:x}\n", address, value);
            if (IsInRange(address, memory_map::MirrorStart, memory_map::MirrorEnd))
                address = (address - memory_map::MirrorStart) + memory_map::WRAM0Start;
            data[address] = value;
            return;
        }

        if (IsIO(address)) {
            if (enableTracing)
                std::cout << fmt::format("*** write: i/o write @ {} ({:x}) <- {:x}\n", IORegisterToString(address), address, value);
            io.Write(address, value);
            return;
        }

        std::cout << fmt::format("** write: ignoring write to non-RAM address {:x} value {:x}\n", address, value);
    }

    void Memory::Write_u16(const Address address, const uint16_t value) {
        Write_u8(address, static_cast<uint8_t>(value & 0xff));
        Write_u8(address + 1, static_cast<uint8_t>(value >> 8));
    }

    uint8_t Memory::At_u8(Address address) const
    {
        if (IsCartridge(address)) return cartridge::Read_u8(address);
        if (IsRAM(address)) {
            if (IsInRange(address, memory_map::MirrorStart, memory_map::MirrorEnd))
                address = (address - memory_map::MirrorStart) + memory_map::WRAM0Start;
            return data[address];
        }

        return 0xff;
    }


}
