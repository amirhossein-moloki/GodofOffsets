#include "Core/OffsetResolver.h"
#include <iostream>

namespace Core {

OffsetResolver::OffsetResolver(const ProcessManager& pm) : m_pm(pm) {
    ZydisDecoderInit(&m_decoder64, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecoderInit(&m_decoder32, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
}

uintptr_t OffsetResolver::ResolveRelativeAddress(uintptr_t instructionAddress, int offsetIndex, int instructionSize) {
    int32_t relativeOffset = m_pm.Read<int32_t>(instructionAddress + offsetIndex);
    return instructionAddress + instructionSize + relativeOffset;
}

uintptr_t OffsetResolver::ResolveWithZydis(uintptr_t address) {
    uint8_t buffer[15]; // Max x86 instruction length
    if (!m_pm.ReadMemory(address, buffer, sizeof(buffer))) return 0;

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    // Determine target architecture
    bool is64Bit = true;
    auto processes = ProcessManager::GetProcessList();
    for (const auto& p : processes) {
        if (p.pid == m_pm.GetPid()) {
            is64Bit = p.is64Bit;
            break;
        }
    }

    ZydisDecoder* decoder = is64Bit ? &m_decoder64 : &m_decoder32;

    if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(decoder, buffer, sizeof(buffer), &instruction, operands))) {
        for (int i = 0; i < instruction.operand_count; ++i) {
            if (operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                operands[i].mem.base == (is64Bit ? ZYDIS_REGISTER_RIP : ZYDIS_REGISTER_EIP)) {

                ZyanU64 targetAddress;
                ZydisCalcAbsoluteAddress(&instruction, &operands[i], address, &targetAddress);
                return (uintptr_t)targetAddress;
            }

            if (operands[i].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operands[i].imm.is_relative) {
                ZyanU64 targetAddress;
                ZydisCalcAbsoluteAddress(&instruction, &operands[i], address, &targetAddress);
                return (uintptr_t)targetAddress;
            }
        }
    }

    return 0;
}

} // namespace Core
