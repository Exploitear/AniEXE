#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <string_view>
#include <array>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>
#include <sstream>
#include <random>
#include <chrono>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace fs = std::filesystem;
using std::wstring;
using std::wstring_view;

constexpr wchar_t CONFIG_FILE[] = L"launcher.ini";
constexpr wchar_t CONFIG_SECTION[] = L"Launcher";
constexpr wchar_t MUTEX_NAME[] = L"Local\\AnimeLauncherSingleInstance";
constexpr size_t CMD_MAX_LENGTH = 32000;
constexpr size_t CMD_SAFETY_MARGIN = 256;
constexpr wchar_t DEFAULT_FLAGS[] = L"--fs --save-position-on-quit --keep-open=no --shuffle=no --force-window=immediate";

const std::unordered_set<wstring> VIDEO_EXTENSIONS = {
    L".mkv", L".mp4", L".avi", L".webm", L".mov",
    L".flv", L".wmv", L".m4v", L".ts", L".ogm"
};

constexpr std::array<wstring_view, 4> DEFAULT_SKIP_PATTERNS = {
    L"sample", L"trailer", L"credit", L"extra"
};

constexpr std::array<wstring_view, 5> SYSTEM_MPV_PATHS = {
    L"C:\\Users\\%USERNAME%\\AppData\\Local\\Programs\\mpv.net\\mpvnet.exe",
    L"C:\\Program Files\\mpv.net\\mpvnet.exe",
    L"C:\\Program Files\\mpv\\mpv.exe",
    L"C:\\Program Files (x86)\\mpv.net\\mpvnet.exe",
    L"C:\\Program Files (x86)\\mpv\\mpv.exe"
};

class HandleGuard {
    HANDLE h_ = nullptr;
public:
    HandleGuard() = default;
    explicit HandleGuard(HANDLE h) : h_(h) {}
    ~HandleGuard() { if (h_ && h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
    HANDLE get() const noexcept { return h_; }
    explicit operator bool() const noexcept { return h_ && h_ != INVALID_HANDLE_VALUE; }
};

class RegKeyGuard {
    HKEY h_ = nullptr;
public:
    RegKeyGuard() = default;
    explicit RegKeyGuard(HKEY h) : h_(h) {}
    ~RegKeyGuard() { if (h_) RegCloseKey(h_); }
    RegKeyGuard(const RegKeyGuard&) = delete;
    RegKeyGuard& operator=(const RegKeyGuard&) = delete;
    HKEY get() const noexcept { return h_; }
};

class MutexGuard {
    HANDLE h_ = nullptr;
    bool owns_ = false;
public:
    MutexGuard() = default;
    explicit MutexGuard(HANDLE h, bool owns = true) : h_(h), owns_(owns) {}
    ~MutexGuard() { 
        if (h_) { 
            if (owns_) ReleaseMutex(h_); 
            CloseHandle(h_); 
        } 
    }
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;
    HANDLE get() const noexcept { return h_; }
    explicit operator bool() const noexcept { return h_ != nullptr && owns_; }
};

namespace {

inline void TrimInPlace(wstring& s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    if (first == wstring::npos) { s.clear(); return; }
    const auto last = s.find_last_not_of(L" \t\r\n");
    s = s.substr(first, last - first + 1);
}

inline void ToLowerInPlace(wstring& s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
}

void SanitizeFlags(wstring& flags) {
    size_t quoteCount = std::count(flags.begin(), flags.end(), L'"');
    if (quoteCount % 2 != 0) {
        flags.erase(std::remove(flags.begin(), flags.end(), L'"'), flags.end());
    }
    flags.erase(std::remove_if(flags.begin(), flags.end(), [](wchar_t c) {
        return c < 32 && c != L'\t';
    }), flags.end());
}

wstring ExpandEnvStrings(const wstring& input) {
    if (input.find(L'%') == wstring::npos) return input;
    
    DWORD requiredSize = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (requiredSize == 0) return input;

    std::vector<wchar_t> buffer(requiredSize);
    ExpandEnvironmentStringsW(input.c_str(), buffer.data(), requiredSize);
    return wstring(buffer.data(), requiredSize - 1);
}

wstring GetExeDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) return L"";
        if (len < buffer.size()) return fs::path(buffer.data()).parent_path().wstring();
        if (buffer.size() > 65536) return L"";
        buffer.resize(buffer.size() * 2);
    }
}

