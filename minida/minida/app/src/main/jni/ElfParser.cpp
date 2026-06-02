#include "ElfParser.h"
#include "Common.h"

// ============================================================================
// 高性能ELF解析器（零拷贝+后台线程）
// ============================================================================

ElfParser::~ElfParser() {
    UnloadFile();
}

void ElfParser::UnloadFile() {
    std::lock_guard<std::mutex> lock(parseMutex);

    // 清理持久化的文件内容
    fileContent.clear();
    fileContent.shrink_to_fit();
    fileData = nullptr;
    fileSize = 0;
    usingMmap = false;
    isLoaded = false;

    // 清理缓存
    sections.clear();
    programs.clear();
    symbols.clear();
    strings.clear();
    imports.clear();
    exports.clear();
    dynamicInfo.clear();
    relocations.clear();

    strTabData = nullptr;
    dynStrTabData = nullptr;

    filteredSymbolIndices.clear();
    filteredExportIndices.clear();
    filteredImportIndices.clear();
    filteredStringIndices.clear();
}

// 设置内存映射数据（外部管理mmap）
void ElfParser::SetMappedData(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(parseMutex);
    // 清除持久化的文件内容
    fileContent.clear();
    fileContent.shrink_to_fit();
    fileData = static_cast<const uint8_t*>(data);
    fileSize = size;
    usingMmap = true;
}

bool ElfParser::LoadFile(const std::string& path, AnalysisProgress* progress) {
    std::lock_guard<std::mutex> lock(parseMutex);

    filePath = path;
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        errorMessage = std::string("无法打开文件: ") + path;
        return false;
    }

    size_t fileSize = file.tellg();
    if (fileSize < 64) {
        errorMessage = "文件太小，不是有效的ELF文件";
        return false;
    }

    file.seekg(0, std::ios::beg);

    // 加载到持久化的vector中，避免悬空指针
    fileContent.resize(fileSize);
    if (!file.read(reinterpret_cast<char*>(fileContent.data()), fileSize)) {
        errorMessage = "读取文件失败";
        fileContent.clear();
        return false;
    }
    file.close();

    // 设置数据并解析
    fileData = fileContent.data();
    this->fileSize = fileSize;
    usingMmap = false;

    return ParseELF(progress);
}

bool ElfParser::Is64Bit() const { return is64Bit; }
bool ElfParser::IsLoaded() const { return isLoaded; }
const std::string& ElfParser::GetFilePath() const { return filePath; }
const std::string& ElfParser::GetError() const { return errorMessage; }
const ElfHeaderInfo& ElfParser::GetHeaderInfo() const { return headerInfo; }
const std::vector<SectionInfo>& ElfParser::GetSections() const { return sections; }
const std::vector<ProgramInfo>& ElfParser::GetPrograms() const { return programs; }
const std::vector<SymbolInfo>& ElfParser::GetSymbols() const { return symbols; }
const std::vector<StringInfo>& ElfParser::GetStrings() const { return strings; }
const std::vector<FunctionImport>& ElfParser::GetImports() const { return imports; }
const std::vector<FunctionExport>& ElfParser::GetExports() const { return exports; }
const std::vector<DynamicInfo>& ElfParser::GetDynamicInfo() const { return dynamicInfo; }
const std::vector<RelocationInfo>& ElfParser::GetRelocations() const { return relocations; }
size_t ElfParser::GetFileSize() const { return fileSize; }
const uint8_t* ElfParser::GetFileData() const { return fileData; }

// 搜索功能（带缓存）
const std::vector<size_t>& ElfParser::GetFilteredSymbols() {
    if (g_Layout.searchDirty && !g_Layout.lastSearchText.empty()) {
        FilterSymbols(g_Layout.lastSearchText);
    }
    return filteredSymbolIndices;
}

const std::vector<size_t>& ElfParser::GetFilteredExports() {
    if (g_Layout.searchDirty && !g_Layout.lastSearchText.empty()) {
        FilterExports(g_Layout.lastSearchText);
    }
    return filteredExportIndices;
}

const std::vector<size_t>& ElfParser::GetFilteredStrings() {
    if (g_Layout.searchDirty && !g_Layout.lastSearchText.empty()) {
        FilterStrings(g_Layout.lastSearchText);
    }
    return filteredStringIndices;
}

void ElfParser::SetSearchText(const char* text) {
    std::string newText = text;
    if (newText != g_Layout.lastSearchText) {
        g_Layout.lastSearchText = newText;
        g_Layout.searchDirty = true;

        // 转换为小写用于搜索
        std::transform(newText.begin(), newText.end(), newText.begin(), ::tolower);

        // 触发异步过滤
        ScheduleFilter(newText);
    }
}

