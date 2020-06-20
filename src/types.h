#pragma once

#include <cstdint>

namespace gb {
    using Address = uint16_t;

    namespace memory_map {
        inline constexpr Address BootstrapROMStart = 0x0000;
        inline constexpr Address BootstrapROMEnd = 0x0100;
        inline constexpr Address Cartridge0Start = 0x0000;
        inline constexpr Address Cartridge0End = 0x7fff;
        inline constexpr Address VRAMStart = 0x8000;
        inline constexpr Address VRAMEnd = 0x9fff;
        inline constexpr Address Cartridge1Start = 0xa000;
        inline constexpr Address Cartridge1End = 0xbfff;
        inline constexpr Address WRAM0Start = 0xc000;
        inline constexpr Address WRAM0End = 0xcfff;
        inline constexpr Address WRAM1Start = 0xd000;
        inline constexpr Address WRAM1End = 0xdfff;
        inline constexpr Address MirrorStart = 0xe000;
        inline constexpr Address MirrorEnd = 0xfdff;
        inline constexpr Address OAMStart = 0xfe00;
        inline constexpr Address OAMEnd = 0xfe9f;
        inline constexpr Address IOStart = 0xff00;
        inline constexpr Address IOEnd = 0xff7f;
        inline constexpr Address HRAMStart = 0xff80;
        inline constexpr Address HRAMEnd = 0xfffe;
        inline constexpr Address IE = 0xffff;
    }

    namespace interrupt {
        inline constexpr uint8_t VBlank = (1 << 0);
        inline constexpr uint8_t LCDStat = (1 << 1);
        inline constexpr uint8_t Timer = (1 << 2);
        inline constexpr uint8_t Serial = (1 << 3);
        inline constexpr uint8_t Joypad = (1 << 4);
    }

    namespace io {
        inline constexpr Address P1 = 0xff00;
        // Serial
        inline constexpr Address SB = 0xff01;
        inline constexpr Address SC = 0xff02;
        // Timer
        inline constexpr Address DIV = 0xff04;
        inline constexpr Address TIMA = 0xff05;
        inline constexpr Address TMA = 0xff06;
        inline constexpr Address TAC = 0xff07;
        inline constexpr Address IF = 0xff0f;
        // Sound
        inline constexpr Address NR10 = 0xff10;
        inline constexpr Address NR11 = 0xff11;
        inline constexpr Address NR12 = 0xff12;
        inline constexpr Address NR13 = 0xff13;
        inline constexpr Address NR14 = 0xff14;
        inline constexpr Address NR21 = 0xff16;
        inline constexpr Address NR22 = 0xff17;
        inline constexpr Address NR23 = 0xff18;
        inline constexpr Address NR24 = 0xff19;
        inline constexpr Address NR30 = 0xff1a;
        inline constexpr Address NR31 = 0xff1b;
        inline constexpr Address NR32 = 0xff1c;
        inline constexpr Address NR33 = 0xff1d;
        inline constexpr Address NR34 = 0xff1e;
        inline constexpr Address NR41 = 0xff20;
        inline constexpr Address NR42 = 0xff21;
        inline constexpr Address NR43 = 0xff22;
        inline constexpr Address NR44 = 0xff23;
        inline constexpr Address NR50 = 0xff24;
        inline constexpr Address NR51 = 0xff25;
        inline constexpr Address NR52 = 0xff26;
        inline constexpr Address AUD3WAVERAM = 0xff30;
        inline constexpr Address AUD3WAVERAMEnd = 0xff3f;
        // Video
        inline constexpr Address LCDC = 0xff40;
        inline constexpr Address STAT = 0xff41;
        inline constexpr Address SCY = 0xff42;
        inline constexpr Address SCX = 0xff43;
        inline constexpr Address LY = 0xff44;
        inline constexpr Address LYC = 0xff45;
        inline constexpr Address DMA = 0xff46;
        inline constexpr Address BGP = 0xff47;
        inline constexpr Address OBP0 = 0xff48;
        inline constexpr Address OBP1 = 0xff49;
        inline constexpr Address WY = 0xff4a;
        inline constexpr Address WX = 0xff4b;
        inline constexpr Address DMG = 0xff50;
        inline constexpr Address IE = 0xffff;
    }

    namespace resolution {
        inline constexpr int Width = 166;
        inline constexpr int Height = 144;
    }

#if 0
    template<typename T>
    concept bool MemoryInterface = requires(T a) {
        { a.Read_u8(Address{}) } -> uint8_t;
        { a.Read_u16(Address{}) } -> uint16_t;
        { a.Write_u8(Address{}, uint8_t{}) } -> void;
        { a.Write_u16(Address{}, uint16_t{}) } -> void;
    };
#endif
}
