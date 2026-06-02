#include "Common.h"

// ============================================================================
// 十六进制视图渲染（修复版：优化间距）
// ============================================================================

void RenderHexViewFull(const std::vector<uint8_t>& data, int startOffset, int bytesPerRow) {
    if (data.empty()) {
        ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "无可用数据");
        return;
    }

    size_t totalSize = data.size();
    size_t endOffset = std::min((size_t)(startOffset + 4096), totalSize);
    size_t rowCount = (endOffset - startOffset + bytesPerRow - 1) / bytesPerRow;

    // 使用表格布局避免重叠
    if (ImGui::BeginTable("HexTable", 3, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 100 * g_Layout.uiScale);
        ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 320 * g_Layout.uiScale);
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);

        for (size_t row = 0; row < rowCount; row++) {
            size_t offset = startOffset + row * bytesPerRow;
            size_t bytesInRow = std::min((size_t)bytesPerRow, endOffset - offset);

            ImGui::TableNextRow();

            // Offset 列
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.50f, 0.65f, 0.50f, 1.0f), "%s", FormatAddressCompact(offset));

            // Hex 列
            ImGui::TableSetColumnIndex(1);
            std::string hexStr;
            for (int i = 0; i < bytesPerRow; i++) {
                if (i < (int)bytesInRow) {
                    char hexBuf[4];
                    snprintf(hexBuf, sizeof(hexBuf), "%02X ", data[offset + i]);
                    hexStr += hexBuf;
                } else {
                    hexStr += "   ";
                }
            }
            ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.85f, 1.0f), "%s", hexStr.c_str());

            // ASCII 列
            ImGui::TableSetColumnIndex(2);
            std::string asciiStr;
            for (int i = 0; i < (int)bytesInRow; i++) {
                uint8_t byte = data[offset + i];
                asciiStr += (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
            }
            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.60f, 1.0f), "%s", asciiStr.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "显示: %s - %s / 总: %s (%zu bytes)",
        FormatAddressCompact(startOffset), FormatAddressCompact(endOffset), FormatAddressCompact(totalSize), totalSize);
}

// ============================================================================
// 工具栏渲染
// ============================================================================

void RenderToolbar() {
    if (ImGui::Button("≡", ImVec2(g_Layout.buttonHeight, g_Layout.buttonHeight))) {
        g_Layout.showNavBar = !g_Layout.showNavBar;
    }
    ImGui::SameLine();

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "MiniIDA");

    float toolbarRightStart = g_Layout.displayWidth - (g_Layout.buttonHeight + g_Layout.spacing) * 4 - g_Layout.padding * 2;
    ImGui::SameLine(toolbarRightStart);

    if (ImGui::Button("打开", ImVec2(g_Layout.buttonHeight * 2.5f, g_Layout.buttonHeight))) {
        fileBrowser.showFilePicker = true;
        fileBrowser.Refresh();
        g_UI.AddLog("打开文件选择器");
    }
    ImGui::SameLine();

    if (ImGui::Button("刷新", ImVec2(g_Layout.buttonHeight * 2.5f, g_Layout.buttonHeight))) {
        if (!currentFilePath.empty()) {
            g_UI.AddLog("重新分析: %s", currentFileName.c_str());
            // 启动异步分析（使用带互斥锁保护的StartAnalysisThread）
            StartAnalysisThread(currentFilePath);
        }
    }
    ImGui::SameLine();

    // CFG 按钮 - 打开当前函数的控制流图
    if (elfParser.IsLoaded() && !g_AsmView.instructions.empty()) {
        if (ImGui::Button("CFG", ImVec2(g_Layout.buttonHeight * 2.5f, g_Layout.buttonHeight))) {
            uint64_t funcAddr = g_AsmView.currentFunctionStart;
            if (funcAddr == 0 && !g_AsmView.instructions.empty()) {
                funcAddr = g_AsmView.instructions.front().address;
            }
            std::string funcName = g_Layout.currentFunctionName.empty() ? "unknown" : g_Layout.currentFunctionName;
            int tabIndex = g_CFGManager.CreateCFGView(funcAddr, funcName);
            if (tabIndex >= 0) {
                CFGViewState& tab = g_CFGManager.tabs[tabIndex];
                BuildCFGGraph(tab.graph, funcAddr, g_AsmView.instructions);
                tab.valid = tab.graph.valid;
                g_Layout.currentCFGTab = tabIndex;
                g_Layout.showCFGView = true;
            }
            g_Layout.currentTabIndex = 10; // 切换到 CFG 标签页
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("打开控制流图视图");
        }
        ImGui::SameLine();
    }

    // Xrefs 按钮 - 打开交叉引用窗口
    if (elfParser.IsLoaded()) {
        if (ImGui::Button("Xrefs", ImVec2(g_Layout.buttonHeight * 2.5f, g_Layout.buttonHeight))) {
            g_Layout.currentTabIndex = 11; // 切换到 Xrefs 标签页
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("打开交叉引用窗口");
        }
        ImGui::SameLine();
    }

    if (ImGui::Button("⚙", ImVec2(g_Layout.buttonHeight * 2.0f, g_Layout.buttonHeight))) {
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("AI 设置");
    }
    ImGui::SameLine();

    const char* outputBtnText = g_Layout.showOutputPanel ? "隐藏输出" : "显示输出";
    if (ImGui::Button(outputBtnText, ImVec2(g_Layout.buttonHeight * 2.5f, g_Layout.buttonHeight))) {
        g_Layout.showOutputPanel = !g_Layout.showOutputPanel;
    }
    ImGui::SameLine();

    if (ImGui::Button("关于", ImVec2(g_Layout.buttonHeight * 2.0f, g_Layout.buttonHeight))) {
        g_UI.AddLog("ELF Analyzer Pro - Android ELF文件分析工具");
        g_UI.AddLog("支持x86, ARM, ARM64, MIPS, RISC-V等架构");
    }

    ImGui::Separator();
}

// ============================================================================
// 导航条渲染（增强版：支持点击跳转、拖动滚动、悬停提示）
// ============================================================================

// 静态变量用于跟踪拖动状态
static bool s_NavBandDragging = false;
static uint64_t s_NavBandDragStartAddr = 0;
static float s_NavBandLastX = 0;

