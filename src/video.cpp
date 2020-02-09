#include "video.h"
#include <SDL.h>
#include <array>
#include <chrono>
#include <thread>
#include <vector>
#include "memory.h"
#include "io.h"

inline constexpr int windowWidth = 166;
inline constexpr int windowHeight = 144;

namespace gb {
namespace {

constexpr std::chrono::duration<double> oneHSyncDuration{ (456 * (1.0 / (1 << 22))) };

struct RGB { uint8_t r{}, g{}, b{}; };

template<int Bit> constexpr inline bool IsBitSet(const uint8_t v)
{
    return (v & (1 << Bit)) != 0;
}

namespace lcd_mode {
    inline constexpr int hBlank = 0;
    inline constexpr int vBlank = 1;
    inline constexpr int scanOAM = 2;
    inline constexpr int readingOAMandVRAM = 3;
}

// From https://lospec.com/palette-list/nintendo-gameboy-bgb
constexpr std::array<RGB, 4> palette{ {
    { 0x08, 0x18, 0x20 },
    { 0x34, 0x68, 0x56 },
    { 0x88, 0xc0, 0x70 },
    { 0xe0, 0xf8, 0xd0 },
} };

int MapSDLKeyEventToButton(const SDL_Event& event)
{
    switch(event.key.keysym.sym) {
        case SDLK_LEFT: return button::Left;
        case SDLK_RIGHT: return button::Right;
        case SDLK_UP: return button::Up;
        case SDLK_DOWN: return button::Down;
        case SDLK_a: return button::A;
        case SDLK_z: return button::B;
        case SDLK_RETURN: return button::Start;
        case SDLK_TAB: return button::Select;
    }
    return 0;
}

void PutPixel(uint32_t* frameBuffer, const int x, const RGB& colour)
{
    if (x < 0 || x >= windowWidth) return;
    const uint32_t c = (0xff << 24) | (colour.r << 16) | (colour.g << 8) | colour.b;
    frameBuffer[x] = c;
}

}

struct Video::Impl
{
    uint8_t& Register(const Address address)
    {
        return data[address - io::LCDC];
    }

    Impl()
    {
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        window = SDL_CreateWindow("GBEMU", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, 0);
        renderer = SDL_CreateRenderer(window, -1, 0);

        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, windowWidth, windowHeight);

        lastHSyncTime = std::chrono::steady_clock::now();
    }

    void FillBG(Memory& memory, uint32_t* displayLine, const int scanLine)
    {
        const auto lcdc = Register(io::LCDC);
        const bool bgEnabled = IsBitSet<0>(lcdc);
        if (!bgEnabled) return;

        const Address bgTileMap = IsBitSet<3>(lcdc) ? 0x9c00 : 0x9800;
        const Address bgAndWindowTileData = IsBitSet<4>(lcdc) ? 0x8000 : 0x8800;

        const auto scx = Register(io::SCX);
        const int tileY = scanLine / 8;

        int pixelOffset = -(scx % 8);
        for(int tileX = 0; tileX < 32; ++tileX) {
            const auto tileIndex = memory.At_u8(bgTileMap + (32 * tileY) + tileX + scx / 8);

            const auto imageAddr = [&]() {
                const auto subOffset = (tileIndex & 127) * 16 + (scanLine & 7) * 2;
                if (tileIndex >= 128)
                        return 0x8800 + subOffset;
                return (bgAndWindowTileData == 0x8000 ? 0x8000 : 0x9000) + subOffset;
            }();
            const auto i1 = memory.At_u8(imageAddr);
            const auto i2 = memory.At_u8(imageAddr + 1);
            for (int offsetX = 0; offsetX < 8; ++offsetX) {
                int c = ((i1 & (1 << (7 - offsetX))) ? 1 : 0);
                c <<= 1;
                c |= ((i2 & (1 << (7 - offsetX))) ? 1 : 0);

                const auto color = palette[c];
                PutPixel(displayLine, pixelOffset, color);
                ++pixelOffset;
            }
        }
    }

