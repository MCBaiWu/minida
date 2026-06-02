#pragma once
// ============================================================================
// CFGEdgeRouter.hpp — 第二/三阶段：管道网格构建 + A*寻路 + 边路由
// 职责：
//   第二阶段：构建 PipeGrid 管道网格（水平/垂直管道在行列间隙）
//   第三阶段：A*寻路在管道交叉点图上搜索，车道分配，正交路径转换
// ============================================================================

#include "cfg/CFGBlockBuilder.hpp"
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <map>
#include <cmath>
#include <vector>
#include <string>
#include <functional>
#include <limits>
#include <stack>
#include <numeric>

// ============================================================================
// 边路由常量
// ============================================================================
#ifndef CFG_ARROW_SIZE
#define CFG_ARROW_SIZE 14.0f
#endif

#ifndef CFG_STROKE_WIDTH
#define CFG_STROKE_WIDTH 3.0f
#endif

#ifndef CFG_HIGHLIGHT_WIDTH
#define CFG_HIGHLIGHT_WIDTH 5.0f
#endif

#ifndef CFG_LANE_SPACING
#define CFG_LANE_SPACING 8
#endif

#ifndef CFG_MAX_PIPE_LANES
#define CFG_MAX_PIPE_LANES 14
#endif

#ifndef CFG_TURN_PENALTY_PG
#define CFG_TURN_PENALTY_PG 200.0f
#endif

#ifndef CFG_VERT_OFFSET
#define CFG_VERT_OFFSET 30.0f
#endif

#ifndef CFG_MIN_VERTICAL_DEPARTURE
#define CFG_MIN_VERTICAL_DEPARTURE 40.0f
#endif

// ============================================================================
// PipeGrid 管道网格系统
// ============================================================================
struct PipeLaneAlloc {
    int maxLanes = CFG_MAX_PIPE_LANES;
    std::vector<bool> used;
    int nextLane = 0;

    PipeLaneAlloc() { used.resize(maxLanes, false); }

    int Alloc() {
        for (int i = 0; i < (int)used.size(); i++) {
            int idx = (nextLane + i) % (int)used.size();
            if (!used[idx]) {
                used[idx] = true;
                nextLane = (idx + 1) % (int)used.size();
                return idx;
            }
        }
        used.push_back(true);
        return (int)used.size() - 1;
    }
};

struct PipeGrid {
    int numNodeRows = 0, numNodeCols = 0;
    int totalRows = 0, totalCols = 0;
    int padTop = 3, padBot = 3, padLeft = 2, padRight = 3;
    std::vector<float> rowCY; // 每行中心Y
    std::vector<float> colCX; // 每列中心X
    std::map<int, PipeLaneAlloc> hLanes;
    std::map<int, PipeLaneAlloc> vLanes;
    bool showPipes = false;

    // 节点层l在网格中的行号
    int NodeGridRow(int l) const { return 2 * (padTop + l) + 1; }
    // 节点列c在网格中的列号
    int NodeGridCol(int c) const { return 2 * (padLeft + c) + 1; }
    // 节点层l下方的管道行号
    int BottomPipeRow(int l) const { return NodeGridRow(l) + 1; }
    // 节点层l上方的管道行号
    int TopPipeRow(int l) const { return NodeGridRow(l) - 1; }

    int AllocHLane(int pr) { return hLanes[pr].Alloc(); }
    int AllocVLane(int pc) { return vLanes[pc].Alloc(); }

    float HLaneY(int pr, int lane) const {
        float cy = (pr >= 0 && pr < totalRows) ? rowCY[pr] : 0;
        auto it = hLanes.find(pr);
        if (it == hLanes.end()) return cy;
        float off = (lane - (it->second.maxLanes - 1) * 0.5f) * (float)CFG_LANE_SPACING;
        return cy + off;
    }
    float VLaneX(int pc, int lane) const {
        float cx = (pc >= 0 && pc < totalCols) ? colCX[pc] : 0;
        auto it = vLanes.find(pc);
        if (it == vLanes.end()) return cx;
        float off = (lane - (it->second.maxLanes - 1) * 0.5f) * (float)CFG_LANE_SPACING;
        return cx + off;
    }

    // 找到离世界X最近的偶数列（管道列）
    int NearestPipeCol(float wx) const {
        int best = 0;
        float bd = 1e9f;
        for (int c = 0; c < totalCols; c += 2) {
            float d = std::fabs(colCX[c] - wx);
            if (d < bd) { bd = d; best = c; }
        }
        return best;
    }
};

// ============================================================================
// 将 PipeGrid 附加到 GraphLayout（通过继承扩展）
// ============================================================================
struct GraphLayoutEx : public GraphLayout {
    PipeGrid pg;

    void Clear() {
        GraphLayout::Clear();
        pg = PipeGrid();
    }
};

