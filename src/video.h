#pragma once

#include <memory>
#include "types.h"

namespace gb {

struct IO;
struct Memory;
    

class Video
{
public:
    Video();
    ~Video();

    void Tick(IO& io, Memory& memory, const int cycles);
    uint8_t Read(const Address address);
    void Write(const Address address, const uint8_t value);
    bool GetRenderFlagAndReset();

    const char* GetFrameBuffer() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}
