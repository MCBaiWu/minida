#pragma once
// ============================================================================
// CFGBlockBuilder.hpp — 第一阶段：基本块生成与布局算法
// 职责：基本块识别、节点创建、边创建、拓扑分层、坐标分配
// ============================================================================

#include "Common.h"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <map>
#include <set>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

// ============================================================================
// 常量（布局相关）
// ============================================================================
#ifndef CFG_GRID_SIZE
#define CFG_GRID_SIZE 20
#endif

#ifndef CFG_NODE_MIN_WIDTH
#define CFG_NODE_MIN_WIDTH 200.0f
#endif

#ifndef CFG_NODE_MIN_HEIGHT
#define CFG_NODE_MIN_HEIGHT 90.0f
#endif

#ifndef CFG_NODE_MAX_WIDTH
#define CFG_NODE_MAX_WIDTH 400.0f
#endif

#ifndef CFG_NODE_MAX_HEIGHT
#define CFG_NODE_MAX_HEIGHT 600.0f
#endif

#ifndef CFG_H_SPACING
#define CFG_H_SPACING 140
#endif

#ifndef CFG_V_SPACING
#define CFG_V_SPACING 160
#endif

#ifndef CFG_NODE_MARGIN
#define CFG_NODE_MARGIN 16.0f
#endif

