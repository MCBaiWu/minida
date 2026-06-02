#include "Common.h"

// ============================================================================
// 响应式尺寸计算器
// ============================================================================

void UpdateLayoutMetrics(float width, float height) {
    g_Layout.displayWidth = width;
    g_Layout.displayHeight = height;
    g_Layout.isLandscape = width > height;

    float minDimension = std::min(width, height);
    float referenceSize = 1080.0f;
    g_Layout.uiScale = (minDimension / referenceSize) + 0.5f;

    if (g_Layout.uiScale < 0.8f) g_Layout.uiScale = 0.8f;
    if (g_Layout.uiScale > 3.5f) g_Layout.uiScale = 3.5f;

    g_Layout.buttonHeight = 48.0f * g_Layout.uiScale;
    g_Layout.rowHeight = 44.0f * g_Layout.uiScale;
    g_Layout.padding = 10.0f * g_Layout.uiScale;
    g_Layout.spacing = 8.0f * g_Layout.uiScale;
    g_Layout.scrollbarWidth = 20.0f * g_Layout.uiScale;

    if (g_Layout.isLandscape) {
        g_Layout.navBarWidth = width * 0.20f;
        g_Layout.showNavBar = true;
    } else {
        if (width >= 800) {
            g_Layout.navBarWidth = width * 0.30f;
            g_Layout.showNavBar = true;
        } else {
            g_Layout.navBarWidth = width * 0.85f;
            g_Layout.showNavBar = false;
        }
    }

    g_Layout.outputPanelHeight = height * 0.18f;
    if (g_Layout.outputPanelHeight < 120 * g_Layout.uiScale) {
        g_Layout.outputPanelHeight = 120 * g_Layout.uiScale;
    }

    g_Layout.toolbarHeight = g_Layout.buttonHeight + g_Layout.padding * 2;
    g_Layout.navBandHeight = 24.0f * g_Layout.uiScale;
    g_Layout.statusBarHeight = 28.0f * g_Layout.uiScale;
    g_Layout.fontSize = 16.0f * g_Layout.uiScale;
}

// ============================================================================
// IDA Pro风格绿色主题
// ============================================================================

void SetupIDATheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.ScaleAllSizes(g_Layout.uiScale);

    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowPadding = ImVec2(g_Layout.padding, g_Layout.padding);
    style.FramePadding = ImVec2(g_Layout.padding, g_Layout.padding / 2);
    style.ItemSpacing = ImVec2(g_Layout.spacing, g_Layout.spacing);
    style.ItemInnerSpacing = ImVec2(g_Layout.spacing, g_Layout.spacing / 2);
    style.IndentSpacing = g_Layout.padding * 1.5f;

    // 增大滚动条尺寸 - 使其更粗更明显
    float scrollbarSize = 24.0f * g_Layout.uiScale;
    style.ScrollbarSize = scrollbarSize;
    style.GrabMinSize = scrollbarSize * 0.7f;

    style.WindowBorderSize = 1.5f;
    style.ChildBorderSize = 1.5f;
    style.PopupBorderSize = 1.5f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;

    // 确保滚动条始终可见
    style.DisplaySafeAreaPadding = ImVec2(0, 0);

    colors[ImGuiCol_Text] = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.48f, 0.45f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 0.45f, 0.30f, 0.50f);

    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.12f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.14f, 0.12f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.16f, 0.14f, 1.00f);

    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.40f, 0.25f, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);

    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.14f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.22f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.25f, 0.20f, 1.00f);

    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.12f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.16f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.10f, 0.08f, 0.95f);

    colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.14f, 0.12f, 1.00f);

    // 滚动条颜色 - 更明显的样式
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.12f, 0.14f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.40f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.55f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.65f, 0.50f, 1.00f);

    colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.75f, 0.40f, 1.00f);

    colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.65f, 0.35f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.80f, 0.45f, 1.00f);

    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.25f, 0.18f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.45f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 0.40f, 0.25f, 1.00f);

    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.22f, 0.18f, 0.80f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.35f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.40f, 0.22f, 1.00f);

    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.38f, 0.25f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.60f, 0.35f, 0.50f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.75f, 0.40f, 0.80f);

    colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.45f, 0.30f, 0.15f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.35f, 0.60f, 0.35f, 0.40f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.75f, 0.40f, 0.70f);

    colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.12f, 0.10f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.28f, 0.22f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.38f, 0.22f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.12f, 0.10f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.20f, 0.16f, 1.00f);

    colors[ImGuiCol_PlotLines] = ImVec4(0.55f, 0.58f, 0.55f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.75f, 0.50f, 0.40f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.55f, 0.55f, 0.25f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.80f, 0.65f, 0.30f, 1.00f);

    colors[ImGuiCol_DragDropTarget] = ImVec4(0.40f, 0.65f, 0.40f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.40f, 0.65f, 0.40f, 1.00f);
}

// ============================================================================
// 工具函数：格式化地址（移除前导零）
// ============================================================================

const char* FormatAddress(uint64_t addr) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "0x%lX", (unsigned long)addr);
    return buf;
}

const char* FormatAddressCompact(uint64_t addr) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "0x%lX", (unsigned long)addr);
    return buf;
}
