#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

// Global flag to stop reading
static volatile bool g_stop = false;

BOOL WINAPI ConsoleHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
        g_stop = true;
        return TRUE;
    }
    return FALSE;
}

DWORD FindProcessId(const std::wstring& processName) {
    PROCESSENTRY32W processInfo;
    processInfo.dwSize = sizeof(processInfo);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &processInfo)) {
        do {
            if (processName == processInfo.szExeFile) {
                pid = processInfo.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &processInfo));
    }
    CloseHandle(snapshot);
    return pid;
}

bool IsModuleLoaded(DWORD pid, const std::wstring& moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &me));
    }
    CloseHandle(snapshot);
    return found;
}

bool InjectDll(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::cerr << "[-] Failed to OpenProcess (PID: " << pid << "). Error: " << GetLastError() << std::endl;
        return false;
    }

    size_t sizeInBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, NULL, sizeInBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) {
        std::cerr << "[-] Failed to VirtualAllocEx. Error: " << GetLastError() << std::endl;
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath.c_str(), sizeInBytes, NULL)) {
        std::cerr << "[-] Failed to WriteProcessMemory. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    LPVOID pLoadLibraryW = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryW, pRemoteMem, 0, NULL);
    if (!hThread) {
        std::cerr << "[-] Failed to CreateRemoteThread. Error: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

std::wstring GetFullDllPath(const std::wstring& relativeOrAbsolute) {
    wchar_t fullPath[MAX_PATH];
    if (GetFullPathNameW(relativeOrAbsolute.c_str(), MAX_PATH, fullPath, NULL)) {
        return std::wstring(fullPath);
    }
    return relativeOrAbsolute;
}

int main(int argc, char* argv[]) {
    // Enable ANSI console colors
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    std::cout << "\033[36m"
              << "==================================================\n"
              << "   Lepus Standalone Launcher & NTQQ Injector      \n"
              << "==================================================\033[0m\n";

    // Locate DLLs
    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exePathBuf, MAX_PATH);
    std::wstring exeDir(exePathBuf);
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }

    std::wstring lepusDllPath = exeDir + L"\\lepus_core.dll";
    std::wstring hookDllPath = exeDir + L"\\..\\third_party\\lepus_hook.dll";
    hookDllPath = GetFullDllPath(hookDllPath);

    // Also check if snowluma is next to launcher
    if (GetFileAttributesW(hookDllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        hookDllPath = exeDir + L"\\lepus_hook.dll";
    }

    std::wcout << L"[*] Target Core DLL: " << lepusDllPath << std::endl;
    std::wcout << L"[*] Target Hook Engine: " << hookDllPath << std::endl;

    if (GetFileAttributesW(lepusDllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "\033[31m[-] Error: " << std::string(lepusDllPath.begin(), lepusDllPath.end()) << " not found!\033[0m\n";
        std::cout << "Press Enter to exit...\n";
        std::cin.get();
        return 1;
    }

    std::cout << "[*] Searching for running QQ.exe process..." << std::endl;
    DWORD pid = 0;
    while (!g_stop && (pid = FindProcessId(L"QQ.exe")) == 0) {
        Sleep(1000);
        std::cout << "." << std::flush;
    }
    if (g_stop) return 0;

    std::cout << "\n\033[32m[+] Found QQ.exe (PID: " << pid << ")\033[0m" << std::endl;

    // Step 1: Ensure Lepus hook layer is injected
    if (GetFileAttributesW(hookDllPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!IsModuleLoaded(pid, L"lepus_hook.dll")) {
            std::cout << "[*] Injecting NTQQ Native Hook Layer..." << std::endl;
            if (InjectDll(pid, hookDllPath)) {
                std::cout << "\033[32m[+] NTQQ Native Hook Layer injected successfully!\033[0m" << std::endl;
                Sleep(1000); // Wait for hook to establish pipe
            } else {
                std::cerr << "\033[33m[!] Warning: Failed to inject lepus_hook.dll\033[0m" << std::endl;
            }
        } else {
            std::cout << "[*] NTQQ Native Hook Layer is already loaded in target process." << std::endl;
        }
    }

    // Step 2: Inject Lepus Core DLL
    if (!IsModuleLoaded(pid, L"lepus_core.dll")) {
        std::cout << "[*] Injecting lepus_core.dll into QQ..." << std::endl;
        if (InjectDll(pid, lepusDllPath)) {
            std::cout << "\033[32m[+] lepus_core.dll injected successfully!\033[0m" << std::endl;
        } else {
            std::cerr << "\033[31m[-] Failed to inject lepus_core.dll!\033[0m" << std::endl;
            std::cout << "Press Enter to exit...\n";
            std::cin.get();
            return 1;
        }
    } else {
        std::cout << "[*] lepus_core.dll is already loaded in target process." << std::endl;
    }

    std::cout << "\n\033[32m[+] Lepus is now actively running inside NTQQ!\033[0m\n"
              << "    HTTP REST: http://127.0.0.1:3000\n"
              << "    WebSocket: ws://127.0.0.1:3001\n"
              << "    Check the 'Lepus NTQQ Core [Active Monitor]' console for live packet logs.\n"
              << "    Launcher is keeping alive. Press Ctrl+C to exit launcher.\n";

    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (hProcess) {
        WaitForSingleObject(hProcess, INFINITE);
        CloseHandle(hProcess);
        std::cout << "\n[*] QQ process has terminated.\n";
    }

    return 0;
}
