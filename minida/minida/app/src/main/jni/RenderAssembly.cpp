#include "Common.h"

// ============================================================================
// 汇编代码上下文菜单（长按弹出）- 修复版：使用保存的字段
// ============================================================================

void RenderAsmContextMenu(uint64_t address) {
    if (ImGui::BeginPopup("AsmContextMenu",
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            g_Layout.showAsmContextMenu) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 popup_pos = ImGui::GetWindowPos();
            ImVec2 popup_size = ImGui::GetWindowSize();
            if (mouse_pos.x < popup_pos.x ||
                mouse_pos.x > popup_pos.x + popup_size.x ||
                mouse_pos.y < popup_pos.y ||
                mouse_pos.y > popup_pos.y + popup_size.y) {
                if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
                    g_Layout.showAsmContextMenu = false;
                }
            }
        }
        char addrStr[32];
        snprintf(addrStr, sizeof(addrStr), "0x%llx", (unsigned long long)address);

        ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "≡ 代码导航");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "地址: %s", addrStr);
        ImGui::Separator();

        if (ImGui::BeginTabBar("ContextTabBar", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("📋 函数信息###FuncInfo")) {
                ImGui::Indent();
                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "当前函数:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.40f, 0.80f, 0.60f, 1.0f), "%s",
                    g_Layout.currentFunctionName.empty() ? "(未知)" : g_Layout.currentFunctionName.c_str());
                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "当前地址:");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.90f, 0.90f, 0.50f, 1.0f), "%s", addrStr);
                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "指令数量:");
                ImGui::SameLine();
                ImGui::Text("%zu 条", g_AsmView.instructions.size());

                const AsmInstruction* curInst = nullptr;
                for (const auto& inst : g_AsmView.instructions) {
                    if (inst.address == address) { curInst = &inst; break; }
                }
                if (curInst) {
                    ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "操作码:");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.90f, 1.0f), "%s", curInst->mnemonic.c_str());
                    if (curInst->isBranch) {
                        ImGui::TextColored(ImVec4(0.90f, 0.60f, 0.40f, 1.0f), "⚠ 分支指令");
                        if (curInst->branchTarget != 0) {
                            char targetStr[32];
                            snprintf(targetStr, sizeof(targetStr), "0x%llx", (unsigned long long)curInst->branchTarget);
                            ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "目标地址:");
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.90f, 0.50f, 0.50f, 1.0f), "%s", targetStr);
                        }
                    }
                }
                ImGui::Unindent();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("🔀 控制流CFG###CFG")) {
                ImGui::Indent();
                ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.40f, 1.0f), "分析跳转目标和被引用来源");
                ImGui::Spacing();

                std::vector<uint64_t> branchTargets;
                std::vector<uint64_t> sourceRefs;

                if (!g_AsmView.instructions.empty()) {
                    uint64_t funcStart = g_AsmView.instructions.front().address;
                    uint64_t funcEnd = g_AsmView.instructions.back().address;

                    for (const auto& inst : g_AsmView.instructions) {
                        if (!inst.isBranch) continue;
                        uint64_t target = inst.branchTarget;
                        if (target != 0) {
                            if (target >= funcStart && target <= funcEnd + 0x1000) {
                                if (std::find(branchTargets.begin(), branchTargets.end(), target) == branchTargets.end())
                                    branchTargets.push_back(target);
                                if (target == address && inst.address != address) {
                                    if (std::find(sourceRefs.begin(), sourceRefs.end(), inst.address) == sourceRefs.end())
                                        sourceRefs.push_back(inst.address);
                                }
                            }
                            if (target != address && inst.address != address) {
                                if (target >= funcStart && target <= funcEnd) {
                                    uint64_t nextAddr = inst.address + inst.bytes;
                                    if (nextAddr >= funcStart && nextAddr <= funcEnd) {
                                        if (std::find(branchTargets.begin(), branchTargets.end(), nextAddr) == branchTargets.end())
                                            branchTargets.push_back(nextAddr);
                                    }
                                }
                            }
                        }
                    }
                    for (const auto& inst : g_AsmView.instructions) {
                        if (inst.address == address) continue;
                        if (!inst.isBranch) continue;
                        if (inst.branchTarget == address) {
                            if (std::find(sourceRefs.begin(), sourceRefs.end(), inst.address) == sourceRefs.end())
                                sourceRefs.push_back(inst.address);
                        }
                    }
                }

                std::sort(branchTargets.begin(), branchTargets.end());
                branchTargets.erase(std::unique(branchTargets.begin(), branchTargets.end()), branchTargets.end());
                std::sort(sourceRefs.begin(), sourceRefs.end());
                sourceRefs.erase(std::unique(sourceRefs.begin(), sourceRefs.end()), sourceRefs.end());

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "跳转目标:");
                if (!branchTargets.empty()) {
                    for (uint64_t target : branchTargets) {
                        char targetStr[32];
                        snprintf(targetStr, sizeof(targetStr), "0x%llx", (unsigned long long)target);
                        std::string mnemonic;
                        for (const auto& inst : g_AsmView.instructions) {
                            if (inst.address == target) { mnemonic = inst.mnemonic; break; }
                        }
                        ImGui::TextColored(ImVec4(0.90f, 0.50f, 0.50f, 1.0f), "  → %s", targetStr);
                        if (!mnemonic.empty()) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.90f, 1.0f), "(%s)", mnemonic.c_str());
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.50f, 1.0f), "  (无直接跳转目标)");
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "来源引用:");
                if (!sourceRefs.empty()) {
                    for (uint64_t src : sourceRefs) {
                        char srcStr[32];
                        snprintf(srcStr, sizeof(srcStr), "0x%llx", (unsigned long long)src);
                        std::string mnemonic;
                        for (const auto& inst : g_AsmView.instructions) {
                            if (inst.address == src) { mnemonic = inst.mnemonic; break; }
                        }
                        ImGui::TextColored(ImVec4(0.50f, 0.90f, 0.50f, 1.0f), "  ← %s", srcStr);
                        if (!mnemonic.empty()) {
                            ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.90f, 1.0f), "(%s)", mnemonic.c_str());
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.50f, 1.0f), "  (无来源引用)");
                }

                bool xrefSafe = elfParser.IsLoaded() && elfParser.GetFileData() != nullptr;
                uint64_t currentFuncStart = g_AsmView.currentFunctionStart;
                uint64_t currentFuncEnd = g_AsmView.currentFunctionEnd;
                std::vector<XrefEntry> globalXrefsTo;
                std::vector<XrefEntry> globalXrefsFrom;

                if (xrefSafe) {
                    {
                        const auto& allExports = elfParser.GetExports();
                        for (const auto& exp : allExports) {
                            if (exp.address == currentFuncStart) continue;
                            uint64_t fileOffset = VirtualAddressToFileOffset(elfParser, exp.address);
                            if (fileOffset == 0) continue;
                            size_t funcSize = EstimateFunctionSize(elfParser, exp.address, 256);
                            const uint8_t* rawData = elfParser.GetFileData();
                            if (rawData == nullptr || fileOffset >= elfParser.GetFileSize()) continue;
                            const uint8_t* codeData = rawData + fileOffset;
                            std::vector<AsmInstruction> tempInsts;
                            if (g_Disassembler.Disassemble(codeData, funcSize, exp.address, tempInsts, 500)) {
                                for (const auto& inst : tempInsts) {
                                    if (inst.isCall && inst.branchTarget >= currentFuncStart && inst.branchTarget < currentFuncEnd) {
                                        XrefEntry entry;
                                        entry.fromAddr = inst.address;
                                        entry.toAddr = inst.branchTarget;
                                        entry.type = XrefType::Code;
                                        entry.fromFuncName = std::string(exp.name);
                                        entry.toFuncName = g_Layout.currentFunctionName;
                                        entry.instruction = inst.mnemonic + " " + inst.operands;
                                        globalXrefsTo.push_back(entry);
                                    }
                                }
                            }
                        }
                    }
                    {
                        const auto& allExports = elfParser.GetExports();
                        const auto& allImports = elfParser.GetImports();
                        for (const auto& inst : g_AsmView.instructions) {
                            if (inst.isCall && inst.branchTarget != 0) {
                                if (inst.branchTarget < currentFuncStart || inst.branchTarget >= currentFuncEnd) {
                                    std::string targetName = "unknown";
                                    for (const auto& exp : allExports) {
                                        if (exp.address == inst.branchTarget) { targetName = std::string(exp.name); break; }
                                    }
                                    for (const auto& imp : allImports) {
                                        if (imp.address == inst.branchTarget) { targetName = imp.library + "::" + imp.function; break; }
                                    }
                                    XrefEntry entry;
                                    entry.fromAddr = inst.address;
                                    entry.toAddr = inst.branchTarget;
                                    entry.type = XrefType::Code;
                                    entry.fromFuncName = g_Layout.currentFunctionName;
                                    entry.toFuncName = targetName;
                                    entry.instruction = inst.mnemonic + " " + inst.operands;
                                    globalXrefsFrom.push_back(entry);
                                }
                            }
                        }
                    }
                } else {
                    ImGui::TextColored(ImVec4(1,0.5,0.5,1), "文件未加载或数据不可用");
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.80f, 0.65f, 0.45f, 1.0f), "全局调用者 (XREF TO):");
                if (globalXrefsTo.empty()) {
                    ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.50f, 1.0f), "  (无全局调用者)");
                } else {
                    for (const auto& xref : globalXrefsTo) {
                        ImGui::TextColored(ImVec4(0.30f, 1.0f, 0.30f, 1.0f), "  %s", xref.fromFuncName.c_str());
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.70f, 1.0f), "@ 0x%llX", (unsigned long long)xref.fromAddr);
                    }
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.80f, 0.65f, 0.45f, 1.0f), "调用目标 (XREF FROM):");
                if (globalXrefsFrom.empty()) {
                    ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.50f, 1.0f), "  (无外部调用)");
                } else {
                    for (const auto& xref : globalXrefsFrom) {
                        ImGui::TextColored(ImVec4(0.30f, 0.80f, 1.0f, 1.0f), "  %s", xref.toFuncName.c_str());
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.70f, 1.0f), "@ 0x%llX", (unsigned long long)xref.toAddr);
                    }
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.60f, 0.80f, 0.60f, 1.0f), "CFG统计:");
                ImGui::Text("  分支目标: %zu 个", branchTargets.size());
                ImGui::Text("  来源引用: %zu 个", sourceRefs.size());

                if (!g_AsmView.instructions.empty() || !branchTargets.empty() || !sourceRefs.empty()) {
                    ImGui::Spacing();
                    if (ImGui::Button("📈 打开CFG视图", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                        uint64_t funcAddr = g_AsmView.instructions.empty() ? address : g_AsmView.instructions.front().address;
                        int tabIndex = g_CFGManager.CreateCFGView(funcAddr, g_Layout.currentFunctionName);
                        if (tabIndex >= 0 && tabIndex < g_CFGManager.GetTabCount()) {
                            CFGViewState& tab = g_CFGManager.tabs[tabIndex];
                            BuildCFGGraph(tab.graph, funcAddr, g_AsmView.instructions);
                            tab.valid = tab.graph.valid;
                            g_Layout.currentCFGTab = tabIndex;
                            g_Layout.showCFGView = true;
                        }
                        g_UI.AddLog("已打开CFG视图: 0x%llx", (unsigned long long)funcAddr);
                    }
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.8f), "提示: 双击主界面CFG标签可关闭");
                }
                ImGui::Unindent();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("❌ 关闭")) {
            g_Layout.showAsmContextMenu = false;
        }
        ImGui::EndPopup();
    }
}