void RenderNavigationBand() {
    if (!elfParser.IsLoaded()) return;
    
    ImVec2 bandPos = ImGui::GetCursorScreenPos();
    ImVec2 bandSize = ImVec2(ImGui::GetContentRegionAvail().x, g_Layout.navBandHeight * g_Layout.uiScale);
    
    // 除零保护
    if (bandSize.x <= 0) { ImGui::Dummy(bandSize); return; }
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // 背景
    drawList->AddRectFilled(bandPos, ImVec2(bandPos.x + bandSize.x, bandPos.y + bandSize.y), 
                           IM_COL32(30, 30, 30, 255));
    
    // 根据ELF段信息绘制色块
    const auto& sections = elfParser.GetSections();
    const auto& programs = elfParser.GetPrograms();
    
    uint64_t totalSize = elfParser.GetFileSize();
    if (totalSize == 0) totalSize = 1;
    
    // 计算地址范围
    uint64_t minAddr = UINT64_MAX, maxAddr = 0;
    if (!programs.empty()) {
        for (const auto& prog : programs) {
            if (prog.type == PT_LOAD && prog.vaddr > 0) {
                minAddr = std::min(minAddr, prog.vaddr);
                maxAddr = std::max(maxAddr, prog.vaddr + prog.memsz);
            }
        }
    }
    if (minAddr == UINT64_MAX) minAddr = 0;
    uint64_t range = maxAddr - minAddr;
    if (range == 0) range = 1;
    
    // 使用 Program Headers 绘制（更准确反映内存布局）
    if (!programs.empty()) {
        for (const auto& prog : programs) {
            if (prog.type != PT_LOAD || prog.memsz == 0) continue;
            
            float startFrac = (float)(prog.vaddr - minAddr) / (float)range;
            float endFrac = (float)(prog.vaddr + prog.memsz - minAddr) / (float)range;
            
            float x1 = bandPos.x + startFrac * bandSize.x;
            float x2 = bandPos.x + endFrac * bandSize.x;
            
            // 根据段权限着色
            ImU32 color;
            if (prog.flags & PF_X) {
                color = IM_COL32(0, 100, 180, 200);  // 可执行 - 蓝色
            } else if (prog.flags & PF_W) {
                color = IM_COL32(180, 100, 0, 200);  // 可写 - 棕色
            } else {
                color = IM_COL32(100, 100, 100, 200); // 只读 - 灰色
            }
            
            drawList->AddRectFilled(ImVec2(x1, bandPos.y), ImVec2(x2, bandPos.y + bandSize.y), color);
        }
        
        // 当前位置指示器（三角形）
        if (g_AsmView.instructions.size() > 0) {
            uint64_t currentAddr = g_AsmView.currentAddress;
            if (currentAddr >= minAddr && currentAddr <= maxAddr) {
                float posFrac = (float)(currentAddr - minAddr) / (float)range;
                float markerX = bandPos.x + posFrac * bandSize.x;
                float markerY = bandPos.y + bandSize.y;
                
                drawList->AddTriangleFilled(
                    ImVec2(markerX - 4, markerY),
                    ImVec2(markerX + 4, markerY),
                    ImVec2(markerX, markerY - 6),
                    IM_COL32(255, 255, 0, 255)
                );
            }
        }
        
        // 导出函数标记（小竖线）
        const auto& exports = elfParser.GetExports();
        for (const auto& exp : exports) {
            if (exp.address >= minAddr && exp.address <= maxAddr) {
                float frac = (float)(exp.address - minAddr) / (float)range;
                float x = bandPos.x + frac * bandSize.x;
                drawList->AddLine(
                    ImVec2(x, bandPos.y),
                    ImVec2(x, bandPos.y + bandSize.y),
                    IM_COL32(0, 255, 100, 150), 1.0f
                );
            }
        }
    }
    
    // 边框
    drawList->AddRect(bandPos, ImVec2(bandPos.x + bandSize.x, bandPos.y + bandSize.y), 
                     IM_COL32(80, 80, 80, 255));
    
    // 段标签
    float labelX = bandPos.x + 5;
    const char* archStr = elfParser.Is64Bit() ? "64" : "32";
    drawList->AddText(ImVec2(labelX, bandPos.y + 2), IM_COL32(200, 200, 200, 200), archStr);
    
    // ===== IDA风格点击跳转和拖动滚动功能 =====
    ImGui::SetCursorScreenPos(bandPos);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 30));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 50));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    
    ImGui::SetCursorScreenPos(bandPos);
    ImGui::PushID("NavBandClick");
    
    // InvisibleButton 用于捕获交互
    bool navBandClicked = ImGui::InvisibleButton("##navband", bandSize);
    bool navBandActive = ImGui::IsItemActive();
    bool navBandHovered = ImGui::IsItemHovered();
    
    if (navBandClicked) {
        // 单击事件：计算点击位置对应的地址并跳转
        ImVec2 mousePos = ImGui::GetMousePos();
        float clickX = mousePos.x - bandPos.x;
        float clickFrac = clickX / bandSize.x;
        if (clickFrac < 0) clickFrac = 0;
        if (clickFrac > 1) clickFrac = 1;
        
        uint64_t targetAddr = minAddr + (uint64_t)(clickFrac * range);
        
        // 跳转到最近的导出函数或该地址
        const auto& exports = elfParser.GetExports();
        uint64_t nearestFunc = 0;
        uint64_t minDist = UINT64_MAX;
        std::string targetFuncName;
        
        for (const auto& exp : exports) {
            if (exp.address >= minAddr && exp.address <= maxAddr) {
                uint64_t dist = (exp.address > targetAddr) ? (exp.address - targetAddr) : (targetAddr - exp.address);
                if (dist < minDist) {
                    minDist = dist;
                    nearestFunc = exp.address;
                    targetFuncName = std::string(exp.name);
                }
            }
        }
        
        // 如果点击位置离某个函数很近（在5%范围内），跳转到该函数
        uint64_t jumpAddr = targetAddr;
        if (minDist < (range / 20) && nearestFunc != 0) {
            jumpAddr = nearestFunc;
        }
        
        // 查找对应的导出函数
        FunctionExport targetExp;
        bool found = false;
        for (const auto& exp : exports) {
            if (exp.address == jumpAddr) {
                targetExp = exp;
                found = true;
                break;
            }
        }
        
        if (found) {
            StartDisassembly(targetExp);
            g_Layout.currentTabIndex = 1;
            g_UI.AddLog("导航跳转到: %s @ 0x%llX", targetFuncName.c_str(), (unsigned long long)jumpAddr);
        } else {
            g_AsmView.currentAddress = jumpAddr;
            g_Layout.currentAsmAddress = jumpAddr;
            g_Layout.currentTabIndex = 1;
            g_UI.AddLog("导航跳转到地址: 0x%llX", (unsigned long long)jumpAddr);
        }
    }
    
    // ===== 拖动滚动功能 =====
    if (navBandActive) {
        if (!s_NavBandDragging) {
            s_NavBandDragging = true;
            s_NavBandDragStartAddr = g_AsmView.currentAddress;
            s_NavBandLastX = ImGui::GetMousePos().x;
        }
        
        // 实时更新位置
        ImVec2 mousePos = ImGui::GetMousePos();
        float dragX = mousePos.x - bandPos.x;
        float dragFrac = dragX / bandSize.x;
        if (dragFrac < 0) dragFrac = 0;
        if (dragFrac > 1) dragFrac = 1;
        
        uint64_t dragAddr = minAddr + (uint64_t)(dragFrac * range);
        
        // 查找最近的函数
        const auto& exports = elfParser.GetExports();
        uint64_t nearestFunc = 0;
        uint64_t minDist = UINT64_MAX;
        
        for (const auto& exp : exports) {
            if (exp.address >= minAddr && exp.address <= maxAddr) {
                uint64_t dist = (exp.address > dragAddr) ? (exp.address - dragAddr) : (dragAddr - exp.address);
                if (dist < minDist) {
                    minDist = dist;
                    nearestFunc = exp.address;
                }
            }
        }
        
        // 实时更新当前地址
        if (nearestFunc != 0 && minDist < (range / 50)) {
            g_AsmView.currentAddress = nearestFunc;
            g_Layout.currentAsmAddress = nearestFunc;
        } else {
            g_AsmView.currentAddress = dragAddr;
            g_Layout.currentAsmAddress = dragAddr;
        }
        
        // 绘制拖动指示器
        float dragMarkerX = bandPos.x + dragFrac * bandSize.x;
        drawList->AddLine(
            ImVec2(dragMarkerX, bandPos.y - 2),
            ImVec2(dragMarkerX, bandPos.y + bandSize.y + 2),
            IM_COL32(255, 200, 0, 255), 2.0f
        );
    } else {
        // 拖动结束
        if (s_NavBandDragging) {
            uint64_t finalAddr = g_AsmView.currentAddress;
            
            const auto& exports = elfParser.GetExports();
            uint64_t nearestFunc = 0;
            uint64_t minDist = UINT64_MAX;
            std::string nearestFuncName;
            
            for (const auto& exp : exports) {
                if (exp.address >= minAddr && exp.address <= maxAddr) {
                    uint64_t dist = (exp.address > finalAddr) ? (exp.address - finalAddr) : (finalAddr - exp.address);
                    if (dist < minDist) {
                        minDist = dist;
                        nearestFunc = exp.address;
                        nearestFuncName = std::string(exp.name);
                    }
                }
            }
            
            if (nearestFunc != 0 && minDist < (range / 10)) {
                FunctionExport targetExp;
                bool found = false;
                for (const auto& exp : exports) {
                    if (exp.address == nearestFunc) {
                        targetExp = exp;
                        found = true;
                        break;
                    }
                }
                
                if (found) {
                    StartDisassembly(targetExp);
                    g_Layout.currentTabIndex = 1;
                    g_UI.AddLog("导航拖动跳转到: %s @ 0x%llX", nearestFuncName.c_str(), (unsigned long long)nearestFunc);
                }
            }
            
            s_NavBandDragging = false;
        }
    }
    
    ImGui::PopID();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    
    // ===== 悬停提示 =====
    if (navBandHovered && !s_NavBandDragging) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float hoverX = mousePos.x - bandPos.x;
        float hoverFrac = hoverX / bandSize.x;
        if (hoverFrac >= 0 && hoverFrac <= 1) {
            uint64_t hoverAddr = minAddr + (uint64_t)(hoverFrac * range);
            
            const auto& exports = elfParser.GetExports();
            std::string nearestFunc;
            uint64_t nearestFuncAddr = 0;
            uint64_t minDist = UINT64_MAX;
            for (const auto& exp : exports) {
                if (exp.address >= minAddr && exp.address <= maxAddr) {
                    uint64_t dist = (exp.address > hoverAddr) ? (exp.address - hoverAddr) : (hoverAddr - exp.address);
                    if (dist < minDist) {
                        minDist = dist;
                        nearestFunc = std::string(exp.name);
                        nearestFuncAddr = exp.address;
                    }
                }
            }
            
            // 查找所在的段
            std::string sectionName;
            const auto& progs = elfParser.GetPrograms();
            for (const auto& prog : progs) {
                if (prog.type == PT_LOAD && prog.memsz > 0) {
                    if (hoverAddr >= prog.vaddr && hoverAddr < prog.vaddr + prog.memsz) {
                        char perm[4] = {0};
                        if (prog.flags & PF_R) strcat(perm, "R");
                        if (prog.flags & PF_W) strcat(perm, "W");
                        if (prog.flags & PF_X) strcat(perm, "X");
                        sectionName = "LOAD[" + std::string(perm) + "]";
                        break;
                    }
                }
            }
            
            char tooltipBuf[512];
            if (!nearestFunc.empty() && minDist < (range / 20)) {
                snprintf(tooltipBuf, sizeof(tooltipBuf), 
                    "地址: 0x%llX\n函数: %s\n函数地址: 0x%llX\n段: %s\n\n点击跳转 | 拖动滚动", 
                    (unsigned long long)hoverAddr, nearestFunc.c_str(), 
                    (unsigned long long)nearestFuncAddr,
                    sectionName.empty() ? "-" : sectionName.c_str());
            } else {
                snprintf(tooltipBuf, sizeof(tooltipBuf), 
                    "地址: 0x%llX\n段: %s\n\n点击跳转 | 拖动滚动", 
                    (unsigned long long)hoverAddr,
                    sectionName.empty() ? "-" : sectionName.c_str());
            }
            ImGui::SetTooltip("%s", tooltipBuf);
        }
    }
    
    ImGui::Dummy(bandSize);
}