void ElfParser::ClearSearch() {
    g_Layout.lastSearchText.clear();
    g_Layout.searchDirty = false;
    filteredSymbolIndices.clear();
    filteredExportIndices.clear();
    filteredImportIndices.clear();
    filteredStringIndices.clear();

    // 填充全部索引
    for (size_t i = 0; i < symbols.size(); i++) {
        filteredSymbolIndices.push_back(i);
    }
    for (size_t i = 0; i < exports.size(); i++) {
        filteredExportIndices.push_back(i);
    }
    for (size_t i = 0; i < strings.size(); i++) {
        filteredStringIndices.push_back(i);
    }
}

void ElfParser::ScheduleFilter(const std::string& lowerText) {
    // 异步执行过滤
    std::thread([this, lowerText]() {
        FilterSymbols(lowerText);
        FilterExports(lowerText);
        FilterStrings(lowerText);
    }).detach();
}

void ElfParser::FilterSymbols(const std::string& lowerText) {
    std::lock_guard<std::mutex> lock(parseMutex);
    filteredSymbolIndices.clear();

    if (lowerText.empty()) {
        for (size_t i = 0; i < symbols.size(); i++) {
            filteredSymbolIndices.push_back(i);
        }
        return;
    }

    for (size_t i = 0; i < symbols.size(); i++) {
        if (symbols[i].name.find(lowerText) != std::string_view::npos) {
            filteredSymbolIndices.push_back(i);
        }
    }
}

void ElfParser::FilterExports(const std::string& lowerText) {
    std::lock_guard<std::mutex> lock(parseMutex);
    filteredExportIndices.clear();

    if (lowerText.empty()) {
        for (size_t i = 0; i < exports.size(); i++) {
            filteredExportIndices.push_back(i);
        }
        return;
    }

    for (size_t i = 0; i < exports.size(); i++) {
        if (exports[i].name.find(lowerText) != std::string_view::npos) {
            filteredExportIndices.push_back(i);
        }
    }
}

void ElfParser::FilterStrings(const std::string& lowerText) {
    std::lock_guard<std::mutex> lock(parseMutex);
    filteredStringIndices.clear();

    if (lowerText.empty()) {
        for (size_t i = 0; i < strings.size(); i++) {
            filteredStringIndices.push_back(i);
        }
        return;
    }

    for (size_t i = 0; i < strings.size(); i++) {
        if (strings[i].value.find(lowerText) != std::string_view::npos) {
            filteredStringIndices.push_back(i);
        }
    }
}

bool ElfParser::ParseELF(AnalysisProgress* progress) {
    if (fileSize < 64) {
        errorMessage = "文件太小，无法解析ELF头";
        return false;
    }

    const unsigned char elfMagic[4] = {0x7F, 'E', 'L', 'F'};
    if (std::memcmp(fileData, elfMagic, 4) != 0) {
        errorMessage = "无效的ELF魔数";
        return false;
    }

    is64Bit = (fileData[4] == 2);

    if (progress) {
        progress->SetStage(1, "解析ELF头...", 10.0f);
        if (progress->ShouldCancel()) return false;
    }

    bool result = is64Bit ? ParseELF64(progress) : ParseELF32(progress);

    if (result && progress) {
        progress->SetStage(6, "分析完成", 100.0f);

        // 初始化搜索索引
        ClearSearch();
    }

    isLoaded = result;
    return result;
}

