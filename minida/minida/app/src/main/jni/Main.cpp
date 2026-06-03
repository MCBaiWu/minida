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

// 软键盘 / 剪贴板全局状态（先于 extern "C" 块声明，供 DrawFrame / SurfaceCreated 使用）
static JavaVM* g_jvm = nullptr;
static std::string g_clipboardText;
static char g_clipboardBuf[4096] = {};
static bool g_wantTextInput = false;

// ImGui 剪贴板回调：缓存到本地 + 同步到 Android 系统剪贴板
static const char* ImGuiGetClipboardText(void*) {
    return g_clipboardBuf;
}

static void ImGuiSetClipboardText(void*, const char* text) {
    if (!text) return;
    snprintf(g_clipboardBuf, sizeof(g_clipboardBuf), "%s", text);
    g_clipboardText = text;

    if (g_jvm) {
        JNIEnv* env = nullptr;
        bool attached = false;
        if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
            if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            }
        }
        if (env) {
            jclass cls = env->FindClass("com/baiwu/imgui/Surface");
            if (cls) {
                jmethodID mid = env->GetStaticMethodID(cls, "nativeSetClipboard", "(Ljava/lang/String;)V");
                if (mid) {
                    jstring jstr = env->NewStringUTF(text);
                    env->CallStaticVoidMethod(cls, mid, jstr);
                    env->DeleteLocalRef(jstr);
                }
                env->DeleteLocalRef(cls);
            }
        }
        if (attached) g_jvm->DetachCurrentThread();
    }
}

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

        // 同步 WantTextInput 给 Java 端，控制软键盘弹/收
        g_wantTextInput = io.WantTextInput;
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

        // 保存 JavaVM 供剪贴板回调跨线程使用
        if (env) {
            env->GetJavaVM(&g_jvm);
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)glWidth, (float)glHeight);
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.IniFilename = NULL;
        io.ConfigDebugIsDebuggerPresent = false;

        // 挂接剪贴板回调
        io.SetClipboardTextFn = ImGuiSetClipboardText;
        io.GetClipboardTextFn = ImGuiGetClipboardText;
        io.ClipboardUserData = nullptr;

        ImGui_ImplOpenGL3_Init("#version 300 es");

        UpdateLayoutMetrics(glWidth, glHeight);

        float fontSize = 24.0f * g_Layout.uiScale;
        io.Fonts->AddFontFromMemoryTTF((void*)OPPOSans_H, OPPOSans_H_size, fontSize, NULL,
            io.Fonts->GetGlyphRangesChineseFull());

        SetupIDATheme();
        fileBrowser.SetRootPath("/sdcard");

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

    // ==================== 软键盘 / 物理键 / 剪贴板 JNI ====================

    // Android keycode (custom id) -> ImGuiKey
    static ImGuiKey AndroidKeyToImGuiKey(int key) {
        switch (key) {
            case 0:   return ImGuiKey_Tab;
            case 1:   return ImGuiKey_LeftArrow;
            case 2:   return ImGuiKey_RightArrow;
            case 3:   return ImGuiKey_DownArrow;
            case 4:   return ImGuiKey_UpArrow;
            case 5:   return ImGuiKey_LeftShift;
            case 6:   return ImGuiKey_RightShift;
            case 7:   return ImGuiKey_LeftCtrl;
            case 8:   return ImGuiKey_RightCtrl;
            case 9:   return ImGuiKey_LeftAlt;
            case 10:  return ImGuiKey_RightAlt;
            case 11:  return ImGuiKey_CapsLock;
            case 12:  return ImGuiKey_ScrollLock;
            case 13:  return ImGuiKey_NumLock;
            case 14:  return ImGuiKey_PrintScreen;
            case 15:  return ImGuiKey_None;
            case 16:  return ImGuiKey_Insert;
            case 17:  return ImGuiKey_Pause;
            case 19:  return ImGuiKey_Home;
            case 20:  return ImGuiKey_End;
            case 21:  return ImGuiKey_PageUp;
            case 22:  return ImGuiKey_PageDown;
            case 23:  return ImGuiKey_Delete;
            case 29:  return ImGuiKey_A;
            case 30:  return ImGuiKey_B;
            case 31:  return ImGuiKey_C;
            case 32:  return ImGuiKey_D;
            case 33:  return ImGuiKey_E;
            case 34:  return ImGuiKey_F;
            case 35:  return ImGuiKey_G;
            case 36:  return ImGuiKey_H;
            case 37:  return ImGuiKey_I;
            case 38:  return ImGuiKey_J;
            case 39:  return ImGuiKey_K;
            case 40:  return ImGuiKey_L;
            case 41:  return ImGuiKey_M;
            case 42:  return ImGuiKey_N;
            case 43:  return ImGuiKey_O;
            case 44:  return ImGuiKey_P;
            case 45:  return ImGuiKey_Q;
            case 46:  return ImGuiKey_R;
            case 47:  return ImGuiKey_S;
            case 48:  return ImGuiKey_T;
            case 49:  return ImGuiKey_U;
            case 50:  return ImGuiKey_V;
            case 51:  return ImGuiKey_W;
            case 52:  return ImGuiKey_X;
            case 53:  return ImGuiKey_Y;
            case 54:  return ImGuiKey_Z;
            case 55:  return ImGuiKey_Comma;
            case 56:  return ImGuiKey_Period;
            case 59:  return ImGuiKey_Space;
            case 61:  return ImGuiKey_Enter;
            case 62:  return ImGuiKey_Backspace;
            case 63:  return ImGuiKey_GraveAccent;
            case 66:  return ImGuiKey_Enter;
            case 67:  return ImGuiKey_Backspace;
            case 75:  return ImGuiKey_Keypad0;
            case 76:  return ImGuiKey_Keypad1;
            case 77:  return ImGuiKey_Keypad2;
            case 78:  return ImGuiKey_Keypad3;
            case 79:  return ImGuiKey_Keypad4;
            case 80:  return ImGuiKey_Keypad5;
            case 81:  return ImGuiKey_Keypad6;
            case 82:  return ImGuiKey_Keypad7;
            case 83:  return ImGuiKey_Keypad8;
            case 84:  return ImGuiKey_Keypad9;
            case 111: return ImGuiKey_Escape;
            case 112: return ImGuiKey_F1;
            case 113: return ImGuiKey_F2;
            case 114: return ImGuiKey_F3;
            case 115: return ImGuiKey_F4;
            case 116: return ImGuiKey_F5;
            case 117: return ImGuiKey_F6;
            case 118: return ImGuiKey_F7;
            case 119: return ImGuiKey_F8;
            case 120: return ImGuiKey_F9;
            case 121: return ImGuiKey_F10;
            case 122: return ImGuiKey_F11;
            case 123: return ImGuiKey_F12;
            default:  return ImGuiKey_None;
        }
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_KeyEvent(JNIEnv*, jclass, jint key, jboolean down) {
        if (!initialized) return;
        ImGuiIO& io = ImGui::GetIO();
        ImGuiKey imguiKey = AndroidKeyToImGuiKey(key);
        if (imguiKey != ImGuiKey_None) {
            io.AddKeyEvent(imguiKey, down != JNI_FALSE);
        }
        // 修饰键
        if (key == 7 || key == 8)  io.AddKeyEvent(ImGuiMod_Ctrl, down != JNI_FALSE);
        if (key == 5 || key == 6)  io.AddKeyEvent(ImGuiMod_Shift, down != JNI_FALSE);
        if (key == 9 || key == 10) io.AddKeyEvent(ImGuiMod_Alt, down != JNI_FALSE);
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_CharEvent(JNIEnv*, jclass, jint c) {
        if (!initialized) return;
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharacter((unsigned int)c);
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_TextInput(JNIEnv* env, jclass, jstring text) {
        if (!initialized) return;
        const char* str = env->GetStringUTFChars(text, nullptr);
        if (str) {
            ImGuiIO& io = ImGui::GetIO();
            for (const char* p = str; *p; p++) {
                io.AddInputCharacter((unsigned int)*p);
            }
            env->ReleaseStringUTFChars(text, str);
        }
    }

    JNIEXPORT jboolean JNICALL Java_com_baiwu_imgui_Surface_WantTextInput(JNIEnv*, jclass) {
        if (!initialized) return JNI_FALSE;
        return g_wantTextInput ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT void JNICALL Java_com_baiwu_imgui_Surface_SetClipboardText(JNIEnv* env, jclass, jstring text) {
        if (!initialized) return;
        const char* str = env->GetStringUTFChars(text, nullptr);
        if (str) {
            g_clipboardText = str;
            snprintf(g_clipboardBuf, sizeof(g_clipboardBuf), "%s", str);
            env->ReleaseStringUTFChars(text, str);
        }
    }

    JNIEXPORT jstring JNICALL Java_com_baiwu_imgui_Surface_GetClipboardText(JNIEnv* env, jclass) {
        return env->NewStringUTF(g_clipboardText.c_str());
    }
}
