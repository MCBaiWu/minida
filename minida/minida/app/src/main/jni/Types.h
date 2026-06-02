#pragma once

// ===== 标准库头文件 =====
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <elf.h>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <stdarg.h>
#include <functional>
#include <math.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <random>
#include <chrono>
#include <condition_variable>

// ===== 第三方库头文件 =====
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "font.h"
#include "include/capstone/capstone.h"

// Android JNI和日志
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>
#include <android/log.h>

#define LOG_TAG "ELFAnalyst"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ELF常量定义
#ifndef SHT_INIT
#define SHT_INIT 14
#endif
#ifndef SHT_FINI
#define SHT_FINI 15
#endif
#ifndef SHN_UNDEF
#define SHN_UNDEF 0
#endif
#ifndef STT_FUNC
#define STT_FUNC 2
#endif

// 性能优化配置
namespace PerfConfig {
    constexpr size_t MAX_VISIBLE_ROWS = 50;
    constexpr size_t SYMBOL_CACHE_LIMIT = 10000;
    constexpr size_t STRING_CACHE_LIMIT = 2000;
    constexpr size_t MAX_SEARCH_RESULTS = 500;
    constexpr int SEARCH_DEBOUNCE_MS = 150;
    constexpr size_t PARSE_CHUNK_SIZE = 4096;
}

// ===== 数据结构 =====

struct ElfHeaderInfo {
    bool valid = false;
    unsigned char magic[4] = {};
    unsigned char class_ = 0;
    unsigned char endian = 0;
    unsigned char version = 0;
    unsigned char osabi = 0;
    uint16_t type = 0;
    uint16_t machine = 0;
    uint32_t version_ = 0;
    uint64_t entryPoint = 0;
    uint64_t phoff = 0;
    uint64_t shoff = 0;
    uint32_t flags = 0;
    uint16_t ehsize = 0;
    uint16_t phentsize = 0;
    uint16_t phnum = 0;
    uint16_t shentsize = 0;
    uint16_t shnum = 0;
    uint16_t shstrndx = 0;
    std::string machineName;
    std::string typeName;
};

struct SectionInfo {
    std::string name;
    uint64_t addr = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t type = 0;
    uint32_t flags = 0;
    std::string cachedDisplay;
};

struct ProgramInfo {
    uint32_t type = 0;
    uint64_t offset = 0;
    uint64_t vaddr = 0;
    uint64_t paddr = 0;
    uint64_t filesz = 0;
    uint64_t memsz = 0;
    uint32_t flags = 0;
    uint64_t align = 0;
    std::string typeName;
};

struct SymbolInfo {
    std::string name;
    uint64_t value = 0;
    uint64_t size = 0;
    unsigned char bind = 0;
    unsigned char type = 0;
    uint16_t shndx = 0;
    std::string_view sectionName;
    std::string bindName;
    std::string typeName;
    std::string lowerName;
};

struct RelocationInfo {
    uint64_t offset = 0;
    uint64_t info = 0;
    int64_t addend = 0;
    std::string targetName;
    std::string symbolName;
    std::string typeName;
};

struct StringInfo {
    std::string_view value;
    uint64_t offset = 0;
    int sectionIndex = -1;
    std::string cachedDisplay;
};

struct FunctionImport {
    std::string library;
    std::string function;
    uint64_t address = 0;
};

struct FunctionExport {
    std::string name;
    uint64_t address = 0;
    uint64_t size = 0;
    std::string lowerName;
};

struct AsmInstruction {
    uint64_t address = 0;
    uint32_t bytes = 0;
    std::string mnemonic;
    std::string operands;
    std::string bytesStr;
    bool isJump = false;
    bool isCall = false;
    bool isBranch = false;
    bool hasDetail = false;
    uint64_t branchTarget = 0;
    int insnId = 0;
};

struct AsmViewState {
    csh handle = 0;
    uint64_t currentAddress = 0;
    std::vector<AsmInstruction> instructions;
    int selectedIndex = -1;
    bool initialized = false;
    int64_t longPressStartTime = 0;
    int64_t longPressDelay = 500;
    int longPressSelectedIndex = -1;
    bool showContextMenu = false;
    uint64_t currentFunctionStart = 0;
    uint64_t currentFunctionEnd = 0;
    std::vector<uint64_t> branchTargets;

    void Reset();
    bool IsLongPress(int64_t currentTime, int index);
    void StartLongPress(int64_t currentTime, int index);
    void EndLongPress();
    void SetContextMenuVisible(bool visible);
    uint64_t GetSelectedAddress() const;
};

struct DynamicInfo {
    std::string tagName;
    uint64_t tagValue = 0;
    std::string stringValue;
};