bool ElfParser::ParseELF64(AnalysisProgress* progress) {
    const Elf64_Ehdr* ehdr = reinterpret_cast<const Elf64_Ehdr*>(fileData);

    std::memcpy(headerInfo.magic, ehdr->e_ident, 4);
    headerInfo.class_ = ehdr->e_ident[EI_CLASS];
    headerInfo.endian = ehdr->e_ident[EI_DATA];
    headerInfo.version = ehdr->e_ident[EI_VERSION];
    headerInfo.osabi = ehdr->e_ident[EI_OSABI];
    headerInfo.type = ehdr->e_type;
    headerInfo.machine = ehdr->e_machine;
    headerInfo.version_ = ehdr->e_version;
    headerInfo.entryPoint = ehdr->e_entry;
    headerInfo.phoff = ehdr->e_phoff;
    headerInfo.shoff = ehdr->e_shoff;
    headerInfo.flags = ehdr->e_flags;
    headerInfo.ehsize = ehdr->e_ehsize;
    headerInfo.phentsize = ehdr->e_phentsize;
    headerInfo.phnum = ehdr->e_phnum;
    headerInfo.shentsize = ehdr->e_shentsize;
    headerInfo.shnum = ehdr->e_shnum;
    headerInfo.shstrndx = ehdr->e_shstrndx;
    headerInfo.valid = true;

    // 架构名称
    switch (ehdr->e_machine) {
        case EM_ARM: headerInfo.machineName = "ARM (32-bit)"; break;
        case EM_AARCH64: headerInfo.machineName = "ARM64 (AArch64)"; break;
        case EM_386: headerInfo.machineName = "x86 (32-bit)"; break;
        case EM_X86_64: headerInfo.machineName = "x86-64"; break;
        case EM_MIPS: headerInfo.machineName = "MIPS"; break;
        case EM_RISCV: headerInfo.machineName = "RISC-V"; break;
        case EM_PPC: headerInfo.machineName = "PowerPC"; break;
        case EM_PPC64: headerInfo.machineName = "PowerPC64"; break;
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "Unknown (0x%X)", ehdr->e_machine);
            headerInfo.machineName = buf;
            break;
        }
    }

    // 文件类型
    switch (ehdr->e_type) {
        case ET_NONE: headerInfo.typeName = "NONE (未知)"; break;
        case ET_REL: headerInfo.typeName = "REL (可重定位文件)"; break;
        case ET_EXEC: headerInfo.typeName = "EXEC (可执行文件)"; break;
        case ET_DYN: headerInfo.typeName = "DYN (共享对象/SO库)"; break;
        case ET_CORE: headerInfo.typeName = "CORE (Core转储文件)"; break;
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "Unknown (0x%X)", ehdr->e_type);
            headerInfo.typeName = buf;
            break;
        }
    }

    if (progress) {
        progress->SetStage(2, "解析节区头...", 25.0f);
        if (progress->ShouldCancel()) return false;
    }

    ParseSectionHeaders64(ehdr);

    if (progress) {
        progress->SetStage(3, "解析程序头...", 40.0f);
        if (progress->ShouldCancel()) return false;
    }

    ParseProgramHeaders64(ehdr);

    if (progress) {
        progress->SetStage(4, "解析符号表...", 55.0f);
        if (progress->ShouldCancel()) return false;
    }

    ParseSymbolTable64();

    if (progress) {
        progress->SetStage(5, "提取字符串...", 80.0f);
        if (progress->ShouldCancel()) return false;
    }

    ExtractStrings();
    ParseDynamicInfo();
    ParseRelocations64();

    return true;
}

bool ElfParser::ParseELF32(AnalysisProgress* progress) {
    const Elf32_Ehdr* ehdr = reinterpret_cast<const Elf32_Ehdr*>(fileData);

    headerInfo.valid = true;
    std::memcpy(headerInfo.magic, ehdr->e_ident, 4);
    headerInfo.class_ = ehdr->e_ident[EI_CLASS];
    headerInfo.endian = ehdr->e_ident[EI_DATA];
    headerInfo.version = ehdr->e_ident[EI_VERSION];
    headerInfo.osabi = ehdr->e_ident[EI_OSABI];
    headerInfo.type = ehdr->e_type;
    headerInfo.machine = ehdr->e_machine;
    headerInfo.version_ = ehdr->e_version;
    headerInfo.entryPoint = ehdr->e_entry;
    headerInfo.phoff = ehdr->e_phoff;
    headerInfo.shoff = ehdr->e_shoff;
    headerInfo.flags = ehdr->e_flags;
    headerInfo.ehsize = ehdr->e_ehsize;
    headerInfo.phentsize = ehdr->e_phentsize;
    headerInfo.phnum = ehdr->e_phnum;
    headerInfo.shentsize = ehdr->e_shentsize;
    headerInfo.shnum = ehdr->e_shnum;
    headerInfo.shstrndx = ehdr->e_shstrndx;

    switch (ehdr->e_machine) {
        case EM_ARM: headerInfo.machineName = "ARM (32-bit)"; break;
        case EM_386: headerInfo.machineName = "x86 (32-bit)"; break;
        case EM_MIPS: headerInfo.machineName = "MIPS"; break;
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "Unknown (0x%X)", ehdr->e_machine);
            headerInfo.machineName = buf;
            break;
        }
    }

    switch (ehdr->e_type) {
        case ET_REL: headerInfo.typeName = "REL (可重定位文件)"; break;
        case ET_EXEC: headerInfo.typeName = "EXEC (可执行文件)"; break;
        case ET_DYN: headerInfo.typeName = "DYN (共享对象)"; break;
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "Unknown (0x%X)", ehdr->e_type);
            headerInfo.typeName = buf;
            break;
        }
    }

    ParseSectionHeaders32(ehdr);
    ParseProgramHeaders32(ehdr);
    ParseSymbolTable32();
    ExtractStrings();
    ParseDynamicInfo();
    ParseRelocations32();

    return true;
}

