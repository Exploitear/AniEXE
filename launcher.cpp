#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>

// Libraries needed for linking
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;

// ==========================================
// CONFIGURATION
// ==========================================
const std::wstring CONFIG_FILE = L"launcher.ini";

// Standard MPV flags for "Binge Mode"
// --fs: Start in Fullscreen
// --save-position-on-quit: Remembers where you left off in the playlist/file
// --keep-open=no: Close player when playlist ends (so Launcher closes and Heroic stops tracking time)
const std::wstring DEFAULT_FLAGS = L" --fs --save-position-on-quit --keep-open=no";

// Supported Extensions (Removed .iso as requested)
const std::vector<std::wstring> VIDEO_EXTENSIONS = {
    L".mkv", L".mp4", L".avi", L".webm", L".mov", L".flv", L".wmv", L".m4v", L".ts"
};

// ==========================================
// UTILS
// ==========================================

// Get the directory where this .exe is currently located
// This is safer than GetCurrentDirectory() for Launchers
std::wstring GetExeDirectory() {
    WCHAR path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) {
        return L"";
    }
    fs::path p(path);
    return p.parent_path().wstring();
}

// Check if a file string ends with a specific extension
bool HasExtension(const std::wstring& filename, const std::wstring& ext) {
    if (filename.length() >= ext.length()) {
        return (0 == filename.compare(filename.length() - ext.length(), ext.length(), ext));
    }
    return false;
}

// ==========================================
// PLAYER DETECTION
// ==========================================

// Helper to check registry keys
std::wstring GetRegistryPath(const std::wstring& subKey) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR path[MAX_PATH];
        DWORD bufferSize = sizeof(path);
        if (RegQueryValueExW(hKey, NULL, NULL, NULL, (LPBYTE)path, &bufferSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            std::wstring cleanPath = path;
            // Strip quotes if registry returned them
            cleanPath.erase(std::remove(cleanPath.begin(), cleanPath.end(), L'"'), cleanPath.end());
            return cleanPath;
        }
        RegCloseKey(hKey);
    }
    return L"";
}

std::wstring FindPlayer(const std::wstring& baseDir) {
    // 1. Check for Manual Override in launcher.ini
    // Format: Just the full path to the exe, e.g., C:\Soft\mpv.exe
    fs::path iniPath = fs::path(baseDir) / CONFIG_FILE;
    if (fs::exists(iniPath)) {
        std::wifstream file(iniPath);
        std::wstring line;
        if (std::getline(file, line) && !line.empty()) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(L" \t\r\n"));
            line.erase(line.find_last_not_of(L" \t\r\n") + 1);
            if (fs::exists(line)) return line;
        }
    }

    // 2. Portable Checks (Inside the anime folder or a /bin subfolder)
    std::vector<fs::path> localPaths = {
        fs::path(baseDir) / L"mpv.net.exe",
        fs::path(baseDir) / L"mpv.exe",
        fs::path(baseDir) / L"bin" / L"mpv.exe",
        fs::path(baseDir) / L"mpv" / L"mpv.exe"
    };

    for (const auto& p : localPaths) {
        if (fs::exists(p)) return p.wstring();
    }

    // 3. Registry / Install Checks
    std::wstring regMpvNet = GetRegistryPath(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\mpvnet.exe");
    if (!regMpvNet.empty() && fs::exists(regMpvNet)) return regMpvNet;

    std::wstring regMpv = GetRegistryPath(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\mpv.exe");
    if (!regMpv.empty() && fs::exists(regMpv)) return regMpv;

    // 4. Common System Paths
    std::vector<std::wstring> sysPaths = {
        L"C:\\Program Files\\mpv.net\\mpvnet.exe",
        L"C:\\Program Files\\mpv\\mpv.exe",
        L"C:\\Program Files (x86)\\mpv.net\\mpvnet.exe"
    };

    for (const auto& p : sysPaths) {
        if (fs::exists(p)) return p;
    }

    // 5. Fallback to PATH (let Windows find it)
    return L"mpv.exe";
}

// ==========================================
// MAIN
// ==========================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    
    // 1. Determine Working Directory (The folder containing this .exe)
    std::wstring workDir = GetExeDirectory();
    if (workDir.empty()) return 1;

    // 2. Scan for Videos (Validation)
    // We check if there are actual video files here before launching player
    bool hasVideo = false;
    try {
        for (const auto& entry : fs::directory_iterator(workDir)) {
            if (entry.is_regular_file()) {
                std::wstring ext = entry.path().extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                
                for (const auto& validExt : VIDEO_EXTENSIONS) {
                    if (ext == validExt) {
                        hasVideo = true;
                        break;
                    }
                }
            }
            if (hasVideo) break;
        }
    } catch (...) {}

    if (!hasVideo) {
        MessageBoxW(NULL, L"No supported video files found in this folder.\n(ISO not supported).", L"Anime Launcher", MB_ICONEXCLAMATION);
        return 1;
    }

    // 3. Locate Player
    std::wstring playerPath = FindPlayer(workDir);

    // 4. Construct Command
    // Logic: "path\to\mpv.exe" "path\to\anime_folder" --flags
    // Passing the folder path tells MPV to load it as a playlist
    std::wstring cmd = L"\"" + playerPath + L"\" \"" + workDir + L"\"" + DEFAULT_FLAGS;

    // Append any user arguments passed to this launcher (optional passthrough)
    if (wcslen(pCmdLine) > 0) {
        cmd += L" ";
        cmd += pCmdLine;
    }

    // 5. Create Process
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    
    // Create a mutable buffer for CreateProcessW as it requires LPWSTR (read/write)
    std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
    cmdBuffer.push_back(0); // Null terminator

    BOOL success = CreateProcessW(
        NULL,               // Application Name
        cmdBuffer.data(),   // Command Line
        NULL, NULL, FALSE, 0, NULL, 
        workDir.c_str(),    // Working Directory
        &si, &pi
    );

    if (!success) {
        std::wstring msg = L"Failed to launch player.\nPath: " + playerPath;
        MessageBoxW(NULL, msg.c_str(), L"Error", MB_ICONERROR);
        return 1;
    }

    // 6. THE HOOK: Wait for MPV to close
    // This ensures Heroic/Steam counts this as "Play Time"
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Cleanup
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}