// ============================================================================
// 进度弹窗
// ============================================================================

void RenderProgressModal() {
    if (!g_Progress.isAnalyzing) return;

    ImGui::OpenPopup("分析进度###ProgressModal");

    ImVec2 modalSize(g_Layout.displayWidth * 0.4f, g_Layout.displayHeight * 0.25f);
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("分析进度###ProgressModal", NULL,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {

        ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "正在分析ELF文件...");
        ImGui::Separator();

        const char* stageName = g_Progress.stageName.load();
        ImGui::Text("当前阶段: %s", stageName ? stageName : "未知");

        float progress = g_Progress.progress.load();
        ImGui::ProgressBar(progress / 100.0f, ImVec2(-1.0f, 0), "");

        char progBuf[32];
        snprintf(progBuf, sizeof(progBuf), "%.1f%%", progress);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.40f, 0.80f, 0.55f, 1.0f), "%s", progBuf);

        ImGui::Separator();

        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 120 * g_Layout.uiScale - g_Layout.padding);
        if (ImGui::Button("取消", ImVec2(120 * g_Layout.uiScale, g_Layout.buttonHeight))) {
            g_Progress.RequestCancel();
        }

        ImGui::EndPopup();
    }
}

// ============================================================================
// 左侧函数列表面板（修复版：正确保存选中状态 + 横向滚动）
// ============================================================================