// ============================================================================
// 路径工具函数
// ============================================================================
namespace CFGPathUtil {

    // 强制正交路径
    static std::vector<ImVec2> EnforceOrthogonal(const std::vector<ImVec2>& pts) {
        if (pts.size() < 2) return pts;
        std::vector<ImVec2> res;
        res.push_back(pts[0]);
        for (size_t i = 1; i < pts.size(); ++i) {
            const auto& prev = res.back();
            const auto& curr = pts[i];
            if (std::fabs(prev.x - curr.x) > 1.0f && std::fabs(prev.y - curr.y) > 1.0f)
                res.push_back(ImVec2(prev.x, curr.y));
            res.push_back(curr);
        }
        return res;
    }

    // 简化路径（去除共线中间点）
    static std::vector<ImVec2> SimplifyPath(const std::vector<ImVec2>& pts) {
        if (pts.size() < 3) return pts;
        std::vector<ImVec2> out;
        out.push_back(pts[0]);
        for (size_t i = 1; i < pts.size() - 1; ++i) {
            const auto& a = out.back(), & b = pts[i], & c = pts[i + 1];
            bool colH = (std::fabs(a.y - b.y) < 1.0f && std::fabs(b.y - c.y) < 1.0f);
            bool colV = (std::fabs(a.x - b.x) < 1.0f && std::fabs(b.x - c.x) < 1.0f);
            if (!colH && !colV) out.push_back(b);
        }
        out.push_back(pts.back());
        return out;
    }

    // 清理路径（正交化 + 简化）
    static std::vector<ImVec2> CleanPath(const std::vector<ImVec2>& pts) {
        auto path = EnforceOrthogonal(pts);
        path = SimplifyPath(path);
        for (size_t i = 1; i < path.size(); ++i) {
            if (std::fabs(path[i].x - path[i - 1].x) > 1.0f && std::fabs(path[i].y - path[i - 1].y) > 1.0f) {
                path.insert(path.begin() + i, ImVec2(path[i - 1].x, path[i].y));
                ++i;
            }
        }
        return SimplifyPath(path);
    }

} // namespace CFGPathUtil

// ============================================================================
// 避障辅助函数
// ============================================================================
namespace CFGObstacleUtil {

    using CFGGridUtil::SnapToGrid;

    // 检查点是否被节点阻挡
    static bool IsPointBlocked(const GraphLayout& g, float x, float y,
                               int exclFrom, int exclTo, float margin = 2.0f) {
        for (int i = 0; i < (int)g.nodes.size(); ++i) {
            if (i == exclFrom || i == exclTo || g.nodes[i].is_dummy) continue;
            const auto& nd = g.nodes[i];
            if (x >= nd.BlockLeft() - margin && x <= nd.BlockRight() + margin &&
                y >= nd.BlockTop() - margin && y <= nd.BlockBottom() + margin)
                return true;
        }
        return false;
    }

    // 检查线段是否畅通
    static bool IsSegmentClear(const GraphLayout& g, ImVec2 a, ImVec2 b,
                               int exclFrom, int exclTo) {
        const float STEP = CFG_GRID_SIZE * 0.25f;
        if (std::fabs(a.x - b.x) < 1.0f) {
            float minY = std::min(a.y, b.y), maxY = std::max(a.y, b.y);
            float x = a.x;
            for (float y = minY; y <= maxY; y += STEP)
                if (IsPointBlocked(g, x, y, exclFrom, exclTo)) return false;
            if (IsPointBlocked(g, x, maxY, exclFrom, exclTo)) return false;
            return true;
        } else if (std::fabs(a.y - b.y) < 1.0f) {
            float minX = std::min(a.x, b.x), maxX = std::max(a.x, b.x);
            float y = a.y;
            for (float x = minX; x <= maxX; x += STEP)
                if (IsPointBlocked(g, x, y, exclFrom, exclTo)) return false;
            if (IsPointBlocked(g, maxX, y, exclFrom, exclTo)) return false;
            return true;
        }
        return false;
    }

    // 检查路径是否被阻挡
    static bool IsPathBlocked(const GraphLayout& g, const std::vector<ImVec2>& pts,
                               int exclFrom, int exclTo) {
        for (size_t i = 1; i < pts.size(); ++i)
            if (!IsSegmentClear(g, pts[i - 1], pts[i], exclFrom, exclTo)) return true;
        return false;
    }

