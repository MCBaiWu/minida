#pragma once
#include "Types.h"

class ElfParser {
private:
    std::vector<uint8_t> fileContent;
    const uint8_t* fileData = nullptr;
    size_t fileSize = 0;
    bool usingMmap = false;

    bool is64Bit = false;
    bool isLoaded = false;
    std::string filePath;

    ElfHeaderInfo headerInfo;
    std::vector<SectionInfo> sections;
    std::vector<ProgramInfo> programs;
    std::vector<SymbolInfo> symbols;
    std::vector<StringInfo> strings;
    std::vector<FunctionImport> imports;
    std::vector<FunctionExport> exports;
    std::vector<DynamicInfo> dynamicInfo;
    std::vector<RelocationInfo> relocations;

    const char* strTabData = nullptr;
    size_t strTabSize = 0;
    const char* dynStrTabData = nullptr;
    size_t dynStrTabSize = 0;

    std::string errorMessage;
    std::mutex parseMutex;

    std::vector<size_t> filteredSymbolIndices;
    std::vector<size_t> filteredExportIndices;
    std::vector<size_t> filteredImportIndices;
    std::vector<size_t> filteredStringIndices;

    void ScheduleFilter(const std::string& lowerText);
    void FilterSymbols(const std::string& lowerText);
    void FilterExports(const std::string& lowerText);
    void FilterStrings(const std::string& lowerText);
    bool ParseELF(AnalysisProgress* progress);
    bool ParseELF64(AnalysisProgress* progress);
    bool ParseELF32(AnalysisProgress* progress);
    const char* GetSectionTypeName(uint32_t type);
    const char* GetProgramTypeName(uint32_t type);
    void ParseSectionHeaders64(const Elf64_Ehdr* ehdr);
    void ParseSectionHeaders32(const Elf32_Ehdr* ehdr);
    void ParseProgramHeaders64(const Elf64_Ehdr* ehdr);
    void ParseProgramHeaders32(const Elf32_Ehdr* ehdr);
    const char* GetStringFromDynStr(size_t offset);
    const char* GetStringFromStrTab(size_t offset);
    void ParseDynamicInfo();
    void ParseRelocations64();
    void ParseRelocations32();
    void ParseSymbolTable64();
    void ParseSymbolTable32();
    void ExtractStrings();

public:
    ElfParser() = default;
    ~ElfParser();

    void UnloadFile();
    void SetMappedData(const void* data, size_t size);
    bool LoadFile(const std::string& path, AnalysisProgress* progress = nullptr);

    bool Is64Bit() const;
    bool IsLoaded() const;
    const std::string& GetFilePath() const;
    const std::string& GetError() const;
    const ElfHeaderInfo& GetHeaderInfo() const;
    const std::vector<SectionInfo>& GetSections() const;
    const std::vector<ProgramInfo>& GetPrograms() const;
    const std::vector<SymbolInfo>& GetSymbols() const;
    const std::vector<StringInfo>& GetStrings() const;
    const std::vector<FunctionImport>& GetImports() const;
    const std::vector<FunctionExport>& GetExports() const;
    const std::vector<DynamicInfo>& GetDynamicInfo() const;
    const std::vector<RelocationInfo>& GetRelocations() const;
    size_t GetFileSize() const;
    const uint8_t* GetFileData() const;

    const std::vector<size_t>& GetFilteredSymbols();
    const std::vector<size_t>& GetFilteredExports();
    const std::vector<size_t>& GetFilteredStrings();
    void SetSearchText(const char* text);
    void ClearSearch();
};
