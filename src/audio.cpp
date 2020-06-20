#include "audio.h"
#include <iostream>
#include "fmt/core.h"

#include <unistd.h>
#include "gui.h"

namespace gb {

namespace {
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

    constexpr int FrequenceToPeriod(const int freq)
    {
        return (2048 - freq) * 4;
    }

    struct Channel {
        // Internal state
        bool enabled{};
        int lengthCounter{};
        int currentVolume{};
        int signalValue{};
        int dutyCycleType{};
        int currentDutyCycle{};
        int frequency{};
        int periodCounter{};
    };

    constexpr std::array<std::array<int, 8>, 4> dutyCycles{{
        { -1, -1, -1, -1, -1, -1, -1, +1 }, // 12.5%
        { +1, -1, -1, -1, -1, -1, -1, +1 }, // 25%
        { +1, -1, -1, -1, -1, +1, +1, +1 }, // 50%
        { -1, +1, +1, +1, +1, +1, +1, -1 }, // 75%
    }};

    template<int Bit> constexpr inline bool IsBitSet(const uint8_t v)
    {
        static_assert(Bit >= 0 && Bit <= 7);
        return (v & (1 << Bit)) != 0;
    }
}

FILE* f = nullptr;

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

#if 0
    static void AudioCallback(void* userData, Uint8* stream, int len)
    {
        reinterpret_cast<Impl*>(userData)->Mix((len / sizeof(uint32_t)) / 2, reinterpret_cast<int32_t*>(stream));
    }
#endif

    Impl()
    {
        unlink("/tmp/out.raw");
        f = fopen("/tmp/out.raw", "wb");
#if 0
        SDL_AudioSpec want{};
        want.freq = 48000;
        want.format = AUDIO_S32;
        want.channels = 2;
        want.samples = 4096;
        want.userdata = this;
        want.callback = AudioCallback;

        SDL_AudioSpec have{};
        dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (dev == 0)
            throw std::runtime_error(fmt::format("cannot init audio {}", SDL_GetError()));
        SDL_PauseAudioDevice(dev, 0);
#endif
    }

    ~Impl()
    {
        fclose(f);
        //SDL_CloseAudioDevice(dev);
    }

    void TickLengthCounter(IO& io, Memory& memory)
    {
        auto& ch = channel[1];
        if (ch.enabled) {
            if (--ch.lengthCounter == 0) {
                ch.enabled = false;
            }
        }
    }

    void TickVolumeEnvelope(IO& io, Memory& memory)
    {
    }

    void TickSweep(IO& io, Memory& memory)
    {
    }

    void UpdateCurrentSignal(IO& io, Memory& memory, int cycleCounter)
    {
        auto& ch = channel[1];
        //ch.signalValue = dutyCycles[ch.dutyCycleType][ch.currentDutyCycle];
        


        ch.periodCounter += cycleCounter;
        // *8 because each waveform takes 8 frequency timer clocks to cycle through
        const auto period = FrequenceToPeriod(ch.frequency) * 8;
        if (ch.periodCounter >= period) {
            int n = ch.periodCounter % period;
            //printf("ch1: period counter %d > period length %d, next sample -> %d\n", ch.periodCounter, period, n);
            ch.currentDutyCycle = (ch.currentDutyCycle + 1) & 7;
            ch.periodCounter = ch.periodCounter % period;
        }

        gui::OnAudioSample(1, ch.signalValue * ch.currentVolume);
    }

    void Tick(IO& io, Memory& memory, const int cycles)
    {
        cycleCounter += cycles;
        if (cycleCounter >= TimerDivisor) {
            cycleCounter -= TimerDivisor;
            if (step == 0 || step == 2 || step == 4 || step == 6)
                TickLengthCounter(io, memory);
            if (step == 2 || step == 6)
                TickSweep(io, memory);
            if (step == 7)
                TickVolumeEnvelope(io, memory);
            if (++step == 8) step = 0;
        }

        UpdateCurrentSignal(io, memory, cycleCounter);

#if 0
        auto& ch = channel[1];
        const int16_t v = ch.signalValue * 16384;
        fwrite(&v, sizeof(v), 1, f);
#endif
    }

    void Mix(int samples, int32_t* buffer)
    {
        auto out = buffer;
        for(int n = 0; n < samples; ++n) {
            int left{}, right{};
            {
                int chNum = 1;
                auto& ch = channel[chNum];
                const int v = ch.signalValue * ch.currentVolume; // -15 .. 15
                if (outputLeft[chNum]) left += v;
                if (outputRight[chNum]) right += v;
            }
            left *= masterVolumeLeft;
            right *= masterVolumeRight;
            // Left/right are now always between -105 .. 105
            left *= 65536; right *= 65536;
            //printf("out %x / %x\n", left, right);
            *out++ = left;
            *out++ = right;
            fwrite(&left, sizeof(left), 1, f);
        }
    }

    uint8_t Read(const Address address)
    {
        uint8_t v = Register(address);
        return v;
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
            case io::NR21: {
                auto& ch = channel[1];
                ch.dutyCycleType = value >> 6;
                ch.lengthCounter = 64 - (value & 63);
                break;
            }
            case io::NR23: {
                auto& ch = channel[1];
                ch.frequency = (ch.frequency & 0x700) | value;
                break;
            }
            case io::NR24: {
                auto& ch = channel[1];
                ch.frequency = (ch.frequency & 0xff) | ((value & 7) << 8);
                // TODO Length enable?
                if (IsBitSet<7>(value)) {
                    ch.enabled = true;
                    if (ch.lengthCounter == 0) ch.lengthCounter = 64;
                    // TODO reload frequency timer with period
                    ch.periodCounter = 0;
                    // TODO reload volume envelope timer with period
                    // TODO reload channel volume from NRx2
                    //ch.currentVolume = Register(io::NR22) >> 4;
                    ch.currentVolume = 15; // XXX
                    ch.currentDutyCycle = 0;
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
