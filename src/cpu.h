/*
 * The Gameboy contains a Sharp LR35902 CPU, which is halfway between an
 * Intel 8080 and a Z80.
 *
 * Opcodes from http://www.pastraiser.com/cpu/gameboy/gameboy_opcodes.html
 */
#pragma once

#include <array>
#include <cstdint>
#include "memory.h"
#include "types.h"

#include <iostream>

namespace gb::cpu {
    using Memory = gb::Memory;
    using Cycles = int;

    enum class Flag : uint8_t {
        z = (1 << 7), // Zero
        n = (1 << 6), // Add/Sub (BCD)
        h = (1 << 5), // Half carry(BCD)
        c = (1 << 4) // Carry
    };

    struct Registers {
        uint8_t a{}, b{}, c{}, d{}, e{}, h{}, l{};
        uint8_t fl{};
        bool ime{};
        bool halt{};
        Address pc{}, sp{};
    };

    namespace flag {
        inline constexpr uint8_t mask = 0xf0;

        constexpr void Set(Registers& regs, const Flag flag)
        {
            regs.fl |= static_cast<uint8_t>(flag);
        }

        constexpr void Clear(Registers& regs, const Flag flag)
        {
            regs.fl &= ~static_cast<uint8_t>(flag);
        }

        constexpr void Assign(Registers& regs, const Flag flag, const bool set)
        {
            if (set)
                Set(regs, flag);
            else
                Clear(regs, flag);
        }

        constexpr bool IsSet(const Registers& regs, const Flag flag)
        {
            return (regs.fl & static_cast<uint8_t>(flag)) != 0;
        }

        constexpr bool IsClear(const Registers& regs, const Flag flag)
        {
            return !IsSet(regs, flag);
        }
    }

    namespace detail {
        constexpr uint16_t FuseRegisters(const uint8_t a, const uint8_t b)
        {
            return static_cast<uint16_t>(a) << 8 | b;
        }

        constexpr void DivideRegisters(const uint16_t v, uint8_t& a, uint8_t& b)
        {
            a = v >> 8;
            b = v & 0xff;
        }

        uint8_t ReadAndAdvancePC_u8(Registers& regs, Memory& mem)
        {
            const auto result = mem.Read_u8(regs.pc);
            ++regs.pc;
            return result;
        }

        uint16_t ReadAndAdvancePC_u16(Registers& regs, Memory& mem)
        {
            const auto lo = ReadAndAdvancePC_u8(regs, mem);
            const auto hi = ReadAndAdvancePC_u8(regs, mem);
            return static_cast<uint16_t>(hi) << 8 | lo;
        }

        template<typename Operation>
        constexpr bool CalculateHalfCarry_u8(const uint8_t a, const uint8_t b, const uint8_t c, Operation op)
        {
            // From https://robdor.com/2016/08/10/gameboy-emulator-half-carry-flag/
            return (op(op((a & 0xf), (b & 0xf)), (c & 0xf)) & 0x10) == 0x10;
        }

        template<typename Operation>
        constexpr bool CalculateHalfCarry_u16(const uint16_t a, const uint16_t b, Operation op)
        {
            // From https://www.reddit.com/r/EmuDev/comments/4ycoix/a_guide_to_the_gameboys_halfcarry_flag/d6ohhp6
            return op((a & 0xfff), (b & 0xfff)) > 0xfff;
        }

        constexpr void Add_u8(Registers& regs, uint8_t& r, const uint16_t imm, const int carry = 0)
        {
            const auto halfCarry = CalculateHalfCarry_u8(r, imm, carry, std::plus<>());
            const uint16_t v = static_cast<uint16_t>(r) + imm + carry;
            r = v;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Assign(regs, Flag::h, halfCarry);
        }

        constexpr void Add_r8(Registers& regs, uint8_t& r, const uint8_t imm8, const bool carry = false)
        {
            const uint16_t c = carry ? 1 : 0;
            flag::Assign(regs, Flag::c, (static_cast<uint16_t>(r) + imm8 + c) > 0xff);
            Add_u8(regs, r, imm8, c);
        }

        constexpr void Adc_r8(Registers& regs, uint8_t& r, const uint8_t imm8)
        {
            Add_r8(regs, r, imm8, flag::IsSet(regs, Flag::c));
        }

        constexpr void Sub_u8(Registers& regs, uint8_t& r, const uint8_t imm8, const int carry = 0)
        {
            const auto halfCarry = CalculateHalfCarry_u8(r, imm8, carry, std::minus<>());
            const uint16_t v = static_cast<uint16_t>(r) - imm8 - carry;
            r = v;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Set(regs, Flag::n);
            flag::Assign(regs, Flag::h, halfCarry);
        }

        constexpr void Sub_r8(Registers& regs, uint8_t& r, const uint8_t imm8, const bool carry = false)
        {
            const uint16_t c = carry ? 1 : 0;
            flag::Assign(regs, Flag::c, (static_cast<int16_t>(r) - imm8 - c) < 0);
            Sub_u8(regs, r, imm8, c);
        }

        constexpr void Sbc_r8(Registers& regs, uint8_t& r, const uint8_t imm8)
        {
            Sub_r8(regs, r, imm8, flag::IsSet(regs, Flag::c));
        }

        constexpr void Add_u16(Registers& regs, uint16_t& r, const uint16_t imm16)
        {
            flag::Assign(regs, Flag::c, (r + imm16) > 0xffff);
            const auto halfCarry = CalculateHalfCarry_u16(r, imm16, std::plus<>());
            r += imm16;
            // Oddl enough, the 16-bit adds do not seem to update the zero flag
            flag::Clear(regs, Flag::n);
            flag::Assign(regs, Flag::h, halfCarry);
        }

        constexpr void Sub_u16(Registers& regs, uint16_t& r, const uint16_t imm16)
        {
            const auto halfCarry = CalculateHalfCarry_u16(r, imm16, std::minus<>());
            r -= imm16;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Set(regs, Flag::n);
            flag::Assign(regs, Flag::h, halfCarry);
        }

        void Inc_r8(Registers& regs, uint8_t& r)
        {
            Add_u8(regs, r, 1);
        }

        void Dec_r8(Registers& regs, uint8_t& r)
        {
            Sub_u8(regs, r, 1);
        }

        void Inc_r16(uint8_t& x, uint8_t& y) {
            auto xy = detail::FuseRegisters(x, y);
            ++xy;
            detail::DivideRegisters(xy, x, y);
        }

        void Dec_r16(uint8_t& x, uint8_t& y) {
            auto xy = detail::FuseRegisters(x, y);
            --xy;
            detail::DivideRegisters(xy, x, y);
        }

        void Add_HL_r16(Registers& regs, const uint8_t x, const uint8_t y)
        {
            auto hl = detail::FuseRegisters(regs.h, regs.l);
            const auto xy = detail::FuseRegisters(x, y);
            detail::Add_u16(regs, hl, xy);
            detail::DivideRegisters(hl, regs.h, regs.l);
        }

        void Push_u8(Registers& regs, Memory& mem, const uint8_t v)
        {
            --regs.sp;
            mem.Write_u8(regs.sp, v);
        }

        void Push_u16(Registers& regs, Memory& mem, const uint16_t v)
        {
            Push_u8(regs, mem, v >> 8);
            Push_u8(regs, mem, v & 0xff);
        }

        uint8_t Pop_u8(Registers& regs, Memory& mem)
        {
            const uint16_t v = mem.Read_u8(regs.sp);
            ++regs.sp;
            return v;
        }

        uint16_t Pop_u16(Registers& regs, Memory& mem)
        {
            const uint16_t lo = Pop_u8(regs, mem);
            const uint16_t hi = Pop_u8(regs, mem);
            return (hi << 8) | lo;
        }

        Cycles HandleRelativeJump(Registers& regs, Memory& mem, const bool take)
        {
            const int8_t v = detail::ReadAndAdvancePC_u8(regs, mem);
            if (take) {
                regs.pc += v;
                return 12;
            }
            return 8;
        }

        Cycles HandleRelativeReturn(Registers& regs, Memory& mem, const bool take)
        {
            if (take) {
                regs.pc = Pop_u16(regs, mem);
                return 20;
            }
            return 8;
        }

        Cycles HandleAbsoluteJump(Registers& regs, Memory& mem, const bool take)
        {
            const auto v = detail::ReadAndAdvancePC_u16(regs, mem);
            if (take) {
                regs.pc = v;
                return 16;
            }
            return 12;
        }

        Cycles HandleAbsoluteCall(Registers& regs, Memory& mem, const bool take)
        {
            const auto v = detail::ReadAndAdvancePC_u16(regs, mem);
            if (take) {
                detail::Push_u16(regs, mem, regs.pc);
                regs.pc = v;
                return 24;
            }
            return 12;
        }

