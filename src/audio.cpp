#include "audio.h"
#include <iostream>
#include "fmt/core.h"

#include <unistd.h>
#include <fcntl.h>
#include "gui.h"

namespace gb {

namespace {
    const uint32_t sampleRate = 48000;

    constexpr std::array<std::array<int, 8>, 4> dutyCycles{{
        { 0, 0, 0, 0, 0, 0, 0, 1 }, // 12.5%
        { 1, 0, 0, 0, 0, 0, 0, 1 }, // 25%
        { 1, 0, 0, 0, 0, 1, 1, 1 }, // 50%
        { 0, 1, 1, 1, 1, 1, 1, 0 }, // 75%
    }};

    template<typename T> void Store(int fd, T value)
    {
        write(fd, &value, sizeof(value));
    }

    int MakeWav()
    {
        int fd = open("/tmp/out.wav", O_CREAT | O_WRONLY | O_TRUNC, 0644);
#if 1
        const uint16_t bitsPerSample = 16;
        const uint16_t numChannels = 2;
        const uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 2);
        const uint16_t blockAlign = numChannels * (bitsPerSample / 8);

        write(fd, "RIFF", 4);
        if (uint32_t fileSize = 0xffffffff; true) Store(fd, fileSize);
        write(fd, "WAVE", 4);
        write(fd, "fmt ", 4);
        if (uint32_t len = 16; true) Store(fd, len);
        if (uint16_t type = 1; true) Store(fd, type);
        Store(fd, numChannels);
        Store(fd, sampleRate);
        Store(fd, byteRate);
        Store(fd, blockAlign);
        Store(fd, bitsPerSample);
        write(fd, "data", 4);
        if (uint32_t dataSize = 0xffffffff; true) Store(fd, dataSize);
#endif
        return fd;
    }

    std::string IORegisterToString(const Address address) {
        switch(address) {
            case io::NR10: return "NR10 [square1: sweep period, negate, shift]";
            case io::NR11: return "NR11 [square1: duty, length load]";
            case io::NR12: return "NR12 [square1: starting vol, envelope add mode, period]";
            case io::NR13: return "NR13 [square1: freq lsb]";
            case io::NR14: return "NR14 [square1: trigger, length enable, freq msb]";
            case io::NR21: return "NR21 [square2: duty, length load]";
            case io::NR22: return "NR22 [square2: starting vol, envelope add mode, period]";
            case io::NR23: return "NR23 [square2: freq lsb]";
            case io::NR24: return "NR24 [square2: trigger, length enable, freq msb]";
            case io::NR30: return "NR30 [wave: dac power]";
            case io::NR31: return "NR31 [wave: length load]";
            case io::NR32: return "NR32 [wave: volume]";
            case io::NR33: return "NR33 [wave: freq lsb]";
            case io::NR34: return "NR34 [wave: trigger, length enable, freq msb]";
            case io::NR41: return "NR41 [noise: length]";
            case io::NR42: return "NR42 [noise: starting vol, envelope add mode, period]";
            case io::NR43: return "NR43 [noise: clock shift, width mode, divisor code]";
            case io::NR44: return "NR44 [noise: trigger, length enable]";
            case io::NR50: return "NR50 [ctrl/stat: vin l enable, l vol, vin r enable, r vol]";
            case io::NR51: return "NR51 [ctrl/stat: left enable, right enable]";
            case io::NR52: return "NR52 [ctr/stat: power control/status, channel stat]";
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
        }
        return fmt::format("{:x}", address);
    }

    constexpr int TimerDivisor = 8192; // 4MHz / 8192 = 512Hz timer
    constexpr int SampleTimerReload = 4'194'304 / sampleRate;

    constexpr int FrequenceToPeriod(const int freq)
    {
        return (2048 - freq) * 4;
    }

    struct Channel {
        // Internal state
        bool enabled{};
        bool lengthEnabled{};
        int lengthCounter{};
        int initialVolume{};
        int currentVolume{};
        int dutyCycleType{};
        int currentDutyCycle{};
        int frequency{};
        int periodTimer{};
        int volumeEnvelopeAdd{};
        int volumeEnvelopePeriod{};
        int volumeEnvelopeTimer{};
        bool sweepEnabled{};
        int sweepTimer{};
        int sweepFrequency{};
        int sweepPeriod{};
        bool sweepAdd{};
        int sweepShift{};

        int GetSample() const {
            if (!enabled) return 0;
            return dutyCycles[dutyCycleType][currentDutyCycle] * currentVolume;
        }

    };

