#pragma once

#include <array>
#include <cstdint>
#include "types.h"

namespace gb {
    struct IO;

    struct Memory {
        Memory(IO& io) : io(io) { }

        uint8_t Read_u8(Address address);
        uint8_t At_u8(Address address) const;

        template<typename Iterator>
        void Fill(Address base, Iterator start, Iterator end)
        {
            while(start != end) {
                data[base] = *start;
                ++base, ++start;
            }
        }

        uint16_t Read_u16(const Address address);

        void Write_u8(Address address, const uint8_t value);
        void Write_u16(const Address address, const uint16_t value);

        IO& io;
        bool enableTracing;
        std::array<uint8_t, 65536> data;
    };
}