// ============================================================================
// 网格工具（内联函数）
// ============================================================================
namespace CFGGridUtil {
    static inline int ToGrid(float world) { return (int)std::round(world / CFG_GRID_SIZE); }
    static inline float ToWorld(int grid) { return grid * CFG_GRID_SIZE; }
    static inline float SnapToGrid(float world) { return ToWorld(ToGrid(world)); }
    static inline float ClampFloat(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

// ============================================================================
// 扩展节点（内部布局用）
// ============================================================================
struct CFGNodeEx {
    uint64_t startAddr = 0, endAddr = 0;
    std::string disassembly;
    bool isEntry = false, isExit = false;
    ImVec2 size = ImVec2(100, 50);
    float x = 0.0f, y = 0.0f;
    int layer = -1, pos_in_layer = -1;
    bool is_dummy = false, is_reversed = false;
    ImU32 bgColor = IM_COL32(42, 52, 42, 255);
    ImU32 borderColor = IM_COL32(90, 130, 90, 255);
    int gridRow = -1, gridCol = -1;

    ImVec2 GetTopPort() const { return ImVec2(x + size.x * 0.5f, y); }
    ImVec2 GetBottomPort() const { return ImVec2(x + size.x * 0.5f, y + size.y); }
    ImVec2 GetLeftPort() const { return ImVec2(x, y + size.y * 0.5f); }
    ImVec2 GetRightPort() const { return ImVec2(x + size.x, y + size.y * 0.5f); }
    float BlockLeft() const { return x - CFG_NODE_MARGIN; }
    float BlockRight() const { return x + size.x + CFG_NODE_MARGIN; }
    float BlockTop() const { return y - CFG_NODE_MARGIN; }
    float BlockBottom() const { return y + size.y + CFG_NODE_MARGIN; }
    bool Contains(float px, float py) const {
        return px >= BlockLeft() && px <= BlockRight() && py >= BlockTop() && py <= BlockBottom();
    }
};

// ============================================================================
// 扩展边（内部布局用）
// ============================================================================
struct CFGEdgeEx {
    int from = -1, to = -1;
    CFGEdgeType type = CFGEdgeType::Normal;
    bool is_reversed = false;
    std::vector<ImVec2> path;
    int priority = 0;
    
    int inPortIndex = 0;    // 目标节点入口序号（新增）
    int portIndex = 0;
};

// ============================================================================
// 层结构
// ============================================================================
struct CFGLayer {
    std::vector<int> nodes;
    float height = 0.0f, width = 0.0f, position = 0.0f, gap = 0.0f;
};

// ============================================================================
// 占用段类型（边重叠检测）
// ============================================================================
enum class SegType { HORIZONTAL, VERTICAL };
struct EdgeSegment {
    SegType type;
    float fixedCoord;
    float minCoord, maxCoord;
    int laneIdx = 0;
};

class OccupancyMap {
public:
    std::vector<EdgeSegment> segments;

    bool IsSegmentOverlapping(const EdgeSegment& seg, float tolerance = 2.0f) const {
        for (const auto& e : segments) {
            if (e.type != seg.type) continue;
            if (std::fabs(e.fixedCoord - seg.fixedCoord) < tolerance) {
                float overlapMin = std::max(e.minCoord, seg.minCoord);
                float overlapMax = std::min(e.maxCoord, seg.maxCoord);
                if (overlapMax - overlapMin > tolerance) return true;
            }
        }
        return false;
    }
    void AddSegment(const EdgeSegment& seg) { segments.push_back(seg); }
    void Clear() { segments.clear(); }
};

// ============================================================================
// 布局主结构
// ============================================================================
struct GraphLayout {
    std::vector<CFGNodeEx> nodes;
    std::vector<CFGEdgeEx> edges;
    std::vector<CFGLayer> layers;
    int n_layers = 0;
    float max_x = 0.0f, max_y = 0.0f;
    std::vector<std::vector<int>> succs, preds;
    OccupancyMap occupancy;

    void Clear() {
        nodes.clear(); edges.clear(); layers.clear();
        n_layers = 0; max_x = max_y = 0.0f;
        succs.clear(); preds.clear();
        occupancy.Clear();
    }
};

// ============================================================================
// 工具函数
// ============================================================================
namespace CFGBlockBuilder {

    // 判断是否为返回指令
    static bool IsReturnInstruction(const std::string& m) {
        if (m.empty()) return false;
        static const char* rets[] = {
            "ret", "bx lr", "pop", "ldm", "mov pc", "b ", "bxeq", "bxne",
            "popeq", "popne", "ldmia", "ldmfd", "moveq", "movne",
            "beq", "bne", "bgt", "blt", "bge", "ble"
        };
        std::string l = m;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        for (auto* r : rets)
            if (l.find(r) == 0) return true;
        return false;
    }

    // 格式化偏移字符串
    static std::string FormatOffsetStr(uint64_t addr, uint64_t baseAddr) {
        int64_t o = (int64_t)addr - (int64_t)baseAddr;
        char buf[32];
        if (o >= 0) snprintf(buf, sizeof(buf), "+0x%llX", (unsigned long long)o);
        else snprintf(buf, sizeof(buf), "-0x%llX", (unsigned long long)(-o));
        return buf;
    }

    // 计算端口偏移
    static float CalcPortOffset(int portIndex, int totalEdges, float nodeWidth) {
        if (totalEdges <= 1) return 0.0f;
        float maxOffset = nodeWidth * 0.35f;
        if (maxOffset < 30.0f) maxOffset = 30.0f;
        if (maxOffset > nodeWidth * 0.4f) maxOffset = nodeWidth * 0.4f;
        float step = (2.0f * maxOffset) / (totalEdges + 1);
        return -maxOffset + step * (portIndex + 1);
    }

} // namespace CFGBlockBuilder

// ============================================================================
// 第一阶段：拓扑分层
// ============================================================================
namespace CFGLayoutAlgo {

    // 拓扑排序 + 层分配
    static void AssignLayersTopo(GraphLayout& g) {
        int n = (int)g.nodes.size();
        if (n == 0) return;
        std::vector<int> inDeg(n, 0);
        for (int i = 0; i < n; i++) inDeg[i] = (int)g.preds[i].size();
        std::queue<int> q;
        for (int i = 0; i < n; i++) if (inDeg[i] == 0) q.push(i);
        std::vector<int> order;
        while (!q.empty()) {
            int u = q.front(); q.pop(); order.push_back(u);
            for (int v : g.succs[u]) { inDeg[v]--; if (inDeg[v] == 0) q.push(v); }
        }
        for (auto& nd : g.nodes) nd.layer = 0;
        for (int u : order)
            for (int v : g.succs[u])
                if (g.nodes[v].layer < g.nodes[u].layer + 1)
                    g.nodes[v].layer = g.nodes[u].layer + 1;
    }

    // 创建层
    static void CreateLayers(GraphLayout& g) {
        int n = (int)g.nodes.size();
        if (n == 0) return;
        g.n_layers = 0;
        for (auto& nd : g.nodes)
            if (nd.layer > g.n_layers) g.n_layers = nd.layer;
        g.n_layers++;
        g.layers.resize(g.n_layers);
        for (int i = 0; i < n; i++) {
            int l = g.nodes[i].layer;
            if (l >= 0 && l < g.n_layers) g.layers[l].nodes.push_back(i);
        }
    }

    // 坐标分配
    static void AssignCoordinates(GraphLayout& g) {
        int n = (int)g.nodes.size();
        if (n == 0) return;

        using CFGGridUtil::SnapToGrid;
        using CFGGridUtil::ClampFloat;

        for (auto& nd : g.nodes) {
            nd.size.x = SnapToGrid(ClampFloat(nd.size.x, CFG_NODE_MIN_WIDTH, CFG_NODE_MAX_WIDTH));
            nd.size.y = SnapToGrid(ClampFloat(nd.size.y, CFG_NODE_MIN_HEIGHT, CFG_NODE_MAX_HEIGHT));
        }

        std::vector<float> layerMaxH(g.n_layers, 0.0f);
        for (int l = 0; l < g.n_layers; l++)
            for (int idx : g.layers[l].nodes)
                layerMaxH[l] = std::max(layerMaxH[l], g.nodes[idx].size.y);

        float curY = 100.0f;
        for (int l = 0; l < g.n_layers; l++) {
            for (int idx : g.layers[l].nodes)
                g.nodes[idx].y = SnapToGrid(curY);
            curY += layerMaxH[l] + CFG_V_SPACING;
        }

        for (int l = 0; l < g.n_layers; l++) {
            auto& vec = g.layers[l].nodes;
            if (vec.empty()) continue;
            float totalWidth = 0.0f;
            for (int idx : vec) totalWidth += g.nodes[idx].size.x;
            totalWidth += (vec.size() - 1) * CFG_H_SPACING;
            float startX = 400.0f - totalWidth * 0.5f;
            startX = SnapToGrid(startX);
            float cx = startX;
            for (int idx : vec) {
                g.nodes[idx].x = cx;
                cx += g.nodes[idx].size.x + CFG_H_SPACING;
            }
        }

        for (auto& nd : g.nodes) {
            nd.x = SnapToGrid(nd.x);
            nd.y = SnapToGrid(nd.y);
        }

        g.max_x = g.max_y = 0;
        for (auto& nd : g.nodes) {
            g.max_x = std::max(g.max_x, nd.x + nd.size.x);
            g.max_y = std::max(g.max_y, nd.y + nd.size.y);
        }
        g.max_x += 100;
        g.max_y += 100;
    }

} // namespace CFGLayoutAlgo

// ============================================================================
// 第一阶段主入口：识别基本块、创建节点和边
// ============================================================================
namespace CFGBlockBuilder {

    // 识别基本块起始地址
    static std::vector<uint64_t> IdentifyBlockStarts(const std::vector<AsmInstruction>& instructions) {
        std::vector<uint64_t> blockStarts;
        if (instructions.empty()) return blockStarts;

        blockStarts.push_back(instructions.front().address);
        for (const auto& inst : instructions) {
            if (!inst.isBranch) continue;
            if (inst.branchTarget != 0) {
                uint64_t t = inst.branchTarget;
                if (t >= instructions.front().address && t <= instructions.back().address + 16)
                    if (std::find(blockStarts.begin(), blockStarts.end(), t) == blockStarts.end())
                        blockStarts.push_back(t);
            }
            uint64_t nxt = inst.address + inst.bytes;
            if (nxt >= instructions.front().address && nxt <= instructions.back().address + 16)
                if (std::find(blockStarts.begin(), blockStarts.end(), nxt) == blockStarts.end())
                    blockStarts.push_back(nxt);
        }
        std::sort(blockStarts.begin(), blockStarts.end());
        return blockStarts;
    }

    // 创建节点（基本块）
    static void CreateNodes(GraphLayout& layout, const std::vector<AsmInstruction>& instructions,
                            const std::vector<uint64_t>& blockStarts,
                            std::unordered_map<uint64_t, int>& addr2idx) {
        using CFGGridUtil::SnapToGrid;
        using CFGGridUtil::ClampFloat;

        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        float charWidth = fontSize * 0.55f;
        float lineHeight = fontSize * 1.4f;
        const float TITLE_H = lineHeight * 1.5f;
        const float PAD_X = 20.0f, PAD_Y = 16.0f;
        uint64_t baseAddr = instructions.front().address;

        for (size_t i = 0; i < blockStarts.size(); i++) {
            CFGNodeEx nd;
            nd.startAddr = blockStarts[i];
            nd.endAddr = (i + 1 < blockStarts.size()) ? blockStarts[i + 1] :
                         (instructions.back().address + instructions.back().bytes);
            nd.isEntry = (i == 0);
            nd.isExit = (i + 1 >= blockStarts.size());

            std::string asmText;
            int lineCount = 0;
            float maxLineWidth = 0.0f;
            for (const auto& inst : instructions) {
                if (inst.address >= nd.startAddr && inst.address < nd.endAddr) {
                    char buf[256];
                    std::string off = FormatOffsetStr(inst.address, baseAddr);
                    snprintf(buf, sizeof(buf), "%s %s", off.c_str(), inst.mnemonic.c_str());
                    asmText += buf;
                    asmText += "\n";
                    lineCount++;
                    float w = (float)strlen(buf) * charWidth;
                    if (w > maxLineWidth) maxLineWidth = w;
                }
            }
            if (!asmText.empty() && asmText.back() == '\n') asmText.pop_back();
            nd.disassembly = asmText;
            nd.size.x = std::max(CFG_NODE_MIN_WIDTH, maxLineWidth + PAD_X * 2.5f);
            nd.size.y = std::max(CFG_NODE_MIN_HEIGHT, TITLE_H + PAD_Y * 2 + lineCount * lineHeight + 10.0f);

            if (nd.isEntry) {
                nd.bgColor = IM_COL32(42, 62, 42, 255);
                nd.borderColor = IM_COL32(70, 190, 70, 255);
            } else if (nd.isExit) {
                nd.bgColor = IM_COL32(62, 42, 42, 255);
                nd.borderColor = IM_COL32(190, 70, 70, 255);
            }

            addr2idx[nd.startAddr] = (int)layout.nodes.size();
            layout.nodes.push_back(nd);
        }
    }

    // 创建边（控制流边）
    static void CreateEdges(GraphLayout& layout, const std::vector<AsmInstruction>& instructions,
                            const std::unordered_map<uint64_t, int>& addr2idx) {
        layout.succs.resize(layout.nodes.size());
        layout.preds.resize(layout.nodes.size());

        for (size_t i = 0; i < layout.nodes.size(); i++) {
            const AsmInstruction* last = nullptr;
            for (const auto& inst : instructions)
                if (inst.address >= layout.nodes[i].startAddr && inst.address < layout.nodes[i].endAddr)
                    if (!last || inst.address > last->address) last = &inst;
            if (!last) continue;

            bool isJump = last->isJump, isCall = last->isCall, isBranch = last->isBranch;
            bool isRet = IsReturnInstruction(last->mnemonic);

            if ((isJump || isBranch || isCall) && last->branchTarget != 0) {
                auto it = addr2idx.find(last->branchTarget);
                if (it != addr2idx.end() && it->second != (int)i) {
                    CFGEdgeEx e;
                    e.from = (int)i;
                    e.to = it->second;
                    if (isCall) e.type = CFGEdgeType::Call;
                    else if (isBranch) e.type = CFGEdgeType::True;
                    else e.type = CFGEdgeType::Jump;
                    layout.edges.push_back(e);
                    layout.succs[i].push_back(it->second);
                    layout.preds[it->second].push_back((int)i);
                }
            }

            if (!isJump && !isRet) {
                uint64_t nxt = last->address + last->bytes;
                auto it = addr2idx.find(nxt);
                if (it != addr2idx.end() && it->second != (int)i) {
                    CFGEdgeEx e;
                    e.from = (int)i;
                    e.to = it->second;
                    e.type = isBranch ? CFGEdgeType::False : CFGEdgeType::Normal;
                    layout.edges.push_back(e);
                    layout.succs[i].push_back(it->second);
                    layout.preds[it->second].push_back((int)i);
                } else if (i + 1 < layout.nodes.size()) {
                    CFGEdgeEx e;
                    e.from = (int)i;
                    e.to = (int)(i + 1);
                    e.type = isBranch ? CFGEdgeType::False : CFGEdgeType::Normal;
                    layout.edges.push_back(e);
                    layout.succs[i].push_back((int)(i + 1));
                    layout.preds[i + 1].push_back((int)i);
                }
            }
        }

        // 确保没有孤立节点（无后继的非退出节点连接到下一个）
        for (size_t i = 0; i < layout.nodes.size(); i++) {
            if (layout.succs[i].empty() && i + 1 < layout.nodes.size() && !layout.nodes[i].isExit) {
                CFGEdgeEx e;
                e.from = (int)i;
                e.to = (int)(i + 1);
                e.type = CFGEdgeType::Normal;
                layout.edges.push_back(e);
                layout.succs[i].push_back((int)(i + 1));
                layout.preds[i + 1].push_back((int)i);
            }
        }
    }

    // 完整的第一阶段：构建基本块 + 布局
    static void BuildBlocks(GraphLayout& layout, uint64_t funcAddress,
                            const std::vector<AsmInstruction>& instructions) {
        layout.Clear();
        if (instructions.empty()) return;

        // 1. 识别基本块起始地址
        auto blockStarts = IdentifyBlockStarts(instructions);

        // 2. 创建节点
        std::unordered_map<uint64_t, int> addr2idx;
        CreateNodes(layout, instructions, blockStarts, addr2idx);

        // 3. 创建边
        CreateEdges(layout, instructions, addr2idx);

        // 4. 拓扑分层
        CFGLayoutAlgo::AssignLayersTopo(layout);

        // 5. 创建层
        CFGLayoutAlgo::CreateLayers(layout);

        // 6. 坐标分配
        CFGLayoutAlgo::AssignCoordinates(layout);
    }

} // namespace CFGBlockBuilder
