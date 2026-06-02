#include "Disassembler.h"
#include "Common.h"

// ============================================================================
// 高性能反汇编器（支持多架构：AArch64, ARM, x86, x86_64）
// ============================================================================

cs_arch Disassembler::GetArchForMachine(uint16_t machine) {
    switch (machine) {
        case EM_AARCH64: return CS_ARCH_AARCH64;
        case EM_ARM:     return CS_ARCH_ARM;
        case EM_386:     return CS_ARCH_X86;
        case EM_X86_64:  return CS_ARCH_X86;
        case EM_MIPS:    return CS_ARCH_MIPS;
        case EM_RISCV:   return CS_ARCH_RISCV;
        default:         return CS_ARCH_AARCH64;  // 默认ARM64
    }
}

cs_mode Disassembler::GetModeForMachine(uint16_t machine) {
    switch (machine) {
        case EM_AARCH64: return CS_MODE_LITTLE_ENDIAN;
        case EM_ARM:     return (cs_mode)(CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN);
        case EM_386:     return CS_MODE_32;
        case EM_X86_64:  return CS_MODE_64;
        case EM_MIPS:    return (cs_mode)(CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN);
        case EM_RISCV:   return (cs_mode)(CS_MODE_RISCV64 | CS_MODE_LITTLE_ENDIAN);
        default:         return CS_MODE_LITTLE_ENDIAN;
    }
}

bool Disassembler::Initialize(cs_arch arch, cs_mode mode) {
    Shutdown();

    currentArch = arch;
    currentMode = mode;

    cs_err err = cs_open(arch, mode, &handle);
    if (err != CS_ERR_OK) {
        LOGE("Failed to initialize capstone: %s", cs_strerror(err));
        return false;
    }

    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
    cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);

    initialized = true;
    return true;
}

void Disassembler::Shutdown() {
    if (handle != 0) {
        cs_close(&handle);
        handle = 0;
    }
    initialized = false;
}

bool Disassembler::Disassemble(const void* code, size_t size, uint64_t address,
                                std::vector<AsmInstruction>& instructions, size_t maxInstructions) {
    if (!initialized || !code || size == 0) return false;

    instructions.clear();
    
    // 只调用一次 cs_disasm，让 capstone 自己分配内存
    cs_insn* insn = nullptr;
    size_t count = cs_disasm(handle, (const uint8_t*)code, size, address, maxInstructions, &insn);
    if (count == 0 || insn == nullptr) {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        AsmInstruction inst;
        inst.address = insn[i].address;
        inst.bytes = insn[i].size;
        inst.mnemonic = insn[i].mnemonic;
        inst.operands = insn[i].op_str;
        inst.insnId = insn[i].id;

        // 构建字节字符串 - 只显示实际指令长度的字节
        inst.bytesStr.clear();
        for (size_t j = 0; j < insn[i].size; j++) {
            if (j > 0) inst.bytesStr += " ";
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X", insn[i].bytes[j]);
            inst.bytesStr += buf;
        }

        // 分析指令组
        inst.isJump = cs_insn_group(handle, &insn[i], CS_GRP_JUMP);
        inst.isCall = cs_insn_group(handle, &insn[i], CS_GRP_CALL);
        inst.isBranch = inst.isJump || inst.isCall;
        inst.hasDetail = (insn[i].detail != nullptr);

        // 提取分支目标地址（支持多架构）
        if (inst.hasDetail && inst.isBranch && insn[i].detail) {
            const cs_detail* detail = insn[i].detail;

            if (currentArch == CS_ARCH_AARCH64) {
                for (int k = 0; k < detail->aarch64.op_count; k++) {
                    const cs_aarch64_op& op = detail->aarch64.operands[k];
                    if (op.type == AARCH64_OP_IMM) {
                        inst.branchTarget = op.imm;
                        break;
                    }
                }
            } else if (currentArch == CS_ARCH_ARM) {
                for (int k = 0; k < detail->arm.op_count; k++) {
                    const cs_arm_op& op = detail->arm.operands[k];
                    if (op.type == ARM_OP_IMM) {
                        inst.branchTarget = op.imm;
                        break;
                    }
                }
            } else if (currentArch == CS_ARCH_X86) {
                for (int k = 0; k < detail->x86.op_count; k++) {
                    const cs_x86_op& op = detail->x86.operands[k];
                    if (op.type == X86_OP_IMM) {
                        inst.branchTarget = op.imm;
                        break;
                    }
                }
            }
        }

        instructions.push_back(inst);
    }
    cs_free(insn, count);
    return true;
}

bool Disassembler::IsInitialized() const { return initialized; }

Disassembler g_Disassembler;