wstring GetErrorMessage(DWORD errorCode) {
    wchar_t* msgBuf = nullptr;
    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&msgBuf), 0, nullptr
    );
    
    if (size == 0 || !msgBuf) {
        return L"Unknown error";
    }
    
    wstring message(msgBuf, size);
    LocalFree(msgBuf);
    
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    
    return message;
}

bool HasVideoExtension(const fs::path& path) {
    if (!path.has_extension()) return false;
    wstring ext = path.extension().wstring();
    ToLowerInPlace(ext);
    return VIDEO_EXTENSIONS.find(ext) != VIDEO_EXTENSIONS.end();
}

bool ShouldSkipFile(const fs::path& path, const std::vector<wstring>& patterns) {
    wstring name = path.filename().wstring();
    ToLowerInPlace(name);
    for (const auto& pattern : patterns) {
        if (name.find(pattern) != wstring::npos) return true;
    }
    return false;
}

std::string WStringToUTF8(const wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    
    std::string str(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    str.resize(size - 1);
    return str;
}

std::vector<wstring> ParseCommaSeparated(const wstring& input) {
    std::vector<wstring> result;
    std::wstringstream ss(input);
    wstring item;
    while (std::getline(ss, item, L',')) {
        TrimInPlace(item);
        if (!item.empty()) {
            ToLowerInPlace(item);
            result.push_back(item);
        }
    }
    return result;
}

std::vector<wstring> CollectVideoFiles(const wstring& workDir, const std::vector<wstring>& skipPatterns) {
    std::vector<wstring> videoFiles;
    videoFiles.reserve(50);
    
    try {
        for (const auto& entry : fs::directory_iterator(workDir, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            if (ShouldSkipFile(entry.path(), skipPatterns)) continue;
            if (HasVideoExtension(entry.path())) {
                videoFiles.push_back(entry.path().filename().wstring());
            }
        }
    } catch (const fs::filesystem_error&) {
        return videoFiles;
    }

    std::sort(videoFiles.begin(), videoFiles.end(),
        [](const wstring& a, const wstring& b) {
            return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
        });

    return videoFiles;
}

wstring GetConfigValue(const wstring& baseDir, const wchar_t* key, const wchar_t* defaultValue) {
    fs::path iniPath = fs::path(baseDir) / CONFIG_FILE;
    if (!fs::exists(iniPath)) return defaultValue;

    wchar_t staticBuf[1024];
    DWORD len = GetPrivateProfileStringW(
        CONFIG_SECTION, key, defaultValue, 
        staticBuf, 1024, 
        iniPath.wstring().c_str()
    );
    
    if (len < 1023) {
        wstring value(staticBuf, len);
        TrimInPlace(value);
        return value;
    }
    
    DWORD size = 2048;
    while (size <= 32768) {
        std::vector<wchar_t> buffer(size, L'\0');
        len = GetPrivateProfileStringW(
            CONFIG_SECTION, key, defaultValue, 
            buffer.data(), size, 
            iniPath.wstring().c_str()
        );
        
        if (len < size - 1) {
            wstring value(buffer.data(), len);
            TrimInPlace(value);
            return value;
        }
        size *= 2;
    }
    return defaultValue;
}

bool GetConfigBool(const wstring& baseDir, const wchar_t* key, bool defaultValue) {
    wstring value = GetConfigValue(baseDir, key, defaultValue ? L"yes" : L"no");
    ToLowerInPlace(value);
    return value == L"yes" || value == L"true" || value == L"1";
}

wstring GetRegistryPath(const wchar_t* subKey) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return L"";
        }
    }
    RegKeyGuard keyGuard(hKey);

    DWORD bufferSize = 0;
    if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr, nullptr, &bufferSize) != ERROR_SUCCESS) {
        return L"";
    }

    std::vector<wchar_t> buffer((bufferSize / sizeof(wchar_t)) + 1, L'\0');
    if (RegQueryValueExW(hKey, nullptr, nullptr, nullptr, 
        reinterpret_cast<LPBYTE>(buffer.data()), &bufferSize) != ERROR_SUCCESS) {
        return L"";
    }

    wstring cleanPath(buffer.data());
    cleanPath.erase(std::remove(cleanPath.begin(), cleanPath.end(), L'"'), cleanPath.end());
    return cleanPath;
}

bool IsValidExe(const wstring& path) {
    if (path.empty()) return false;
    if (path.find(L'"') != wstring::npos) return false;
    
    if (path == L"mpv.exe" || path == L"mpvnet.exe") return true;
    
    wstring expanded = ExpandEnvStrings(path);
    fs::path p(expanded);
    
    if (!fs::exists(p)) return false;
    wstring ext = p.extension().wstring();
    ToLowerInPlace(ext);
    return ext == L".exe";
}