    // 尝试偏移线段避开障碍
    static bool TryOffsetSegment(const GraphLayout& g, ImVec2& a, ImVec2& b,
                                  int exclFrom, int exclTo, int maxAttempts = 60) {
        if (IsSegmentClear(g, a, b, exclFrom, exclTo)) return true;
        const float STEP = CFG_LANE_SPACING * 0.5f;
        if (std::fabs(a.x - b.x) < 1.0f) {
            float baseX = a.x;
            for (int i = 1; i <= maxAttempts; ++i) {
                for (int sign : {-1, 1}) {
                    float newX = SnapToGrid(baseX + sign * i * STEP);
                    if (!IsPointBlocked(g, newX, a.y, exclFrom, exclTo) &&
                        !IsPointBlocked(g, newX, b.y, exclFrom, exclTo) &&
                        IsSegmentClear(g, ImVec2(newX, a.y), ImVec2(newX, b.y), exclFrom, exclTo)) {
                        a.x = newX; b.x = newX; return true;
                    }
                }
            }
        } else {
            float baseY = a.y;
            for (int i = 1; i <= maxAttempts; ++i) {
                for (int sign : {-1, 1}) {
                    float newY = SnapToGrid(baseY + sign * i * STEP);
                    if (!IsPointBlocked(g, a.x, newY, exclFrom, exclTo) &&
                        !IsPointBlocked(g, b.x, newY, exclFrom, exclTo) &&
                        IsSegmentClear(g, ImVec2(a.x, newY), ImVec2(b.x, newY), exclFrom, exclTo)) {
                        a.y = newY; b.y = newY; return true;
                    }
                }
            }
        }
        return false;
    }

    // 调整路径远离节点
    static std::vector<ImVec2> AdjustPathAwayFromNodes(GraphLayout& g,
            const std::vector<ImVec2>& original, int exclFrom, int exclTo) {
        if (original.size() < 2) return original;
        std::vector<ImVec2> path = original;
        bool changed = true;
        int globalIter = 0;
        while (changed && globalIter < 20) {
            changed = false;
            globalIter++;
            for (size_t i = 1; i < path.size(); ++i) {
                ImVec2 a = path[i - 1], b = path[i];
                if (!IsSegmentClear(g, a, b, exclFrom, exclTo)) {
                    if (TryOffsetSegment(g, a, b, exclFrom, exclTo)) {
                        path[i - 1] = a; path[i] = b; changed = true;
                    }
                }
            }
        }
        if (IsPathBlocked(g, path, exclFrom, exclTo)) return {};
        return path;
    }

    // 创建安全外部路径（绕过所有节点）
    static std::vector<ImVec2> CreateSafeExternalPath(const GraphLayout& g,
            int fromIdx, int toIdx, const ImVec2& start, const ImVec2& end) {
        float safeY = 0.0f;
        for (const auto& nd : g.nodes) {
            if (nd.is_dummy) continue;
            safeY = std::max(safeY, nd.BlockBottom());
        }
        safeY = SnapToGrid(safeY + 60.0f);
        std::vector<ImVec2> path;
        path.push_back(start);
        float departY = std::max(safeY, start.y + CFG_MIN_VERTICAL_DEPARTURE);
        path.push_back(ImVec2(start.x, departY));
        path.push_back(ImVec2(end.x, departY));
        path.push_back(ImVec2(end.x, end.y - CFG_VERT_OFFSET));
        path.push_back(end);
        return CFGPathUtil::CleanPath(path);
    }

