#pragma once
// ============================================================================
// CFGBlockRenderer.h — 控制流图绘制模块（基本块样式 + 边样式）
// 职责：绘制节点（基本块）、边（含箭头）、管道网格可视化、画布交互
// ============================================================================

#include "cfg/CFGEdgeRouter.hpp"

// ============================================================================
// 坐标转换工具
// ============================================================================
namespace CFGRendererUtil {

    static inline ImVec2 WorldToScreen(ImVec2 p, const ImVec2& canvasPos,
                                        const ImVec2& scroll, float zoom) {
        return ImVec2(canvasPos.x + (p.x + scroll.x) * zoom,
                      canvasPos.y + (p.y + scroll.y) * zoom);
    }

    static inline ImVec2 ScreenToWorld(ImVec2 p, const ImVec2& canvasPos,
                                        const ImVec2& scroll, float zoom) {
        return ImVec2((p.x - canvasPos.x) / zoom - scroll.x,
                      (p.y - canvasPos.y) / zoom - scroll.y);
    }

} // namespace CFGRendererUtil

// ============================================================================
// 拖动状态
// ============================================================================
struct CFGDragState {
    ImVec2 startMouse, startScroll;
    bool active;
    CFGDragState() : active(false) {}
};

// ============================================================================
// 绘制函数声明
// ============================================================================

// 绘制管道网格（调试可视化）
void RenderPipeGrid(ImDrawList* drawList, const PipeGrid& pg,
                     const ImVec2& canvasPos, const ImVec2& canvasSize,
                     const ImVec2& scroll, float zoom);

// 绘制单条边（含箭头）
void RenderEdge(ImDrawList* drawList, const CFGEdgeEx& edge,
                const GraphLayoutEx& layout, uint64_t selectedAddr,
                const ImVec2& canvasPos, const ImVec2& scroll, float zoom,
                bool highlight);

// 绘制所有边
void RenderAllEdges(ImDrawList* drawList, const GraphLayoutEx& layout,
                     uint64_t selectedAddr,
                     const ImVec2& canvasPos, const ImVec2& scroll, float zoom);

// 绘制单个节点（基本块）
void RenderNode(ImDrawList* drawList, const CFGNode& node,
                uint64_t selectedAddr, uint64_t firstNodeAddr,
                const ImVec2& canvasPos, const ImVec2& scroll, float zoom,
                ImFont* font);

// 绘制所有节点
void RenderAllNodes(ImDrawList* drawList, const std::vector<CFGNode>& nodes,
                    uint64_t selectedAddr, uint64_t firstNodeAddr,
                    const ImVec2& canvasPos, const ImVec2& canvasSize,
                    const ImVec2& scroll, float zoom, ImFont* font);