void RenderFunctionsWindow() {
    if (!g_Layout.showNavBar) return;

    ImGui::BeginChild("FunctionsWindow", ImVec2(g_Layout.navBarWidth, 0), true);

    ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Functions window");
    ImGui::Separator();

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "搜索...", g_Layout.searchText, sizeof(g_Layout.searchText))) {
        elfParser.SetSearchText(g_Layout.searchText);
    }

    ImGui::Spacing();

    float tableHeight = ImGui::GetContentRegionAvail().y - g_Layout.rowHeight;

    // 地址格式化缓冲区
    char addrBuf[64];

    // 添加水平滚动支持
    if (ImGui::BeginTable("FunctionsTable", 3,
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable,
        ImVec2(0, tableHeight))) {

        ImGui::TableSetupColumn("地址", ImGuiTableColumnFlags_WidthFixed, 90 * g_Layout.uiScale);
        ImGui::TableSetupColumn("函数/符号名###funcname", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 10);  // 额外列用于滚动空间
        ImGui::TableHeadersRow();

        if (elfParser.IsLoaded()) {
            const auto& exports = elfParser.GetExports();
            const auto& symbols = elfParser.GetSections();
            const auto& filteredExports = elfParser.GetFilteredExports();

            // 使用Clipper优化渲染
            ImGuiListClipper clipper;
            clipper.Begin(std::min((size_t)100, filteredExports.size()), g_Layout.rowHeight * 0.8f);

            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    if (i < 0 || i >= (int)filteredExports.size()) continue;

                    const FunctionExport& exp = exports[filteredExports[i]];

                    // 使用地址作为唯一ID，避免索引变化导致的选择问题
                    ImGui::PushID((int)(exp.address & 0xFFFFFFFF));  // 使用地址作为唯一ID

                    ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);
                    ImGui::TableSetColumnIndex(0);

                    // 使用紧凑格式显示地址（移除前导零）
                    ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(exp.address));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.80f, 0.60f, 1.0f));

                    // 检查是否选中：使用地址比较而不是索引
                    bool isSelected = (g_Layout.selectedFunctionAddress == exp.address);

                    if (ImGui::Selectable(exp.name.data(), isSelected,
                        ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, g_Layout.rowHeight * 0.8f))) {
                        // 保存选中的函数信息用于持久化
                        g_Layout.selectedFunctionAddress = exp.address;
                        g_Layout.selectedFunctionDisplayName = std::string(exp.name);
                        g_Layout.selectedFunctionIndex = i;

                        // 格式化地址
                        snprintf(addrBuf, sizeof(addrBuf), "0x%llx", (unsigned long long)exp.address);
                        g_UI.AddLog("选中导出函数: %s at %s", exp.name.data(), addrBuf);
                        // 启动反汇编并切换到汇编视图
                        StartDisassembly(exp);
                        g_Layout.currentTabIndex = 1;  // 切换到Asm View标签
                    }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
            }

            // 如果有保存的选中函数但当前不在列表中，尝试高亮匹配的函数
            if (g_Layout.selectedFunctionAddress != 0 && g_Layout.selectedFunctionIndex < 0) {
                for (size_t i = 0; i < filteredExports.size(); i++) {
                    if (exports[filteredExports[i]].address == g_Layout.selectedFunctionAddress) {
                        g_Layout.selectedFunctionIndex = (int)i;
                        break;
                    }
                }
            }
        } else {
            ImGui::TableNextRow(0, g_Layout.rowHeight);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.45f, 1.0f), "-");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.45f, 1.0f), "未加载ELF文件");
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();

    if (g_Layout.isLandscape) {
        ImGui::SameLine();
    }
}

// ============================================================================
// 中央工作区（修复版：正确的Tab切换逻辑 + 滚动支持 + 地址点击）
// ============================================================================

