# MiniDA - Android 端 SO 逆向分析工具

## 项目简介

MiniDA 是一款运行在 Android 平台上的 SO（Shared Object）文件逆向分析工具，使用 **ImGui** 作为 UI 框架，参考 IDA 的界面布局进行实现。工具通过 **Capstone** 反汇编引擎将机器码解析为汇编指令，并通过 **Keystone** 汇编引擎支持控制流流程图（CFG）的绘制，方便开发者在移动端进行二进制文件的分析工作。

## 功能特性

- **ELF 文件解析**：加载并解析 Android SO 库（32 位 / 64 位）
- **汇编指令反汇编**：基于 Capstone 引擎，将机器码转换为可读汇编指令
- **控制流图（CFG）**：自动构建并可视化函数的基本块和跳转关系
- **IDA 风格 UI**：参考 IDA Pro 布局，包含工具栏、导航栏、函数列表、工作区、输出面板、状态栏
- **多架构支持**：基于 ELF 头自动识别 x86、x86_64、ARM、ARM64、MIPS 等架构
- **符号 / 字符串 / 导入导出表查看**：完整展示 ELF 文件的结构信息
- **文件浏览器**：内置文件浏览功能，方便选择待分析文件
- **中文字体支持**：使用 OPPOSans 字体并加载中文字形
- **可调 UI 缩放**：支持运行时调整界面缩放比例

## 技术栈

| 组件 | 用途 |
| --- | --- |
| ImGui | 即时模式 GUI 框架 |
| ImGui Android Backend | ImGui Android 输入桥接 |
| ImGui OpenGL3 Backend | ImGui OpenGL ES 3.0 渲染后端 |
| Capstone | 多架构反汇编引擎（静态库 `libcapstone.a`） |
| Keystone | 多架构汇编引擎（静态库 `libkeystone.a`） |
| Android NDK | 原生层 C++ 编译 |
| OpenGL ES 3.0 | 图形渲染 |
| Android SDK | Java 层承载与 SurfaceView |

## 目录结构

```
minida/
├── build.gradle                # 顶层 Gradle 配置
├── gradle.properties           # Gradle 属性
├── gradlew / gradlew.bat       # Gradle Wrapper 脚本
├── local.properties            # 本地 SDK/NDK 路径
├── settings.gradle             # 模块设置
└── app/
    ├── build.gradle            # app 模块构建配置
    ├── proguard-rules.pro      # ProGuard 规则
    └── src/main/
        ├── AndroidManifest.xml # Android 清单
        ├── java/com/baiwu/imgui/
        │   ├── MainActivity.java   # 入口 Activity
        │   └── Surface.java        # GLSurfaceView 桥接 JNI
        ├── jni/                # 原生 C++ 代码
        │   ├── Main.cpp        # JNI 入口 / 主循环
        │   ├── Common.h        # 公共定义与全局状态
        │   ├── Types.h         # 数据结构定义
        │   ├── Disassembler.*  # Capstone 反汇编封装
        │   ├── ElfParser.*     # ELF 文件解析
        │   ├── FileBrowser.*   # 文件浏览器
        │   ├── RenderUI.cpp    # 工具栏 / 状态栏 UI
        │   ├── RenderAssembly.cpp # 汇编视图渲染
        │   ├── RenderCFG.cpp   # 控制流图渲染
        │   ├── Theme.cpp       # IDA 风格主题
        │   ├── Android.mk      # NDK 构建脚本
        │   ├── Application.mk  # NDK 平台配置
        │   ├── ImGui/          # ImGui 源码与 backends
        │   ├── cfg/            # CFG 构造与渲染相关
        │   ├── include/
        │   │   ├── capstone/   # Capstone 头文件
        │   │   └── keystone/   # Keystone 头文件
        │   ├── libs/
        │   │   ├── libcapstone.a
        │   │   └── libkeystone.a
        │   └── font.h          # 内嵌 OPPOSans 字体
        └── res/                # 资源文件
            ├── drawable/ic_launcher.png
            ├── layout/activity_main.xml
            ├── values/         # 颜色 / 字符串 / 样式
            └── values-v21/styles.xml
```

## 核心模块说明

