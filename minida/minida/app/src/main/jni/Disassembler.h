#pragma once
#include "Types.h"

class Disassembler {
private:
    csh handle = 0;
    const uint8_t* codeData = nullptr;
    size_t codeSize = 0;
    uint64_t codeAddress = 0;
    bool initialized = false;
    cs_arch currentArch = CS_ARCH_AARCH64;  // 当前架构
    cs_mode currentMode = CS_MODE_LITTLE_ENDIAN;  // 当前模式

public:
    bool Initialize(cs_arch arch = CS_ARCH_AARCH64, cs_mode mode = CS_MODE_LITTLE_ENDIAN);
    void Shutdown();
    bool Disassemble(const void* code, size_t size, uint64_t address,
                     std::vector<AsmInstruction>& instructions, size_t maxInstructions = 1000);
    bool IsInitialized() const;
    cs_arch GetCurrentArch() const { return currentArch; }

    // 根据ELF machine字段自动选择架构
    static cs_arch GetArchForMachine(uint16_t machine);
    static cs_mode GetModeForMachine(uint16_t machine);
};