    // 解决占用冲突
    static std::vector<ImVec2> ResolveOverlapWithOccupancy(GraphLayout& g,
            const std::vector<ImVec2>& candidate, int exclFrom, int exclTo) {
        auto path = candidate;
        const int MAX_GLOBAL_ITER = 30;
        const float STEP = CFG_LANE_SPACING * 0.5f;
        for (int global = 0; global < MAX_GLOBAL_ITER; ++global) {
            bool conflict = false;
            for (size_t i = 1; i < path.size(); ++i) {
                const auto& a = path[i - 1], & b = path[i];
                EdgeSegment seg;
                if (std::fabs(a.x - b.x) < 1.0f) {
                    seg.type = SegType::VERTICAL;
                    seg.fixedCoord = a.x;
                    seg.minCoord = std::min(a.y, b.y);
                    seg.maxCoord = std::max(a.y, b.y);
                } else {
                    seg.type = SegType::HORIZONTAL;
                    seg.fixedCoord = a.y;
                    seg.minCoord = std::min(a.x, b.x);
                    seg.maxCoord = std::max(a.x, b.x);
                }
                if (g.occupancy.IsSegmentOverlapping(seg)) {
                    conflict = true;
                    bool fixed = false;
                    if (seg.type == SegType::HORIZONTAL) {
                        for (int sign : {-1, 1}) {
                            for (int offset = 1; offset <= 20; ++offset) {
                                float newY = SnapToGrid(seg.fixedCoord + sign * STEP * offset);
                                if (!IsPointBlocked(g, a.x, newY, exclFrom, exclTo) &&
                                    !IsPointBlocked(g, b.x, newY, exclFrom, exclTo) &&
                                    !g.occupancy.IsSegmentOverlapping(
                                        {SegType::HORIZONTAL, newY, seg.minCoord, seg.maxCoord})) {
                                    path[i - 1].y = newY; path[i].y = newY; fixed = true; break;
                                }
                            }
                            if (fixed) break;
                        }
                    } else {
                        for (int sign : {-1, 1}) {
                            for (int offset = 1; offset <= 20; ++offset) {
                                float newX = SnapToGrid(seg.fixedCoord + sign * STEP * offset);
                                if (!IsPointBlocked(g, newX, a.y, exclFrom, exclTo) &&
                                    !IsPointBlocked(g, newX, b.y, exclFrom, exclTo) &&
                                    !g.occupancy.IsSegmentOverlapping(
                                        {SegType::VERTICAL, newX, seg.minCoord, seg.maxCoord})) {
                                    path[i - 1].x = newX; path[i].x = newX; fixed = true; break;
                                }
                            }
                            if (fixed) break;
                        }
                    }
                    if (!fixed) {
                        float bestOffset = 0;
                        bool found = false;
                        for (int sign : {-1, 1}) {
                            for (int offset = 1; offset <= 25; ++offset) {
                                float shift = sign * STEP * offset;
                                std::vector<ImVec2> shifted = path;
                                for (auto& p : shifted) {
                                    if (seg.type == SegType::HORIZONTAL) p.y += shift;
                                    else p.x += shift;
                                }
                                bool clear = true;
                                for (size_t j = 1; j < shifted.size(); ++j) {
                                    EdgeSegment s;
                                    auto& pa = shifted[j - 1], & pb = shifted[j];
                                    if (std::fabs(pa.x - pb.x) < 1.0f) {
                                        s.type = SegType::VERTICAL;
                                        s.fixedCoord = pa.x;
                                        s.minCoord = std::min(pa.y, pb.y);
                                        s.maxCoord = std::max(pa.y, pb.y);
                                    } else {
                                        s.type = SegType::HORIZONTAL;
                                        s.fixedCoord = pa.y;
                                        s.minCoord = std::min(pa.x, pb.x);
                                        s.maxCoord = std::max(pa.x, pb.x);
                                    }
                                    if (g.occupancy.IsSegmentOverlapping(s) ||
                                        !IsSegmentClear(g, pa, pb, exclFrom, exclTo)) {
                                        clear = false; break;
                                    }
                                }
                                if (clear) { bestOffset = shift; found = true; break; }
                            }
                            if (found) break;
                        }
                        if (found) {
                            for (auto& p : path) {
                                if (seg.type == SegType::HORIZONTAL) p.y += bestOffset;
                                else p.x += bestOffset;
                            }
                        } else {
                            return CreateSafeExternalPath(g, exclFrom, exclTo,
                                                          candidate.front(), candidate.back());
                        }
                    }
                    break;
                }
            }
            if (!conflict) break;
        }
        return CFGPathUtil::CleanPath(path);
    }

} // namespace CFGObstacleUtil

// ============================================================================
// 第二阶段：构建 PipeGrid 管道网格
// ============================================================================
namespace CFGPipeGridBuilder {

    using CFGGridUtil::SnapToGrid;

