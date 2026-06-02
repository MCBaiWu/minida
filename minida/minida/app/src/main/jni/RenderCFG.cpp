// ============================================================================
// RenderCFG.cpp — 控制流图主入口
// 使用封装模块：
//   cfg/CFGBlockBuilder.hpp  — 第一阶段：基本块生成与布局
//   cfg/CFGEdgeRouter.hpp    — 第二/三阶段：管道网格 + A*寻路 + 边路由
//   cfg/CFGBlockRenderer.h/.cpp — 绘制基本块和边的样式
// ============================================================================

#include "Common.h"
#include "cfg/CFGBlockRenderer.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <cmath>

// ============================================================================
// 全局状态（布局缓存）
// ============================================================================
static std::unordered_map<uint64_t, GraphLayoutEx> g_layouts;
static std::unordered_map<uint64_t, uint64_t> g_selectedNodeByFunc;
static std::unordered_map<uint64_t, CFGDragState> g_dragStates;
static std::unordered_map<uint64_t, float> s_lastZoom;

// ============================================================================
// CFGGraph::Clear()
// ============================================================================
void CFGGraph::Clear() {
    nodes.clear(); edges.clear(); valid = false;
    funcName.clear(); funcAddress = 0;
}

// ============================================================================
// 主构建流程：三阶段
//   阶段1：基本块识别 + 拓扑分层 + 坐标分配（CFGBlockBuilder）
//   阶段2：管道网格构建（CFGPipeGridBuilder）
//   阶段3：A*寻路 + 边路由（CFGEdgeRouter）
// ============================================================================
void BuildCFGGraph(CFGGraph& graph, uint64_t funcAddress,
                   const std::vector<AsmInstruction>& instructions) {
    graph.Clear();
    graph.funcAddress = funcAddress;
    if (instructions.empty()) return;
    graph.funcName = g_Layout.currentFunctionName.empty() ? "unknown" : g_Layout.currentFunctionName;

    GraphLayoutEx& layout = g_layouts[funcAddress];
    layout.Clear();

    // ---- 阶段1：基本块生成 + 布局 ----
    CFGBlockBuilder::BuildBlocks(layout, funcAddress, instructions);

    // ---- 阶段2：构建管道网格 ----
    CFGPipeGridBuilder::BuildPipeGrid(layout);

    // ---- 阶段3：边路由（回边标记 + 端口序号 + A*寻路） ----
    CFGEdgeRouter::RouteAllEdges(layout);

    // ---- 同步外部图（CFGGraph） ----
    graph.nodes.clear(); graph.edges.clear();
    for (const auto& nd : layout.nodes) {
        if (nd.is_dummy) continue;
        CFGNode n;
        n.startAddr = nd.startAddr; n.endAddr = nd.endAddr;
        n.disassembly = nd.disassembly;
        n.isEntry = nd.isEntry; n.isExit = nd.isExit;
        n.position = ImVec2(nd.x, nd.y); n.size = nd.size;
        graph.nodes.push_back(n);
    }

    std::unordered_map<uint64_t, int> addr2idx;
    for (size_t i = 0; i < graph.nodes.size(); i++)
        addr2idx[graph.nodes[i].startAddr] = (int)i;

    for (const auto& edge : layout.edges) {
        if (layout.nodes[edge.from].is_dummy || layout.nodes[edge.to].is_dummy) continue;
        CFGEdge e;
        e.fromAddr = layout.nodes[edge.from].startAddr;
        e.toAddr = layout.nodes[edge.to].startAddr;
        e.type = edge.type;
        graph.edges.push_back(e);
    }
    graph.valid = true;
}

// ============================================================================
// 工具栏
// ============================================================================
void RenderCFGToolbar(CFGViewState& tab, bool isWindowLevel) {
    ImGui::PushID((int)(tab.funcAddress & 0xFFFFFFFF) ^ (isWindowLevel ? 0xAAAA : 0x5555));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.35f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.50f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.40f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.40f, 0.70f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.50f, 0.80f, 0.50f, 1.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("缩放:"); ImGui::SameLine();
    if (ImGui::Button("-")) tab.zoom = std::max(0.3f, tab.zoom - 0.1f); ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1.0f), "%.2fx", tab.zoom); ImGui::SameLine();
    if (ImGui::Button("+")) tab.zoom = std::min(3.0f, tab.zoom + 0.1f); ImGui::SameLine();
    ImGui::SetNextItemWidth(300.0f);
    float zoomCopy = tab.zoom;
    if (ImGui::SliderFloat("##zoomslider", &zoomCopy, 0.3f, 3.0f, "%.1fx")) tab.zoom = zoomCopy;
    ImGui::SameLine();
    if (ImGui::Button("重置")) {
        tab.scroll = ImVec2(0, 0); tab.zoom = 1.0f;
        g_selectedNodeByFunc[tab.funcAddress] = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("刷新")) {
        BuildCFGGraph(tab.graph, tab.funcAddress, g_AsmView.instructions);
        tab.valid = tab.graph.valid;
    }
    ImGui::SameLine();
    uint64_t sel = g_selectedNodeByFunc[tab.funcAddress];
    if (!tab.graph.nodes.empty() && sel != 0)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "+0x%llX", (unsigned long long)(sel - tab.graph.nodes[0].startAddr));
    else
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.7f), "单击高亮");
    ImGui::SameLine();
    auto itLayout = g_layouts.find(tab.funcAddress);
    if (itLayout != g_layouts.end()) {
        bool show = itLayout->second.pg.showPipes;
        if (ImGui::Button(show ? "隐藏管道" : "显示管道"))
            itLayout->second.pg.showPipes = !show;
    }
    ImGui::PopStyleColor(5); ImGui::PopID();
}

