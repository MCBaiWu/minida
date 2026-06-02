#pragma once

// ===== 引入所有类型定义和头文件 =====
#include "Types.h"

// ===== 前向函数声明 =====
const char* FormatAddress(uint64_t addr);
const char* FormatAddressCompact(uint64_t addr);
void RenderAsmContextMenu(uint64_t address);

// ===== 全局变量声明 =====
extern int glWidth;
extern int glHeight;
extern bool initialized;
extern int touch_action;

extern AsmViewState g_AsmView;
extern AnalysisProgress g_Progress;
extern UIState g_UI;
extern LayoutConfig g_Layout;
extern CFGManager g_CFGManager;
extern std::string currentFilePath;
extern std::string currentFileName;

// ===== 类头文件引入（提供完整类定义） =====
#include "ElfParser.h"
#include "Disassembler.h"
#include "FileBrowser.h"

extern ElfParser elfParser;
extern FileBrowser fileBrowser;
extern Disassembler g_Disassembler;
struct DomTree;
// ===== 函数声明 =====
void UpdateLayoutMetrics(float width, float height);
void SetupIDATheme();
void RenderHexViewFull(const std::vector<uint8_t>& data, int startOffset, int bytesPerRow);
void StartDisassembly(const FunctionExport& exp);
void BuildCFGGraph(CFGGraph& graph, uint64_t funcAddress, const std::vector<AsmInstruction>& instructions);
void RenderCFGCanvas(CFGViewState& tabState);
void RenderCFGView();
void RenderAssemblyView();
void RenderToolbar();
void RenderNavigationBand();
void RenderProgressModal();
void RenderFunctionsWindow();
void RenderWorkspace();
void RenderOutputPanel();
void RenderStatusBar();
void RenderCFGToolbar(CFGViewState& tab, bool isWindowLevel);
void AnalysisWorker(const std::string& filePath);
void StartAnalysisThread(const std::string& filePath);
uint64_t VirtualAddressToFileOffset(const ElfParser& parser, uint64_t virtualAddr);
size_t EstimateFunctionSize(const ElfParser& parser, uint64_t funcAddr, size_t defaultSize = 256);


