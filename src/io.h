#pragma once

#include "types.h"
#include <array>
#include <optional>

namespace gb {
    namespace cpu { struct Registers; }
    struct Memory;
    class Video;
    class Audio;

    namespace button {
        inline constexpr uint8_t A = (1 << 0);
        inline constexpr uint8_t B = (1 << 1);
        inline constexpr uint8_t Left = (1 << 2);
        inline constexpr uint8_t Right = (1 << 3);
        inline constexpr uint8_t Up = (1 << 4);
        inline constexpr uint8_t Down = (1 << 5);
        inline constexpr uint8_t Start = (1 << 6);
        inline constexpr uint8_t Select = (1 << 7);
    }

    struct IO {
        IO(Video& video, Audio& audio) : video(video), audio(audio) { }

        uint8_t Read(Address address);
        void Write(Address address, uint8_t value);
        std::optional<int> GetPendingIRQ();
        void ClearPendingIRQ(int n);
        void Tick(const int cycles);
        bool IsBootstrapROMEnabled();

        uint8_t& Register(const Address address);

        Video& video;
        Audio& audio;
        std::array<uint8_t, 128> data{};
        int timaCount{}, divCount{}, lcdCount{};

        uint8_t buttonPressed{};
        uint8_t ie{};
    };
}