const char* ElfParser::GetSectionTypeName(uint32_t type) {
    switch (type) {
        case SHT_NULL: return "NULL";
        case SHT_PROGBITS: return "PROGBITS";
        case SHT_SYMTAB: return "SYMTAB";
        case SHT_STRTAB: return "STRTAB";
        case SHT_RELA: return "RELA";
        case SHT_HASH: return "HASH";
        case SHT_DYNAMIC: return "DYNAMIC";
        case SHT_NOTE: return "NOTE";
        case SHT_NOBITS: return "NOBITS";
        case SHT_REL: return "REL";
        case SHT_SHLIB: return "SHLIB";
        case SHT_DYNSYM: return "DYNSYM";
        case SHT_INIT: return "INIT";
        case SHT_FINI: return "FINI";
        default: {
            static char buf[16];
            snprintf(buf, sizeof(buf), "0x%X", type);
            return buf;
        }
    }
}

const char* ElfParser::GetProgramTypeName(uint32_t type) {
    switch (type) {
        case PT_NULL: return "NULL";
        case PT_LOAD: return "LOAD";
        case PT_DYNAMIC: return "DYNAMIC";
        case PT_INTERP: return "INTERP";
        case PT_NOTE: return "NOTE";
        case PT_SHLIB: return "SHLIB";
        case PT_PHDR: return "PHDR";
        case PT_TLS: return "TLS";
        default: {
            static char buf[16];
            snprintf(buf, sizeof(buf), "0x%X", type);
            return buf;
        }
    }
}

void ElfParser::ParseSectionHeaders64(const Elf64_Ehdr* ehdr) {
    sections.clear();
    sections.reserve(headerInfo.shnum);

    const char* strTab = nullptr;
    size_t strTabSize = 0;

    if (headerInfo.shstrndx < headerInfo.shnum && headerInfo.shnum > 0) {
        const Elf64_Shdr* shdr = reinterpret_cast<const Elf64_Shdr*>(
            fileData + headerInfo.shoff + headerInfo.shstrndx * headerInfo.shentsize);
        if (shdr->sh_offset + shdr->sh_size <= fileSize) {
            strTab = reinterpret_cast<const char*>(fileData + shdr->sh_offset);
            strTabSize = shdr->sh_size;
        }
    }

    for (uint16_t i = 0; i < headerInfo.shnum; i++) {
        const Elf64_Shdr* shdr = reinterpret_cast<const Elf64_Shdr*>(
            fileData + headerInfo.shoff + i * headerInfo.shentsize);

        SectionInfo info;
        info.addr = shdr->sh_addr;
        info.offset = shdr->sh_offset;
        info.size = shdr->sh_size;
        info.type = shdr->sh_type;
        info.flags = shdr->sh_flags;

        if (shdr->sh_name < strTabSize) {
            info.name = std::string_view(strTab + shdr->sh_name);
        } else {
            static char buf[32];
            snprintf(buf, sizeof(buf), ".section_%u", i);
            info.name = std::string_view(buf);
        }

        sections.push_back(info);
    }
}

void ElfParser::ParseSectionHeaders32(const Elf32_Ehdr* ehdr) {
    sections.clear();
    sections.reserve(headerInfo.shnum);

    const char* strTab = nullptr;
    uint32_t strTabSize = 0;

    if (headerInfo.shstrndx < headerInfo.shnum && headerInfo.shnum > 0) {
        const Elf32_Shdr* shdr = reinterpret_cast<const Elf32_Shdr*>(
            fileData + headerInfo.shoff + headerInfo.shstrndx * headerInfo.shentsize);
        if (shdr->sh_offset + shdr->sh_size <= fileSize) {
            strTab = reinterpret_cast<const char*>(fileData + shdr->sh_offset);
            strTabSize = shdr->sh_size;
        }
    }

    for (uint16_t i = 0; i < headerInfo.shnum; i++) {
        const Elf32_Shdr* shdr = reinterpret_cast<const Elf32_Shdr*>(
            fileData + headerInfo.shoff + i * headerInfo.shentsize);

        SectionInfo info;
        info.addr = shdr->sh_addr;
        info.offset = shdr->sh_offset;
        info.size = shdr->sh_size;
        info.type = shdr->sh_type;
        info.flags = shdr->sh_flags;

        if (shdr->sh_name < strTabSize) {
            info.name = std::string_view(strTab + shdr->sh_name);
        } else {
            static char buf[32];
            snprintf(buf, sizeof(buf), ".section_%u", i);
            info.name = std::string_view(buf);
        }

        sections.push_back(info);
    }
}

