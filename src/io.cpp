#include "io.h"
#include "memory.h"
#include "audio.h"
#include "video.h"

namespace gb {
    uint8_t& IO::Register(const Address address)
    {
        const auto index = address - memory_map::IOStart;
        return data[index];
    }

    uint8_t IO::Read(const Address address)
    {
        switch(address) {
            case io::P1: {
                const auto p1 = Register(io::P1);
                uint8_t v = 0xcf;
                if ((p1 & (1 << 5)) == 0) {
                    // Button keys
                    if (buttonPressed & button::Start) v &= ~(1 << 3);
                    if (buttonPressed & button::Select) v &= ~(1 << 2);
                    if (buttonPressed & button::B) v &= ~(1 << 1);
                    if (buttonPressed & button::A) v &= ~(1 << 0);
                }
                if ((p1 & (1 << 4)) == 0) {
                    // Direction keys
                    if (buttonPressed & button::Down) v &= ~(1 << 3);
                    if (buttonPressed & button::Up) v &= ~(1 << 2);
                    if (buttonPressed & button::Left) v &= ~(1 << 1);
                    if (buttonPressed & button::Right) v &= ~(1 << 0);
                }
                return v;
            }
            case io::IE:
                return ie;
        }
        if (address >= io::LCDC && address <= io::WX)
            return video.Read(address);
        if (address >= io::NR10 && address <= io::AUD3WAVERAMEnd)
            return audio.Read(address);
        return Register(address);
    }

    void IO::Write(const Address address, uint8_t value)
    {
        if (address >= io::LCDC && address <= io::WX) {
            video.Write(address, value);
            return;
        }
        if (address >= io::NR10 && address <= io::AUD3WAVERAMEnd) {
            audio.Write(address, value);
            return;
        }

        switch(address) {
            case io::LY:
                break;
            case io::IE:
                ie = value;
                break;
            case io::DIV:
                Register(address) = 0;
                break;
            default:
                Register(address) = value;
                break;
        }
    }

    std::optional<int> IO::GetPendingIRQ()
    {
        const auto interruptScheduled = Register(io::IF);
        const auto pendingInterrupts = interruptScheduled & ie;
        if (pendingInterrupts == 0) return {};

        for (int n = 0; n < 8; ++n) {
            if ((pendingInterrupts & (1 << n)) == 0) continue;
            return n;
        }

        return {};
    }

    void IO::ClearPendingIRQ(int n)
    {
        Register(io::IF) &= ~(1 << n);
    }

    bool IO::IsBootstrapROMEnabled()
    {
        return Register(io::DMG) == 0;
    }

    void IO::Tick(const int cycles)
    {
        divCount += cycles;
        if (divCount >= 256) {
            ++Register(io::DIV);
            divCount = 0;
        }

        lcdCount += cycles;
        if (lcdCount >= 10) {
            auto& ly = Register(io::LY);
            if (++ly == 154) {
                ly = 0;
            }
        }

        const auto tac = Register(io::TAC);
        if ((tac & 1) == 0) return;
        const auto timaInterval = [tac]() {
            switch(tac&3) {
                case 0: return 1024;
                case 1: return 16;
                case 2: return 64;
                case 3: return 256;
            }
            return 0;
        }();

        timaCount += cycles;
        if (timaCount >= timaInterval) {
            auto& tima = Register(io::TIMA);
            if (tima == 255) {
                tima = Register(io::TMA);
                Register(io::IF) |= interrupt::Timer;
            } else {
                ++tima;
            }
            timaCount = 0;
        }
    }
}
