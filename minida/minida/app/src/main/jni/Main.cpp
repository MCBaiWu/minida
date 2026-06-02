#include "Common.h"

// ============================================================================
// 全局变量定义
// ============================================================================

int glWidth = 1920;
int glHeight = 1080;
bool initialized = false;

AsmViewState g_AsmView;
AnalysisProgress g_Progress;
UIState g_UI;
LayoutConfig g_Layout;
CFGManager g_CFGManager;

int touch_action;

// ============================================================================
// UIState 方法实现
// ============================================================================

void UIState::AddLog(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    {
        std::lock_guard<std::mutex> lock(logMutex);
        outputLog.push_back(std::string(buffer));
        if (outputLog.size() > 500) {
            outputLog.erase(outputLog.begin());
        }
    }
}

void UIState::ClearLog() {
    std::lock_guard<std::mutex> lock(logMutex);
    outputLog.clear();
}

size_t UIState::GetLogCount() {
    std::lock_guard<std::mutex> lock(logMutex);
    return outputLog.size();
}

void UIState::CopyLogTo(std::vector<std::string>& dest) {
    std::lock_guard<std::mutex> lock(logMutex);
    dest = outputLog;
}

// ============================================================================
// AnalysisProgress 方法实现
// ============================================================================

void AnalysisProgress::Start() {
    isAnalyzing = true;
    cancelRequested = false;
    progress = 0.0f;
    currentStage = 0;
    stageName = "初始化";
}

void AnalysisProgress::Stop() {
    isAnalyzing = false;
    progress = 100.0f;
    stageName = "完成";
    cv.notify_all();
}

void AnalysisProgress::SetStage(int stage, const char* name, float prog) {
    currentStage = stage;
    progress = prog;
    stageName = name;
}

void AnalysisProgress::RequestCancel() { cancelRequested = true; }
bool AnalysisProgress::ShouldCancel() { return cancelRequested.load(); }

void AnalysisProgress::WaitIfAnalyzing() {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this] { return !isAnalyzing.load(); });
}

// ============================================================================
// 分析工作函数（后台线程执行）
// ============================================================================

void AnalysisWorker(const std::string& filePath) {
    g_Progress.Start();
    g_Progress.SetStage(0, "加载文件...", 5.0f);

    g_UI.ClearLog();
    g_UI.AddLog("开始分析文件: %s", filePath.c_str());

    elfParser.UnloadFile();

    if (elfParser.LoadFile(filePath, &g_Progress)) {
        // 根据ELF架构初始化反汇编器
        uint16_t machine = elfParser.GetHeaderInfo().machine;
        cs_arch arch = Disassembler::GetArchForMachine(machine);
        cs_mode mode = Disassembler::GetModeForMachine(machine);
        if (!g_Disassembler.Initialize(arch, mode)) {
            g_UI.AddLog("警告: 反汇编器初始化失败，架构: %d", machine);
        }
        g_UI.AddLog("反汇编器架构: %s", elfParser.Is64Bit() ? "64-bit" : "32-bit");

        g_UI.AddLog("分析完成!");
        g_UI.AddLog("类型: %s", elfParser.GetHeaderInfo().typeName.c_str());
        g_UI.AddLog("架构: %s", elfParser.GetHeaderInfo().machineName.c_str());
        g_UI.AddLog("入口点: %s", FormatAddressCompact(elfParser.GetHeaderInfo().entryPoint));
        g_UI.AddLog("文件大小: %zu bytes", elfParser.GetFileSize());
        g_UI.AddLog("Sections: %zu", elfParser.GetSections().size());
        g_UI.AddLog("Programs: %zu", elfParser.GetPrograms().size());
        g_UI.AddLog("Symbols: %zu", elfParser.GetSymbols().size());
        g_UI.AddLog("Imports: %zu", elfParser.GetImports().size());
        g_UI.AddLog("Exports: %zu", elfParser.GetExports().size());
        g_UI.AddLog("Strings: %zu", elfParser.GetStrings().size());
    } else {
        g_UI.AddLog("Error: %s", elfParser.GetError().c_str());
    }

    g_Progress.Stop();
}

// ============================================================================
// 启动分析线程
// ============================================================================

void StartAnalysisThread(const std::string& filePath) {
    if (g_Progress.isAnalyzing) {
        g_UI.AddLog("警告: 已有分析任务在进行中");
        return;
    }

    std::thread worker([filePath]() {
        AnalysisWorker(filePath);
    });
    worker.detach();
}