        void DAA(Registers& regs)
        {
            // From https://forums.nesdev.com/viewtopic.php?t=15944
            uint8_t& a = regs.a;
            const bool n_flag = flag::IsSet(regs, Flag::n);
            const bool c_flag = flag::IsSet(regs, Flag::c);
            const bool h_flag = flag::IsSet(regs, Flag::h);
            if (!n_flag) {
                // After addition, adjusts if (half)-carry occured or result is out of bounds
                if (c_flag || a > 0x99) { a += 0x60; flag::Set(regs, Flag::c); }
                if (h_flag || (a & 0x0f) > 0x09) { a += 0x06; }
            } else {
                // After subtraction, only adjusts if (half)-carry occured
                if (c_flag) a -= 0x60;
                if (h_flag) a -= 0x06;
            }
            flag::Assign(regs, Flag::z, a == 0);
            flag::Clear(regs, Flag::h);
        }

        constexpr Cycles And_A_r8(Registers& regs, const uint8_t imm8)
        {
            regs.a &= imm8;
            flag::Assign(regs, Flag::z, regs.a == 0);
            flag::Clear(regs, Flag::n);
            flag::Set(regs, Flag::h);
            flag::Clear(regs, Flag::c);
            return 4;
        }

        constexpr Cycles Xor_A_r8(Registers& regs, const uint8_t imm8)
        {
            regs.a ^= imm8;
            flag::Assign(regs, Flag::z, regs.a == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Clear(regs, Flag::c);
            return 4;
        }

        constexpr Cycles Or_A_r8(Registers& regs, const uint8_t imm8)
        {
            regs.a |= imm8;
            flag::Assign(regs, Flag::z, regs.a == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Clear(regs, Flag::c);
            return 4;
        }

        constexpr Cycles Cp_A_r8(Registers& regs, const uint8_t imm8)
        {
            auto v = regs.a;
            Sub_r8(regs, v, imm8);
            return 4;
        }

        Cycles Rst(Registers& regs, Memory& mem, const uint8_t op)
        {
            Push_u16(regs, mem, regs.pc);
            regs.pc = op;
            return 16;
        }

        Cycles InvalidInstruction(Registers& regs, Memory& mem)
        {
            std::cerr << "Invalid instruction!\n";
            return 4;
        }

        Cycles Rlc(Registers& regs, uint8_t& r)
        {
            const bool carry = (r & 0x80) != 0;
            r = (r << 1) & 0xff;
            if (carry) r |= 1;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Assign(regs, Flag::c, carry);
            return 4;
        }

        void Rrc(Registers& regs, uint8_t& r)
        {
            const bool carry = (r & 1) != 0;
            r = r >> 1;
            if (carry) r |= 0x80;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Assign(regs, Flag::c, carry);
        }

        template<typename Operation>
        void PerformOnHLIndirect(Registers& regs, Memory& mem, Operation op)
        {
            const auto hl = detail::FuseRegisters(regs.h, regs.l);
            auto v = mem.Read_u8(hl);
            op(regs, v);
            mem.Write_u8(hl, v);
        }

        void Rl(Registers& regs, uint8_t& r)
        {
            const bool carry = (r & 0x80) != 0;
            r = (r << 1) & 0xff;
            if (flag::IsSet(regs, Flag::c)) r |= 1;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Assign(regs, Flag::c, carry);
        }

        void Rr(Registers& regs, uint8_t& r)
        {
            const bool carry = (r & 1) != 0;
            r = r >> 1;
            if (flag::IsSet(regs, Flag::c)) r |= 0x80;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Assign(regs, Flag::c, carry);
        }

        void Sla(Registers& regs, uint8_t& r)
        {
            const bool carry = (r & 0x80) != 0;
            r = (r << 1) & 0xff;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Assign(regs, Flag::c, carry);
        }

        void Sra(Registers& regs, uint8_t& r)
        {
            const bool carry = (r & 1) != 0;
            r = (r & 0x80) | r >> 1;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Assign(regs, Flag::c, carry);
        }

        void Swap(Registers& regs, uint8_t& r)
        {
            r = (r >> 4) | ((r & 0xf) << 4);
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Clear(regs, Flag::c);
        }

        void Srl(Registers& regs, uint8_t& r)
        {
            const bool carry = (r & 1) != 0;
            r = r >> 1;
            flag::Assign(regs, Flag::z, r == 0);
            flag::Clear(regs, Flag::n);
            flag::Clear(regs, Flag::h);
            flag::Assign(regs, Flag::c, carry);
        }

        template<int N>
        void Bit(Registers& regs, const uint8_t r)
        {
            flag::Assign(regs, Flag::z, (r & (1 << N)) == 0);
            flag::Clear(regs, Flag::n);
            flag::Set(regs, Flag::h);
        }

        template<int N>
        void Res(Registers& regs, uint8_t& r)
        {
            r &= ~(1 << N);
        }

        template<int N>
        void Set(Registers& regs, uint8_t& r)
        {
            r |= (1 << N);
        }
    }

    using Function = Cycles (*)(Registers&, Memory&);

    enum class Argument {
        None,
        Imm8,
        Imm16,
        Rel8
    };

    struct Instruction {
        std::string_view name;
        Argument arg;
        Function func;
    };

    void InvokeIRQ(Registers& regs, Memory& memory, int n)
    {
        // TODO wait 20 cycles
        detail::Push_u16(regs, memory, regs.pc);
        regs.pc = 0x40 + 8 * n;
    }

    // Instructions prefixed with CB xx
    constexpr std::array<Instruction, 256> opcode_cb{ {
        /* 00 */ { "rlc b", Argument::None,  [](Registers& regs, Memory& mem) {
                    detail::Rlc(regs, regs.b);
                    return 8;
                 } },
        /* 01 */ { "rlc c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rlc(regs, regs.c);
                    return 8;
                 } },
        /* 02 */ { "rlc d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rlc(regs, regs.d);
                    return 8;
                 } },
        /* 03 */ { "rlc e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rlc(regs, regs.e);
                    return 8;
                 } },
        /* 04 */ { "rlc h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rlc(regs, regs.h);
                    return 8;
                 } },
        /* 05 */ { "rlc l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rlc(regs, regs.l);
                    return 8;
                 } },
        /* 06 */ { "rlc (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Rlc(regs, v);
                    });
                    return 16;
                 } },
        /* 07 */ { "rlc a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rlc(regs, regs.a);
                    return 8;
                 } },
        /* 08 */ { "rrc b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rrc(regs, regs.b);
                    return 8;
                 } },
        /* 09 */ { "rrc c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rrc(regs, regs.c);
                    return 8;
                 } },
        /* 0a */ { "rrc d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rrc(regs, regs.d);
                    return 8;
                 } },
        /* 0b */ { "rrc e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rrc(regs, regs.e);
                    return 8;
                 } },
        /* 0c */ { "rrc h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rrc(regs, regs.h);
                    return 8;
                 } },
        /* 0d */ { "rrc l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rrc(regs, regs.l);
                    return 8;
                 } },
        /* 0e */ { "rrc (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Rrc(regs, v);
                    });
                    return 16;
                 } },
        /* 0f */ { "rrc a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rrc(regs, regs.a);
                    return 8;
                 } },
        /* 10 */ { "rl b", Argument::None,  [](Registers& regs, Memory& mem) {
                    detail::Rl(regs, regs.b);
                    return 8;
                 } },
        /* 11 */ { "rl c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rl(regs, regs.c);
                    return 8;
                 } },
        /* 12 */ { "rl d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rl(regs, regs.d);
                    return 8;
                 } },
        /* 13 */ { "rl e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rl(regs, regs.e);
                    return 8;
                 } },
        /* 14 */ { "rl h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rl(regs, regs.h);
                    return 8;
                 } },
        /* 15 */ { "rl l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rl(regs, regs.l);
                    return 8;
                 } },
        /* 16 */ { "rl (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Rl(regs, v);
                    });
                    return 16;
                 } },
        /* 17 */ { "rl a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rl(regs, regs.a);
                    return 8;
                 } },
        /* 18 */ { "rr b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rr(regs, regs.b);
                    return 8;
                 } },
        /* 19 */ { "rr c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rr(regs, regs.c);
                    return 8;
                 } },
        /* 1a */ { "rr d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rr(regs, regs.d);
                    return 8;
                 } },
        /* 1b */ { "rr e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rr(regs, regs.e);
                    return 8;
                 } },
        /* 1c */ { "rr h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rr(regs, regs.h);
                    return 8;
                 } },
        /* 1d */ { "rr l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rr(regs, regs.l);
                    return 8;
                 } },
        /* 1e */ { "rr (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Rr(regs, v);
                    });
                    return 16;
                 } },
        /* 1f */ { "rr a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rr(regs, regs.a);
                    return 8;
                 } },
        /* 20 */ { "sla b", Argument::None,  [](Registers& regs, Memory& mem) {
                    detail::Sla(regs, regs.b);
                    return 8;
                 } },
        /* 21 */ { "sla c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sla(regs, regs.c);
                    return 8;
                 } },
        /* 22 */ { "sla d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sla(regs, regs.d);
                    return 8;
                 } },
        /* 23 */ { "sla e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sla(regs, regs.e);
                    return 8;
                 } },
        /* 24 */ { "sla h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sla(regs, regs.h);
                    return 8;
                 } },
        /* 25 */ { "sla l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sla(regs, regs.l);
                    return 8;
                 } },
        /* 26 */ { "sla (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Sla(regs, v);
                    });
                    return 16;
                 } },
        /* 27 */ { "sla a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sla(regs, regs.a);
                    return 8;
                 } },
        /* 28 */ { "sra b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sra(regs, regs.b);
                    return 8;
                 } },
        /* 29 */ { "sra c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sra(regs, regs.c);
                    return 8;
                 } },
        /* 2a */ { "sra d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sra(regs, regs.d);
                    return 8;
                 } },
        /* 2b */ { "sra e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sra(regs, regs.e);
                    return 8;
                 } },
        /* 2c */ { "sra h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sra(regs, regs.h);
                    return 8;
                 } },
        /* 2d */ { "sra l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sra(regs, regs.l);
                    return 8;
                 } },
        /* 2e */ { "sra (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Sra(regs, v);
                    });
                    return 16;
                 } },
        /* 2f */ { "sra a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sra(regs, regs.a);
                    return 8;
                 } },
        /* 30 */ { "swap b", Argument::None,  [](Registers& regs, Memory& mem) {
                    detail::Swap(regs, regs.b);
                    return 8;
                 } },
        /* 31 */ { "swap c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Swap(regs, regs.c);
                    return 8;
                 } },
        /* 32 */ { "swap d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Swap(regs, regs.d);
                    return 8;
                 } },
        /* 33 */ { "swap e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Swap(regs, regs.e);
                    return 8;
                 } },
        /* 34 */ { "swap h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Swap(regs, regs.h);
                    return 8;
                 } },
        /* 35 */ { "swap l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Swap(regs, regs.l);
                    return 8;
                 } },
        /* 36 */ { "swap (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Swap(regs, v);
                    });
                    return 16;
                 } },
        /* 37 */ { "swap a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Swap(regs, regs.a);
                    return 8;
                 } },
        /* 38 */ { "srl b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Srl(regs, regs.b);
                    return 8;
                 } },
        /* 39 */ { "srl c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Srl(regs, regs.c);
                    return 8;
                 } },
        /* 3a */ { "srl d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Srl(regs, regs.d);
                    return 8;
                 } },
        /* 3b */ { "srl e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Srl(regs, regs.e);
                    return 8;
                 } },
        /* 3c */ { "srl h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Srl(regs, regs.h);
                    return 8;
                 } },
        /* 3d */ { "srl l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Srl(regs, regs.l);
                    return 8;
                 } },
        /* 3e */ { "srl (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Srl(regs, v);
                    });
                    return 16;
                 } },
        /* 3f */ { "srl a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Srl(regs, regs.a);
                    return 8;
                 } },
        /* 40 */ { "bit 0,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<0>(regs, regs.b);
                    return 8;
                 } },
        /* 41 */ { "bit 0,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<0>(regs, regs.c);
                    return 8;
                 } },
        /* 42 */ { "bit 0,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<0>(regs, regs.d);
                    return 8;
                 } },
        /* 43 */ { "bit 0,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<0>(regs, regs.e);
                    return 8;
                 } },
        /* 44 */ { "bit 0,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<0>(regs, regs.h);
                    return 8;
                 } },
        /* 45 */ { "bit 0,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<0>(regs, regs.l);
                    return 8;
                 } },
        /* 46 */ { "bit 0,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Bit<0>(regs, v);
                    return 16;
                 } },
        /* 47 */ { "bit 0,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<0>(regs, regs.a);
                    return 8;
                 } },
        /* 48 */ { "bit 1,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<1>(regs, regs.b);
                    return 8;
                 } },
        /* 49 */ { "bit 1,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<1>(regs, regs.c);
                    return 8;
                 } },
        /* 4a */ { "bit 1,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<1>(regs, regs.d);
                    return 8;
                 } },
        /* 4b */ { "bit 1,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<1>(regs, regs.e);
                    return 8;
                 } },
        /* 4c */ { "bit 1,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<1>(regs, regs.h);
                    return 8;
                 } },
        /* 4d */ { "bit 1,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<1>(regs, regs.l);
                    return 8;
                 } },
        /* 4e */ { "bit 1,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Bit<1>(regs, v);
                    return 8;
                 } },
        /* 4f */ { "bit 1,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<1>(regs, regs.a);
                    return 8;
                 } },
        /* 50 */ { "bit 2,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<2>(regs, regs.b);
                    return 8;
                 } },
        /* 51 */ { "bit 2,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<2>(regs, regs.c);
                    return 8;
                 } },
        /* 52 */ { "bit 2,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<2>(regs, regs.d);
                    return 8;
                 } },
        /* 53 */ { "bit 2,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<2>(regs, regs.e);
                    return 8;
                 } },
        /* 54 */ { "bit 2,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<2>(regs, regs.h);
                    return 8;
                 } },
        /* 55 */ { "bit 2,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<2>(regs, regs.l);
                    return 8;
                 } },
        /* 56 */ { "bit 2,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Bit<2>(regs, v);
                    return 16;
                 } },
        /* 57 */ { "bit 2,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<2>(regs, regs.a);
                    return 8;
                 } },
        /* 58 */ { "bit 3,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<3>(regs, regs.b);
                    return 8;
                 } },
        /* 59 */ { "bit 3,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<3>(regs, regs.c);
                    return 8;
                 } },
        /* 5a */ { "bit 3,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<3>(regs, regs.d);
                    return 8;
                 } },
        /* 5b */ { "bit 3,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<3>(regs, regs.e);
                    return 8;
                 } },
        /* 5c */ { "bit 3,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<3>(regs, regs.h);
                    return 8;
                 } },
        /* 5d */ { "bit 3,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<3>(regs, regs.l);
                    return 8;
                 } },
        /* 5e */ { "bit 3,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Bit<3>(regs, v);
                    return 8;
                 } },
        /* 5f */ { "bit 3,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<3>(regs, regs.a);
                    return 8;
                 } },
        /* 60 */ { "bit 4,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<4>(regs, regs.b);
                    return 8;
                 } },
        /* 61 */ { "bit 4,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<4>(regs, regs.c);
                    return 8;
                 } },
        /* 62 */ { "bit 4,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<4>(regs, regs.d);
                    return 8;
                 } },
        /* 63 */ { "bit 4,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<4>(regs, regs.e);
                    return 8;
                 } },
        /* 64 */ { "bit 4,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<4>(regs, regs.h);
                    return 8;
                 } },
        /* 65 */ { "bit 4,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<4>(regs, regs.l);
                    return 8;
                 } },
        /* 66 */ { "bit 4,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Bit<4>(regs, v);
                    return 16;
                 } },
        /* 67 */ { "bit 4,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<4>(regs, regs.a);
                    return 8;
                 } },
        /* 68 */ { "bit 5,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<5>(regs, regs.b);
                    return 8;
                 } },
        /* 69 */ { "bit 5,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<5>(regs, regs.c);
                    return 8;
                 } },
        /* 6a */ { "bit 5,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<5>(regs, regs.d);
                    return 8;
                 } },
        /* 6b */ { "bit 5,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<5>(regs, regs.e);
                    return 8;
                 } },
        /* 6c */ { "bit 5,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<5>(regs, regs.h);
                    return 8;
                 } },
        /* 6d */ { "bit 5,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<5>(regs, regs.l);
                    return 8;
                 } },
        /* 6e */ { "bit 5,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Bit<5>(regs, v);
                    return 8;
                 } },
        /* 6f */ { "bit 5,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<5>(regs, regs.a);
                    return 8;
                 } },
        /* 70 */ { "bit 6,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<6>(regs, regs.b);
                    return 8;
                 } },
        /* 71 */ { "bit 6,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<6>(regs, regs.c);
                    return 8;
                 } },
        /* 72 */ { "bit 6,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<6>(regs, regs.d);
                    return 8;
                 } },
        /* 73 */ { "bit 6,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<6>(regs, regs.e);
                    return 8;
                 } },
        /* 74 */ { "bit 6,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<6>(regs, regs.h);
                    return 8;
                 } },
        /* 75 */ { "bit 6,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<6>(regs, regs.l);
                    return 8;
                 } },
        /* 76 */ { "bit 6,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Bit<6>(regs, v);
                    return 16;
                 } },
        /* 77 */ { "bit 6,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<6>(regs, regs.a);
                    return 8;
                 } },
        /* 78 */ { "bit 7,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<7>(regs, regs.b);
                    return 8;
                 } },
        /* 79 */ { "bit 7,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<7>(regs, regs.c);
                    return 8;
                 } },
        /* 7a */ { "bit 7,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<7>(regs, regs.d);
                    return 8;
                 } },
        /* 7b */ { "bit 7,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<7>(regs, regs.e);
                    return 8;
                 } },
        /* 7c */ { "bit 7,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<7>(regs, regs.h);
                    return 8;
                 } },
        /* 7d */ { "bit 7,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<7>(regs, regs.l);
                    return 8;
                 } },
        /* 7e */ { "bit 7,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Bit<7>(regs, v);
                    return 8;
                 } },
        /* 7f */ { "bit 7,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Bit<7>(regs, regs.a);
                    return 8;
                 } },
        /* 80 */ { "res 0,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<0>(regs, regs.b);
                    return 8;
                 } },
        /* 81 */ { "res 0,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<0>(regs, regs.c);
                    return 8;
                 } },
        /* 82 */ { "res 0,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<0>(regs, regs.d);
                    return 8;
                 } },
        /* 83 */ { "res 0,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<0>(regs, regs.e);
                    return 8;
                 } },
        /* 84 */ { "res 0,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<0>(regs, regs.h);
                    return 8;
                 } },
        /* 85 */ { "res 0,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<0>(regs, regs.l);
                    return 8;
                 } },
        /* 86 */ { "res 0,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Res<0>(regs, v);
                    });
                    return 16;
                 } },
        /* 87 */ { "res 0,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<0>(regs, regs.a);
                    return 8;
                 } },
        /* 88 */ { "res 1,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<1>(regs, regs.b);
                    return 8;
                 } },
        /* 89 */ { "res 1,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<1>(regs, regs.c);
                    return 8;
                 } },
        /* 8a */ { "res 1,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<1>(regs, regs.d);
                    return 8;
                 } },
        /* 8b */ { "res 1,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<1>(regs, regs.e);
                    return 8;
                 } },
        /* 8c */ { "res 1,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<1>(regs, regs.h);
                    return 8;
                 } },
        /* 8d */ { "res 1,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<1>(regs, regs.l);
                    return 8;
                 } },
        /* 8e */ { "res 1,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Res<1>(regs, v);
                    });
                    return 16;
                 } },
        /* 8f */ { "res 1,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<1>(regs, regs.a);
                    return 8;
                 } },
        /* 90 */ { "res 2,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<2>(regs, regs.b);
                    return 8;
                 } },
        /* 91 */ { "res 2,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<2>(regs, regs.c);
                    return 8;
                 } },
        /* 92 */ { "res 2,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<2>(regs, regs.d);
                    return 8;
                 } },
        /* 93 */ { "res 2,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<2>(regs, regs.e);
                    return 8;
                 } },
        /* 94 */ { "res 2,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<2>(regs, regs.h);
                    return 8;
                 } },
        /* 95 */ { "res 2,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<2>(regs, regs.l);
                    return 8;
                 } },
        /* 96 */ { "res 2,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Res<2>(regs, v);
                    });
                    return 16;
                 } },
        /* 97 */ { "res 2,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<2>(regs, regs.a);
                    return 8;
                 } },
        /* 98 */ { "res 3,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<3>(regs, regs.b);
                    return 8;
                 } },
        /* 99 */ { "res 3,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<3>(regs, regs.c);
                    return 8;
                 } },
        /* 9a */ { "res 3,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<3>(regs, regs.d);
                    return 8;
                 } },
        /* 9b */ { "res 3,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<3>(regs, regs.e);
                    return 8;
                 } },
        /* 9c */ { "res 3,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<3>(regs, regs.h);
                    return 8;
                 } },
        /* 9d */ { "res 3,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<3>(regs, regs.l);
                    return 8;
                 } },
        /* 9e */ { "res 3,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Res<3>(regs, v);
                    });
                    return 16;
                 } },
        /* 9f */ { "res 3,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<3>(regs, regs.a);
                    return 8;
                 } },
        /* a0 */ { "res 4,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<4>(regs, regs.b);
                    return 8;
                 } },
        /* a1 */ { "res 4,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<4>(regs, regs.c);
                    return 8;
                 } },
        /* a2 */ { "res 4,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<4>(regs, regs.d);
                    return 8;
                 } },
        /* a3 */ { "res 4,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<4>(regs, regs.e);
                    return 8;
                 } },
        /* a4 */ { "res 4,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<4>(regs, regs.h);
                    return 8;
                 } },
        /* a5 */ { "res 4,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<4>(regs, regs.l);
                    return 8;
                 } },
        /* a6 */ { "res 4,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Res<4>(regs, v);
                    });
                    return 16;
                 } },
        /* a7 */ { "res 4,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<4>(regs, regs.a);
                    return 8;
                 } },
        /* a8 */ { "res 5,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<5>(regs, regs.b);
                    return 8;
                 } },
        /* a9 */ { "res 5,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<5>(regs, regs.c);
                    return 8;
                 } },
        /* aa */ { "res 5,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<5>(regs, regs.d);
                    return 8;
                 } },
        /* ab */ { "res 5,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<5>(regs, regs.e);
                    return 8;
                 } },
        /* ac */ { "res 5,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<5>(regs, regs.h);
                    return 8;
                 } },
        /* ad */ { "res 5,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<5>(regs, regs.l);
                    return 8;
                 } },
        /* ae */ { "res 5,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Res<5>(regs, v);
                    });
                    return 16;
                 } },
        /* af */ { "res 5,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<5>(regs, regs.a);
                    return 8;
                 } },
        /* b0 */ { "res 6,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<6>(regs, regs.b);
                    return 8;
                 } },
        /* b1 */ { "res 6,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<6>(regs, regs.c);
                    return 8;
                 } },
        /* b2 */ { "res 6,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<6>(regs, regs.d);
                    return 8;
                 } },
        /* b3 */ { "res 6,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<6>(regs, regs.e);
                    return 8;
                 } },
        /* b4 */ { "res 6,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<6>(regs, regs.h);
                    return 8;
                 } },
        /* b5 */ { "res 6,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<6>(regs, regs.l);
                    return 8;
                 } },
        /* b6 */ { "res 6,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Res<6>(regs, v);
                    });
                    return 16;
                 } },
        /* b7 */ { "res 6,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<6>(regs, regs.a);
                    return 8;
                 } },
        /* b8 */ { "res 7,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<7>(regs, regs.b);
                    return 8;
                 } },
        /* b9 */ { "res 7,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<7>(regs, regs.c);
                    return 8;
                 } },
        /* ba */ { "res 7,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<7>(regs, regs.d);
                    return 8;
                 } },
        /* bb */ { "res 7,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<7>(regs, regs.e);
                    return 8;
                 } },
        /* bc */ { "res 7,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<7>(regs, regs.h);
                    return 8;
                 } },
        /* bd */ { "res 7,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<7>(regs, regs.l);
                    return 8;
                 } },
        /* be */ { "res 7,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Res<7>(regs, v);
                    });
                    return 16;
                 } },
        /* bf */ { "res 7,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Res<7>(regs, regs.a);
                    return 8;
                 } },
        /* c0 */ { "set 0,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<0>(regs, regs.b);
                    return 8;
                 } },
        /* c1 */ { "set 0,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<0>(regs, regs.c);
                    return 8;
                 } },
        /* c2 */ { "set 0,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<0>(regs, regs.d);
                    return 8;
                 } },
        /* c3 */ { "set 0,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<0>(regs, regs.e);
                    return 8;
                 } },
        /* c4 */ { "set 0,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<0>(regs, regs.h);
                    return 8;
                 } },
        /* c5 */ { "set 0,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<0>(regs, regs.l);
                    return 8;
                 } },
        /* c6 */ { "set 0,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Set<0>(regs, v);
                    });
                    return 16;
                 } },
        /* c7 */ { "set 0,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<0>(regs, regs.a);
                    return 8;
                 } },
        /* c8 */ { "set 1,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<1>(regs, regs.b);
                    return 8;
                 } },
        /* c9 */ { "set 1,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<1>(regs, regs.c);
                    return 8;
                 } },
        /* ca */ { "set 1,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<1>(regs, regs.d);
                    return 8;
                 } },
        /* cb */ { "set 1,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<1>(regs, regs.e);
                    return 8;
                 } },
        /* cc */ { "set 1,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<1>(regs, regs.h);
                    return 8;
                 } },
        /* cd */ { "set 1,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<1>(regs, regs.l);
                    return 8;
                 } },
        /* ce */ { "set 1,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Set<1>(regs, v);
                    });
                    return 16;
                 } },
        /* cf */ { "set 1,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<1>(regs, regs.a);
                    return 8;
                 } },
        /* d0 */ { "set 2,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<2>(regs, regs.b);
                    return 8;
                 } },
        /* d1 */ { "set 2,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<2>(regs, regs.c);
                    return 8;
                 } },
        /* d2 */ { "set 2,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<2>(regs, regs.d);
                    return 8;
                 } },
        /* d3 */ { "set 2,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<2>(regs, regs.e);
                    return 8;
                 } },
        /* d4 */ { "set 2,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<2>(regs, regs.h);
                    return 8;
                 } },
        /* d5 */ { "set 2,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<2>(regs, regs.l);
                    return 8;
                 } },
        /* d6 */ { "set 2,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Set<2>(regs, v);
                    });
                    return 16;
                 } },
        /* d7 */ { "set 2,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<2>(regs, regs.a);
                    return 8;
                 } },
        /* d8 */ { "set 3,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<3>(regs, regs.b);
                    return 8;
                 } },
        /* d9 */ { "set 3,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<3>(regs, regs.c);
                    return 8;
                 } },
        /* da */ { "set 3,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<3>(regs, regs.d);
                    return 8;
                 } },
        /* db */ { "set 3,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<3>(regs, regs.e);
                    return 8;
                 } },
        /* dc */ { "set 3,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<3>(regs, regs.h);
                    return 8;
                 } },
        /* dd */ { "set 3,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<3>(regs, regs.l);
                    return 8;
                 } },
        /* de */ { "set 3,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Set<3>(regs, v);
                    });
                    return 16;
                 } },
        /* df */ { "set 3,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<3>(regs, regs.a);
                    return 8;
                 } },
        /* e0 */ { "set 4,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<4>(regs, regs.b);
                    return 8;
                 } },
        /* e1 */ { "set 4,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<4>(regs, regs.c);
                    return 8;
                 } },
        /* e2 */ { "set 4,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<4>(regs, regs.d);
                    return 8;
                 } },
        /* e3 */ { "set 4,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<4>(regs, regs.e);
                    return 8;
                 } },
        /* e4 */ { "set 4,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<4>(regs, regs.h);
                    return 8;
                 } },
        /* e5 */ { "set 4,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<4>(regs, regs.l);
                    return 8;
                 } },
        /* e6 */ { "set 4,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Set<4>(regs, v);
                    });
                    return 16;
                 } },
        /* e7 */ { "set 4,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<4>(regs, regs.a);
                    return 8;
                 } },
        /* e8 */ { "set 5,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<5>(regs, regs.b);
                    return 8;
                 } },
        /* e9 */ { "set 5,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<5>(regs, regs.c);
                    return 8;
                 } },
        /* ea */ { "set 5,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<5>(regs, regs.d);
                    return 8;
                 } },
        /* eb */ { "set 5,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<5>(regs, regs.e);
                    return 8;
                 } },
        /* ec */ { "set 5,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<5>(regs, regs.h);
                    return 8;
                 } },
        /* ed */ { "set 5,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<5>(regs, regs.l);
                    return 8;
                 } },
        /* ee */ { "set 5,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Set<5>(regs, v);
                    });
                    return 16;
                 } },
        /* ef */ { "set 5,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<5>(regs, regs.a);
                    return 8;
                 } },
        /* f0 */ { "set 6,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<6>(regs, regs.b);
                    return 8;
                 } },
        /* f1 */ { "set 6,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<6>(regs, regs.c);
                    return 8;
                 } },
        /* f2 */ { "set 6,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<6>(regs, regs.d);
                    return 8;
                 } },
        /* f3 */ { "set 6,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<6>(regs, regs.e);
                    return 8;
                 } },
        /* f4 */ { "set 6,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<6>(regs, regs.h);
                    return 8;
                 } },
        /* f5 */ { "set 6,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<6>(regs, regs.l);
                    return 8;
                 } },
        /* f6 */ { "set 6,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Set<6>(regs, v);
                    });
                    return 16;
                 } },
        /* f7 */ { "set 6,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<6>(regs, regs.a);
                    return 8;
                 } },
        /* f8 */ { "set 7,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<7>(regs, regs.b);
                    return 8;
                 } },
        /* f9 */ { "set 7,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<7>(regs, regs.c);
                    return 8;
                 } },
        /* fa */ { "set 7,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<7>(regs, regs.d);
                    return 8;
                 } },
        /* fb */ { "set 7,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<7>(regs, regs.e);
                    return 8;
                 } },
        /* fc */ { "set 7,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<7>(regs, regs.h);
                    return 8;
                 } },
        /* fd */ { "set 7,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<7>(regs, regs.l);
                    return 8;
                 } },
        /* fe */ { "set 7,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Set<7>(regs, v);
                    });
                    return 16;
                 } },
        /* ff */ { "set 7,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Set<7>(regs, regs.a);
                    return 8;
                 } },
    } };

    // Instructions
    constexpr std::array<Instruction, 256> opcode{ {
        /* 00 */ { "nop",      Argument::None,  [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* 01 */ { "ld bc,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    regs.c = detail::ReadAndAdvancePC_u8(regs, mem);
                    regs.b = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 12;
                 } },
        /* 02 */ { "ld (bc),a", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto bc = detail::FuseRegisters(regs.b, regs.c);
                    mem.Write_u8(bc, regs.a);
                    return 8;
                 } },
        /* 03 */ { "inc bc", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r16(regs.b, regs.c);
                    return 8;
                 } },
        /* 04 */ { "inc b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r8(regs, regs.b);
                    return 4;
                 } },
        /* 05 */ { "dec b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r8(regs, regs.b);
                    return 4;
                 } },
        /* 06 */ { "ld b,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    regs.b = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 8;
                 } },
        /* 07 */ { "rlca", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rlc(regs, regs.a);
                    flag::Clear(regs, Flag::z);
                    return 4;
                 } },
        /* 08 */ { "ld ({}),sp", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u16(regs, mem);
                    mem.Write_u16(v, regs.sp);
                    return 20;
                 } },
        /* 09 */ { "add hl,bc", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_HL_r16(regs, regs.b, regs.c);
                    return 8;
                 } },
        /* 0a */ { "ld a,(bc)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto bc = detail::FuseRegisters(regs.b, regs.c);
                    regs.a = mem.Read_u16(bc);
                    return 8;
                 } },
        /* 0b */ { "dec bc", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r16(regs.b, regs.c);
                    return 8;
                 } },
        /* 0c */ { "inc c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r8(regs, regs.c);
                    return 4;
                 } },
        /* 0d */ { "dec c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r8(regs, regs.c);
                    return 4;
                 } },
        /* 0e */ { "ld c,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    regs.c = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 8;
                 } },
        /* 0f */ { "rrca", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rrc(regs, regs.a);
                    flag::Clear(regs, Flag::z);
                    return 4;
                 } },
        /* 10 */ { "stop {}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    std::abort(); // XXX for now
                    return 4;
                 } },
        /* 11 */ { "ld de,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    regs.e = detail::ReadAndAdvancePC_u8(regs, mem);
                    regs.d = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 12;
                 } },
        /* 12 */ { "ld (de),a", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto de = detail::FuseRegisters(regs.d, regs.e);
                    mem.Write_u8(de, regs.a);
                    return 8;
                 } },
        /* 13 */ { "inc de", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r16(regs.d, regs.e);
                    return 8;
                 } },
        /* 14 */ { "inc d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r8(regs, regs.d);
                    return 4;
                 } },
        /* 15 */ { "dec d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r8(regs, regs.d);
                    return 4;
                 } },
        /* 16 */ { "ld d,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    regs.d = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 8;
                 } },
        /* 17 */ { "rla", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rl(regs, regs.a);
                    flag::Clear(regs, Flag::z);
                    return 4;
                 } },
        /* 18 */ { "jr {}", Argument::Rel8, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeJump(regs, mem, true);
                 } },
        /* 19 */ { "add hl,de", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_HL_r16(regs, regs.d, regs.e);
                    return 8;
                 } },
        /* 1a */ { "ld a,(de)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto de = detail::FuseRegisters(regs.d, regs.e);
                    regs.a = mem.Read_u16(de);
                    return 8;
                 } },
        /* 1b */ { "dec de", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r16(regs.d, regs.e);
                    return 8;
                 } },
        /* 1c */ { "inc e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r8(regs, regs.e);
                    return 4;
                 } },
        /* 1d */ { "dec e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r8(regs, regs.e);
                    return 4;
                 } },
        /* 1e */ { "ld e,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    regs.e = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 8;
                 } },
        /* 1f */ { "rra", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Rr(regs, regs.a);
                    flag::Clear(regs, Flag::z);
                    return 4;
                 } },
        /* 20 */ { "jr nz,{}", Argument::Rel8, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeJump(regs, mem, flag::IsClear(regs, Flag::z));
                 } },
        /* 21 */ { "ld hl,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    regs.l = detail::ReadAndAdvancePC_u8(regs, mem);
                    regs.h = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 12;
                 } },
        /* 22 */ { "ld (hl+),a", Argument::None, [](Registers& regs, Memory& mem) {
                    auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.a);
                    ++hl;
                    detail::DivideRegisters(hl, regs.h, regs.l);
                    return 8;
                 } },
        /* 23 */ { "inc hl", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r16(regs.h, regs.l);
                    return 8;
                 } },
        /* 24 */ { "inc h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r8(regs, regs.h);
                    return 4;
                 } },
        /* 25 */ { "dec h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r8(regs, regs.h);
                    return 4;
                 } },
        /* 26 */ { "ld h,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    regs.h = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 8;
                 } },
        /* 27 */ { "daa", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::DAA(regs);
                    return 4;
                 } },
        /* 28 */ { "jr z,{}", Argument::Rel8, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeJump(regs, mem, flag::IsSet(regs, Flag::z));
                 } },
        /* 29 */ { "add hl,hl", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_HL_r16(regs, regs.h, regs.l);
                    return 8;
                 } },
        /* 2a */ { "ld a,(hl+)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.a = mem.Read_u8(hl);
                    detail::Inc_r16(regs.h, regs.l);
                    return 8;
                 } },
        /* 2b */ { "dec hl", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r16(regs.h, regs.l);
                    return 8;
                 } },
        /* 2c */ { "inc l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r8(regs, regs.l);
                    return 4;
                 } },
        /* 2d */ { "dec l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r8(regs, regs.l);
                    return 4;
                 } },
        /* 2e */ { "ld l,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    regs.l = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 8;
                 } },
        /* 2f */ { "cpl", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.a = ~regs.a;
                    flag::Set(regs, Flag::n);
                    flag::Set(regs, Flag::h);
                    return 4;
                 } },
        /* 30 */ { "jr nc,{}", Argument::Rel8, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeJump(regs, mem, flag::IsClear(regs, Flag::c));
                 } },
        /* 31 */ { "ld sp,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    regs.sp = detail::ReadAndAdvancePC_u16(regs, mem);
                    return 12;
                 } },
        /* 32 */ { "ld (hl-),a", Argument::None, [](Registers& regs, Memory& mem) {
                    auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.a);
                    --hl;
                    detail::DivideRegisters(hl, regs.h, regs.l);
                    return 8;
                 } },
        /* 33 */ { "inc sp", Argument::None, [](Registers& regs, Memory& mem) {
                    ++regs.sp;
                    return 8;
                 } },
        /* 34 */ { "inc (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Inc_r8(regs, v);
                    });
                    return 12;
                 } },
        /* 35 */ { "dec (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::PerformOnHLIndirect(regs, mem, [](auto& regs, auto& v) {
                        detail::Dec_r8(regs, v);
                    });
                    return 12;
                 } },
        /* 36 */ { "ld (hl),{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    mem.Write_u8(hl, v);
                    return 12;
                 } },
        /* 37 */ { "scf", Argument::None, [](Registers& regs, Memory& mem) {
                    flag::Set(regs, Flag::c);
                    flag::Clear(regs, Flag::n);
                    flag::Clear(regs, Flag::h);
                    return 4;
                 } },
        /* 38 */ { "jr c,{}", Argument::Rel8, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeJump(regs, mem, flag::IsSet(regs, Flag::c));
                 } },
        /* 39 */ { "add hl,sp", Argument::None, [](Registers& regs, Memory& mem) {
                    auto hl = detail::FuseRegisters(regs.h, regs.l);
                    detail::Add_u16(regs, hl, regs.sp);
                    detail::DivideRegisters(hl, regs.h, regs.l);
                    return 8;
                 } },
        /* 3a */ { "ld a,(hl-)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.a = mem.Read_u16(hl);
                    detail::Dec_r16(regs.h, regs.l);
                    return 8;
                 } },
        /* 3b */ { "dec sp", Argument::None, [](Registers& regs, Memory& mem) {
                    --regs.sp;
                    return 8;
                 } },
        /* 3c */ { "inc a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Inc_r8(regs, regs.a);
                    return 4;
                 } },
        /* 3d */ { "dec a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Dec_r8(regs, regs.a);
                    return 4;
                 } },
        /* 3e */ { "ld a,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    regs.a = detail::ReadAndAdvancePC_u8(regs, mem);
                    return 8;
                 } },
        /* 3f */ { "ccf", Argument::None, [](Registers& regs, Memory& mem) {
                    flag::Assign(regs, Flag::c, !flag::IsSet(regs, Flag::c));
                    flag::Clear(regs, Flag::n);
                    flag::Clear(regs, Flag::h);
                    return 4;
                 } },
        /* 40 */ { "ld b,b", Argument::None, [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* 41 */ { "ld b,c", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.b = regs.c;
                    return 4;
                 } },
        /* 42 */ { "ld b,d", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.b = regs.d;
                    return 4;
                 } },
        /* 43 */ { "ld b,e", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.b = regs.e;
                    return 4;
                 } },
        /* 44 */ { "ld b,h", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.b = regs.h;
                    return 4;
                 } },
        /* 45 */ { "ld b,l", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.b = regs.l;
                    return 4;
                 } },
        /* 46 */ { "ld b,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.b = mem.Read_u8(hl);
                    return 8;
                 } },
        /* 47 */ { "ld b,a", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.b = regs.a;
                    return 4;
                 } },
        /* 48 */ { "ld c,b", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.c = regs.b;
                    return 4;
                 } },
        /* 49 */ { "ld c,c", Argument::None, [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* 4a */ { "ld c,d", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.c = regs.d;
                    return 4;
                 } },
        /* 4b */ { "ld c,e", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.c = regs.e;
                    return 4;
                 } },
        /* 4c */ { "ld c,h", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.c = regs.h;
                    return 4;
                 } },
        /* 4d */ { "ld c,l", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.c = regs.l;
                    return 4;
                 } },
        /* 4e */ { "ld c,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.c = mem.Read_u8(hl);
                    return 8;
                 } },
        /* 4f */ { "ld c,a", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.c = regs.a;
                    return 4;
                 } },
        /* 50 */ { "ld d,b", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.d = regs.b;
                    return 4;
                 } },
        /* 51 */ { "ld d,c", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.d = regs.c;
                    return 4;
                 } },
        /* 52 */ { "ld d,d", Argument::None, [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* 53 */ { "ld d,e", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.d = regs.e;
                    return 4;
                 } },
        /* 54 */ { "ld d,h", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.d = regs.h;
                    return 4;
                 } },
        /* 55 */ { "ld d,l", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.d = regs.l;
                    return 4;
                 } },
        /* 56 */ { "ld d,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.d = mem.Read_u8(hl);
                    return 8;
                 } },
        /* 57 */ { "ld d,a", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.d = regs.a;
                    return 4;
                 } },
        /* 58 */ { "ld e,b", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.e = regs.b;
                    return 4;
                 } },
        /* 59 */ { "ld e,c", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.e = regs.c;
                    return 4;
                 } },
        /* 5a */ { "ld e,d", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.e = regs.d;
                    return 4;
                 } },
        /* 5b */ { "ld e,e", Argument::None, [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* 5c */ { "ld e,h", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.e = regs.h;
                    return 4;
                 } },
        /* 5d */ { "ld e,l", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.e = regs.l;
                    return 4;
                 } },
        /* 5e */ { "ld e,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.e = mem.Read_u8(hl);
                    return 8;
                 } },
        /* 5f */ { "ld e,a", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.e = regs.a;
                    return 4;
                 } },
        /* 60 */ { "ld h,b", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.h = regs.b;
                    return 4;
                 } },
        /* 61 */ { "ld h,c", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.h = regs.c;
                    return 4;
                 } },
        /* 62 */ { "ld h,d", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.h = regs.d;
                    return 4;
                 } },
        /* 63 */ { "ld h,e", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.h = regs.e;
                    return 4;
                 } },
        /* 64 */ { "ld h,h", Argument::None, [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* 65 */ { "ld h,l", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.h = regs.l;
                    return 4;
                 } },
        /* 66 */ { "ld h,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.h = mem.Read_u8(hl);
                    return 8;
                 } },
        /* 67 */ { "ld h,a", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.h = regs.a;
                    return 4;
                 } },
        /* 68 */ { "ld l,b", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.l = regs.b;
                    return 4;
                 } },
        /* 69 */ { "ld l,c", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.l = regs.c;
                    return 4;
                 } },
        /* 6a */ { "ld l,d", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.l = regs.d;
                    return 4;
                 } },
        /* 6b */ { "ld l,e", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.l = regs.e;
                    return 4;
                 } },
        /* 6c */ { "ld l,h", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.l = regs.h;
                    return 4;
                 } },
        /* 6d */ { "ld l,l", Argument::None, [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* 6e */ { "ld l,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.l = mem.Read_u8(hl);
                    return 8;
                 } },
        /* 6f */ { "ld l,a", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.l = regs.a;
                    return 4;
                 } },
        /* 70 */ { "ld (hl),b", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.b);
                    return 8;
                 } },
        /* 71 */ { "ld (hl),c", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.c);
                    return 8;
                 } },
        /* 72 */ { "ld (hl),d", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.d);
                    return 8;
                 } },
        /* 73 */ { "ld (hl),e", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.e);
                    return 8;
                 } },
        /* 74 */ { "ld (hl),h", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.h);
                    return 8;
                 } },
        /* 75 */ { "ld (hl),l", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.l);
                    return 8;
                 } },
        /* 76 */ { "halt", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.halt = true;
                    return 8;
                 } },
        /* 77 */ { "ld (hl),a", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    mem.Write_u8(hl, regs.a);
                    return 8;
                 } },
        /* 78 */ { "ld a,b", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.a = regs.b;
                    return 4;
                 } },
        /* 79 */ { "ld a,c", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.a = regs.c;
                    return 4;
                 } },
        /* 7a */ { "ld a,d", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.a = regs.d;
                    return 4;
                 } },
        /* 7b */ { "ld a,e", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.a = regs.e;
                    return 4;
                 } },
        /* 7c */ { "ld a,h", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.a = regs.h;
                    return 4;
                 } },
        /* 7d */ { "ld a,l", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.a = regs.l;
                    return 4;
                 } },
        /* 7e */ { "ld a,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    regs.a = mem.Read_u8(hl);
                    return 8;
                 } },
        /* 7f */ { "ld a,a", Argument::None, [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* 80 */ { "add a,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_r8(regs, regs.a, regs.b);
                    return 4;
                 } },
        /* 81 */ { "add a,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_r8(regs, regs.a, regs.c);
                    return 4;
                 } },
        /* 82 */ { "add a,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_r8(regs, regs.a, regs.d);
                    return 4;
                 } },
        /* 83 */ { "add a,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_r8(regs, regs.a, regs.e);
                    return 4;
                 } },
        /* 84 */ { "add a,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_r8(regs, regs.a, regs.h);
                    return 4;
                 } },
        /* 85 */ { "add a,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_r8(regs, regs.a, regs.l);
                    return 4;
                 } },
        /* 86 */ { "add a,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Add_r8(regs, regs.a, v);
                    return 8;
                 } },
        /* 87 */ { "add a,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Add_r8(regs, regs.a, regs.a);
                    return 4;
                 } },
        /* 88 */ { "adc a,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Adc_r8(regs, regs.a, regs.b);
                    return 4;
                 } },
        /* 89 */ { "adc a,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Adc_r8(regs, regs.a, regs.c);
                    return 4;
                 } },
        /* 8a */ { "adc a,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Adc_r8(regs, regs.a, regs.d);
                    return 4;
                 } },
        /* 8b */ { "adc a,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Adc_r8(regs, regs.a, regs.e);
                    return 4;
                 } },
        /* 8c */ { "adc a,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Adc_r8(regs, regs.a, regs.h);
                    return 4;
                 } },
        /* 8d */ { "adc a,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Adc_r8(regs, regs.a, regs.l);
                    return 4;
                 } },
        /* 8e */ { "adc a,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Adc_r8(regs, regs.a, v);
                    return 4;
                 } },
        /* 8f */ { "adc a,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Adc_r8(regs, regs.a, regs.a);
                    return 4;
                 } },
        /* 90 */ { "sub b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sub_r8(regs, regs.a, regs.b);
                    return 4;
                 } },
        /* 91 */ { "sub c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sub_r8(regs, regs.a, regs.c);
                    return 4;
                 } },
        /* 92 */ { "sub d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sub_r8(regs, regs.a, regs.d);
                    return 4;
                 } },
        /* 93 */ { "sub e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sub_r8(regs, regs.a, regs.e);
                    return 4;
                 } },
        /* 94 */ { "sub h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sub_r8(regs, regs.a, regs.h);
                    return 4;
                 } },
        /* 95 */ { "sub l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sub_r8(regs, regs.a, regs.l);
                    return 4;
                 } },
        /* 96 */ { "sub (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Sub_r8(regs, regs.a, v);
                    return 8;
                 } },
        /* 97 */ { "sub a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sub_r8(regs, regs.a, regs.a);
                    return 4;
                 } },
        /* 98 */ { "sbc a,b", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sbc_r8(regs, regs.a, regs.b);
                    return 4;
                 } },
        /* 99 */ { "sbc a,c", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sbc_r8(regs, regs.a, regs.c);
                    return 4;
                 } },
        /* 9a */ { "sbc a,d", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sbc_r8(regs, regs.a, regs.d);
                    return 4;
                 } },
        /* 9b */ { "sbc a,e", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sbc_r8(regs, regs.a, regs.e);
                    return 4;
                 } },
        /* 9c */ { "sbc a,h", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sbc_r8(regs, regs.a, regs.h);
                    return 4;
                 } },
        /* 9d */ { "sbc a,l", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sbc_r8(regs, regs.a, regs.l);
                    return 4;
                 } },
        /* 9e */ { "sbc a,(hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Sbc_r8(regs, regs.a, v);
                    return 4;
                 } },
        /* 9f */ { "sbc a,a", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Sbc_r8(regs, regs.a, regs.a);
                    return 4;
                 } },
        /* a0 */ { "and b", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::And_A_r8(regs, regs.b);
                 } },
        /* a1 */ { "and c", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::And_A_r8(regs, regs.c);
                 } },
        /* a2 */ { "and d", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::And_A_r8(regs, regs.d);
                 } },
        /* a3 */ { "and e", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::And_A_r8(regs, regs.e);
                 } },
        /* a4 */ { "and h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::And_A_r8(regs, regs.h);
                 } },
        /* a5 */ { "and l", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::And_A_r8(regs, regs.l);
                 } },
        /* a6 */ { "and (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::And_A_r8(regs, v);
                    return 8;
                 } },
        /* a7 */ { "and a", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::And_A_r8(regs, regs.a);
                 } },
        /* a8 */ { "xor b", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Xor_A_r8(regs, regs.b);
                 } },
        /* a9 */ { "xor c", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Xor_A_r8(regs, regs.c);
                 } },
        /* aa */ { "xor d", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Xor_A_r8(regs, regs.d);
                 } },
        /* ab */ { "xor e", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Xor_A_r8(regs, regs.e);
                 } },
        /* ac */ { "xor h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Xor_A_r8(regs, regs.h);
                 } },
        /* ad */ { "xor l", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Xor_A_r8(regs, regs.l);
                 } },
        /* ae */ { "xor (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Xor_A_r8(regs, v);
                    return 8;
                 } },
        /* af */ { "xor a", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Xor_A_r8(regs, regs.a);
                 } },
        /* b0 */ { "or b", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Or_A_r8(regs, regs.b);
                 } },
        /* b1 */ { "or c", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Or_A_r8(regs, regs.c);
                 } },
        /* b2 */ { "or d", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Or_A_r8(regs, regs.d);
                 } },
        /* b3 */ { "or e", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Or_A_r8(regs, regs.e);
                 } },
        /* b4 */ { "or h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Or_A_r8(regs, regs.h);
                 } },
        /* b5 */ { "or l", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Or_A_r8(regs, regs.l);
                 } },
        /* b6 */ { "or (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Or_A_r8(regs, v);
                    return 8;
                 } },
        /* b7 */ { "or a", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Or_A_r8(regs, regs.a);
                 } },
        /* b8 */ { "cp b", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Cp_A_r8(regs, regs.b);
                 } },
        /* b9 */ { "cp c", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Cp_A_r8(regs, regs.c);
                 } },
        /* ba */ { "cp d", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Cp_A_r8(regs, regs.d);
                 } },
        /* bb */ { "cp e", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Cp_A_r8(regs, regs.e);
                 } },
        /* bc */ { "cp h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Cp_A_r8(regs, regs.h);
                 } },
        /* bd */ { "cp l", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Cp_A_r8(regs, regs.l);
                 } },
        /* be */ { "cp (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    const auto v = mem.Read_u8(hl);
                    detail::Cp_A_r8(regs, v);
                    return 8;
                 } },
        /* bf */ { "cp a", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Cp_A_r8(regs, regs.a);
                 } },
        /* c0 */ { "ret nz", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeReturn(regs, mem, flag::IsClear(regs, Flag::z));
                 } },
        /* c1 */ { "pop bc", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto v = detail::Pop_u16(regs, mem);
                    detail::DivideRegisters(v, regs.b, regs.c);
                    return 12;
                 } },
        /* c2 */ { "jp nz,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteJump(regs, mem, flag::IsClear(regs, Flag::z));
                 } },
        /* c3 */ { "jp {}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteJump(regs, mem, true);
                 } },
        /* c4 */ { "call nz,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteCall(regs, mem, flag::IsClear(regs, Flag::z));
                 } },
        /* c5 */ { "push bc", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto bc = detail::FuseRegisters(regs.b, regs.c);
                    detail::Push_u16(regs, mem, bc);
                    return 16;
                 } },
        /* c6 */ { "add a,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    detail::Add_r8(regs, regs.a, v);
                    return 8;
                 } },
        /* c7 */ { "rst 0", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Rst(regs, mem, 0);
                 } },
        /* c8 */ { "ret z", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeReturn(regs, mem, flag::IsSet(regs, Flag::z));
                 } },
        /* c9 */ { "ret", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::HandleRelativeReturn(regs, mem, true);
                    return 16;
                 } },
        /* ca */ { "jp z,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteJump(regs, mem, flag::IsSet(regs, Flag::z));
                 } },
        /* cb */ { "<cb>", Argument::None, [](Registers& regs, Memory& mem) {
                    return 4;
                 } },
        /* cc */ { "call z,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteCall(regs, mem, flag::IsSet(regs, Flag::z));
                 } },
        /* cd */ { "call {}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteCall(regs, mem, true);
                 } },
        /* ce */ { "adc a,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    detail::Adc_r8(regs, regs.a, v);
                    return 8;
                 } },
        /* cf */ { "rst 08h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Rst(regs, mem, 0x08);
                 } },
        /* d0 */ { "ret nc", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeReturn(regs, mem, flag::IsClear(regs, Flag::c));
                 } },
        /* d1 */ { "pop de", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto v = detail::Pop_u16(regs, mem);
                    detail::DivideRegisters(v, regs.d, regs.e);
                    return 12;
                 } },
        /* d2 */ { "jp nc,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteJump(regs, mem, flag::IsClear(regs, Flag::c));
                 } },
        /* d3 */ { "<inv_d3>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* d4 */ { "call nc,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteCall(regs, mem, flag::IsClear(regs, Flag::c));
                 } },
        /* d5 */ { "push de", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto de = detail::FuseRegisters(regs.d, regs.e);
                    detail::Push_u16(regs, mem, de);
                    return 16;
                 } },
        /* d6 */ { "sub {}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    detail::Sub_r8(regs, regs.a, v);
                    return 8;
                 } },
        /* d7 */ { "rst 10h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Rst(regs, mem, 0x10);
                 } },
        /* d8 */ { "ret c", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::HandleRelativeReturn(regs, mem, flag::IsSet(regs, Flag::c));
                 } },
        /* d9 */ { "reti", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::HandleRelativeReturn(regs, mem, true);
                    regs.ime = true;
                    return 16;
                 } },
        /* da */ { "jp c,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteJump(regs, mem, flag::IsSet(regs, Flag::c));
                 } },
        /* db */ { "<inv_db>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* dc */ { "call c,{}", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    return detail::HandleAbsoluteCall(regs, mem, flag::IsSet(regs, Flag::c));
                 } },
        /* dd */ { "<inv_dd>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* de */ { "sbc a,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    detail::Sbc_r8(regs, regs.a, v);
                    return 8;
                 } },
        /* df */ { "rst 18h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Rst(regs, mem, 0x18);
                 } },
        /* e0 */ { "ldh ({}),a", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const uint16_t addr = 0xff00 + detail::ReadAndAdvancePC_u8(regs, mem);
                    mem.Write_u8(addr, regs.a);
                    return 12;
                 } },
        /* e1 */ { "pop hl", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto v = detail::Pop_u16(regs, mem);
                    detail::DivideRegisters(v, regs.h, regs.l);
                    return 12;
                 } },
        /* e2 */ { "ld (ff00+c),a", Argument::None, [](Registers& regs, Memory& mem) {
                    const uint16_t addr = 0xff00 + regs.c;
                    mem.Write_u8(addr, regs.a);
                    return 8;
                 } },
        /* e3 */ { "<inv_e3>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* e4 */ { "<inv_e4>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* e5 */ { "push hl", Argument::None, [](Registers& regs, Memory& mem) {
                    const auto hl = detail::FuseRegisters(regs.h, regs.l);
                    detail::Push_u16(regs, mem, hl);
                    return 16;
                 } },
        /* e6 */ { "and {}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    detail::And_A_r8(regs, v);
                    return 8;
                 } },
        /* e7 */ { "rst 20h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Rst(regs, mem, 0x20);
                 } },
        /* e8 */ { "add sp,{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    flag::Assign(regs, Flag::h, detail::CalculateHalfCarry_u8(regs.sp, v, 0, std::plus<>()));
                    flag::Assign(regs, Flag::c, ((regs.sp & 0xff) + v) > 0xff);
                    flag::Clear(regs, Flag::z);
                    flag::Clear(regs, Flag::n);
                    regs.sp = regs.sp + static_cast<int8_t>(v);
                    return 16;
                 } },
        /* e9 */ { "jp (hl)", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.pc = detail::FuseRegisters(regs.h, regs.l);
                    return 4;
                 } },
        /* ea */ { "ld ({}),a", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    const auto addr = detail::ReadAndAdvancePC_u16(regs, mem);
                    mem.Write_u8(addr, regs.a);
                    return 16;
                 } },
        /* eb */ { "<inv_eb>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* ec */ { "<inv_ec>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* ed */ { "<inv_ed>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* ee */ { "xor {}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    detail::Xor_A_r8(regs, v);
                    return 8;
                 } },
        /* ef */ { "rst 28h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Rst(regs, mem, 0x28);
                 } },
        /* f0 */ { "ldh a,({})", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const uint16_t addr = 0xff00 + detail::ReadAndAdvancePC_u8(regs, mem);
                    regs.a = mem.Read_u8(addr);
                    return 12;
                 } },
        /* f1 */ { "pop af", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.fl = detail::Pop_u8(regs, mem) & flag::mask;
                    regs.a = detail::Pop_u8(regs, mem);
                    return 12;
                 } },
        /* f2 */ { "ld a,(ff00+c)", Argument::None, [](Registers& regs, Memory& mem) {
                    const uint16_t addr = 0xff00 + regs.c;
                    regs.a = mem.Read_u8(addr);
                    return 8;
                 } },
        /* f3 */ { "di", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.ime = false;
                    return 4;
                 } },
        /* f4 */ { "<inv_f4>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* f5 */ { "push af", Argument::None, [](Registers& regs, Memory& mem) {
                    detail::Push_u8(regs, mem, regs.a);
                    detail::Push_u8(regs, mem, regs.fl);
                    return 16;
                 } },
        /* f6 */ { "or {}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    detail::Or_A_r8(regs, v);
                    return 8;
                 } },
        /* f7 */ { "rst 30h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Rst(regs, mem, 0x30);
                 } },
        /* f8 */ { "ld hl,sp+{}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    flag::Assign(regs, Flag::h, detail::CalculateHalfCarry_u8(regs.sp, v, 0, std::plus<>()));
                    flag::Assign(regs, Flag::c, ((regs.sp & 0xff) + v) > 0xff);
                    flag::Clear(regs, Flag::z);
                    flag::Clear(regs, Flag::n);
                    const auto hl = regs.sp + static_cast<int8_t>(v);
                    detail::DivideRegisters(hl, regs.h, regs.l);
                    return 12;
                 } },
        /* f9 */ { "ld sp,hl", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.sp = detail::FuseRegisters(regs.h, regs.l);
                    return 8;
                 } },
        /* fa */ { "ld a,({})", Argument::Imm16, [](Registers& regs, Memory& mem) {
                    const auto addr = detail::ReadAndAdvancePC_u16(regs, mem);
                    regs.a = mem.Read_u8(addr);
                    return 16;
                 } },
        /* fb */ { "ei", Argument::None, [](Registers& regs, Memory& mem) {
                    regs.ime = true;
                    return 4;
                 } },
        /* fc */ { "<inv_fc>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* fd */ { "<inv_fd>", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::InvalidInstruction(regs, mem);
                 } },
        /* fe */ { "cp {}", Argument::Imm8, [](Registers& regs, Memory& mem) {
                    const auto v = detail::ReadAndAdvancePC_u8(regs, mem);
                    detail::Cp_A_r8(regs, v);
                    return 8;
                 } },
        /* ff */ { "rst 38h", Argument::None, [](Registers& regs, Memory& mem) {
                    return detail::Rst(regs, mem, 0x38);
                 } },
    } };
}