// ============================================================================
// 辅助函数：获取助记符对应的颜色
// ============================================================================

static ImVec4 GetMnemonicColor(const std::string& mnemonic) {
    // 数据处理指令 - 蓝色
    if (mnemonic == "mov" || mnemonic == "movs" || mnemonic == "movw" || mnemonic == "movt")
        return ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
    // 分支指令 - 橙色
    if (mnemonic == "b" || mnemonic == "bl" || mnemonic == "bx" || mnemonic == "blx" ||
        mnemonic == "bne" || mnemonic == "beq" || mnemonic == "cbz" || mnemonic == "cbnz" ||
        mnemonic == "bxeq" || mnemonic == "bgt" || mnemonic == "blt" || mnemonic == "bge" ||
        mnemonic == "ble" || mnemonic == "bhi" || mnemonic == "bls" || mnemonic == "bcs" ||
        mnemonic == "bcc" || mnemonic == "bmi" || mnemonic == "bpl" || mnemonic == "bvs" ||
        mnemonic == "bvc" || mnemonic == "bal" || mnemonic == "b.eq" || mnemonic == "b.ne" ||
        mnemonic == "b.gt" || mnemonic == "b.lt" || mnemonic == "b.ge" || mnemonic == "b.le" ||
        mnemonic == "b.hi" || mnemonic == "b.ls" || mnemonic == "b.cs" || mnemonic == "b.cc" ||
        mnemonic == "b.mi" || mnemonic == "b.pl" || mnemonic == "ret" || mnemonic == "br" ||
        mnemonic == "blr" || mnemonic == "eret")
        return ImVec4(1.0f, 0.5f, 0.3f, 1.0f);
    // 加载/存储指令 - 黄色
    if (mnemonic == "ldr" || mnemonic == "str" || mnemonic == "ldm" || mnemonic == "stm" ||
        mnemonic == "ldrb" || mnemonic == "strb" || mnemonic == "ldrh" || mnemonic == "strh" ||
        mnemonic == "push" || mnemonic == "pop" || mnemonic == "ldp" || mnemonic == "stp" ||
        mnemonic == "ldrsw" || mnemonic == "ldxr" || mnemonic == "stxr" || mnemonic == "ldaxr" ||
        mnemonic == "stlxr")
        return ImVec4(0.8f, 0.6f, 0.2f, 1.0f);
    // 比较指令 - 浅绿
    if (mnemonic == "cmp" || mnemonic == "cmn" || mnemonic == "tst" || mnemonic == "teq" ||
        mnemonic == "ccmp" || mnemonic == "ccmn" || mnemonic == "fcmp")
        return ImVec4(0.6f, 0.8f, 0.6f, 1.0f);
    // NOP和特殊指令 - 灰色
    if (mnemonic == "nop" || mnemonic == "yield" || mnemonic == "wfe" || mnemonic == "wfi" ||
        mnemonic == "sev" || mnemonic == "dmb" || mnemonic == "dsb" || mnemonic == "isb" ||
        mnemonic == "clrex" || mnemonic == "udf")
        return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    // 算术指令 - 青色
    if (mnemonic == "add" || mnemonic == "sub" || mnemonic == "mul" || mnemonic == "udiv" ||
        mnemonic == "sdiv" || mnemonic == "madd" || mnemonic == "msub" || mnemonic == "adc" ||
        mnemonic == "sbc" || mnemonic == "rsb" || mnemonic == "rsc" || mnemonic == "and" ||
        mnemonic == "orr" || mnemonic == "eor" || mnemonic == "bic" || mnemonic == "lsl" ||
        mnemonic == "lsr" || mnemonic == "asr" || mnemonic == "ror" || mnemonic == "rrx" ||
        mnemonic == "neg" || mnemonic == "mvn")
        return ImVec4(0.5f, 0.9f, 0.9f, 1.0f);
    // 默认白色
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

// ============================================================================
// 辅助函数：渲染带颜色高亮的操作数字符串
// 寄存器用蓝色，立即数用橙色，内存引用用黄色
// ============================================================================

static void RenderColoredOperands(const std::string& operands) {
    if (operands.empty()) return;
    
    // 简化版本：直接显示操作数字符串，避免复杂的解析导致崩溃
    ImVec4 color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // 默认浅灰色
    
    // 检查是否包含内存引用
    if (operands.find('[') != std::string::npos) {
        color = ImVec4(0.9f, 0.9f, 0.4f, 1.0f); // 黄色表示内存引用
    }
    // 检查是否包含立即数
    else if (operands.find('#') != std::string::npos) {
        color = ImVec4(1.0f, 0.6f, 0.2f, 1.0f); // 橙色表示立即数
    }
    // 检查是否主要是寄存器
    else if (operands.find('r') != std::string::npos || 
             operands.find('x') != std::string::npos ||
             operands.find('w') != std::string::npos) {
        color = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); // 蓝色表示寄存器
    }
    
    ImGui::TextColored(color, "%s", operands.c_str());
}



