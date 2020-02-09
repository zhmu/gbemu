#pragma once

#include <memory>
#include "types.h"

namespace gb {

struct IO;
struct Memory;

class Audio
{
public:
    Audio();
    ~Audio();

    void Tick(IO& io, Memory& memory, const int cycles);
    uint8_t Read(const Address address);
    void Write(const Address address, const uint8_t value);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}