    template<int Bit> constexpr inline bool IsBitSet(const uint8_t v)
    {
        static_assert(Bit >= 0 && Bit <= 7);
        return (v & (1 << Bit)) != 0;
    }
}

int fd = -1;

struct Audio::Impl
{
    uint8_t& Register(const Address address)
    {
        return data[address - io::NR10];
    }

    bool IsEnabled()
    {
        return IsBitSet<7>(Register(io::NR52));
    }

    Impl()
    {
        sampleTimer = SampleTimerReload;
    }

    ~Impl()
    {
        if (fd >= 0) close(fd);
    }

    void TickLengthCounter()
    {
        for(auto& ch: channel) {
            if (ch.lengthCounter > 0)
                --ch.lengthCounter;
            if (ch.lengthCounter == 0 && ch.lengthEnabled)
                ch.enabled = false;
        }
    }

    void TickVolumeEnvelope()
    {
        for(auto& ch: channel) {
            if (ch.volumeEnvelopePeriod == 0) continue;
            if (--ch.volumeEnvelopeTimer == 0) {
                ch.volumeEnvelopeTimer = ch.volumeEnvelopePeriod;
                int newVolume = ch.currentVolume + ch.volumeEnvelopeAdd;
                if (newVolume >= 0 && newVolume <= 15)
                    ch.currentVolume = newVolume;
            }
        }
    }

    int CalculateSweep(const Channel& ch)
    {
        int v = ch.sweepFrequency;
        const int sweepPortion = ch.sweepFrequency >> ch.sweepShift;
        if (ch.sweepAdd)
            v += sweepPortion;
        else
            v -= sweepPortion;
        return v;
    }

    void TickSweep()
    {
        auto& ch = channel[0];
        if (ch.sweepTimer > 0) --ch.sweepTimer;
        if (ch.sweepEnabled && ch.sweepPeriod != 0 && ch.sweepTimer == 0) {
            ch.sweepTimer = ch.sweepPeriod;
            const auto newFreq = CalculateSweep(ch);
            if (newFreq <= 2047 && ch.sweepShift != 0) {
                ch.sweepFrequency = newFreq;
                ch.frequency = newFreq;
                ch.periodTimer = FrequenceToPeriod(ch.frequency);
                if (CalculateSweep(2047) > 2047)
                    ch.enabled = false;
            }
        }
    }

    void TickChannels()
    {
        for(auto& ch: channel) {
            --ch.periodTimer;
            if (ch.periodTimer == 0) {
                ch.currentDutyCycle = (ch.currentDutyCycle + 1) % 8;
                ch.periodTimer = FrequenceToPeriod(ch.frequency);
            }
        }
    }

    void Tick(IO& io, Memory& memory, const int cycles)
    {
        for (int n = 0; n < cycles; ++n)
            TickChannels();

        cycleCounter += cycles;
        if (cycleCounter >= TimerDivisor) {
            cycleCounter -= TimerDivisor;
            if (step == 0 || step == 2 || step == 4 || step == 6)
                TickLengthCounter();
            if (step == 2 || step == 6)
                TickSweep();
            if (step == 7)
                TickVolumeEnvelope();
            step = (step + 1) % 8;
        }

        sampleTimer -= cycles;
        if (sampleTimer <= 0) {
            sampleTimer = SampleTimerReload;
            int16_t left{}, right{};
            for (size_t chNum = 0; chNum < channel.size(); ++chNum) {
                const int v = channel[chNum].GetSample();
                if (outputLeft[chNum]) left += v;
                if (outputRight[chNum]) right += v;
            }
            left *= masterVolumeLeft * 8;
            right *= masterVolumeRight * 8;

            static bool first = true;
            if (first) {
                first = false;
                fd = MakeWav();
            }

            if (fd >= 0) {
                write(fd, &left, sizeof(left)); 
                write(fd, &right, sizeof(right)); 
            }
        }
    }

    uint8_t Read(const Address address)
    {
        constexpr std::array<uint8_t, io::NR52 - io::NR10 + 1> registerOrMask{
            0x80, 0x3f, 0x00, 0xff, 0xbf, // NR10..NR14
            0xff, 0x3f, 0x00, 0xff, 0xbf, // NR20..NR24
            0x7f, 0xff, 0x9f, 0xff, 0xbf, // NR30..NR34
            0xff, 0xff, 0x00, 0x00, 0xbf, // NR40..NR44
            0x00, 0x00, 0x70              // NR50..NR52
        };
        uint8_t value = Register(address);
        if (address >= 0xff27 && address <= 0xff2f)
            value = 0xff;
        else {
            value = value | registerOrMask[address - io::NR10];
        }
        std::cout << fmt::format("audio: read {} ({:x}): {:x}\n", IORegisterToString(address), address, value);
        return value;
    }