    // 构建管道网格
    static void BuildPipeGrid(GraphLayoutEx& g) {
        PipeGrid& pg = g.pg;
        if (g.n_layers == 0) return;

        pg.numNodeRows = g.n_layers;
        pg.numNodeCols = 0;
        for (auto& lay : g.layers)
            pg.numNodeCols = std::max(pg.numNodeCols, (int)lay.nodes.size());

        pg.totalRows = 2 * (pg.padTop + pg.numNodeRows + pg.padBot) + 1;
        pg.totalCols = 2 * (pg.padLeft + pg.numNodeCols + pg.padRight) + 1;

        // 分配节点网格位置
        for (int l = 0; l < g.n_layers; l++) {
            auto& vec = g.layers[l].nodes;
            int count = (int)vec.size();
            int startCol = (pg.numNodeCols - count) / 2;
            for (int k = 0; k < count; k++) {
                g.nodes[vec[k]].gridRow = l;
                g.nodes[vec[k]].gridCol = startCol + k;
            }
        }

        // ---- 计算每行中心Y ----
        pg.rowCY.resize(pg.totalRows, 0);

        // 节点行：使用实际节点Y
        for (int l = 0; l < pg.numNodeRows; l++) {
            int gr = pg.NodeGridRow(l);
            if (!g.layers[l].nodes.empty()) {
                float sumY = 0; int cnt = 0;
                for (int idx : g.layers[l].nodes) {
                    sumY += g.nodes[idx].y + g.nodes[idx].size.y * 0.5f;
                    cnt++;
                }
                pg.rowCY[gr] = sumY / cnt;
            }
        }

        // 层间管道行：两层中心Y的中间
        for (int l = 0; l < pg.numNodeRows - 1; l++) {
            int pr = pg.BottomPipeRow(l);
            float botL = -1e9f, topL1 = 1e9f;
            for (int idx : g.layers[l].nodes) botL = std::max(botL, g.nodes[idx].y + g.nodes[idx].size.y);
            for (int idx : g.layers[l + 1].nodes) topL1 = std::min(topL1, g.nodes[idx].y);
            pg.rowCY[pr] = SnapToGrid((botL + topL1) * 0.5f);
        }

        // 最后一层下方管道行
        {
            int lastL = pg.numNodeRows - 1;
            int pr = pg.BottomPipeRow(lastL);
            float bot = -1e9f;
            for (int idx : g.layers[lastL].nodes) bot = std::max(bot, g.nodes[idx].y + g.nodes[idx].size.y);
            pg.rowCY[pr] = SnapToGrid(bot + CFG_V_SPACING * 0.5f);
        }

        // 第一层上方管道行
        {
            int pr = pg.TopPipeRow(0);
            float top = 1e9f;
            for (int idx : g.layers[0].nodes) top = std::min(top, g.nodes[idx].y);
            pg.rowCY[pr] = SnapToGrid(top - CFG_V_SPACING * 0.5f);
        }

        // 上方padding行递推
        {
            int firstNodeGR = pg.NodeGridRow(0);
            for (int r = firstNodeGR - 1; r >= 0; r--) {
                if (pg.rowCY[r] == 0)
                    pg.rowCY[r] = SnapToGrid(pg.rowCY[r + 1] - CFG_V_SPACING);
            }
        }
        // 下方padding行递推
        {
            int lastNodeGR = pg.NodeGridRow(pg.numNodeRows - 1);
            for (int r = lastNodeGR + 1; r < pg.totalRows; r++) {
                if (pg.rowCY[r] == 0)
                    pg.rowCY[r] = SnapToGrid(pg.rowCY[r - 1] + CFG_V_SPACING);
            }
        }

        // ---- 计算每列中心X ----
        pg.colCX.resize(pg.totalCols, 0);

        // 收集每列节点的中心X
        std::map<int, std::vector<float>> colXValues;
        for (auto& nd : g.nodes) {
            if (nd.gridCol >= 0) {
                colXValues[nd.gridCol].push_back(nd.x + nd.size.x * 0.5f);
            }
        }

        // 节点列中心X
        std::map<int, float> nodeColCX;
        for (auto& kv : colXValues) {
            float sum = 0;
            for (float v : kv.second) sum += v;
            nodeColCX[kv.first] = sum / kv.second.size();
        }

        // 填充所有节点列的中心X（包括没有节点的列）
        int firstCol = -1, lastCol = -1;
        for (auto& kv : nodeColCX) {
            if (firstCol < 0 || kv.first < firstCol) firstCol = kv.first;
            if (lastCol < 0 || kv.first > lastCol) lastCol = kv.first;
        }

        float avgColSpacing = (CFG_NODE_MIN_WIDTH + CFG_H_SPACING) * 1.5f;
        if (nodeColCX.size() >= 2 && lastCol > firstCol) {
            avgColSpacing = (nodeColCX[lastCol] - nodeColCX[firstCol]) / (lastCol - firstCol);
        }

        // 向左外推
        if (firstCol >= 0) {
            for (int c = firstCol - 1; c >= 0; c--)
                nodeColCX[c] = nodeColCX[c + 1] - avgColSpacing;
            for (int c = lastCol + 1; c < pg.numNodeCols; c++)
                nodeColCX[c] = nodeColCX[c - 1] + avgColSpacing;
        }

        // 设置节点列和管道列的中心X
        for (int nc = 0; nc < pg.numNodeCols + pg.padLeft + pg.padRight; nc++) {
            int gc = 2 * nc + 1;
            if (gc >= pg.totalCols) break;
            if (nodeColCX.count(nc))
                pg.colCX[gc] = nodeColCX[nc];
            else
                pg.colCX[gc] = 400.0f + (nc - (pg.numNodeCols + pg.padLeft) * 0.5f) * avgColSpacing;
        }

        // 管道列：相邻节点列的中点
        for (int c = 0; c < pg.totalCols; c += 2) {
            float leftCX = (c > 0) ? pg.colCX[c - 1] : pg.colCX[1] - avgColSpacing;
            float rightCX = (c + 1 < pg.totalCols) ? pg.colCX[c + 1] : pg.colCX[pg.totalCols - 2] + avgColSpacing;
            pg.colCX[c] = SnapToGrid((leftCX + rightCX) * 0.5f);
        }

        // 确保所有行列都有值
        for (int r = 0; r < pg.totalRows; r++)
            if (pg.rowCY[r] == 0) pg.rowCY[r] = 100.0f + r * CFG_V_SPACING;
        for (int c = 0; c < pg.totalCols; c++)
            if (pg.colCX[c] == 0) pg.colCX[c] = 400.0f + (c - pg.totalCols * 0.5f) * avgColSpacing;

        // 初始化车道分配器
        pg.hLanes.clear();
        pg.vLanes.clear();
        for (int r = 0; r < pg.totalRows; r += 2)
            pg.hLanes[r] = PipeLaneAlloc();
        for (int c = 0; c < pg.totalCols; c += 2)
            pg.vLanes[c] = PipeLaneAlloc();
    }

} // namespace CFGPipeGridBuilder