// ============================================================================
// AsmViewState 方法实现
// ============================================================================

void AsmViewState::Reset() {
    currentAddress = 0;
    instructions.clear();
    selectedIndex = -1;
    initialized = false;
    longPressStartTime = 0;
    longPressSelectedIndex = -1;
    showContextMenu = false;
    currentFunctionStart = 0;
    currentFunctionEnd = 0;
    branchTargets.clear();
    handle = 0;
}

bool AsmViewState::IsLongPress(int64_t currentTime, int index) {
    return (longPressSelectedIndex == index &&
            currentTime - longPressStartTime >= longPressDelay);
}

void AsmViewState::StartLongPress(int64_t currentTime, int index) {
    if (longPressSelectedIndex != index) {
        longPressStartTime = currentTime;
        longPressSelectedIndex = index;
        showContextMenu = false;
    }
}

void AsmViewState::EndLongPress() {
    longPressStartTime = 0;
    longPressSelectedIndex = -1;
    if (!showContextMenu) {
        showContextMenu = false;
    }
}

void AsmViewState::SetContextMenuVisible(bool visible) {
    showContextMenu = visible;
}

uint64_t AsmViewState::GetSelectedAddress() const {
    if (longPressSelectedIndex >= 0 &&
        longPressSelectedIndex < (int)instructions.size()) {
        return instructions[longPressSelectedIndex].address;
    }
    if (selectedIndex >= 0 && selectedIndex < (int)instructions.size()) {
        return instructions[selectedIndex].address;
    }
    return currentAddress;
}

// ============================================================================
// 虚拟地址到文件偏移转换（使用Program Headers）
// ============================================================================

uint64_t VirtualAddressToFileOffset(const ElfParser& parser, uint64_t virtualAddr) {
    if (!parser.IsLoaded()) return 0;

    const auto& programs = parser.GetPrograms();
    const auto& sections = parser.GetSections();

    // 首先尝试使用Program Headers（更准确）
    for (const ProgramInfo& prog : programs) {
        if (prog.type == PT_LOAD && prog.filesz > 0) {
            if (virtualAddr >= prog.vaddr && virtualAddr < prog.vaddr + prog.memsz) {
                return prog.offset + (virtualAddr - prog.vaddr);
            }
        }
    }

    // 如果Program Headers找不到，尝试使用Section Headers作为后备
    for (const SectionInfo& sec : sections) {
        if (sec.size > 0 && sec.addr > 0) {
            if (virtualAddr >= sec.addr && virtualAddr < sec.addr + sec.size) {
                return sec.offset + (virtualAddr - sec.addr);
            }
        }
    }

    // 如果都不匹配，尝试直接使用地址（可能是文件偏移伪装成VA）
    if (virtualAddr < parser.GetFileSize()) {
        return virtualAddr;
    }

    return 0;
}

// ============================================================================
// 估计函数大小
// ============================================================================

size_t EstimateFunctionSize(const ElfParser& parser, uint64_t funcAddr, size_t defaultSize) {
    if (!parser.IsLoaded()) return defaultSize;

    const auto& exports = parser.GetExports();

    // 查找下一个函数来确定当前函数的大小
    for (size_t i = 0; i < exports.size(); i++) {
        if (exports[i].address > funcAddr) {
            // 找到下一个函数，计算差值
            size_t size = exports[i].address - funcAddr;
            // 限制最大大小，避免读取过多数据
            return std::min(size, (size_t)512);
        }
    }

    // 如果是最后一个函数，查找.text节区末尾
    const auto& sections = parser.GetSections();
    for (const SectionInfo& sec : sections) {
        if (sec.name == ".text" && sec.addr > 0) {
            if (funcAddr >= sec.addr && funcAddr < sec.addr + sec.size) {
                size_t remaining = sec.addr + sec.size - funcAddr;
                return std::min(remaining, (size_t)512);
            }
        }
    }

    return defaultSize;
}

// ============================================================================
// 启动函数反汇编（修复版：正确映射虚拟地址到文件偏移）
// ============================================================================

