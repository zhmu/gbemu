#pragma once

#include "types.h"
#include <string>

namespace gb::cartridge {

void Load(const std::string& path);
uint8_t Read_u8(const Address address);
void Write_u8(const Address address, uint8_t value);
void SetTracing(const bool enabled);

}