// ============================================================================
// 第三阶段：PipeGrid A* 寻路
// ============================================================================
namespace CFGAStar {

    using CFGGridUtil::ClampFloat;

    // A* 节点
    struct PGANode {
        int pr, pc;
        float g, f;
        int parentKey, dir;
        bool closed;
    };

    // PipeGrid A* 寻路（在交叉点图上搜索）
    // 状态=(偶数行, 偶数列)，步长±2，天然避障
    static std::vector<std::pair<int, int>> PipeGridAStar(
        const PipeGrid& pg, int spr, int spc, int epr, int epc)
    {
        // 确保起终点在有效范围内且为偶数
        spr = (int)ClampFloat((float)spr, 0, (float)(pg.totalRows - 1));
        if (spr % 2 != 0) spr = std::max(0, spr - 1);
        epr = (int)ClampFloat((float)epr, 0, (float)(pg.totalRows - 1));
        if (epr % 2 != 0) epr = std::min(pg.totalRows - 1, epr + 1);
        spc = (int)ClampFloat((float)spc, 0, (float)(pg.totalCols - 1));
        if (spc % 2 != 0) spc = std::max(0, spc - 1);
        epc = (int)ClampFloat((float)epc, 0, (float)(pg.totalCols - 1));
        if (epc % 2 != 0) epc = std::min(pg.totalCols - 1, epc + 1);

        if (spr == epr && spc == epc) return {{spr, spc}};

        auto key = [](int r, int c) { return r * 100000 + c; };
        std::unordered_map<int, PGANode> ns;
        int sk = key(spr, spc);
        float h0 = std::fabs(pg.rowCY[spr] - pg.rowCY[epr]) + std::fabs(pg.colCX[spc] - pg.colCX[epc]);
        ns[sk] = {spr, spc, 0, h0, -1, -1, false};
        using PQ = std::pair<float, int>;
        std::priority_queue<PQ, std::vector<PQ>, std::greater<>> open;
        open.push({h0, sk});

        // 4方向：右(0)、左(1)、下(2)、上(3)，步长2
        const int dr[] = {0, 0, 2, -2}, dc[] = {2, -2, 0, 0};

        while (!open.empty()) {
            int ck = open.top().second; open.pop();
            auto it = ns.find(ck);
            if (it == ns.end()) continue;
            PGANode& cur = it->second;
            if (cur.closed) continue;
            if (cur.pr == epr && cur.pc == epc) {
                std::vector<std::pair<int, int>> path;
                int k = ck;
                while (k != -1) {
                    auto& n = ns[k];
                    path.push_back({n.pr, n.pc});
                    k = n.parentKey;
                }
                std::reverse(path.begin(), path.end());
                return path;
            }
            cur.closed = true;
            for (int d = 0; d < 4; d++) {
                int nr = cur.pr + dr[d], nc = cur.pc + dc[d];
                if (nr < 0 || nr >= pg.totalRows || nc < 0 || nc >= pg.totalCols) continue;
                if (nr % 2 != 0 || nc % 2 != 0) continue;
                int nk = key(nr, nc);

                float stepDist;
                if (dr[d] != 0) stepDist = std::fabs(pg.rowCY[nr] - pg.rowCY[cur.pr]);
                else stepDist = std::fabs(pg.colCX[nc] - pg.colCX[cur.pc]);

                if (cur.dir != -1 && cur.dir != d) stepDist += CFG_TURN_PENALTY_PG;
                float ng = cur.g + stepDist;

                auto& nb = ns[nk];
                if (nb.closed) continue;
                bool isNew = (ns.find(nk) == ns.end() && nk != sk) || (nb.g == 0 && nk != sk);
                if (isNew || ng < nb.g) {
                    float h = std::fabs(pg.rowCY[nr] - pg.rowCY[epr]) + std::fabs(pg.colCX[nc] - pg.colCX[epc]);
                    nb = {nr, nc, ng, ng + h, ck, d, false};
                    open.push({nb.f, nk});
                }
            }
        }
        return {};
    }

} // namespace CFGAStar

