// ============================================================================
// CFGBlockRenderer.cpp — 控制流图绘制实现
// 绘制基本块样式、边样式、管道网格可视化
// ============================================================================

#include "cfg/CFGBlockRenderer.h"
#include <cmath>
#include <vector>
#include <unordered_map>

// ============================================================================
// 绘制管道网格（调试可视化）
// ============================================================================
void RenderPipeGrid(ImDrawList* drawList, const PipeGrid& pg,
                    const ImVec2& canvasPos, const ImVec2& canvasSize,
                    const ImVec2& scroll, float zoom) {
    using CFGRendererUtil::WorldToScreen;

    // 水平管道线
    for (int r = 0; r < pg.totalRows; r += 2) {
        float sy = WorldToScreen(ImVec2(0, pg.rowCY[r]), canvasPos, scroll, zoom).y;
        drawList->AddLine(ImVec2(canvasPos.x, sy),
                          ImVec2(canvasPos.x + canvasSize.x, sy),
                          IM_COL32(120, 120, 255, 70), 1.0f);
    }
    // 垂直管道线
    for (int c = 0; c < pg.totalCols; c += 2) {
        float sx = WorldToScreen(ImVec2(pg.colCX[c], 0), canvasPos, scroll, zoom).x;
        drawList->AddLine(ImVec2(sx, canvasPos.y),
                          ImVec2(sx, canvasPos.y + canvasSize.y),
                          IM_COL32(100, 255, 180, 70), 1.0f);
    }
    // 交叉点
    for (int r = 0; r < pg.totalRows; r += 2) {
        for (int c = 0; c < pg.totalCols; c += 2) {
            ImVec2 ip(pg.colCX[c], pg.rowCY[r]);
            ImVec2 sp = WorldToScreen(ip, canvasPos, scroll, zoom);
            drawList->AddCircleFilled(sp, 2.0f * zoom, IM_COL32(255, 255, 100, 80));
        }
    }
}

// ============================================================================
// 绘制单条边（含箭头）
// ============================================================================
void RenderEdge(ImDrawList* drawList, const CFGEdgeEx& edge,
                const GraphLayoutEx& layout, uint64_t selectedAddr,
                const ImVec2& canvasPos, const ImVec2& scroll, float zoom,
                bool highlight) {
    using CFGRendererUtil::WorldToScreen;

    std::vector<ImVec2> sp;
    for (const auto& p : edge.path)
        sp.push_back(WorldToScreen(p, canvasPos, scroll, zoom));
    if (sp.size() < 2) return;

    ImU32 ec;
    float th;
    if (!highlight) {
        switch (edge.type) {
            case CFGEdgeType::True:    ec = IM_COL32(0, 230, 0, 255); break;
            case CFGEdgeType::False:   ec = IM_COL32(230, 60, 60, 255); break;
            case CFGEdgeType::Call:    ec = IM_COL32(80, 160, 255, 255); break;
            case CFGEdgeType::Jump:    ec = IM_COL32(255, 170, 60, 255); break;
            default:                   ec = IM_COL32(160, 160, 160, 255);
        }
        th = std::max(CFG_STROKE_WIDTH, CFG_STROKE_WIDTH * zoom);
    } else {
        bool isChild = (selectedAddr != 0 && layout.nodes[edge.from].startAddr == selectedAddr);
        ec = isChild ? IM_COL32(100, 200, 255, 255) : IM_COL32(200, 140, 255, 255);
        th = std::max(CFG_HIGHLIGHT_WIDTH, CFG_HIGHLIGHT_WIDTH * zoom);
        ImU32 gc = isChild ? IM_COL32(80, 180, 255, 90) : IM_COL32(180, 120, 255, 90);
        for (size_t i = 1; i < sp.size(); i++)
            drawList->AddLine(sp[i - 1], sp[i], gc, th + 10.0f);
    }

    for (size_t i = 1; i < sp.size(); i++)
        drawList->AddLine(sp[i - 1], sp[i], ec, th);

    // 箭头
    ImVec2 tip = sp.back(), base = sp[sp.size() - 2];
    ImVec2 dir(tip.x - base.x, tip.y - base.y);
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (len > 0.01f) {
        dir.x /= len; dir.y /= len;
        float as = std::max(10.0f, CFG_ARROW_SIZE * zoom);
        float aw = as * 0.6f;
        ImVec2 perp(-dir.y * aw, dir.x * aw);
        ImVec2 ab(tip.x - dir.x * as, tip.y - dir.y * as);
        drawList->AddTriangleFilled(tip,
            ImVec2(ab.x + perp.x, ab.y + perp.y),
            ImVec2(ab.x - perp.x, ab.y - perp.y), ec);
    }
}