void StartDisassembly(const FunctionExport& exp) {
    if (!elfParser.IsLoaded()) {
        g_UI.AddLog("警告: 未加载ELF文件");
        return;
    }
    // 安全检查：确保函数名有效
    if (exp.name.empty() || exp.name.data() == nullptr) {
        g_UI.AddLog("警告: 函数名为空");
        return;
    }


    if (!g_Disassembler.IsInitialized()) {
        uint16_t machine = elfParser.GetHeaderInfo().machine;
        cs_arch arch = Disassembler::GetArchForMachine(machine);
        cs_mode mode = Disassembler::GetModeForMachine(machine);
        if (!g_Disassembler.Initialize(arch, mode)) {
            g_UI.AddLog("Error: 初始化反汇编器失败");
            return;
        }
    }

    g_AsmView.Reset();
    g_AsmView.currentAddress = exp.address;
    g_Layout.currentAsmAddress = exp.address;
    g_Layout.currentFunctionName = std::string(exp.name);
    g_Layout.showAsmView = true;

    // 重置上下文菜单状态
    g_Layout.showAsmContextMenu = false;
    g_Layout.contextMenuOpenTime = 0;

    // 使用正确的方法将虚拟地址转换为文件偏移
    uint64_t fileOffset = VirtualAddressToFileOffset(elfParser, exp.address);

    if (fileOffset == 0) {
        g_UI.AddLog("Warning: 无法将地址 0x%08lX 转换为文件偏移", (unsigned long)exp.address);
        return;
    }

    // 估计函数大小
    size_t funcSize = EstimateFunctionSize(elfParser, exp.address);

    // 检查偏移是否有效
    if (fileOffset >= elfParser.GetFileSize()) {
        g_UI.AddLog("Warning: 文件偏移 0x%08lX 超出文件范围", (unsigned long)fileOffset);
        return;
    }

    // 计算要读取的实际大小
    size_t maxReadSize = elfParser.GetFileSize() - fileOffset;
    size_t readSize = std::min(funcSize, maxReadSize);
    readSize = std::min(readSize, (size_t)4096); // 限制最大读取大小
    // 安全检查：确保数据指针有效
    const uint8_t* fileData = elfParser.GetFileData();
    if (fileData == nullptr) {
        g_UI.AddLog("Error: 文件数据指针为空");
        return;
    }


    if (readSize == 0) {
        g_UI.AddLog("Warning: 无法读取函数 %s 的代码数据", exp.name.data());
        return;
    }

    // 获取代码数据
    const uint8_t* codePtr = elfParser.GetFileData() + fileOffset;

    if (codePtr == nullptr) {
        g_UI.AddLog("Warning: 无法获取文件数据指针");
        return;
    }

    // 执行反汇编
    if (g_Disassembler.Disassemble(codePtr, readSize, exp.address, g_AsmView.instructions, 500)) {
        g_AsmView.initialized = true;
        // 保存 capstone handle 到 AsmViewState 用于后续查询
        g_AsmView.handle = g_Disassembler.IsInitialized() ? 1 : 0; // 标记已初始化
        // 设置函数范围（用于交叉引用分析和CFG）
        g_AsmView.currentFunctionStart = exp.address;
        if (!g_AsmView.instructions.empty()) {
            g_AsmView.currentFunctionEnd = g_AsmView.instructions.back().address \
                                   + g_AsmView.instructions.back().bytes;
        } else {
            g_AsmView.currentFunctionEnd = exp.address + 256;
        }
        g_UI.AddLog("反汇编函数: %s at VA=0x%08lX, FOA=0x%08lX (%zu 条指令)",
            exp.name.data(), (unsigned long)exp.address, (unsigned long)fileOffset, g_AsmView.instructions.size());
    } else {
        g_UI.AddLog("Warning: 无法反汇编函数 %s (尝试从偏移 0x%08lX 读取 %zu 字节)",
            exp.name.data(), (unsigned long)fileOffset, readSize);

        // 尝试另一种方法：从入口点开始读取更大的区域
        const ElfHeaderInfo& header = elfParser.GetHeaderInfo();
        uint64_t entryOffset = VirtualAddressToFileOffset(elfParser, header.entryPoint);

        if (entryOffset > 0 && entryOffset < elfParser.GetFileSize()) {
            size_t entryReadSize = std::min((size_t)8192, elfParser.GetFileSize() - entryOffset);
            if (g_Disassembler.Disassemble(elfParser.GetFileData() + entryOffset, entryReadSize,
                    header.entryPoint, g_AsmView.instructions, 500)) {
                g_AsmView.initialized = true;
                g_Layout.currentAsmAddress = header.entryPoint;
                // 设置函数范围（用于交叉引用分析和CFG）
                g_AsmView.currentFunctionStart = header.entryPoint;
                if (!g_AsmView.instructions.empty()) {
                    g_AsmView.currentFunctionEnd = g_AsmView.instructions.back().address \
                                           + g_AsmView.instructions.back().bytes;
                } else {
                    g_AsmView.currentFunctionEnd = header.entryPoint + 256;
                }
                g_Layout.currentFunctionName = "entry_point";
                g_UI.AddLog("已切换到入口点反汇编 (%zu 条指令)", g_AsmView.instructions.size());
            }
        }
    }
}