void ElfParser::ParseProgramHeaders64(const Elf64_Ehdr* ehdr) {
    programs.clear();
    programs.reserve(headerInfo.phnum);

    for (uint16_t i = 0; i < headerInfo.phnum; i++) {
        const Elf64_Phdr* phdr = reinterpret_cast<const Elf64_Phdr*>(
            fileData + headerInfo.phoff + i * headerInfo.phentsize);

        ProgramInfo info;
        info.type = phdr->p_type;
        info.offset = phdr->p_offset;
        info.vaddr = phdr->p_vaddr;
        info.paddr = phdr->p_paddr;
        info.filesz = phdr->p_filesz;
        info.memsz = phdr->p_memsz;
        info.flags = phdr->p_flags;
        info.align = phdr->p_align;
        info.typeName = GetProgramTypeName(info.type);

        programs.push_back(info);
    }
}

void ElfParser::ParseProgramHeaders32(const Elf32_Ehdr* ehdr) {
    programs.clear();
    programs.reserve(headerInfo.phnum);

    for (uint16_t i = 0; i < headerInfo.phnum; i++) {
        const Elf32_Phdr* phdr = reinterpret_cast<const Elf32_Phdr*>(
            fileData + headerInfo.phoff + i * headerInfo.phentsize);

        ProgramInfo info;
        info.type = phdr->p_type;
        info.offset = phdr->p_offset;
        info.vaddr = phdr->p_vaddr;
        info.paddr = phdr->p_paddr;
        info.filesz = phdr->p_filesz;
        info.memsz = phdr->p_memsz;
        info.flags = phdr->p_flags;
        info.align = phdr->p_align;
        info.typeName = GetProgramTypeName(info.type);

        programs.push_back(info);
    }
}

const char* ElfParser::GetStringFromDynStr(size_t offset) {
    if (dynStrTabData && offset < dynStrTabSize) {
        return dynStrTabData + offset;
    }
    return "";
}

const char* ElfParser::GetStringFromStrTab(size_t offset) {
    if (strTabData && offset < strTabSize) {
        return strTabData + offset;
    }
    return "";
}

void ElfParser::ParseDynamicInfo() {
    dynamicInfo.clear();

    for (const SectionInfo& sec : sections) {
        if (sec.type == SHT_DYNAMIC) {
            size_t entryCount = sec.size / (is64Bit ? sizeof(Elf64_Dyn) : sizeof(Elf32_Dyn));

            for (size_t i = 0; i < entryCount; i++) {
                size_t entryOffset = sec.offset + i * (is64Bit ? sizeof(Elf64_Dyn) : sizeof(Elf32_Dyn));
                if (entryOffset + (is64Bit ? sizeof(Elf64_Dyn) : sizeof(Elf32_Dyn)) > fileSize) break;

                DynamicInfo dinfo;

                if (is64Bit) {
                    const Elf64_Dyn* dyn = reinterpret_cast<const Elf64_Dyn*>(fileData + entryOffset);
                    dinfo.tagValue = dyn->d_tag;
                    dinfo.tagName = (dyn->d_tag == DT_NULL) ? "NULL" :
                        (dyn->d_tag == DT_NEEDED) ? "NEEDED" :
                        (dyn->d_tag == DT_STRTAB) ? "STRTAB" :
                        (dyn->d_tag == DT_SYMTAB) ? "SYMTAB" :
                        (dyn->d_tag == DT_RELA) ? "RELA" :
                        (dyn->d_tag == DT_REL) ? "REL" :
                        (dyn->d_tag == DT_PLTGOT) ? "PLTGOT" :
                        (dyn->d_tag == DT_TEXTREL) ? "TEXTREL" :
                        (dyn->d_tag == DT_BIND_NOW) ? "BIND_NOW" :
                        (dyn->d_tag == DT_RUNPATH) ? "RUNPATH" :
                        (dyn->d_tag == DT_FLAGS) ? "FLAGS" :
                        (dyn->d_tag == DT_PREINIT_ARRAY) ? "PREINIT_ARRAY" :
                        (dyn->d_tag == DT_INIT_ARRAY) ? "INIT_ARRAY" :
                        (dyn->d_tag == DT_FINI_ARRAY) ? "FINI_ARRAY" :
                        (dyn->d_tag == DT_GNU_HASH) ? "GNU_HASH" :
                        (dyn->d_tag == DT_VERNEED) ? "VERNEED" :
                        (dyn->d_tag == DT_VERSYM) ? "VERSYM" : "UNKNOWN";

                    if (dyn->d_tag == DT_NEEDED || dyn->d_tag == DT_RUNPATH) {
                        dinfo.stringValue = GetStringFromDynStr(dyn->d_un.d_val);
                    }
                } else {
                    const Elf32_Dyn* dyn = reinterpret_cast<const Elf32_Dyn*>(fileData + entryOffset);
                    dinfo.tagValue = dyn->d_tag;
                    dinfo.tagName = (dyn->d_tag == DT_NULL) ? "NULL" :
                        (dyn->d_tag == DT_NEEDED) ? "NEEDED" :
                        (dyn->d_tag == DT_STRTAB) ? "STRTAB" :
                        (dyn->d_tag == DT_SYMTAB) ? "SYMTAB" : "UNKNOWN";

                    if (dyn->d_tag == DT_NEEDED) {
                        dinfo.stringValue = GetStringFromDynStr(dyn->d_un.d_val);
                    }
                }

                dynamicInfo.push_back(dinfo);
            }
            break;
        }
    }
}