    void Write(const Address address, uint8_t value)
    {
        if (address == io::NR52) {
            const bool nextEnabled = IsBitSet<7>(value);
            if (!audioEnabled && nextEnabled) {
                // Ensure the next cycle triggers the sound statemachine
                cycleCounter = TimerDivisor;
                step = 0;
            }
            audioEnabled = nextEnabled;
            Register(address) = value & 0x80;
            return;
        }

        if (!IsEnabled()) {
            std::cout << fmt::format("Audio: ignoring write of address{:4} value {:2x}, sound disabled", address, value);
            return;
        }

        switch(address) {
            case io::NR10: {
                auto& ch = channel[0];
                ch.sweepPeriod = (value >> 4) & 7;
                ch.sweepAdd = IsBitSet<3>(value);
                ch.sweepShift = value & 7;
                break;
            }
            case io::NR11:
            case io::NR21: {
                auto& ch = channel[address == io::NR11 ? 0 : 1];
                ch.dutyCycleType = value >> 6;
                ch.lengthCounter = 64 - (value & 63);
                break;
            }
            case io::NR12:
            case io::NR22: {
                auto& ch = channel[address == io::NR12 ? 0 : 1];
                ch.initialVolume = value >> 4;
                ch.currentVolume = ch.initialVolume;
                ch.volumeEnvelopeAdd = IsBitSet<3>(value) ? 1 : -1;
                ch.volumeEnvelopePeriod = value & 7;
                ch.volumeEnvelopeTimer = ch.volumeEnvelopePeriod;
                break;
            }
            case io::NR13:
            case io::NR23: {
                auto& ch = channel[address == io::NR13 ? 0 : 1];
                ch.frequency = (ch.frequency & 0x700) | value;
                break;
            }
            case io::NR14:
            case io::NR24: {
                auto& ch = channel[address == io::NR14 ? 0 : 1];
                ch.frequency = (ch.frequency & 0xff) | ((value & 7) << 8);
                ch.lengthEnabled = IsBitSet<6>(value);
                if (IsBitSet<7>(value)) {
                    ch.enabled = true;
                    if (ch.lengthCounter == 0) ch.lengthCounter = 64;
                    ch.periodTimer = FrequenceToPeriod(ch.frequency);
                    ch.volumeEnvelopeTimer = ch.volumeEnvelopePeriod;
                    ch.currentVolume = ch.initialVolume;
                    ch.currentDutyCycle = 0;

                    if (address == io::NR14) {
                        ch.sweepEnabled = ch.sweepPeriod > 0 || ch.sweepShift > 0;
                        ch.sweepFrequency = ch.frequency;
                        ch.sweepTimer = ch.sweepPeriod;
                    }
                }
                break;
            }
            case io::NR50:
                masterVolumeLeft = (value >> 4) & 7;
                masterVolumeRight = value & 7;
                break;
            case io::NR51:
                outputLeft[3] = IsBitSet<7>(value);
                outputLeft[2] = IsBitSet<6>(value);
                outputLeft[1] = IsBitSet<5>(value);
                outputLeft[0] = IsBitSet<4>(value);
                outputRight[3] = IsBitSet<3>(value);
                outputRight[2] = IsBitSet<2>(value);
                outputRight[1] = IsBitSet<1>(value);
                outputRight[0] = IsBitSet<0>(value);
                break;
        }
        std::cout << fmt::format("audio: write {} ({:x}): {:x}\n", IORegisterToString(address), address, value);
        Register(address) = value;
    }

    std::array<Channel, 3> channel{};

    std::array<bool, 4> outputLeft{};
    std::array<bool, 4> outputRight{};
    int masterVolumeLeft{};
    int masterVolumeRight{};

    std::array<uint8_t, 48> data{};

    //SDL_AudioDeviceID dev{};
    bool audioEnabled{};
    int cycleCounter{};
    int step{};
    int sampleTimer{};
};

Audio::Audio()
    : impl(std::make_unique<Audio::Impl>())
{
}

Audio::~Audio() = default;

void Audio::Tick(IO& io, Memory& memory, const int cycles)
{
    impl->Tick(io, memory, cycles);
}

uint8_t Audio::Read(const Address address)
{
    return impl->Read(address);
}

void Audio::Write(const Address address, const uint8_t value)
{
    impl->Write(address, value);
}

}
