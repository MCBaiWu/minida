LOCAL_PATH := $(call my-dir)

# =============================================================================
# 预编译静态库定义 - Capstone
# =============================================================================
include $(CLEAR_VARS)
LOCAL_MODULE := capstone_static
LOCAL_SRC_FILES := libs/libcapstone.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include/capstone
include $(PREBUILT_STATIC_LIBRARY)

# =============================================================================
# 预编译静态库定义 - Keystone
# =============================================================================
include $(CLEAR_VARS)
LOCAL_MODULE := keystone_static
LOCAL_SRC_FILES := libs/libkeystone.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include/keystone
include $(PREBUILT_STATIC_LIBRARY)

# ModMenu Module
include $(CLEAR_VARS)
LOCAL_MODULE    := imgui
# Code optimization flags
LOCAL_CFLAGS := -Wno-error=format-security -fvisibility=hidden -ffunction-sections -fdata-sections -w
LOCAL_CFLAGS += -fno-rtti -fexceptions -fpermissive
LOCAL_CPPFLAGS := -Wno-error=format-security -fvisibility=hidden -ffunction-sections -fdata-sections -w -Werror -s -std=c++17
LOCAL_CPPFLAGS += -Wno-error=c++11-narrowing -fms-extensions -fno-rtti -fexceptions -fpermissive
LOCAL_LDFLAGS += -Wl,--gc-sections, -llog

LOCAL_LDLIBS := -llog -landroid -lz -lEGL -lGLESv3 -lGLESv2

LOCAL_SRC_FILES := Main.cpp \
                  ElfParser.cpp \
                  Disassembler.cpp \
                  FileBrowser.cpp \
                  Theme.cpp \
                  RenderUI.cpp \
                  RenderCFG.cpp \
                  cfg/CFGBlockRenderer.cpp \
                  RenderAssembly.cpp \
                  ImGui/backends/imgui_impl_opengl3.cpp \
                  ImGui/backends/imgui_impl_android.cpp \
                  ImGui/imgui.cpp \
                  ImGui/imgui_draw.cpp \
                  ImGui/imgui_demo.cpp \
                  ImGui/imgui_tables.cpp \
                  ImGui/imgui_widgets.cpp

# 静态库依赖
LOCAL_STATIC_LIBRARIES := capstone_static keystone_static

include $(BUILD_SHARED_LIBRARY)