// ============================================================================
// 第三阶段：核心路由 — PipeGrid A* → 世界坐标路径
// ============================================================================
namespace CFGEdgeRouter {

    using CFGGridUtil::ClampFloat;
    using CFGBlockBuilder::CalcPortOffset;
    using CFGPathUtil::CleanPath;
    using CFGObstacleUtil::AdjustPathAwayFromNodes;
    using CFGObstacleUtil::IsPathBlocked;
    using CFGObstacleUtil::CreateSafeExternalPath;
    using CFGObstacleUtil::ResolveOverlapWithOccupancy;
    using CFGAStar::PipeGridAStar;

    // 路由单条边
    static std::vector<ImVec2> RouteEdgeOnPipeGrid(
        GraphLayoutEx& g, int edgeIdx, const std::map<int, int>& outDeg)
    {
        auto& e = g.edges[edgeIdx];
        const auto& src = g.nodes[e.from];
        const auto& dst = g.nodes[e.to];
        PipeGrid& pg = g.pg;

        int totalOut = outDeg.count(e.from) ? outDeg.at(e.from) : 1;
        float portOff = CalcPortOffset(e.portIndex, totalOut, src.size.x);
        ImVec2 startPort(src.GetBottomPort().x + portOff, src.GetBottomPort().y);
        ImVec2 endPort = dst.GetTopPort();

        // 确定起终点管道行
        int spr = pg.BottomPipeRow(src.gridRow);
        int epr = pg.TopPipeRow(dst.gridRow);
        spr = (int)ClampFloat((float)spr, 0, (float)(pg.totalRows - 1));
        epr = (int)ClampFloat((float)epr, 0, (float)(pg.totalRows - 1));

        // 确定起终点管道列
        int spc = pg.NearestPipeCol(startPort.x);
        int epc = pg.NearestPipeCol(endPort.x);

        // A* 搜索
        auto ipath = PipeGridAStar(pg, spr, spc, epr, epc);
        if (ipath.empty()) {
            return CleanPath(CreateSafeExternalPath(g, e.from, e.to, startPort, endPort));
        }

        // ---- 为每段分配车道 ----
        struct SegInfo { bool horiz; int pipeIdx; int lane; };
        std::vector<SegInfo> segInfos(ipath.size() - 1);
        for (size_t i = 0; i + 1 < ipath.size(); ++i) {
            if (ipath[i].first == ipath[i + 1].first) {
                segInfos[i] = {true, ipath[i].first, pg.AllocHLane(ipath[i].first)};
            } else {
                segInfos[i] = {false, ipath[i].second, pg.AllocVLane(ipath[i].second)};
            }
        }

        // ---- 计算每个交叉点的转弯世界坐标 ----
        struct TurnPt { float x, y; };
        std::vector<TurnPt> turns(ipath.size());

        for (size_t i = 0; i < ipath.size(); ++i) {
            int pr = ipath[i].first, pc = ipath[i].second;
            float x = pg.colCX[pc], y = pg.rowCY[pr];

            int hLane = -1;
            if (i > 0 && segInfos[i - 1].horiz) hLane = segInfos[i - 1].lane;
            if (i + 1 < ipath.size() && segInfos[i].horiz) hLane = segInfos[i].lane;

            int vLane = -1;
            if (i > 0 && !segInfos[i - 1].horiz) vLane = segInfos[i - 1].lane;
            if (i + 1 < ipath.size() && !segInfos[i].horiz) vLane = segInfos[i].lane;

            if (hLane >= 0) y = pg.HLaneY(pr, hLane);
            if (vLane >= 0) x = pg.VLaneX(pc, vLane);

            turns[i] = {x, y};
        }

        // ---- 构建规整正交路径 ----
        std::vector<ImVec2> wp;
        wp.push_back(startPort);

        if (ipath.size() == 1) {
            float py = turns[0].y;
            wp.push_back(ImVec2(startPort.x, py));
            if (std::fabs(startPort.x - endPort.x) > 1.0f)
                wp.push_back(ImVec2(endPort.x, py));
            wp.push_back(endPort);
        } else {
            // 1. 起点到第一个管道交叉点
            {
                float entryY = turns[0].y;
                wp.push_back(ImVec2(startPort.x, entryY));
                if (std::fabs(startPort.x - turns[0].x) > 1.0f)
                    wp.push_back(ImVec2(turns[0].x, entryY));
            }

            // 2. 沿管道网格严格正交移动
            for (size_t i = 1; i < turns.size(); ++i) {
                ImVec2 prev = wp.back();
                ImVec2 cur(turns[i].x, turns[i].y);

                if (std::fabs(prev.x - cur.x) < 1.0f && std::fabs(prev.y - cur.y) < 1.0f)
                    continue;

                if (std::fabs(prev.x - cur.x) < 1.0f || std::fabs(prev.y - cur.y) < 1.0f) {
                    wp.push_back(cur);
                    continue;
                }

                bool lastWasHorizontal = false;
                if (wp.size() >= 2) {
                    ImVec2 beforePrev = wp[wp.size() - 2];
                    lastWasHorizontal = std::fabs(beforePrev.y - prev.y) < 1.0f;
                }

                if (lastWasHorizontal) {
                    wp.push_back(ImVec2(prev.x, cur.y));
                } else {
                    wp.push_back(ImVec2(cur.x, prev.y));
                }
                wp.push_back(cur);
            }

            // 3. 最后一个管道点到终点
            {
                ImVec2 lastPt = wp.back();
                if (std::fabs(lastPt.x - endPort.x) > 1.0f)
                    wp.push_back(ImVec2(endPort.x, lastPt.y));
                if (std::fabs(lastPt.y - endPort.y) > 1.0f)
                    wp.push_back(endPort);
            }
        }

        // 严格正交化 + 简化
        auto clean = CleanPath(wp);

        // 安全检查
        auto nodeSafe = AdjustPathAwayFromNodes(g, clean, e.from, e.to);
        if (nodeSafe.empty() || IsPathBlocked(g, nodeSafe, e.from, e.to))
            nodeSafe = CreateSafeExternalPath(g, e.from, e.to, startPort, endPort);

        // 占用冲突解决
        auto finalPath = ResolveOverlapWithOccupancy(g, nodeSafe, e.from, e.to);
        if (finalPath.empty()) finalPath = nodeSafe;

        // 最终正交化确保
        finalPath = CleanPath(finalPath);

        // 注册占用
        for (size_t i = 1; i < finalPath.size(); ++i) {
            const auto& a = finalPath[i - 1];
            const auto& b = finalPath[i];
            EdgeSegment seg;
            if (std::fabs(a.x - b.x) < 1.0f) {
                seg.type = SegType::VERTICAL;
                seg.fixedCoord = a.x;
                seg.minCoord = std::min(a.y, b.y);
                seg.maxCoord = std::max(a.y, b.y);
            } else {
                seg.type = SegType::HORIZONTAL;
                seg.fixedCoord = a.y;
                seg.minCoord = std::min(a.x, b.x);
                seg.maxCoord = std::max(a.x, b.x);
            }
            g.occupancy.AddSegment(seg);
        }
        return finalPath;
    }