    void FillObjects(Memory& memory, uint32_t* displayLine, const int scanLine, const int spriteIndex)
    {
        if (spriteIndex >= activeSprites) return;

        const auto lcdc = Register(io::LCDC);
        const bool objDisplayEnable = IsBitSet<1>(lcdc);
        if (!objDisplayEnable) return;

        const auto& sprite = sprites[spriteIndex];
        const int spriteY = scanLine - sprite.y;
        auto imageAddr = 0x8000 + sprite.tileNumber * 16;
        if ((sprite.flags & (1 << 6)) == 0)
            imageAddr += spriteY * 2;
        else
            imageAddr += (8 - spriteY) * 2;

        const auto i1 = memory.At_u8(imageAddr);
        const auto i2 = memory.At_u8(imageAddr + 1);

        auto getSpriteBit = [&](int n) {
            if ((sprite.flags & (1 << 5)) != 0)
                return 1 << n;
            return 1 << (7 - n);
        };

        int pixelOffset = sprite.x;
        for (int offsetX = 0; offsetX < 8; ++offsetX) {
            int c = (i1 & getSpriteBit(offsetX)) ? 1 : 0;
            c <<= 1;
            c |= ((i2 & getSpriteBit(offsetX)) ? 1 : 0);
            if (c != 0) {
                const auto color = palette[c];
                PutPixel(displayLine, pixelOffset, color);
            }
            ++pixelOffset;
        }
    }

    void Tick(IO& io, Memory& memory, const int cycles)
    {
        const uint8_t scanLine = Register(io::LY);
        const auto stat = Register(io::STAT);

        auto triggerLYCInterrupt = [&]() {
            if (scanLine == Register(io::LYC) && ((stat & (1 << 6)) != 0)) {
                io.Register(io::IF) |= interrupt::LCDStat;
            }
        };

        auto triggerOAMInterrupt = [&]() {
            if ((stat & (1 << 5)) != 0) {
                io.Register(io::IF) |= interrupt::LCDStat;
            }
        };

        auto setMode = [&](const int newMode, const int cyclesToSubtract) {
            mode = newMode;
            stateCounter = 0;
            stateCycles -= cyclesToSubtract;
        };

        stateCycles += cycles;
        if (mode == lcd_mode::scanOAM) { // 2
            // XXX prepare
            if (stateCycles >= 80) {
                activeSprites = 0;
                for(int spriteReg = 0xfe00; activeSprites < sprites.size() && spriteReg < 0xfea0; spriteReg += 4) {
                    const int spriteY = memory.Read_u8(spriteReg + 0) - 16;
                    if (spriteY < 0 || spriteY >= 160) continue; // XXX shouldn't be necessary
                    if (scanLine < spriteY || scanLine >= spriteY+8) {
                        continue;
                    }

                    const int spriteX = memory.Read_u8(spriteReg + 1) - 8;
                    if (spriteX <= -8 || spriteX >= 165) {
                        continue; // XXX correct?
                    }

                    const int tileNumber = memory.Read_u8(spriteReg + 2);
                    const int flags = memory.Read_u8(spriteReg + 3);
                    sprites[activeSprites] = Sprite{ spriteX, spriteY, tileNumber, flags };
                    ++activeSprites;
                }
                setMode(lcd_mode::readingOAMandVRAM, 80);
            }
        }

        if (mode == lcd_mode::readingOAMandVRAM) { // 3
            if (stateCounter == 1) {
                // Fill current display line
                FillBG(memory, displayLine.data(), scanLine);
            }
            if (stateCounter >= 2 && stateCounter < static_cast<int>(2 + activeSprites)) {
                FillObjects(memory, displayLine.data(), scanLine, stateCounter - 2);
            }

            // XXX 200 is somewhat in between 168..291 dots
            if (stateCycles >= 200) {
                // Update texture with new scanline content
                {
                    uint32_t* framebuffer;
                    int pitch;
                    SDL_LockTexture(texture, NULL, reinterpret_cast<void**>(&framebuffer), &pitch);
                    memcpy(&framebuffer[scanLine * windowWidth], &displayLine[0], sizeof(uint32_t) * windowWidth);
                    SDL_UnlockTexture(texture);
                }

                setMode(lcd_mode::hBlank, 200);
                if ((stat & (1 << 3)) != 0) {
                    printf("video::Tick(): H-BLANK\n");
                    io.Register(io::IF) |= interrupt::LCDStat;
                }
            }
        }
        if (mode == lcd_mode::hBlank) { // 0
            // need to delay one line - 80 - 200 = 456 - 80 - 200 = 176 dots
            if (stateCycles >= 176) {
                auto now = std::chrono::steady_clock::now();
                auto diff = std::chrono::duration<double>(now - lastHSyncTime);

                auto toDelay = oneHSyncDuration - (now - lastHSyncTime);

                lastHSyncTime = now;
#if 0
                //printf("hsync did    take %f s\n", diff.count());
                //printf("hsync should take %f s\n", oneHSyncDuration.count());
                printf("need to delay %.2f us\n", std::chrono::duration<double, std::micro>(toDelay).count());
#endif

                std::this_thread::sleep_for(toDelay);

                uint8_t& scanLine = Register(io::LY);
                ++scanLine;
                triggerLYCInterrupt();
                if (scanLine == 144) {
                    setMode(lcd_mode::vBlank, 176);
                    io.Register(io::IF) |= interrupt::VBlank;
                    if ((stat & (1 << 4)) != 0) {
                        io.Register(io::IF) |= interrupt::LCDStat;
                    }
                } else {
                    setMode(lcd_mode::scanOAM, 176);
                    triggerOAMInterrupt();
                }
            }
        }
        if (mode == lcd_mode::vBlank) { // 1
            if (stateCycles >= 456) {
                stateCycles -= 456;
                uint8_t& scanLine = Register(io::LY);
                ++scanLine;
                triggerLYCInterrupt();
            }

            if (scanLine == 154) {
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);

                Register(io::LY) = 0;
                triggerLYCInterrupt();
                setMode(lcd_mode::scanOAM, 0);
                triggerOAMInterrupt();
            }
        }