void ElfParser::ParseRelocations64() {
    relocations.clear();

    const char* dynStrTab = nullptr;
    size_t dynStrTabSize = 0;

    for (const SectionInfo& sec : sections) {
        if (sec.name == ".dynstr") {
            if (sec.offset + sec.size <= fileSize) {
                dynStrTab = reinterpret_cast<const char*>(fileData + sec.offset);
                dynStrTabSize = sec.size;
            }
            break;
        }
    }

    const char* dynSymTab = nullptr;
    size_t dynSymTabSize = 0;

    for (const SectionInfo& sec : sections) {
        if (sec.name == ".dynsym") {
            if (sec.offset + sec.size <= fileSize) {
                dynSymTab = reinterpret_cast<const char*>(fileData + sec.offset);
                dynSymTabSize = sec.size;
            }
            break;
        }
    }

    for (const SectionInfo& sec : sections) {
        if (sec.type == SHT_REL || sec.type == SHT_RELA) {
            RelocationInfo rel;
            rel.addend = 0;

            size_t entrySize = (sec.type == SHT_RELA) ? sizeof(Elf64_Rela) : sizeof(Elf64_Rel);
            size_t entryCount = sec.size / entrySize;

            for (size_t i = 0; i < entryCount; i++) {
                size_t entryOffset = sec.offset + i * entrySize;

                if (sec.type == SHT_RELA) {
                    const Elf64_Rela* rela = reinterpret_cast<const Elf64_Rela*>(fileData + entryOffset);
                    rel.offset = rela->r_offset;
                    rel.info = rela->r_info;
                    rel.addend = rela->r_addend;
                } else {
                    const Elf64_Rel* relEntry = reinterpret_cast<const Elf64_Rel*>(fileData + entryOffset);
                    rel.offset = relEntry->r_offset;
                    rel.info = relEntry->r_info;
                    rel.addend = 0;
                }

                size_t symIndex = ELF64_R_SYM(rel.info);
                if (dynSymTab && symIndex * sizeof(Elf64_Sym) < dynSymTabSize) {
                    const Elf64_Sym* sym = reinterpret_cast<const Elf64_Sym*>(dynSymTab + symIndex * sizeof(Elf64_Sym));
                    if (sym->st_name < dynStrTabSize) {
                        rel.symbolName = dynStrTab + sym->st_name;
                    }
                }

                relocations.push_back(rel);
            }
        }
    }
}

void ElfParser::ParseRelocations32() {
    relocations.clear();

    const char* dynStrTab = nullptr;
    size_t dynStrTabSize = 0;

    for (const SectionInfo& sec : sections) {
        if (sec.name == ".dynstr") {
            if (sec.offset + sec.size <= fileSize) {
                dynStrTab = reinterpret_cast<const char*>(fileData + sec.offset);
                dynStrTabSize = sec.size;
            }
            break;
        }
    }

    const char* dynSymTab = nullptr;
    size_t dynSymTabSize = 0;

    for (const SectionInfo& sec : sections) {
        if (sec.name == ".dynsym") {
            if (sec.offset + sec.size <= fileSize) {
                dynSymTab = reinterpret_cast<const char*>(fileData + sec.offset);
                dynSymTabSize = sec.size;
            }
            break;
        }
    }

    for (const SectionInfo& sec : sections) {
        if (sec.type == SHT_REL || sec.type == SHT_RELA) {
            RelocationInfo rel;

            size_t entrySize = (sec.type == SHT_RELA) ? sizeof(Elf32_Rela) : sizeof(Elf32_Rel);
            size_t entryCount = sec.size / entrySize;

            for (size_t i = 0; i < entryCount; i++) {
                size_t entryOffset = sec.offset + i * entrySize;

                if (sec.type == SHT_RELA) {
                    const Elf32_Rela* rela = reinterpret_cast<const Elf32_Rela*>(fileData + entryOffset);
                    rel.offset = rela->r_offset;
                    rel.info = rela->r_info;
                    rel.addend = rela->r_addend;
                } else {
                    const Elf32_Rel* relEntry = reinterpret_cast<const Elf32_Rel*>(fileData + entryOffset);
                    rel.offset = relEntry->r_offset;
                    rel.info = relEntry->r_info;
                    rel.addend = 0;
                }

                size_t symIndex = ELF32_R_SYM(rel.info);
                if (dynSymTab && symIndex * sizeof(Elf32_Sym) < dynSymTabSize) {
                    const Elf32_Sym* sym = reinterpret_cast<const Elf32_Sym*>(dynSymTab + symIndex * sizeof(Elf32_Sym));
                    if (sym->st_name < dynStrTabSize) {
                        rel.symbolName = dynStrTab + sym->st_name;
                    }
                }

                relocations.push_back(rel);
            }
        }
    }
}