// ============================================================================
// 画布渲染
// ============================================================================
void RenderCFGCanvas(CFGViewState& tabState) {
    if (!tabState.graph.valid) return;
    auto itLayout = g_layouts.find(tabState.funcAddress);
    if (itLayout == g_layouts.end()) return;
    GraphLayoutEx& layout = itLayout->second;
    PipeGrid& pg = layout.pg;

    ImGui::BeginChild("##CFGCanvasArea", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 50) canvasSize.x = 50;
    if (canvasSize.y < 50) canvasSize.y = 50;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    drawList->AddRectFilled(canvasPos,
                            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                            IM_COL32(25, 25, 30, 255));
    drawList->PushClipRect(canvasPos,
                            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

    // 缩放保持中心
    float& lastZoom = s_lastZoom[tabState.funcAddress];
    if (lastZoom > 0.0f && std::fabs(tabState.zoom - lastZoom) > 0.001f) {
        ImVec2 cc(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
        float ratio = lastZoom / tabState.zoom;
        ImVec2 aw = CFGRendererUtil::ScreenToWorld(cc, canvasPos, tabState.scroll, lastZoom);
        tabState.scroll.x = aw.x * (ratio - 1.0f) + tabState.scroll.x * ratio;
        tabState.scroll.y = aw.y * (ratio - 1.0f) + tabState.scroll.y * ratio;
    }
    lastZoom = tabState.zoom;

    // 交互
    ImGui::InvisibleButton("cfgCanvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool isHovered = ImGui::IsItemHovered(), isActive = ImGui::IsItemActive();
    CFGDragState& ds = g_dragStates[tabState.funcAddress];
    if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        if (!ds.active) {
            ds.active = true;
            ds.startMouse = io.MousePos;
            ds.startScroll = tabState.scroll;
        }
        tabState.scroll.x = ds.startScroll.x + (io.MousePos.x - ds.startMouse.x) / tabState.zoom;
        tabState.scroll.y = ds.startScroll.y + (io.MousePos.y - ds.startMouse.y) / tabState.zoom;
    } else ds.active = false;

    if (io.MouseWheel != 0.0f && isHovered) {
        float zf = io.MouseWheel > 0.0f ? 1.08f : 0.93f;
        tabState.zoom *= zf;
        tabState.zoom = CFGGridUtil::ClampFloat(tabState.zoom, 0.3f, 3.0f);
    }

    // 节点点击选择
    uint64_t& selectedAddr = g_selectedNodeByFunc[tabState.funcAddress];
    if (ImGui::IsMouseClicked(0) && isHovered && !ImGui::IsMouseDragging(0, 5.0f)) {
        ImVec2 mw = CFGRendererUtil::ScreenToWorld(io.MousePos, canvasPos, tabState.scroll, tabState.zoom);
        bool hit = false;
        for (auto& node : tabState.graph.nodes) {
            if (mw.x >= node.position.x && mw.x <= node.position.x + node.size.x &&
                mw.y >= node.position.y && mw.y <= node.position.y + node.size.y) {
                selectedAddr = node.startAddr; hit = true; break;
            }
        }
        if (!hit) selectedAddr = 0;
    }

    // ---- 绘制管道网格（调试） ----
    if (pg.showPipes) {
        RenderPipeGrid(drawList, pg, canvasPos, canvasSize, tabState.scroll, tabState.zoom);
    }

    // ---- 绘制边 ----
    RenderAllEdges(drawList, layout, selectedAddr, canvasPos, tabState.scroll, tabState.zoom);

    // ---- 绘制节点 ----
    uint64_t firstNodeAddr = tabState.graph.nodes.empty() ? 0 : tabState.graph.nodes[0].startAddr;
    ImFont* font = ImGui::GetFont();
    RenderAllNodes(drawList, tabState.graph.nodes, selectedAddr, firstNodeAddr,
                   canvasPos, canvasSize, tabState.scroll, tabState.zoom, font);

    drawList->PopClipRect();
    ImGui::EndChild();
}

// ============================================================================
// CFGView 管理
// ============================================================================
void RenderCFGView() {
    if (g_CFGManager.GetTabCount() == 0) { g_Layout.showCFGView = false; return; }
    if (!g_Layout.showCFGView && g_CFGManager.GetTabCount() > 0) g_Layout.showCFGView = true;

    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (!ImGui::Begin("控制流图##CFGView", &g_Layout.showCFGView, flags)) {
        ImGui::End(); return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("视图")) {
            if (ImGui::MenuItem("刷新", "F5")) {
                if (g_Layout.currentCFGTab >= 0 && g_Layout.currentCFGTab < g_CFGManager.GetTabCount()) {
                    CFGViewState& tab = g_CFGManager.tabs[g_Layout.currentCFGTab];
                    BuildCFGGraph(tab.graph, tab.funcAddress, g_AsmView.instructions);
                    tab.valid = tab.graph.valid;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("重置视图", nullptr)) {
                if (g_Layout.currentCFGTab >= 0 && g_Layout.currentCFGTab < g_CFGManager.GetTabCount()) {
                    CFGViewState& tab = g_CFGManager.tabs[g_Layout.currentCFGTab];
                    tab.scroll = ImVec2(0, 0); tab.zoom = 1.0f;
                    g_selectedNodeByFunc[tab.funcAddress] = 0;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("关闭视图", "Ctrl+W")) g_Layout.showCFGView = false;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    for (auto& tab : g_CFGManager.tabs) {
        char tbuf[128];
        snprintf(tbuf, sizeof(tbuf), "CFG: %s###CFG_%llx",
                 tab.graph.funcName.empty() ? "未知" : tab.graph.funcName.c_str(),
                 (unsigned long long)tab.funcAddress);
        tab.title = tbuf;
    }

    ImGuiTabBarFlags tbf = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs;
    if (ImGui::BeginTabBar("CFGTabBar", tbf)) {
        for (int i = 0; i < g_CFGManager.GetTabCount(); ) {
            CFGViewState& tab = g_CFGManager.tabs[i];
            bool keepOpen = true;
            if (ImGui::BeginTabItem(tab.title.c_str(), &keepOpen)) {
                g_Layout.currentCFGTab = i;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) keepOpen = false;
                ImGui::Separator(); RenderCFGToolbar(tab, false); ImGui::Separator();
                if (tab.graph.valid) {
                    RenderCFGCanvas(tab);
                } else {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    ImGui::BeginChild("##CFGInvalidArea", avail, false,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                    ImVec2 childSize = ImGui::GetContentRegionAvail();
                    float cx = childSize.x * 0.5f, cy = childSize.y * 0.5f;
                    ImGui::SetCursorPos(ImVec2(cx - 80, cy - 30));
                    ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "CFG图无效，请重新生成");
                    ImGui::SetCursorPos(ImVec2(cx - 75, cy + 10));
                    if (ImGui::Button("生成CFG图", ImVec2(150, 35))) {
                        BuildCFGGraph(tab.graph, tab.funcAddress, g_AsmView.instructions);
                        tab.valid = tab.graph.valid;
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            if (!keepOpen) {
                g_CFGManager.CloseCFGView(i);
                if (g_Layout.currentCFGTab >= i && g_CFGManager.GetTabCount() > 0)
                    g_Layout.currentCFGTab = std::max(0, g_Layout.currentCFGTab - 1);
            } else i++;
        }
        ImGui::EndTabBar();
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "没有打开的CFG视图");
    }
    ImGui::End();
}

// ============================================================================
// CFGManager 实现
// ============================================================================
int CFGManager::CreateCFGView(uint64_t funcAddr, const std::string& funcName) {
    for (int i = 0; i < (int)tabs.size(); i++)
        if (tabs[i].funcAddress == funcAddr && tabs[i].valid) return i;
    CFGViewState newTab;
    newTab.funcAddress = funcAddr;
    newTab.title = "CFG##" + std::to_string(funcAddr);
    newTab.valid = false;
    newTab.zoom = 1.0f;
    newTab.scroll = ImVec2(0, 0);
    tabs.push_back(newTab);
    return (int)tabs.size() - 1;
}

void CFGManager::CloseCFGView(int index) {
    if (index >= 0 && index < (int)tabs.size()) {
        g_selectedNodeByFunc.erase(tabs[index].funcAddress);
        tabs.erase(tabs.begin() + index);
    }
}

bool CFGManager::HasOpenTabs() const { return !tabs.empty(); }
int CFGManager::GetTabCount() const { return (int)tabs.size(); }
int CFGManager::FindTabByAddress(uint64_t funcAddr) const {
    for (int i = 0; i < (int)tabs.size(); i++)
        if (tabs[i].funcAddress == funcAddr && tabs[i].valid) return i;
    return -1;
}