// ============================================================================
// 主渲染循环
// ============================================================================

extern "C" {
    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_DrawFrame(JNIEnv *env, jobject thiz) {
        if (!initialized) {
            LOGI("Not initialized, skipping frame");
            return;
        }

        UpdateLayoutMetrics(glWidth, glHeight);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)glWidth, (float)glHeight));

        ImGui::Begin("ELF Analyst Pro", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        RenderToolbar();
        RenderNavigationBand();

        float contentHeight = g_Layout.displayHeight - g_Layout.toolbarHeight
            - g_Layout.navBandHeight - g_Layout.outputPanelHeight
            - g_Layout.statusBarHeight - g_Layout.padding * 2;

        if (contentHeight < 100) contentHeight = 100;

        RenderFunctionsWindow();
        ImGui::SameLine();
        RenderWorkspace();
        RenderOutputPanel();
        RenderStatusBar();

        RenderProgressModal();


        // CFG 视图窗口（独立浮动窗口）
        if (g_Layout.showCFGView) {
            RenderCFGView();
        }

        ImGui::End();

        fileBrowser.OnFileSelected = [](const std::string& path) {
            currentFilePath = path;

            size_t pos = path.rfind('/');
            if (pos != std::string::npos) {
                currentFileName = path.substr(pos + 1);
            } else {
                currentFileName = path;
            }

            StartAnalysisThread(path);
        };

        fileBrowser.Render();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::EndFrame();
        ImGui::Render();

        glClearColor(0.08f, 0.10f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_ShutDown(JNIEnv *env, jobject thiz) {
        if (!initialized) return;

        // 关闭Capstone反汇编器
        g_Disassembler.Shutdown();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();

        initialized = false;
        LOGI("ELF Analyst shutdown complete");
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_SurfaceCreated(JNIEnv *env, jobject thiz) {
        ImGuiContext* ctx = ImGui::CreateContext();
        if (ctx == NULL) {
            LOGI("Failed to create ImGui context");
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)glWidth, (float)glHeight);
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.IniFilename = NULL;
        io.ConfigDebugIsDebuggerPresent = false;

        ImGui_ImplOpenGL3_Init("#version 300 es");

        UpdateLayoutMetrics(glWidth, glHeight);

        float fontSize = 24.0f * g_Layout.uiScale;
        io.Fonts->AddFontFromMemoryTTF((void*)OPPOSans_H, OPPOSans_H_size, fontSize, NULL,
            io.Fonts->GetGlyphRangesChineseFull());

        SetupIDATheme();
        fileBrowser.SetRootPath("/sdcard");

        // Disassembler 将在加载文件后根据ELF架构自动初始化

        g_UI.AddLog("ELF Analyst Pro 启动完成");
        g_UI.AddLog("请选择文件进行分析");

        initialized = true;
        LOGI("ELF Analyst initialized with full features");
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_SurfaceChanged(JNIEnv *env,
        jobject thiz, jint width, jint height) {

        glWidth = width;
        glHeight = height;

        if (initialized) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(glWidth, glHeight);
            UpdateLayoutMetrics(glWidth, glHeight);
            SetupIDATheme();
            LOGI("Window size: %dx%d, UI scale: %.2f", glWidth, glHeight, g_Layout.uiScale);
        }

        glViewport(0, 0, glWidth, glHeight);
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_MotionEventClick(JNIEnv *env,
        jclass obj, jboolean down, jfloat PosX, jfloat PosY,jint actions) {

        ImGuiIO& io = ImGui::GetIO();
        io.MouseDown[0] = down;
        io.MousePos = ImVec2(PosX, PosY);
         touch_action=actions;

    }

    JNIEXPORT jstring JNICALL Java_com_baiwu_imgui_Surface_getWindowRect(JNIEnv *env, jclass thiz) {
        char result[256];
        snprintf(result, sizeof(result), "%d|%d|%d|%d", 0, 0, glWidth, glHeight);
        return env->NewStringUTF(result);
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_setUIScale(JNIEnv *env,
        jclass thiz, jfloat scale) {
        if (scale > 0.5f && scale < 4.0f) {
            g_Layout.uiScale = scale;
            SetupIDATheme();
            LOGI("UI scale set to: %f", g_Layout.uiScale);
        }
    }
}
