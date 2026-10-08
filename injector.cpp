#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>

static DWORD FindProcess(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = {sizeof(pe)};
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int main(int argc, char* argv[])
{
    const char* dllName = "DishonoredESP.dll";
    if (argc > 1) dllName = argv[1];

    printf("[*] Dishonored ESP DLL Injector\n");
    printf("[*] Looking for Dishonored.exe...\n");

    DWORD pid = 0;
    for (int i = 0; i < 60 && !pid; i++) {
        pid = FindProcess("Dishonored.exe");
        if (!pid) { Sleep(1000); printf("."); }
    }
    if (!pid) { printf("\n[!] Dishonored.exe not found\n"); return 1; }
    printf("\n[+] Found PID: %lu\n", pid);

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) { printf("[!] OpenProcess failed (%lu)\n", GetLastError()); return 1; }

    char fullPath[MAX_PATH];
    GetFullPathNameA(dllName, MAX_PATH, fullPath, nullptr);
    DWORD attr = GetFileAttributesA(fullPath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("[!] DLL not found: %s\n", fullPath);
        CloseHandle(hProc);
        return 1;
    }
    printf("[*] Injecting: %s\n", fullPath);

    size_t pathLen = strlen(fullPath) + 1;
    LPVOID remBuf = VirtualAllocEx(hProc, nullptr, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remBuf) { printf("[!] VirtualAllocEx failed\n"); CloseHandle(hProc); return 1; }

    if (!WriteProcessMemory(hProc, remBuf, fullPath, pathLen, nullptr)) {
        printf("[!] WriteProcessMemory failed\n");
        VirtualFreeEx(hProc, remBuf, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    FARPROC loadLib = GetProcAddress(k32, "LoadLibraryA");

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
        (LPTHREAD_START_ROUTINE)loadLib, remBuf, 0, nullptr);
    if (!hThread) {
        printf("[!] CreateRemoteThread failed (%lu)\n", GetLastError());
        VirtualFreeEx(hProc, remBuf, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return 1;
    }

    printf("[+] Injected! Waiting for thread...\n");
    WaitForSingleObject(hThread, 5000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    if (exitCode == 0) {
        printf("[!] LoadLibrary returned NULL - injection may have failed\n");
    } else {
        printf("[+] DLL loaded at 0x%08lX\n", exitCode);
    }

    CloseHandle(hThread);
    VirtualFreeEx(hProc, remBuf, 0, MEM_RELEASE);
    CloseHandle(hProc);
    printf("[+] Done! Press END in-game to eject.\n");
    return 0;
}