    // 完整的边路由流程（标记回边 + 端口序号 + 排序 + 路由）
    static void RouteAllEdges(GraphLayoutEx& layout) {
        // 回边标记
        for (auto& e : layout.edges)
            if (layout.nodes[e.to].layer <= layout.nodes[e.from].layer)
                e.is_reversed = true;

        // 端口序号分配
        std::map<int, std::vector<std::pair<int, int>>> outEdgesByNode;
        for (int i = 0; i < (int)layout.edges.size(); ++i) {
            auto& e = layout.edges[i];
            float tx = layout.nodes[e.to].x + layout.nodes[e.to].size.x * 0.5f;
            outEdgesByNode[e.from].push_back({(int)tx, i});
        }
        for (auto& kv : outEdgesByNode) {
            auto& vec = kv.second;
            std::sort(vec.begin(), vec.end(), [](auto& a, auto& b) { return a.first < b.first; });
            for (int i = 0; i < (int)vec.size(); ++i)
                layout.edges[vec[i].second].portIndex = i;
        }

        // 出/入度统计
        std::map<int, int> outDeg, inDeg;
        for (const auto& e : layout.edges) { outDeg[e.from]++; inDeg[e.to]++; }

        // 边排序：回边优先，长距离优先
        std::vector<int> edgeOrder(layout.edges.size());
        std::iota(edgeOrder.begin(), edgeOrder.end(), 0);
        std::sort(edgeOrder.begin(), edgeOrder.end(), [&](int a, int b) {
            const auto& ea = layout.edges[a];
            const auto& eb = layout.edges[b];
            bool revA = ea.is_reversed, revB = eb.is_reversed;
            if (revA != revB) return revA > revB;
            float lenA = std::abs(layout.nodes[ea.from].x - layout.nodes[ea.to].x) +
                         std::abs(layout.nodes[ea.from].y - layout.nodes[ea.to].y);
            float lenB = std::abs(layout.nodes[eb.from].x - layout.nodes[eb.to].x) +
                         std::abs(layout.nodes[eb.from].y - layout.nodes[eb.to].y);
            return lenA > lenB;
        });

        // 执行路由
        for (int idx : edgeOrder) {
            auto& e = layout.edges[idx];
            if (layout.nodes[e.from].is_dummy || layout.nodes[e.to].is_dummy) continue;
            e.path = RouteEdgeOnPipeGrid(layout, idx, outDeg);
        }
    }

} // namespace CFGEdgeRouter
