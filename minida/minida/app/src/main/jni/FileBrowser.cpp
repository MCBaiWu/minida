#include "FileBrowser.h"
#include "Common.h"

// ============================================================================
// 高性能文件浏览器
// ============================================================================

FileBrowser::FileBrowser() {
    currentPath = "/sdcard";
}

void FileBrowser::SetRootPath(const std::string& path) {
    currentPath = path;
    directoryStack.clear();
    Refresh();
}

void FileBrowser::Refresh() {
    currentFiles.clear();
    selectedFile.clear();

    DIR* dir = opendir(currentPath.c_str());
    if (dir == NULL) {
        const char* paths[] = {"/sdcard", "/storage/emulated/0", "/data/local/tmp", "/data/data", "/"};
        for (size_t p = 0; p < sizeof(paths) / sizeof(paths[0]); p++) {
            dir = opendir(paths[p]);
            if (dir != NULL) {
                currentPath = paths[p];
                break;
            }
        }

        if (dir == NULL) {
            LOGE("无法打开目录: %s", currentPath.c_str());
            return;
        }
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        FileEntry file;
        file.name = entry->d_name;

        if (entry->d_type != DT_UNKNOWN) {
            file.isDirectory = (entry->d_type == DT_DIR);
        } else {
            std::string fullPath = currentPath + "/" + file.name;
            struct stat st;
            if (stat(fullPath.c_str(), &st) == 0) {
                file.isDirectory = S_ISDIR(st.st_mode);
            } else {
                file.isDirectory = false;
            }
        }
        file.size = 0;
        file.modified = 0;

        std::string fullPath = currentPath + "/" + file.name;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0) {
            file.size = st.st_size;
            file.modified = st.st_mtime;
            if (entry->d_type == DT_UNKNOWN) {
                file.isDirectory = S_ISDIR(st.st_mode);
            }
        }

        currentFiles.push_back(file);
    }

    closedir(dir);

    std::sort(currentFiles.begin(), currentFiles.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
}

bool FileBrowser::IsELF(const std::string& filename) {
    size_t pos = filename.rfind('.');
    if (pos == std::string::npos) return false;

    std::string ext = filename.substr(pos);
    for (size_t i = 0; i < ext.length(); i++) {
        ext[i] = std::tolower(ext[i]);
    }

    return (ext == ".so" || ext == ".elf" || ext == ".o" ||
            ext == ".bin" || ext == ".apk" || ext == "");
}

std::string FileBrowser::FormatSize(uint64_t size) {
    const char* suffixes[] = {"B", "KB", "MB", "GB"};
    int suffixIndex = 0;
    double dSize = (double)size;
    while (dSize >= 1024 && suffixIndex < 3) {
        dSize /= 1024;
        suffixIndex++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", dSize, suffixes[suffixIndex]);
    return std::string(buf);
}

void FileBrowser::Render() {
    if (!showFilePicker) return;

    if (!ImGui::IsPopupOpen("选择ELF文件###FilePicker")) {
        ImGui::OpenPopup("选择ELF文件###FilePicker");
    }

    ImVec2 popupSize = ImVec2(glWidth * 0.6f, glHeight * 0.7f);
    ImGui::SetNextWindowSize(popupSize, ImGuiCond_FirstUseEver);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool isWindowOpen = showFilePicker;

    if (ImGui::BeginPopupModal("选择ELF文件###FilePicker", &isWindowOpen,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {

    // 路径显示
    ImGui::TextColored(ImVec4(0.40f, 0.75f, 0.45f, 1.0f), "当前路径:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.85f, 1.0f), "%s", currentPath.c_str());

    ImGui::Separator();

    // 工具栏
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(g_Layout.padding, g_Layout.padding / 2));

    if (ImGui::Button("上级", ImVec2(80 * g_Layout.uiScale, g_Layout.buttonHeight))) {
        if (!directoryStack.empty()) {
            currentPath = directoryStack.back();
            directoryStack.pop_back();
            Refresh();
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("刷新", ImVec2(80 * g_Layout.uiScale, g_Layout.buttonHeight))) {
        Refresh();
    }
    ImGui::SameLine();

    char buf[64];
    snprintf(buf, sizeof(buf), "共 %zu 项", currentFiles.size());
    ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "%s", buf);

    ImGui::PopStyleVar();
    ImGui::Separator();

    // 文件列表
    float listHeight = ImGui::GetContentRegionAvail().y - g_Layout.buttonHeight - g_Layout.padding * 2;

    if (ImGui::BeginTable("FileTable", 3,
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable,
        ImVec2(0, listHeight))) {

        ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthFixed, 80 * g_Layout.uiScale);
        ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 70 * g_Layout.uiScale);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < currentFiles.size(); i++) {
            const FileEntry& file = currentFiles[i];

            if (!file.name.empty() && file.name[0] == '.') continue;

            ImGui::TableNextRow(0, g_Layout.rowHeight);
            ImGui::TableSetColumnIndex(0);

            ImVec4 iconColor;
            const char* icon;

            if (file.isDirectory) {
                icon = "[DIR]";
                iconColor = ImVec4(0.40f, 0.70f, 0.90f, 1.0f);
            } else if (IsELF(file.name)) {
                icon = "[ELF]";
                iconColor = ImVec4(0.40f, 0.80f, 0.50f, 1.0f);
            } else {
                icon = "[FILE]";
                iconColor = ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
            }

            ImGui::PushStyleColor(ImGuiCol_Text, iconColor);
            if (ImGui::Selectable((std::string(icon) + " " + file.name).c_str(), false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0, g_Layout.rowHeight))) {

                selectedFile = file.name;

                if (file.isDirectory) {
                    directoryStack.push_back(currentPath);
                    currentPath = currentPath + "/" + file.name;
                    Refresh();
                } else {
                    std::string fullPath = currentPath + "/" + file.name;
                    showFilePicker = false;
                    ImGui::CloseCurrentPopup();
                    if (OnFileSelected) {
                        OnFileSelected(fullPath);
                    }
                }
            }
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(1);
            if (!file.isDirectory && file.size > 0) {
                ImGui::TextColored(ImVec4(0.50f, 0.55f, 0.50f, 1.0f), "%s", FormatSize(file.size).c_str());
            } else {
                ImGui::TextColored(ImVec4(0.35f, 0.38f, 0.35f, 1.0f), "-");
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(iconColor, "%s", file.isDirectory ? "文件夹" : (IsELF(file.name) ? "ELF" : "文件"));
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    // 底部按钮
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 100 * g_Layout.uiScale - g_Layout.padding);
    if (ImGui::Button("取消", ImVec2(100 * g_Layout.uiScale, g_Layout.buttonHeight))) {
        showFilePicker = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    }

    if (!isWindowOpen) {
        showFilePicker = false;
    }
}

FileBrowser fileBrowser;
