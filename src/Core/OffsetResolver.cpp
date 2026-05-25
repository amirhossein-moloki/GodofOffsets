#include "Core/OffsetResolver.h"
#include <iostream>

namespace Core {

OffsetResolver::OffsetResolver(const MemoryManager& mm) : m_mm(mm) {
    ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
}

uintptr_t OffsetResolver::ResolveRelativeAddress(uintptr_t instructionAddress, int offsetIndex, int instructionSize) {
    int32_t relativeOffset = m_mm.Read<int32_t>(instructionAddress + offsetIndex);
    return instructionAddress + instructionSize + relativeOffset;
}

uintptr_t OffsetResolver::ResolveWithZydis(uintptr_t address) {
    uint8_t buffer[15]; // Max x86 instruction length
    if (!m_mm.ReadRaw(address, buffer, sizeof(buffer))) return 0;

    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&m_decoder, buffer, sizeof(buffer), &instruction, operands))) {
        for (int i = 0; i < instruction.operand_count; ++i) {
            if (operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                operands[i].mem.base == ZYDIS_REGISTER_RIP) {

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
