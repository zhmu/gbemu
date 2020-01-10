#include "cpu.h"
#include "memory.h"
#include "io.h"
#include "video.h"
#include "cartridge.h"

#include <fstream>
#include <unistd.h>
#include <chrono>

#include "fmt/core.h"

using IO = gb::IO;
using Memory = gb::Memory;
using Registers = gb::cpu::Registers;
using Video = gb::Video;

namespace {

bool optionTraceCPU = false;
bool optionTraceMemory = false;
bool optionTraceCartridge = false;

std::string RegistersToString(const Registers& regs)
{
    return fmt::format("{:04x} [a {:02x} b/c {:02x}{:02x} d/e {:02x}{:02x} h/l {:02x}{:02x} flags {}{}{}{}{}{} sp {:04x}]",
        regs.pc,
        regs.a, regs.b, regs.c, regs.d, regs.e, regs.h, regs.l,
        gb::cpu::flag::IsSet(regs, gb::cpu::Flag::z) ? 'Z' : '-',
        gb::cpu::flag::IsSet(regs, gb::cpu::Flag::n) ? 'N' : '-',
        gb::cpu::flag::IsSet(regs, gb::cpu::Flag::h) ? 'H' : '-',
        gb::cpu::flag::IsSet(regs, gb::cpu::Flag::c) ? 'C' : '-',
        regs.ime ? 'I' : '-',
        regs.halt ? 'h' : '-',
        regs.sp);
}

std::string Disassemble(const Registers& regs, const Memory& memory, const gb::cpu::Instruction& instruction, const bool has_prefix)
{
    using Argument = gb::cpu::Argument;

    std::string arg{"???"};
    int num_bytes = has_prefix ? 2 : 1;
    switch(instruction.arg) {
        case Argument::None:
            arg = "";
            break;
        case Argument::Imm8:
            arg = fmt::format("{:02x}", memory.At_u8(regs.pc));
            num_bytes += 1;
            break;
        case Argument::Imm16:
            arg = fmt::format("{:02x}{:02x}", memory.At_u8(regs.pc + 1), memory.At_u8(regs.pc));
            num_bytes += 2;
            break;
        case Argument::Rel8: {
            const uint16_t new_pc = regs.pc + 1 + static_cast<int8_t>(memory.At_u8(regs.pc));
            arg = fmt::format("{:x}", new_pc);
            num_bytes += 1;
            break;
        }
    }

    std::string bytes;
    auto pc_start = regs.pc - (has_prefix ? 2 : 1);
    for(int n = 0; n < num_bytes; ++n) {
        bytes += fmt::format("{:02x}", memory.At_u8(pc_start + n));
    }

    return fmt::format("{:8s} {}", bytes, fmt::format(instruction.name, arg));
}

bool ProcessOptions(int argc, char* argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "h?tmc")) != -1) {
        switch(opt) {
            case 'h':
            case '?':
                std::cout << fmt::format("usage: {} [-h?tmc] cartridge.gb\n\n", argv[0]);
                std::cout << fmt::format("  -h, -?     this help\n");
                std::cout << fmt::format("  -t         trace CPU instructions\n");
                std::cout << fmt::format("  -m         trace memory access\n");
                std::cout << fmt::format("  -c         trace cartridge access\n");
                return false;
            case 't':
                optionTraceCPU = true;
            case 'm':
                optionTraceMemory = true;
                break;
            case 'c':
                optionTraceCartridge = true;
                break;
        }
    }

    if (optind >= argc) {
        std::cout << fmt::format("expected cartridge.gb file after options\n");
        return false;
    }

    try {
        gb::cartridge::Load(argv[optind]);
        gb::cartridge::SetTracing(optionTraceCartridge);
    } catch (std::exception& e) {
        std::cout << fmt::format("cannot load '{}': {}\n", argv[optind], e.what());
        return false;
    }
    return true;
}

}

int main(int argc, char* argv[])
{
    if (!ProcessOptions(argc, argv)) return 1;

    Video video;
    IO io(video);
    Memory memory(io);
    memory.enableTracing = optionTraceMemory;
    Registers regs;

    {
        // From https://gbdev.gg8.se/wiki/articles/Power_Up_Sequence
        regs.a = 0x01; regs.fl = 0xb0;
        regs.b = 0x00; regs.c = 0x13;
        regs.d = 0x00; regs.e = 0xd8;
        regs.h = 0x01; regs.l = 0x4d;
        regs.sp = 0xfffe;
        regs.pc = 0x100;
    }

    auto current = std::chrono::steady_clock::now();
    while(video.HandleEvents(io)) {
        auto tick = [&](const int numClocks) {
            io.Tick(numClocks);
            video.Tick(io, memory, numClocks);
        };

        int numClocks = 4; // simulate NOP if halted
        if (!regs.halt) {
            const auto orig_regs = regs;
            const auto opcode = gb::cpu::detail::ReadAndAdvancePC_u8(regs, memory);

            const auto& instruction = [&]() {
                if(opcode != 0xcb)
                    return gb::cpu::opcode[opcode];

                const auto opcode2 = gb::cpu::detail::ReadAndAdvancePC_u8(regs, memory);
                return gb::cpu::opcode_cb[opcode2];
            }();

            if (optionTraceCPU) {
                const auto disasm = Disassemble(regs, memory, instruction, opcode == 0xcb);
                std::cout << fmt::format("{} {}", RegistersToString(orig_regs), disasm) << "\n";
            }
            numClocks = instruction.func(regs, memory);
        }
        tick(numClocks);

        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration<double, std::micro>(now - current);
        //printf("%d clocks took %.2f us\n", numClocks, diff.count());
        current = now;

        if (auto pendingIrq = io.GetPendingIRQ(); pendingIrq) {
            regs.halt = false;
            if (regs.ime) {
                io.ClearPendingIRQ(*pendingIrq);
                gb::cpu::InvokeIRQ(regs, memory, *pendingIrq);
            }
        }
    }

    return 0;
}