void ElfParser::ParseSymbolTable64() {
    symbols.clear();
    exports.clear();
    imports.clear();

    // 查找字符串表
    for (const SectionInfo& sec : sections) {
        if (sec.type == SHT_STRTAB || sec.name == ".strtab") {
            if (sec.offset + sec.size <= fileSize) {
                strTabData = reinterpret_cast<const char*>(fileData + sec.offset);
                strTabSize = sec.size;
                break;
            }
        }
    }

    // 查找dynstr
    for (const SectionInfo& sec : sections) {
        if (sec.name == ".dynstr") {
            if (sec.offset + sec.size <= fileSize) {
                dynStrTabData = reinterpret_cast<const char*>(fileData + sec.offset);
                dynStrTabSize = sec.size;
            }
            break;
        }
    }

    // 解析符号表
    for (const SectionInfo& sec : sections) {
        if (sec.type == SHT_SYMTAB || sec.type == SHT_DYNSYM) {
            size_t symCount = sec.size / sizeof(Elf64_Sym);
            if (symCount == 0 || sec.offset + sec.size > fileSize) continue;

            const Elf64_Sym* syms = reinterpret_cast<const Elf64_Sym*>(fileData + sec.offset);

            for (size_t j = 0; j < symCount; j++) {
                const Elf64_Sym& sym = syms[j];
                SymbolInfo info;
                info.value = sym.st_value;
                info.size = sym.st_size;
                info.bind = sym.st_info >> 4;
                info.type = sym.st_info & 0xF;
                info.shndx = sym.st_shndx;

                // 获取名（零拷贝）
                const char* namePtr = nullptr;
                if (sec.type == SHT_DYNSYM && sym.st_name < dynStrTabSize) {
                    namePtr = dynStrTabData + sym.st_name;
                } else if (sym.st_name < strTabSize) {
                    namePtr = strTabData + sym.st_name;
                }

                if (namePtr) {
                    info.name = std::string_view(namePtr);
                }

                // 获取段名（零拷贝）
                if (info.shndx < sections.size()) {
                    info.sectionName = sections[info.shndx].name;
                } else if (info.shndx == SHN_UNDEF) {
                    static const std::string_view undStr = "UND";
                    info.sectionName = undStr;
                } else {
                    static char buf[16];
                    snprintf(buf, sizeof(buf), "%u", info.shndx);
                    info.sectionName = std::string_view(buf);
                }

                // 绑定类型
                switch (info.bind) {
                    case STB_LOCAL: info.bindName = "LOCAL"; break;
                    case STB_GLOBAL: info.bindName = "GLOBAL"; break;
                    case STB_WEAK: info.bindName = "WEAK"; break;
                    default: info.bindName = ""; break;
                }

                // 类型
                switch (info.type) {
                    case STT_NOTYPE: info.typeName = "NOTYPE"; break;
                    case STT_FUNC: info.typeName = "FUNC"; break;
                    case STT_OBJECT: info.typeName = "OBJECT"; break;
                    case STT_SECTION: info.typeName = "SECTION"; break;
                    case STT_FILE: info.typeName = "FILE"; break;
                    default: info.typeName = ""; break;
                }

                symbols.push_back(info);

                // 导出函数
                if (info.type == STT_FUNC && info.value != 0 && info.shndx != SHN_UNDEF) {
                    FunctionExport exp;
                    exp.name = info.name;
                    exp.address = info.value;

                    bool found = false;
                    for (const auto& e : exports) {
                        if (e.name == exp.name) {
                            found = true;
                            break;
                        }
                    }
                    if (!found && !info.name.empty()) {
                        exports.push_back(exp);
                    }
                }

                // 导入函数
                if (info.type == STT_FUNC && info.shndx == SHN_UNDEF && !info.name.empty()) {
                    FunctionImport imp;
                    imp.library = "Unknown";
                    imp.function = std::string(info.name);
                    imp.address = 0;

                    bool found = false;
                    for (const auto& im : imports) {
                        if (im.function == imp.function) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        imports.push_back(imp);
                    }
                }
            }
        }
    }

    // 从NEEDED条目解析库名称
    for (const DynamicInfo& dinfo : dynamicInfo) {
        if (dinfo.tagName == "NEEDED" && !dinfo.stringValue.empty()) {
            for (auto& imp : imports) {
                if (imp.library == "Unknown") {
                    imp.library = dinfo.stringValue;
                }
            }
        }
    }
}

