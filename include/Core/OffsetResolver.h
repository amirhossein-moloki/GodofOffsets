#pragma once
#include <windows.h>
#include <Zydis/Zydis.h>
#include "Core/ProcessManager.h"

namespace Core {

class OffsetResolver {
public:
    OffsetResolver(const ProcessManager& pm);

    // Resolves RIP-relative addresses (e.g., from LEA, MOV, CALL)
    uintptr_t ResolveRelativeAddress(uintptr_t instructionAddress, int offsetIndex, int instructionSize);

    // Advanced resolution using Zydis disassembler
    uintptr_t ResolveWithZydis(uintptr_t address);

private:
    const ProcessManager& m_pm;
    ZydisDecoder m_decoder64;
    ZydisDecoder m_decoder32;
};

} // namespace Core