        ++stateCounter;
    }

    uint8_t Read(const Address address)
    {
        uint8_t v = Register(address);
        switch(address) {
            case io::STAT:
                v = (v | 0x80) | mode;
                break;
        }
        return v;
    }

    void Write(const Address address, uint8_t value)
    {
        switch(address) {
            case io::STAT:
                value &= 0x78; // filter r/o bits
                break;
        }
        Register(address) = value;
    }

    bool HandleEvents(IO& io)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_QUIT: return false;
                case SDL_KEYDOWN: {
                    const auto button = MapSDLKeyEventToButton(event);
                    io.buttonPressed |= button;
                    break;
                }
                case SDL_KEYUP: {
                    const auto button = MapSDLKeyEventToButton(event);
                    io.buttonPressed &= ~button;
                    break;
                }
            }
        }
        return true;
    }

    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    int mode{lcd_mode::scanOAM};
    int stateCycles{};
    int stateCounter{};
    std::array<uint32_t, windowWidth> displayLine{};
    std::array<uint8_t, 12> data{};
    std::chrono::time_point<std::chrono::steady_clock> lastHSyncTime;

    struct Sprite {
        int x{}, y{};
        int tileNumber{};
        int flags{};
    };
    std::array<Sprite, 10> sprites{};
    size_t activeSprites{};
};

Video::Video()
    : impl(std::make_unique<Video::Impl>())
{
}

Video::~Video() = default;

void Video::Tick(IO& io, Memory& memory, const int cycles)
{
    impl->Tick(io, memory, cycles);
}

uint8_t Video::Read(const Address address)
{
    return impl->Read(address);
}

void Video::Write(const Address address, const uint8_t value)
{
    impl->Write(address, value);
}

bool Video::HandleEvents(IO& io)
{
    return impl->HandleEvents(io);
}

}