void RenderWorkspace() {
    float centerWidth = g_Layout.displayWidth;
    if (g_Layout.isLandscape && g_Layout.showNavBar) {
        centerWidth -= g_Layout.navBarWidth;
    }
    centerWidth -= g_Layout.padding * 2;

    float contentHeight = g_Layout.displayHeight - g_Layout.toolbarHeight
        - g_Layout.navBandHeight - g_Layout.outputPanelHeight
        - g_Layout.statusBarHeight - g_Layout.padding * 3;

    if (contentHeight < 100) contentHeight = 100;

    // 启用滚动和触摸滑动支持
    ImGui::BeginChild("Workspace", ImVec2(centerWidth, contentHeight), false,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // 使用正确的TabBar设置方法
    ImGuiTabBarFlags tabFlags = ImGuiTabBarFlags_None;

    if (ImGui::BeginTabBar("WorkspaceTabs", tabFlags)) {

        // IDA View
        if (ImGui::BeginTabItem("MiniIDA View-A")) {
            g_Layout.currentTabIndex = 0;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "IDA View-A");
            ImGui::Separator();

            if (!elfParser.IsLoaded()) {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件 - 请打开ELF文件进行分析");
            } else {
                const ElfHeaderInfo& info = elfParser.GetHeaderInfo();

                // 入口点地址 - 可点击跳转
                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "入口点:");
                ImGui::SameLine();
                if (ImGui::Selectable(FormatAddressCompact(info.entryPoint), false,
                    ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                    // 跳转到入口点地址的反汇编
                    const auto& exports = elfParser.GetExports();
                    if (!exports.empty()) {
                        // 找到入口点对应的函数
                        for (const auto& exp : exports) {
                            if (exp.address == info.entryPoint ||
                                (exp.address <= info.entryPoint && exp.address + exp.size > info.entryPoint)) {
                                g_Layout.selectedFunctionAddress = exp.address;
                                g_Layout.selectedFunctionDisplayName = std::string(exp.name);
                                g_Layout.currentAsmAddress = exp.address;
                                g_Layout.currentFunctionName = std::string(exp.name);
                                g_Layout.showAsmView = true;
                                StartDisassembly(exp);
                                g_Layout.currentTabIndex = 1;  // 切换到汇编视图
                                g_UI.AddLog("跳转到入口点: %s", exp.name.data());
                                break;
                            }
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "<EntryPoint>");
                ImGui::Separator();

                const auto& exports = elfParser.GetExports();
                const auto& imports = elfParser.GetImports();
                const auto& sections = elfParser.GetSections();

                ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "文件概要:");
                ImGui::Columns(2, "SummaryCols", false);
                ImGui::SetColumnWidth(0, 120 * g_Layout.uiScale);

                ImGui::Text("文件类型:");
                ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.40f, 0.80f, 0.55f, 1.0f), "%s", info.typeName.c_str());
                ImGui::NextColumn();

                ImGui::Text("目标架构:");
                ImGui::NextColumn();
                ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.85f, 1.0f), "%s", info.machineName.c_str());
                ImGui::NextColumn();

                ImGui::Text("入口地址:");
                ImGui::NextColumn();
                // 使用紧凑格式显示地址
                ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(info.entryPoint));
                ImGui::NextColumn();

                ImGui::Text("文件大小:");
                ImGui::NextColumn();
                ImGui::Text("%zu bytes", elfParser.GetFileSize());
                ImGui::NextColumn();

                ImGui::Text("Sections:");
                ImGui::NextColumn();
                ImGui::Text("%zu", sections.size());
                ImGui::NextColumn();

                ImGui::Text("Programs:");
                ImGui::NextColumn();
                ImGui::Text("%zu", elfParser.GetPrograms().size());
                ImGui::NextColumn();

                ImGui::Text("Symbols:");
                ImGui::NextColumn();
                ImGui::Text("%zu", elfParser.GetSymbols().size());
                ImGui::NextColumn();

                ImGui::Columns(1);
            }
            ImGui::EndTabItem();
        }

        // Asm View (IDA风格汇编视图)
        if (ImGui::BeginTabItem("Asm View")) {
            g_Layout.currentTabIndex = 1;
            RenderAssemblyView();
            ImGui::EndTabItem();
        }

        // CFG View (控制流图)
        if (ImGui::BeginTabItem("CFG")) {
            g_Layout.currentTabIndex = 10;
            
            if (elfParser.IsLoaded() && !g_AsmView.instructions.empty()) {
                // 自动创建或更新 CFG
                bool needRebuild = false;
                if (g_CFGManager.GetTabCount() == 0) {
                    needRebuild = true;
                } else if (g_CFGManager.tabs[0].funcAddress != g_AsmView.currentFunctionStart) {
                    // 函数变化，需要重建
                    needRebuild = true;
                }
                
                if (needRebuild) {
                    uint64_t funcAddr = g_AsmView.currentFunctionStart;
                    if (funcAddr == 0 && !g_AsmView.instructions.empty()) {
                        funcAddr = g_AsmView.instructions.front().address;
                    }
                    std::string funcName = g_Layout.currentFunctionName.empty() ? "unknown" : g_Layout.currentFunctionName;
                    
                    g_CFGManager.tabs.clear();
                    int idx = g_CFGManager.CreateCFGView(funcAddr, funcName);
                    if (idx >= 0) {
                        CFGViewState& tab = g_CFGManager.tabs[idx];
                        BuildCFGGraph(tab.graph, funcAddr, g_AsmView.instructions);
                        tab.valid = tab.graph.valid;
                        g_Layout.currentCFGTab = idx;
                    }
                }
                // 渲染当前CFG标签页
                if (g_Layout.currentCFGTab >= 0 && g_Layout.currentCFGTab < g_CFGManager.GetTabCount()) {
                    RenderCFGToolbar(g_CFGManager.tabs[g_Layout.currentCFGTab], false);
    ImGui::Separator();
                    RenderCFGCanvas(g_CFGManager.tabs[g_Layout.currentCFGTab]);
                } else if (g_CFGManager.GetTabCount() > 0) {
                    g_Layout.currentCFGTab = 0;
                    RenderCFGToolbar(g_CFGManager.tabs[0], false);
    ImGui::Separator();
                    RenderCFGCanvas(g_CFGManager.tabs[0]);
                } else {
                    ImGui::TextColored(ImVec4(1,1,0,1), "请先在汇编视图中选择一个函数");
                }
            } else {
                ImGui::TextColored(ImVec4(0.5,0.5,0.5,1), "请先加载文件并选择函数");
            }
            ImGui::EndTabItem();
        }

        // Hex View
        if (ImGui::BeginTabItem("Hex View")) {
            g_Layout.currentTabIndex = 2;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Hex View-1");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 150 * g_Layout.uiScale);

            if (ImGui::Button("<", ImVec2(30 * g_Layout.uiScale, g_Layout.buttonHeight * 0.6f))) {
                g_Layout.hexOffset = std::max(0, g_Layout.hexOffset - 256);
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "%s", FormatAddressCompact(g_Layout.hexOffset));
            ImGui::SameLine();
            if (ImGui::Button(">", ImVec2(30 * g_Layout.uiScale, g_Layout.buttonHeight * 0.6f))) {
                if (elfParser.IsLoaded()) {
                    g_Layout.hexOffset = std::min((int)elfParser.GetFileSize() - 256, g_Layout.hexOffset + 256);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Goto", ImVec2(50 * g_Layout.uiScale, g_Layout.buttonHeight * 0.6f))) {
                g_Layout.hexOffset = 0;
            }

            ImGui::Separator();

            if (elfParser.IsLoaded()) {
                // 传递实际的ELF文件数据而不是空向量
                const uint8_t* fileData = elfParser.GetFileData();
                size_t fileSize = elfParser.GetFileSize();

                // 创建临时向量用于Hex View显示
                std::vector<uint8_t> fileContent(fileData, fileData + fileSize);
                RenderHexViewFull(fileContent, g_Layout.hexOffset, g_Layout.hexBytesPerRow);
            } else {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件");
            }
            ImGui::EndTabItem();
        }

        // Imports
        if (ImGui::BeginTabItem("Imports")) {
            g_Layout.currentTabIndex = 3;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Imports (导入表)");
            ImGui::Separator();

            if (elfParser.IsLoaded()) {
                const auto& imports = elfParser.GetImports();

                float tableHeight = ImGui::GetContentRegionAvail().y - g_Layout.rowHeight;

                if (ImGui::BeginTable("ImportsTable", 3,
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                    ImVec2(0, tableHeight))) {

                    ImGui::TableSetupColumn("序号", ImGuiTableColumnFlags_WidthFixed, 50 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("库名称", ImGuiTableColumnFlags_WidthFixed, 120 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("函数名", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(std::min((size_t)500, imports.size()), g_Layout.rowHeight * 0.8f);

                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            if (i < 0 || i >= (int)imports.size()) continue;

                            const FunctionImport& imp = imports[i];

                            ImGui::PushID(2000 + i);  // 使用偏移索引作为唯一ID

                            ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%d", i + 1);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%s", imp.library.c_str());

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "%s", imp.function.c_str());

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTable();
                }

                if (imports.empty()) {
                    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "无导入函数");
                }
            } else {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件");
            }
            ImGui::EndTabItem();
        }

        // Exports
        if (ImGui::BeginTabItem("Exports")) {
            g_Layout.currentTabIndex = 4;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Exports (导出表)");
            ImGui::Separator();

            if (elfParser.IsLoaded()) {
                const auto& exports = elfParser.GetExports();
                const auto& filteredIndices = elfParser.GetFilteredExports();

                float tableHeight = ImGui::GetContentRegionAvail().y - g_Layout.rowHeight;

                if (ImGui::BeginTable("ExportsTable", 3,
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                    ImVec2(0, tableHeight))) {

                    ImGui::TableSetupColumn("序号", ImGuiTableColumnFlags_WidthFixed, 50 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("地址", ImGuiTableColumnFlags_WidthFixed, 100 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("函数名", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(std::min((size_t)500, filteredIndices.size()), g_Layout.rowHeight * 0.8f);

                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            if (i < 0 || i >= (int)filteredIndices.size()) continue;

                            const FunctionExport& exp = exports[filteredIndices[i]];

                            ImGui::PushID(3000 + i);  // 使用偏移索引作为唯一ID

                            ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%d", i + 1);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(exp.address));

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "%.*s", (int)exp.name.size(), exp.name.data());

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTable();
                }

                if (exports.empty()) {
                    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "无导出函数");
                }
            } else {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件");
            }
            ImGui::EndTabItem();
        }

        // Symbols
        if (ImGui::BeginTabItem("Symbols")) {
            g_Layout.currentTabIndex = 5;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Symbols (符号表)");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 120 * g_Layout.uiScale);
            const auto& symbols = elfParser.GetSymbols();
            ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "[%zu]", symbols.size());
            ImGui::Separator();

            if (elfParser.IsLoaded()) {
                const auto& filteredIndices = elfParser.GetFilteredSymbols();

                float tableHeight = ImGui::GetContentRegionAvail().y - g_Layout.rowHeight;

                // 如果没有符号，显示提示信息
                if (symbols.empty()) {
                    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f),
                        "未找到符号表 - 文件可能已strip或符号表被移除");
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.60f, 0.65f, 0.60f, 1.0f),
                        "提示: 尝试查看Dynamic标签获取动态符号信息");
                } else if (filteredIndices.empty() && !g_Layout.lastSearchText.empty()) {
                    // 搜索结果为空
                    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f),
                        "搜索 '%s' 未找到匹配符号", g_Layout.lastSearchText.c_str());
                } else {
                    // 显示符号表
                    const size_t displayCount = filteredIndices.empty() ? symbols.size() : filteredIndices.size();

                    if (ImGui::BeginTable("SymbolsTable", 4,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                        ImVec2(0, tableHeight))) {

                        ImGui::TableSetupColumn("地址", ImGuiTableColumnFlags_WidthFixed, 90 * g_Layout.uiScale);
                        ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthFixed, 60 * g_Layout.uiScale);
                        ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 60 * g_Layout.uiScale);
                        ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        ImGuiListClipper clipper;
                        clipper.Begin(std::min((size_t)500, displayCount), g_Layout.rowHeight * 0.8f);

                        while (clipper.Step()) {
                            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                                if (i < 0 || i >= (int)displayCount) continue;

                                const SymbolInfo* pSym = nullptr;
                                if (filteredIndices.empty()) {
                                    if (i >= (int)symbols.size()) continue;
                                    pSym = &symbols[i];
                                } else {
                                    if (i >= (int)filteredIndices.size()) continue;
                                    size_t symIdx = filteredIndices[i];
                                    if (symIdx >= symbols.size()) continue;
                                    pSym = &symbols[symIdx];
                                }

                                const SymbolInfo& sym = *pSym;

                                // 跳过真正为空的名称（而不是未初始化的情况）
                                if (sym.name.empty() && sym.value == 0 && sym.size == 0) continue;

                                ImGui::PushID(4000 + i);  // 使用偏移索引作为唯一ID

                                ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);

                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(sym.value));

                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("0x%lX", (unsigned long)sym.size);

                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%s", sym.typeName.empty() ? "UNKNOWN" : sym.typeName.c_str());

                                ImGui::TableSetColumnIndex(3);
                                if (!sym.name.empty()) {
                                    ImGui::Text("%.*s", (int)sym.name.size(), sym.name.data());
                                } else {
                                    // 显示未命名符号的提示
                                    ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.45f, 1.0f), "<anonymous>");
                                }

                                ImGui::PopID();
                            }
                        }

                        ImGui::EndTable();
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件");
            }
            ImGui::EndTabItem();
        }

        // Sections
        if (ImGui::BeginTabItem("Sections")) {
            g_Layout.currentTabIndex = 6;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Sections (节区)");
            ImGui::Separator();

            if (elfParser.IsLoaded()) {
                const auto& sections = elfParser.GetSections();

                float tableHeight = ImGui::GetContentRegionAvail().y - g_Layout.rowHeight;

                if (ImGui::BeginTable("SectionsTable", 4,
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                    ImVec2(0, tableHeight))) {

                    ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("地址", ImGuiTableColumnFlags_WidthFixed, 90 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthFixed, 80 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 80 * g_Layout.uiScale);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(sections.size(), g_Layout.rowHeight * 0.8f);

                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            if (i < 0 || i >= (int)sections.size()) continue;

                            const SectionInfo& sec = sections[i];

                            ImGui::PushID(5000 + i);  // 使用偏移索引作为唯一ID

                            ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%.*s", (int)sec.name.size(), sec.name.data());

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(sec.addr));

                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("0x%lX", (unsigned long)sec.size);

                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("0x%X", sec.type);

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTable();
                }
            } else {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件");
            }
            ImGui::EndTabItem();
        }

        // Programs
        if (ImGui::BeginTabItem("Programs")) {
            g_Layout.currentTabIndex = 7;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Programs (程序头)");
            ImGui::Separator();

            if (elfParser.IsLoaded()) {
                const auto& programs = elfParser.GetPrograms();

                float tableHeight = ImGui::GetContentRegionAvail().y - g_Layout.rowHeight;

                if (ImGui::BeginTable("ProgramsTable", 5,
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                    ImVec2(0, tableHeight))) {

                    ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 80 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("偏移", ImGuiTableColumnFlags_WidthFixed, 90 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("虚拟地址", ImGuiTableColumnFlags_WidthFixed, 110 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("文件大小", ImGuiTableColumnFlags_WidthFixed, 90 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("内存大小", ImGuiTableColumnFlags_WidthFixed, 90 * g_Layout.uiScale);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(programs.size(), g_Layout.rowHeight * 0.8f);

                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            if (i < 0 || i >= (int)programs.size()) continue;

                            const ProgramInfo& prog = programs[i];

                            ImGui::PushID(7000 + i);  // 使用偏移索引作为唯一ID

                            ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", prog.typeName.c_str());

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(prog.offset));

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(prog.vaddr));

                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("0x%lX", (unsigned long)prog.filesz);

                            ImGui::TableSetColumnIndex(4);
                            ImGui::Text("0x%lX", (unsigned long)prog.memsz);

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTable();
                }
            } else {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件");
            }
            ImGui::EndTabItem();
        }

        // Strings
        if (ImGui::BeginTabItem("Strings")) {
            g_Layout.currentTabIndex = 8;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Strings (字符串)");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 80 * g_Layout.uiScale);
            ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "[%zu]", elfParser.GetStrings().size());

            ImGui::Separator();

            if (elfParser.IsLoaded()) {
                const auto& strings = elfParser.GetStrings();
                const auto& filteredIndices = elfParser.GetFilteredStrings();

                float tableHeight = ImGui::GetContentRegionAvail().y - g_Layout.rowHeight;

                if (ImGui::BeginTable("StringsTable", 2,
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                    ImVec2(0, tableHeight))) {

                    ImGui::TableSetupColumn("偏移", ImGuiTableColumnFlags_WidthFixed, 80 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("内容", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(std::min((size_t)500, filteredIndices.size()), g_Layout.rowHeight * 0.8f);

                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            if (i < 0 || i >= (int)filteredIndices.size()) continue;

                            const StringInfo& str = strings[filteredIndices[i]];

                            ImGui::PushID(8000 + i);  // 使用偏移索引作为唯一ID

                            ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(str.offset));

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(ImVec4(0.40f, 0.80f, 0.60f, 1.0f), "%.*s", (int)str.value.size(), str.value.data());

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTable();
                }
            } else {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件");
            }
            ImGui::EndTabItem();
        }

        // Dynamic
        if (ImGui::BeginTabItem("Dynamic")) {
            g_Layout.currentTabIndex = 9;

            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "Dynamic (动态链接)");
            ImGui::Separator();

            if (elfParser.IsLoaded()) {
                const auto& dynamicInfo = elfParser.GetDynamicInfo();
                const auto& relocations = elfParser.GetRelocations();

                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "动态标签 (%zu):", dynamicInfo.size());

                float dynamicTableHeight = 150 * g_Layout.uiScale;

                if (ImGui::BeginTable("DynamicTable", 2,
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                    ImVec2(0, dynamicTableHeight))) {

                    ImGui::TableSetupColumn("标签", ImGuiTableColumnFlags_WidthFixed, 120 * g_Layout.uiScale);
                    ImGui::TableSetupColumn("值/字符串", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    ImGuiListClipper clipper;
                    clipper.Begin(dynamicInfo.size(), g_Layout.rowHeight * 0.8f);

                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            if (i < 0 || i >= (int)dynamicInfo.size()) continue;

                            const DynamicInfo& dinfo = dynamicInfo[i];

                            ImGui::PushID(9000 + i);  // 使用偏移索引作为唯一ID

                            ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%s", dinfo.tagName.c_str());

                            ImGui::TableSetColumnIndex(1);
                            if (!dinfo.stringValue.empty()) {
                                ImGui::TextColored(ImVec4(0.40f, 0.80f, 0.60f, 1.0f), "%s (0x%lX)",
                                    dinfo.stringValue.c_str(), (unsigned long)dinfo.tagValue);
                            } else {
                                ImGui::Text("0x%lX", (unsigned long)dinfo.tagValue);
                            }

                            ImGui::PopID();
                        }
                    }

                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.50f, 0.75f, 0.85f, 1.0f), "重定位条目 (%zu):", relocations.size());

                if (relocations.size() > 0) {
                    float relocTableHeight = 150 * g_Layout.uiScale;

                    if (ImGui::BeginTable("RelocationTable", 3,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter,
                        ImVec2(0, relocTableHeight))) {

                        ImGui::TableSetupColumn("偏移", ImGuiTableColumnFlags_WidthFixed, 90 * g_Layout.uiScale);
                        ImGui::TableSetupColumn("符号", ImGuiTableColumnFlags_WidthFixed, 120 * g_Layout.uiScale);
                        ImGui::TableSetupColumn("信息", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        ImGuiListClipper clipper;
                        clipper.Begin(std::min((size_t)100, relocations.size()), g_Layout.rowHeight * 0.8f);

                        while (clipper.Step()) {
                            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                                if (i < 0 || i >= (int)relocations.size()) continue;

                                const RelocationInfo& rel = relocations[i];

                                ImGui::PushID(9500 + i);  // 使用偏移索引作为唯一ID

                                ImGui::TableNextRow(0, g_Layout.rowHeight * 0.8f);

                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextColored(ImVec4(0.80f, 0.75f, 0.45f, 1.0f), "%s", FormatAddressCompact(rel.offset));

                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("%s", rel.symbolName.empty() ? "-" : rel.symbolName.c_str());

                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("info=0x%lX, addend=0x%lX", (unsigned long)rel.info, (unsigned long)rel.addend);

                                ImGui::PopID();
                            }
                        }

                        ImGui::EndTable();
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "无重定位信息");
                }
            } else {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "未加载文件");
            }
            ImGui::EndTabItem();
        }

        // Xrefs (交叉引用)
        if (ImGui::BeginTabItem("Xrefs")) {
            g_Layout.currentTabIndex = 11;
            
            ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "交叉引用分析");
            ImGui::Separator();
            
            if (elfParser.IsLoaded()) {
                // 当前选中地址
                uint64_t currentAddr = g_AsmView.currentAddress;
                ImGui::Text("当前地址: %s", FormatAddress(currentAddr));
                ImGui::Separator();
                
                // 显示当前函数的调用关系
                if (!g_AsmView.instructions.empty()) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "调用目标:");
                    ImGui::Indent();
                    
                    const auto& exports = elfParser.GetExports();
                    const auto& imports = elfParser.GetImports();
                    uint64_t funcStart = g_AsmView.currentFunctionStart;
                    uint64_t funcEnd = g_AsmView.currentFunctionEnd;
                    
                    bool hasCalls = false;
                    for (const auto& inst : g_AsmView.instructions) {
                        if (inst.isCall && inst.branchTarget != 0) {
                            // 检查是否是外部调用
                            if (inst.branchTarget < funcStart || inst.branchTarget >= funcEnd) {
                                hasCalls = true;
                                std::string targetName = "unknown";
                                
                                // 在导出表中查找
                                for (const auto& exp : exports) {
                                    if (exp.address == inst.branchTarget) {
                                        targetName = std::string(exp.name);
                                        break;
                                    }
                                }
                                
                                // 在导入表中查找
                                for (const auto& imp : imports) {
                                    if (imp.address == inst.branchTarget) {
                                        targetName = imp.library + "::" + imp.function;
                                        break;
                                    }
                                }
                                
                                ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), 
                                    "%s @ %s -> %s", 
                                    inst.mnemonic.c_str(), 
                                    FormatAddressCompact(inst.address),
                                    targetName.c_str());
                            }
                        }
                    }
                    if (!hasCalls) {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "无外部调用");
                    }
                    ImGui::Unindent();
                    
                    ImGui::Separator();
                    
                    // 显示跳转目标
                    ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.4f, 1.0f), "跳转目标:");
                    ImGui::Indent();
                    
                    bool hasJumps = false;
                    for (const auto& inst : g_AsmView.instructions) {
                        if (inst.isJump && inst.branchTarget != 0) {
                            hasJumps = true;
                            ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.5f, 1.0f), 
                                "%s @ %s -> %s", 
                                inst.mnemonic.c_str(), 
                                FormatAddressCompact(inst.address),
                                FormatAddressCompact(inst.branchTarget));
                        }
                    }
                    if (!hasJumps) {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "无跳转指令");
                    }
                    ImGui::Unindent();
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "请先在汇编视图中选择函数");
                }
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "请先加载文件");
            }
            
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();
}