struct AnalysisProgress {
    std::atomic<bool> isAnalyzing{false};
    std::atomic<bool> cancelRequested{false};
    std::atomic<float> progress{0.0f};
    std::atomic<int> currentStage{0};
    std::atomic<const char*> stageName{"就绪"};
    std::mutex mutex;
    std::condition_variable cv;

    void Start();
    void Stop();
    void SetStage(int stage, const char* name, float prog);
    void RequestCancel();
    bool ShouldCancel();
    void WaitIfAnalyzing();
};

// CFG 数据结构
struct CFGNode {
    uint64_t startAddr = 0;
    uint64_t endAddr = 0;
    std::string disassembly;
    ImVec2 position = ImVec2(0, 0);
    ImVec2 size = ImVec2(100, 50);
    bool isEntry = false;
    bool isExit = false;
};

enum class CFGEdgeType {
    Normal, True, False, Call, Jump
};

struct CFGEdge {
    uint64_t fromAddr;
    uint64_t toAddr;
    CFGEdgeType type;
    ImVec2 fromPos;
    ImVec2 toPos;
};

struct CFGGraph {
    uint64_t funcAddress = 0;
    std::string funcName;
    std::vector<CFGNode> nodes;
    std::vector<CFGEdge> edges;
    bool valid = false;
    void Clear();
};

struct CFGViewState {
    bool open = true;
    int tabIndex = -1;
    uint64_t funcAddress = 0;
    std::string title;
    ImVec2 scroll = ImVec2(0, 0);
    float zoom = 1.0f;
    ImVec2 lastPan = ImVec2(0, 0);
    bool isDragging = false;
    float pinchDistance = 0;
    bool valid = false;
    CFGGraph graph;
};

struct CFGManager {
    std::vector<CFGViewState> tabs;
    int CreateCFGView(uint64_t funcAddr, const std::string& funcName);
    void CloseCFGView(int index);
    bool HasOpenTabs() const;
    int GetTabCount() const;
    int FindTabByAddress(uint64_t funcAddr) const;
};


struct LayoutConfig {
    float uiScale = 1.0f;
    float displayWidth = 1920.0f;
    float displayHeight = 1080.0f;
    bool isLandscape = true;
    float navBarWidth = 300.0f;
    float outputPanelHeight = 150.0f;
    float buttonHeight = 48.0f;
    float rowHeight = 44.0f;
    float fontSize = 16.0f;
    float padding = 10.0f;
    float spacing = 8.0f;
    float scrollbarWidth = 20.0f;
    float toolbarHeight = 60.0f;
    float navBandHeight = 24.0f;
    float statusBarHeight = 28.0f;
    bool showNavBar = true;
    bool showOutputPanel = true;
    bool showFilePicker = false;
    bool showProgressModal = false;
    int currentTabIndex = 0;
    int selectedFunctionIndex = -1;
    int hexOffset = 0;
    int hexBytesPerRow = 16;
    char searchText[256] = "";
    std::string lastSearchText;
    bool searchDirty = false;
    bool showAsmView = false;
    uint64_t currentAsmAddress = 0;
    std::string currentFunctionName;
    bool showAsmContextMenu = false;
    ImVec2 contextMenuPosition = ImVec2(0, 0);
    int64_t contextMenuOpenTime = 0;
    bool showCFGView = false;
    int currentCFGTab = -1;
    int hoveredCFGTab = -1;
    float cfgZoom = 1.0f;
    ImVec2 cfgScroll = ImVec2(0, 0);
    bool pseudoCWindowOpen = false;
    uint64_t selectedFunctionAddress = 0;
    std::string selectedFunctionDisplayName;

};

struct UIState {
    std::vector<std::string> outputLog;
    bool autoScrollOutput = true;
    std::mutex logMutex;
    void AddLog(const char* format, ...);
    void ClearLog();
    size_t GetLogCount();
    void CopyLogTo(std::vector<std::string>& dest);
};

// ===== 交叉引用数据结构 =====
enum class XrefType {
    Code,       // 代码引用（调用/跳转）
    Data,       // 数据引用
    String      // 字符串引用
};

struct XrefEntry {
    uint64_t fromAddr = 0;     // 引用来源地址
    uint64_t toAddr = 0;       // 引用目标地址
    XrefType type = XrefType::Code;
    std::string fromFuncName;  // 来源函数名
    std::string toFuncName;    // 目标函数名
    std::string instruction;   // 相关指令文本
};

struct XrefInfo {
    uint64_t address = 0;               // 被引用的地址
    std::vector<XrefEntry> referencesTo;   // 谁引用了这个地址（入边）
    std::vector<XrefEntry> referencesFrom; // 这个地址引用了谁（出边）
    
    int GetRefCount() const { return (int)(referencesTo.size() + referencesFrom.size()); }
};
