#pragma once
#include <windows.h>
#include <Zydis/Zydis.h>
#include "Core/MemoryManager.h"

namespace Core {

class OffsetResolver {
public:
    OffsetResolver(const MemoryManager& mm);

    // Resolves RIP-relative addresses (e.g., from LEA, MOV, CALL)
    uintptr_t ResolveRelativeAddress(uintptr_t instructionAddress, int offsetIndex, int instructionSize);

    // Advanced resolution using Zydis disassembler
    uintptr_t ResolveWithZydis(uintptr_t address);

private:
    const MemoryManager& m_mm;
    ZydisDecoder m_decoder;
};

} // namespace Core