// ============================================================================
// 输出面板渲染
// ============================================================================

void RenderOutputPanel() {
    if (!g_Layout.showOutputPanel) return;
    
    float panelHeight = g_Layout.outputPanelHeight * g_Layout.uiScale;
    
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, panelHeight * 0.5f), ImVec2(FLT_MAX, panelHeight * 2.0f));
    
    ImGui::BeginChild("OutputPanel", ImVec2(0, panelHeight), true);
    
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "输出日志");
    ImGui::Separator();
    
    // 显示日志内容
    std::vector<std::string> logCopy;
    g_UI.CopyLogTo(logCopy);
    
    ImGui::BeginChild("LogScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    
    for (const auto& line : logCopy) {
        // 根据日志内容着色
        if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
        } else if (line.find("Warning") != std::string::npos || line.find("warning") != std::string::npos) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", line.c_str());
        } else if (line.find("Success") != std::string::npos || line.find("success") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", line.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", line.c_str());
        }
    }
    
    // 自动滚动到底部
    if (g_UI.autoScrollOutput && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
    ImGui::EndChild();
}

// ============================================================================
// 状态栏渲染
// ============================================================================

void RenderStatusBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    
    float statusBarHeight = g_Layout.statusBarHeight * g_Layout.uiScale;
    
    ImGui::BeginChild("StatusBar", ImVec2(0, statusBarHeight), false, ImGuiWindowFlags_NoScrollbar);
    
    // 左侧：文件信息
    if (elfParser.IsLoaded()) {
        const ElfHeaderInfo& header = elfParser.GetHeaderInfo();
        
        // 架构信息
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%s", header.machineName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "|");
        ImGui::SameLine();
        
        // 文件类型
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.8f, 1.0f), "%s", header.typeName.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "|");
        ImGui::SameLine();
        
        // 文件大小
        char sizeBuf[32];
        size_t fileSize = elfParser.GetFileSize();
        if (fileSize > 1024 * 1024) {
            snprintf(sizeBuf, sizeof(sizeBuf), "%.1f MB", fileSize / (1024.0 * 1024.0));
        } else if (fileSize > 1024) {
            snprintf(sizeBuf, sizeof(sizeBuf), "%.1f KB", fileSize / 1024.0);
        } else {
            snprintf(sizeBuf, sizeof(sizeBuf), "%zu B", fileSize);
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", sizeBuf);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "|");
        ImGui::SameLine();
        
        // 导出/导入函数数量
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.6f, 1.0f), "Exports: %zu", elfParser.GetExports().size());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.4f, 1.0f), "Imports: %zu", elfParser.GetImports().size());
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "未加载文件");
    }
    
    // 右侧：当前地址
    ImGui::SameLine(ImGui::GetWindowWidth() - 150 * g_Layout.uiScale);
    if (g_AsmView.initialized && !g_AsmView.instructions.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.5f, 1.0f), "Addr: %s", FormatAddressCompact(g_AsmView.currentAddress));
    }
    
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