// ============================================================================
// 绘制所有边
// ============================================================================
void RenderAllEdges(ImDrawList* drawList, const GraphLayoutEx& layout,
                    uint64_t selectedAddr,
                    const ImVec2& canvasPos, const ImVec2& scroll, float zoom) {
    std::vector<const CFGEdgeEx*> normalEdges, highlightEdges;
    for (const auto& edge : layout.edges) {
        if (layout.nodes[edge.from].is_dummy || layout.nodes[edge.to].is_dummy) continue;
        bool isChild = (selectedAddr != 0 && layout.nodes[edge.from].startAddr == selectedAddr);
        bool isParent = (selectedAddr != 0 && layout.nodes[edge.to].startAddr == selectedAddr);
        if (isChild || isParent) highlightEdges.push_back(&edge);
        else normalEdges.push_back(&edge);
    }
    for (auto* e : normalEdges) RenderEdge(drawList, *e, layout, selectedAddr, canvasPos, scroll, zoom, false);
    for (auto* e : highlightEdges) RenderEdge(drawList, *e, layout, selectedAddr, canvasPos, scroll, zoom, true);
}

// ============================================================================
// 绘制单个节点（基本块）
// ============================================================================
void RenderNode(ImDrawList* drawList, const CFGNode& node,
                uint64_t selectedAddr, uint64_t firstNodeAddr,
                const ImVec2& canvasPos, const ImVec2& scroll, float zoom,
                ImFont* font) {
    using CFGRendererUtil::WorldToScreen;
    using CFGGridUtil::ClampFloat;

    ImVec2 mn = WorldToScreen(node.position, canvasPos, scroll, zoom);
    ImVec2 mx = WorldToScreen(ImVec2(node.position.x + node.size.x, node.position.y + node.size.y),
                               canvasPos, scroll, zoom);

    const float MIN_SW = 120, MIN_SH = 60;
    float sw = mx.x - mn.x, sh = mx.y - mn.y;
    if (sw < MIN_SW) { float c = (mn.x + mx.x) * 0.5f; mn.x = c - MIN_SW * 0.5f; mx.x = c + MIN_SW * 0.5f; }
    if (sh < MIN_SH) { float c = (mn.y + mx.y) * 0.5f; mn.y = c - MIN_SH * 0.5f; mx.y = c + MIN_SH * 0.5f; }

    bool sel = (node.startAddr == selectedAddr);
    ImU32 bg = IM_COL32(42, 52, 42, 255), bd = IM_COL32(90, 130, 90, 255);
    ImU32 tBg = IM_COL32(52, 72, 52, 255), tCol = IM_COL32(160, 220, 160, 255);
    ImU32 txCol = IM_COL32(190, 210, 190, 255);

    if (node.isEntry) {
        bg = IM_COL32(42, 62, 42, 255); bd = IM_COL32(70, 190, 70, 255);
        tBg = IM_COL32(52, 92, 52, 255); tCol = IM_COL32(140, 255, 140, 255);
    }
    if (node.isExit) {
        bg = IM_COL32(62, 42, 42, 255); bd = IM_COL32(190, 70, 70, 255);
        tBg = IM_COL32(92, 52, 52, 255); tCol = IM_COL32(255, 140, 140, 255);
    }

    if (sel) {
        bd = IM_COL32(255, 220, 100, 255);
        float gp = std::max(3.0f, 4.0f * zoom);
        drawList->AddRect(ImVec2(mn.x - gp, mn.y - gp), ImVec2(mx.x + gp, mx.y + gp),
                          IM_COL32(255, 220, 100, 100),
                          std::max(2.0f, 4.0f * zoom), 0, std::max(2.0f, 3.0f * zoom));
    }

    float fSize = ImGui::GetFontSize() * ClampFloat(zoom, 0.5f, 2.0f);
    float lh = fSize * 1.4f;
    float rnd = std::max(2.0f, 3.0f * zoom);
    float bt = std::max(1.0f, 1.5f * zoom);
    float th = lh * 1.5f;

    drawList->AddRectFilled(mn, mx, bg, rnd);
    if (th > 2.0f) drawList->AddRectFilled(mn, ImVec2(mx.x, mn.y + th), tBg, rnd);
    drawList->AddRect(mn, mx, bd, rnd, 0, bt);

    float tb = mn.y + th;
    if (tb < mx.y - 2.0f)
        drawList->AddLine(ImVec2(mn.x + rnd, tb), ImVec2(mx.x - rnd, tb), IM_COL32(70, 110, 70, 180), 1.0f);

    // 标题偏移
    char tbuf[64];
    int64_t off = (int64_t)node.startAddr - (int64_t)firstNodeAddr;
    if (off >= 0) snprintf(tbuf, sizeof(tbuf), "+0x%llX", (unsigned long long)off);
    else snprintf(tbuf, sizeof(tbuf), "-0x%llX", (unsigned long long)(-off));
    drawList->AddText(font, fSize, ImVec2(mn.x + 10 * zoom, mn.y + 4 * zoom), tCol, tbuf);

    // 反汇编文本
    if (!node.disassembly.empty() && zoom > 0.25f) {
        float tt = tb + 8 * zoom, tbt = mx.y - 6 * zoom;
        float ah = tbt - tt;
        if (ah > lh && tt < tbt) {
            ImVec2 tp(mn.x + 10 * zoom, tt);
            int ml = (int)(ah / lh), cl = 0;
            std::string line;
            float mlw = (mx.x - mn.x) - 20 * zoom;
            for (size_t c = 0; c < node.disassembly.size() && cl < ml; c++) {
                if (node.disassembly[c] == '\n') {
                    if (!line.empty()) {
                        ImVec2 ls = ImGui::CalcTextSize(line.c_str());
                        if (ls.x > mlw && line.length() > 15) {
                            float acw = ls.x / line.length();
                            int vc = (int)((mlw - ImGui::CalcTextSize("...").x) / acw);
                            vc = std::max(10, vc);
                            drawList->AddText(font, fSize, tp, txCol, (line.substr(0, vc) + "...").c_str());
                        } else {
                            drawList->AddText(font, fSize, tp, txCol, line.c_str());
                        }
                    }
                    tp.y += lh; cl++; line.clear();
                    if (tp.y + lh > tbt) break;
                } else {
                    line += node.disassembly[c];
                }
            }
            if (!line.empty() && cl < ml && tp.y + lh <= tbt) {
                ImVec2 ls = ImGui::CalcTextSize(line.c_str());
                if (ls.x > mlw && line.length() > 15) {
                    float acw = ls.x / line.length();
                    int vc = (int)((mlw - ImGui::CalcTextSize("...").x) / acw);
                    vc = std::max(10, vc);
                    drawList->AddText(font, fSize, tp, txCol, (line.substr(0, vc) + "...").c_str());
                } else {
                    drawList->AddText(font, fSize, tp, txCol, line.c_str());
                }
            }
        }
    }
}

// ============================================================================
// 绘制所有节点
// ============================================================================
void RenderAllNodes(ImDrawList* drawList, const std::vector<CFGNode>& nodes,
                    uint64_t selectedAddr, uint64_t firstNodeAddr,
                    const ImVec2& canvasPos, const ImVec2& canvasSize,
                    const ImVec2& scroll, float zoom, ImFont* font) {
    using CFGRendererUtil::WorldToScreen;

    for (size_t i = 0; i < nodes.size(); i++) {
        const auto& nd = nodes[i];
        ImVec2 mn = WorldToScreen(nd.position, canvasPos, scroll, zoom);
        ImVec2 mx = WorldToScreen(ImVec2(nd.position.x + nd.size.x, nd.position.y + nd.size.y),
                                   canvasPos, scroll, zoom);
        // 视口裁剪
        if (mx.x < canvasPos.x || mn.x > canvasPos.x + canvasSize.x ||
            mx.y < canvasPos.y || mn.y > canvasPos.y + canvasSize.y)
            continue;
        RenderNode(drawList, nd, selectedAddr, firstNodeAddr,
                   canvasPos, scroll, zoom, font);
    }
}