### 1. JNI 入口（`Main.cpp`）
定义 JNI 桥接函数，在 `Java_com_baiwu_imgui_Surface_*` 中完成：
- `SurfaceCreated`：创建 ImGui 上下文、初始化 OpenGL3 后端、加载中文字体、应用 IDA 主题
- `SurfaceChanged`：更新视口大小和 UI 缩放
- `DrawFrame`：每帧调用 ImGui 渲染管线
- `MotionEventClick`：将 Android 触摸事件转换为 ImGui 鼠标输入
- `ShutDown`：释放 ImGui / OpenGL / Capstone 资源

### 2. ELF 解析（`ElfParser.cpp/h`）
负责加载 SO 文件，解析：
- ELF Header（类型、机器、入口点、文件大小）
- Sections
- Program Headers
- Symbols / Imports / Exports
- Strings

### 3. 反汇编（`Disassembler.cpp/h`）
封装 Capstone，根据 `ElfParser::GetHeaderInfo().machine` 自动选择 `cs_arch` 与 `cs_mode`，支持 32 / 64 位指令解码。

### 4. 控制流图（`cfg/` + `RenderCFG.cpp`）
- `CFGBlockBuilder.hpp`：构建基本块和块间跳转关系
- `CFGEdgeRouter.hpp`：计算跳转箭头路径
- `CFGBlockRenderer.cpp/h`：绘制基本块
- `RenderCFG.cpp`：浮动 CFG 视图窗口

### 5. UI 主题（`Theme.cpp`）
通过 `SetupIDATheme()` 调整 ImGui 配色，模拟 IDA Pro 的深色风格。

### 6. 文件浏览器（`FileBrowser.cpp/h`）
内置简易文件浏览，根目录默认 `/sdcard`，选中文件后通过 `OnFileSelected` 回调启动分析线程。

### 7. 异步分析（`Main.cpp::AnalysisWorker`）
使用 `std::thread` + `AnalysisProgress` 实现后台分析，通过 `g_UI.AddLog` 输出到日志面板，避免阻塞主线程。

## 构建与运行

### 环境要求
- Android NDK（用于原生层编译）
- Android SDK（compileSdkVersion 33，minSdkVersion 19，targetSdkVersion 26）
- Gradle（项目自带 Wrapper）

### 构建步骤
1. 在 `local.properties` 中配置 `sdk.dir` 和 `ndk.dir`
2. 在项目根目录执行：
   ```bash
   ./gradlew assembleDebug
   ```
3. 生成的 APK 位于 `app/build/outputs/apk/debug/`

### 运行
将 APK 安装到 Android 设备，启动后通过文件浏览器选择待分析的 SO 文件，工具会自动解析并展示汇编视图与控制流图。

## 关键 JNI 接口

| JNI 函数 | Java 端调用方 | 作用 |
| --- | --- | --- |
| `Surface_DrawFrame` | `Surface` | 每帧渲染 |
| `Surface_SurfaceCreated` | `Surface` | GL 上下文创建 |
| `Surface_SurfaceChanged` | `Surface` | 视口尺寸变更 |
| `Surface_MotionEventClick` | `Surface` | 触摸事件 |
| `Surface_ShutDown` | `Surface` | 资源释放 |
| `Surface_getWindowRect` | `Surface` | 获取窗口尺寸 |
| `Surface_setUIScale` | `Surface` | 调整 UI 缩放 |

## 全局状态（`Main.cpp`）

| 变量 | 说明 |
| --- | --- |
| `glWidth / glHeight` | 当前视口大小 |
| `initialized` | ImGui 是否已初始化 |
| `g_AsmView` | 汇编视图状态 |
| `g_Progress` | 分析进度与取消标志 |
| `g_UI` | UI 全局状态（输出日志等） |
| `g_Layout` | 布局配置（高度、缩放等） |
| `g_CFGManager` | CFG 数据管理 |

## 注意事项

- `RenderCFG.cpp.bak` 为旧版本备份，开发时可酌情清理
- 项目使用 `gradle.properties` / `local.properties` 等本地配置，发布前请勿提交敏感路径
- 第三方静态库（Capstone、Keystone）以预编译 `.a` 形式随源码分发，无需额外安装

## 后续可扩展方向

- 引入函数重命名与注释保存
- 集成 Unicorn 引擎进行指令级模拟执行
- 增加 ARM 反编译（伪 C 代码）能力
- 支持插件机制，扩展更多分析视图