// ============================================================================
// R2Droid风格专业反汇编视图
// ============================================================================

void RenderAssemblyView() {
    if (!g_AsmView.initialized || g_AsmView.instructions.empty()) {
        ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f),
            "未选择函数或无法反汇编 - 请从左侧Functions窗口选择一个导出函数");
        return;
    }

    // 函数信息头部
    ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f),
        "函数: %s", g_Layout.currentFunctionName.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 200 * g_Layout.uiScale);
    if (ImGui::Selectable(FormatAddressCompact(g_AsmView.currentAddress), false,
        ImGuiSelectableFlags_None, ImVec2(0, 0))) {
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), " 地址");
    ImGui::Separator();

    float tableHeight = ImGui::GetContentRegionAvail().y - g_Layout.rowHeight;

    ImGui::BeginChild("AsmScroll", ImVec2(0, tableHeight), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    int64_t currentTime = ImGui::GetTime() * 1000;

    ImGuiListClipper clipper;
    clipper.Begin(g_AsmView.instructions.size(), g_Layout.rowHeight * 0.8f);

    // 预计算交叉引用信息
    std::map<uint64_t, std::vector<uint64_t>> xrefMap;
    for (const auto& inst : g_AsmView.instructions) {
        if (inst.isBranch && inst.branchTarget != 0) {
            xrefMap[inst.branchTarget].push_back(inst.address);
        }
    }

    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            if (i < 0 || i >= (int)g_AsmView.instructions.size()) continue;

            const AsmInstruction& inst = g_AsmView.instructions[i];

            ImGui::PushID(i);

            // 函数入口标记
            if (i == 0 && g_AsmView.currentFunctionStart == inst.address) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                    ";-- %s:", g_Layout.currentFunctionName.c_str());
            }

            // XREF标注
            auto xrefIt = xrefMap.find(inst.address);
            if (xrefIt != xrefMap.end()) {
                for (uint64_t fromAddr : xrefIt->second) {
                    char xrefBuf[64];
                    snprintf(xrefBuf, sizeof(xrefBuf), "; XREF from 0x%llX",
                        (unsigned long long)fromAddr);
                    ImGui::TextColored(ImVec4(0.7f, 0.4f, 0.9f, 1.0f), "%s", xrefBuf);
                }
            }

            // 选中高亮背景
            bool isSelected = (g_AsmView.selectedIndex == i);
            if (isSelected) {
                ImVec2 rowPos = ImGui::GetCursorScreenPos();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(
                    rowPos,
                    ImVec2(rowPos.x + ImGui::GetContentRegionAvail().x,
                           rowPos.y + g_Layout.rowHeight * 0.8f),
                    IM_COL32(40, 60, 40, 255));
            }

            // 地址列 - 绿色
            char addrBuf[16];
            snprintf(addrBuf, sizeof(addrBuf), "%06llX", (unsigned long long)inst.address);
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", addrBuf);
            ImGui::SameLine(0, 4);

            // 机器码列 - 灰色
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", inst.bytesStr.c_str());
            ImGui::SameLine(0, 8);

            // 助记符列 - 按类型着色
            ImVec4 mnemColor = GetMnemonicColor(inst.mnemonic);
            ImGui::TextColored(mnemColor, "%-8s", inst.mnemonic.c_str());
            ImGui::SameLine(0, 4);

            // 操作数列 - 带颜色高亮
            if (inst.operands.empty()) {
                ImGui::Text("");
            } else {
                RenderColoredOperands(inst.operands);
            }

            // 交互检测 - 使用InvisibleButton覆盖整行
            char selectableLabel[32];
            snprintf(selectableLabel, sizeof(selectableLabel), "##row%d", i);

            // 记录当前光标位置
            float rowY = ImGui::GetCursorPosY() - g_Layout.rowHeight * 0.8f;
            ImVec2 savedCursor = ImGui::GetCursorPos();
            ImGui::SetCursorPosY(rowY);
            ImGui::SetCursorPosX(0);

            bool isClicked = ImGui::InvisibleButton(selectableLabel,
                ImVec2(ImGui::GetContentRegionAvail().x + ImGui::GetScrollX(),
                       g_Layout.rowHeight * 0.8f));

            if (isClicked) {
                g_AsmView.selectedIndex = i;
                g_UI.AddLog("选中指令 #%d: %s %s", i, inst.mnemonic.c_str(), inst.operands.c_str());
            }

            // Android触摸长按检测
            if (ImGui::IsItemActive()) {
                if (g_AsmView.longPressStartTime == 0) {
                    g_AsmView.longPressStartTime = currentTime;
                    g_AsmView.longPressSelectedIndex = i;
                }
                if (currentTime - g_AsmView.longPressStartTime >= g_AsmView.longPressDelay) {
                    g_Layout.selectedFunctionAddress = inst.address;
                    g_Layout.showAsmContextMenu = true;
                    g_UI.AddLog("长按检测: %s", FormatAddressCompact(inst.address));
                }
            } else {
                if (g_AsmView.longPressSelectedIndex == i) {
                    g_AsmView.longPressStartTime = 0;
                    g_AsmView.longPressSelectedIndex = -1;
                }
            }

            ImGui::SetCursorPos(savedCursor);
            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    // 底部信息栏
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.50f, 0.60f, 0.50f, 1.0f),
        "Addr: 0x%llX  |  Loaded: %zu instrs  |  (长按指令显示导航菜单)",
        (unsigned long long)g_AsmView.currentAddress,
        g_AsmView.instructions.size());

    // 弹窗触发
    if (g_Layout.showAsmContextMenu) {
        if (!ImGui::IsPopupOpen("AsmContextMenu")) {
            ImGui::OpenPopup("AsmContextMenu");
        }
    }

    RenderAsmContextMenu(g_Layout.selectedFunctionAddress);

    if (!ImGui::IsPopupOpen("AsmContextMenu")) {
        g_Layout.showAsmContextMenu = false;
    }
}
