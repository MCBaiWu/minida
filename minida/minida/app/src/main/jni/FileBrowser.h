#pragma once
#include "Types.h"

class FileBrowser {
private:
    std::string currentPath;
    std::vector<std::string> directoryStack;

public:
    struct FileEntry {
        std::string name;
        bool isDirectory;
        size_t size;
        time_t modified;
    };

    std::vector<FileEntry> currentFiles;
    std::string selectedFile;
    bool showFilePicker = false;

    FileBrowser();
    void SetRootPath(const std::string& path);
    void Refresh();
    bool IsELF(const std::string& filename);
    std::string FormatSize(uint64_t size);
    void Render();
    std::function<void(const std::string&)> OnFileSelected;
};