wstring FindPlayer(const wstring& baseDir) {
    wstring manual = GetConfigValue(baseDir, L"player", L"");
    if (!manual.empty()) {
        wstring expanded = ExpandEnvStrings(manual);
        fs::path p(expanded);
        if (p.is_relative()) {
            p = fs::path(baseDir) / p;
        }
        if (IsValidExe(p.wstring())) {
            return p.wstring();
        }
    }

    const std::array<fs::path, 5> localPaths = {
        fs::path(baseDir) / L"mpvnet.exe",
        fs::path(baseDir) / L"mpv.exe",
        fs::path(baseDir) / L"bin" / L"mpvnet.exe",
        fs::path(baseDir) / L"bin" / L"mpv.exe",
        fs::path(baseDir) / L"mpv" / L"mpv.exe"
    };
    for (const auto& p : localPaths) {
        if (fs::exists(p)) return p.wstring();
    }

    wstring regPath = GetRegistryPath(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\mpvnet.exe");
    if (IsValidExe(regPath)) return regPath;

    regPath = GetRegistryPath(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\mpv.exe");
    if (IsValidExe(regPath)) return regPath;

    for (auto p : SYSTEM_MPV_PATHS) {
        wstring expanded = ExpandEnvStrings(wstring(p));
        if (fs::exists(expanded)) return expanded;
    }

    return L"mpvnet.exe";
}

enum class LaunchMode {
    PLAYLIST,
    EXPLICIT_LIST,
    DIRECTORY
};

bool TryGeneratePlaylist(const wstring& workDir, const std::vector<wstring>& videoFiles, 
                         wstring& outPlaylistPath, bool useUtf8Bom) {
    wchar_t tempFile[MAX_PATH];

    DWORD result = GetTempFileNameW(workDir.c_str(), L"ani", 0, tempFile);
    if (result == 0) {
        wchar_t tempDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tempDir);
        result = GetTempFileNameW(tempDir, L"ani", 0, tempFile);
        if (result == 0) return false;
    }

    fs::path targetPath(tempFile);
    fs::path m3u8Path = targetPath;
    m3u8Path.replace_extension(L".m3u8");
    
    try {
        fs::rename(targetPath, m3u8Path);
    } catch (...) {
        return false;
    }

    std::ofstream playlist(m3u8Path, std::ios::binary | std::ios::trunc);
    if (!playlist.is_open()) return false;

    try {
        if (useUtf8Bom) {
            const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
            playlist.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        }
        
        playlist << "#EXTM3U\n";
        
        for (const auto& file : videoFiles) {
            fs::path fullPath = fs::path(workDir) / file;
            playlist << WStringToUTF8(fullPath.wstring()) << "\n";
        }
        
        playlist.close();
        outPlaylistPath = m3u8Path.wstring();
        return true;
    } catch (...) {
        return false;
    }
}

bool TryBuildExplicitList(const wstring& workDir, const std::vector<wstring>& videoFiles, 
                          const wstring& baseCmd, wstring& outCmd) {
    std::wostringstream cmdStream;
    cmdStream << baseCmd;

    wstring workDirWithSep = workDir;
    if (!workDirWithSep.empty() && workDirWithSep.back() != L'\\' && workDirWithSep.back() != L'/') {
        workDirWithSep += L'\\';
    }

    for (const auto& file : videoFiles) {
        size_t estimatedAdd = 4 + workDirWithSep.size() + file.size();
        if (cmdStream.tellp() + estimatedAdd >= CMD_MAX_LENGTH - CMD_SAFETY_MARGIN) {
            return false;
        }
        
        cmdStream << L" \"" << workDirWithSep << file << L"\"";
    }
    
    outCmd = cmdStream.str();
    return true;
}

wstring BuildDirectoryCommand(const wstring& baseCmd, const wstring& workDir) {
    return baseCmd + L" \"" + workDir + L"\"";
}

static MutexGuard AcquireSingleInstanceLock() {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (!hMutex) return MutexGuard();
    
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return MutexGuard();
    }
    
    return MutexGuard(hMutex, true);
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR pCmdLine, int) {
    MutexGuard instanceLock = AcquireSingleInstanceLock();
    if (!instanceLock) {
        MessageBoxW(nullptr, 
            L"Anime Launcher is already running.\n\n"
            L"Check your taskbar for the mpv window.",
            L"Already Running", MB_ICONINFORMATION);
        return 0;
    }

    wstring workDir = GetExeDirectory();
    if (workDir.empty()) {
        MessageBoxW(nullptr, L"Failed to determine launcher directory.", L"Error", MB_ICONERROR);
        return 1;
    }

    std::vector<wstring> skipPatterns;
    wstring skipConfig = GetConfigValue(workDir, L"skip_patterns", L"");
    if (!skipConfig.empty()) {
        skipPatterns = ParseCommaSeparated(skipConfig);
    } else {
        skipPatterns.assign(DEFAULT_SKIP_PATTERNS.begin(), DEFAULT_SKIP_PATTERNS.end());
    }
    
    bool respectChapters = GetConfigBool(workDir, L"respect_chapters", true);
    bool useUtf8Bom = GetConfigBool(workDir, L"playlist_utf8_bom", true);

    std::vector<wstring> videoFiles = CollectVideoFiles(workDir, skipPatterns);
    if (videoFiles.empty()) {
        MessageBoxW(nullptr, 
            L"No supported video files found.\n\n"
            L"Supported: MKV, MP4, AVI, WebM, MOV, FLV, WMV, M4V, TS, OGM\n"
            L"(Files matching skip patterns are excluded)",
            L"Anime Launcher", MB_ICONEXCLAMATION);
        return 1;
    }

    wstring playerPath = FindPlayer(workDir);
    if (!IsValidExe(playerPath)) {
        MessageBoxW(nullptr, 
            L"Could not find MPV or MPV.net player.\n\n"
            L"Please install MPV.net or MPV, or create launcher.ini with:\n"
            L"[Launcher]\n"
            L"player=C:\\path\\to\\mpvnet.exe",
            L"Player Not Found", MB_ICONERROR);
        return 1;
    }

    wstring customFlags = GetConfigValue(workDir, L"flags", L"");
    if (!customFlags.empty()) {
        SanitizeFlags(customFlags);
        if (customFlags.front() != L' ') {
            customFlags.insert(customFlags.begin(), L' ');
        }
    }

    wstring baseCmd = L"\"" + ExpandEnvStrings(playerPath) + L"\" " + DEFAULT_FLAGS;
    
    if (respectChapters) {
        baseCmd += L" --ordered-chapters";
    }
    
    if (!customFlags.empty()) {
        baseCmd += L" " + customFlags;
    }

    wstring finalCmd;
    wstring playlistPath;
    LaunchMode mode = LaunchMode::DIRECTORY;
    
    if (TryGeneratePlaylist(workDir, videoFiles, playlistPath, useUtf8Bom)) {
        finalCmd = baseCmd + L" \"" + playlistPath + L"\"";
        mode = LaunchMode::PLAYLIST;
    }
    else if (TryBuildExplicitList(workDir, videoFiles, baseCmd, finalCmd)) {
        mode = LaunchMode::EXPLICIT_LIST;
    }
    else {
        finalCmd = BuildDirectoryCommand(baseCmd, workDir);
        mode = LaunchMode::DIRECTORY;
    }

    if (pCmdLine && wcslen(pCmdLine) > 0) {
        finalCmd += L" ";
        finalCmd += pCmdLine;
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    std::vector<wchar_t> cmdBuf(finalCmd.begin(), finalCmd.end());
    cmdBuf.push_back(0);

    BOOL success = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, 
        FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);

    if (!success) {
        DWORD err = GetLastError();
        wstring errMsg = GetErrorMessage(err);
        
        wstring msg = L"Failed to launch player.\n\n"
                      L"Player: " + playerPath + L"\n"
                      L"Error Code: " + std::to_wstring(err) + L"\n"
                      L"Error: " + errMsg + L"\n\n"
                      L"Full Command:\n" + finalCmd;
        MessageBoxW(nullptr, msg.c_str(), L"Launch Error", MB_ICONERROR);
        return 1;
    }

    HandleGuard hProc(pi.hProcess);
    HandleGuard hThread(pi.hThread);
    WaitForSingleObject(hProc.get(), INFINITE);

    if (mode == LaunchMode::PLAYLIST && !playlistPath.empty()) {
        constexpr int MAX_RETRIES = 3;
        DWORD delay = 100;
        
        for (int retry = 0; retry < MAX_RETRIES; ++retry) {
            try {
                fs::remove(playlistPath);
                break;
            } catch (...) {
                if (retry < MAX_RETRIES - 1) {
                    Sleep(delay);
                    delay *= 2;
                }
            }
        }
    }

    return 0;
}