void ElfParser::ParseSymbolTable32() {
    symbols.clear();
    exports.clear();
    imports.clear();

    for (const SectionInfo& sec : sections) {
        if (sec.type == SHT_STRTAB || sec.name == ".strtab") {
            if (sec.offset + sec.size <= fileSize) {
                strTabData = reinterpret_cast<const char*>(fileData + sec.offset);
                strTabSize = sec.size;
                break;
            }
        }
    }

    for (const SectionInfo& sec : sections) {
        if (sec.type == SHT_SYMTAB || sec.type == SHT_DYNSYM) {
            size_t symCount = sec.size / sizeof(Elf32_Sym);
            if (symCount == 0 || sec.offset + sec.size > fileSize) continue;

            const Elf32_Sym* syms = reinterpret_cast<const Elf32_Sym*>(fileData + sec.offset);

            for (size_t j = 0; j < symCount; j++) {
                const Elf32_Sym& sym = syms[j];
                SymbolInfo info;
                info.value = sym.st_value;
                info.size = sym.st_size;
                info.bind = sym.st_info >> 4;
                info.type = sym.st_info & 0xF;
                info.shndx = sym.st_shndx;

                if (sym.st_name < strTabSize) {
                    info.name = std::string_view(strTabData + sym.st_name);
                }

                if (info.shndx < sections.size()) {
                    info.sectionName = sections[info.shndx].name;
                } else if (info.shndx == SHN_UNDEF) {
                    static const std::string_view undStr = "UND";
                    info.sectionName = undStr;
                } else {
                    static char buf[16];
                    snprintf(buf, sizeof(buf), "%u", info.shndx);
                    info.sectionName = std::string_view(buf);
                }

                symbols.push_back(info);

                if (info.type == STT_FUNC && info.value != 0 && info.shndx != SHN_UNDEF) {
                    FunctionExport exp;
                    exp.name = info.name;
                    exp.address = info.value;

                    bool found = false;
                    for (const auto& e : exports) {
                        if (e.name == exp.name) {
                            found = true;
                            break;
                        }
                    }
                    if (!found && !info.name.empty()) {
                        exports.push_back(exp);
                    }
                }

                if (info.type == STT_FUNC && info.shndx == SHN_UNDEF && !info.name.empty()) {
                    FunctionImport imp;
                    imp.library = "Unknown";
                    imp.function = std::string(info.name);
                    imp.address = 0;

                    bool found = false;
                    for (const auto& im : imports) {
                        if (im.function == imp.function) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        imports.push_back(imp);
                    }
                }
            }
        }
    }
}

void ElfParser::ExtractStrings() {
    strings.clear();

    const size_t minLength = 4;
    const size_t maxLength = 512;

    const char* data = reinterpret_cast<const char*>(fileData);
    size_t fileSize = this->fileSize;

    std::string current;
    int currentSection = -1;

    for (size_t i = 0; i < fileSize; i++) {
        char c = data[i];

        if (c >= 32 && c < 127) {
            current += c;
        } else {
            if (current.length() >= minLength && current.length() <= maxLength) {
                StringInfo info;
                info.value = std::string_view(data + i - current.length() + 1, current.length());
                info.offset = i - current.length() + 1;
                info.sectionIndex = currentSection;
                strings.push_back(info);
            }
            current.clear();
        }
    }

    if (current.length() >= minLength) {
        StringInfo info;
        info.value = std::string_view(data + fileSize - current.length(), current.length());
        info.offset = fileSize - current.length();
        info.sectionIndex = -1;
        strings.push_back(info);
    }

    if (strings.size() > PerfConfig::STRING_CACHE_LIMIT) {
        strings.resize(PerfConfig::STRING_CACHE_LIMIT);
    }
}

ElfParser elfParser;
std::string currentFilePath;
std::string currentFileName;
