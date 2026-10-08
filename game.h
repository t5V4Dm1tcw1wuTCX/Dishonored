#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace Game {

struct Vec3 {
    float X, Y, Z;
    Vec3() : X(0), Y(0), Z(0) {}
    Vec3(float x, float y, float z) : X(x), Y(y), Z(z) {}
    Vec3 operator-(const Vec3& o) const { return {X-o.X, Y-o.Y, Z-o.Z}; }
    Vec3 operator+(const Vec3& o) const { return {X+o.X, Y+o.Y, Z+o.Z}; }
    float Length() const { return sqrtf(X*X + Y*Y + Z*Z); }
};

struct Rotator { int Pitch, Yaw, Roll; };
struct Matrix4 { float M[4][4]; };

struct BoneAtom {
    float Rotation[4];
    Vec3 Translation;
    float Scale;
};

namespace Off {
    constexpr uintptr_t GNames    = 0x01435674;
    constexpr uintptr_t GObjects  = 0x01423630;
    constexpr uintptr_t GEngine   = 0x0145f614;
    constexpr uintptr_t LocalPawn = 0x0145f628;

    constexpr int ActorLocation  = 0xC4;
    constexpr int ActorCachedL2W = 0x50;
    constexpr int PawnMesh       = 0x3DC;
    constexpr int MeshSpaceBases = 0x208;
    constexpr int MeshL2W        = 0x60;
    constexpr int MeshSkelMesh   = 0x1D4;
    constexpr int SkelRefSkel    = 0xEC;
}

static constexpr int FMESHBONE_SIZE = 80;

// 0=unaware, 1=aware, 2=surprised, 3=suspicious, 4=fearful, 5=combat, 6=begging, 7=choked
enum EAIState : uint8_t {
    AI_Unaware=0, AI_Aware=1, AI_Surprised=2, AI_Suspicious=3,
    AI_Fearful=4, AI_Combat=5, AI_Begging=6, AI_Choked=7, AI_Unknown=255
};

struct ActorInfo {
    uintptr_t Address;
    Vec3 Location;
    std::string Name;
    int relation; // 0=ally, 1=neutral, 2=enemy
    bool bIsVisible;
    float Distance;
    std::vector<Vec3> Bones;
    std::vector<std::pair<int,int>> BoneConnections;
    int headBoneIdx = -1;
    int headEndBoneIdx = -1;
    float healthPct = 1.0f;
    EAIState aiState = AI_Unknown;
};

struct ItemInfo {
    uintptr_t Address;
    Vec3 Location;
    Vec3 BoxExtent; // half-extents from mesh bounds
    std::string Name;
    std::string ClassName;
    float Distance;
};

struct InteractInfo {
    uintptr_t Address;
    Vec3 Location;
    Vec3 BoxExtent;
    std::string Name;
    float Distance;
};

inline bool WorldToScreen(const Vec3& w, const Matrix4& vpm,
                          float scrW, float scrH, Vec3& out)
{
    float wp = w.X*vpm.M[0][3] + w.Y*vpm.M[1][3] + w.Z*vpm.M[2][3] + vpm.M[3][3];
    if (wp < 0.001f) return false;
    float x = w.X*vpm.M[0][0] + w.Y*vpm.M[1][0] + w.Z*vpm.M[2][0] + vpm.M[3][0];
    float y = w.X*vpm.M[0][1] + w.Y*vpm.M[1][1] + w.Z*vpm.M[2][1] + vpm.M[3][1];
    out.X = (scrW*0.5f) + (scrW*0.5f) * (x/wp);
    out.Y = (scrH*0.5f) - (scrH*0.5f) * (y/wp);
    out.Z = wp;
    return true;
}

inline void LogToFile(const char* fmt, ...) {
    FILE* f = fopen("esp_debug.log", "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fflush(f);
    fclose(f);
}

// ===== PROCESSEVENT HOOK VIA MINHOOK FOR THREAD-SAFE LINE TRACES =====
// MinHook detours ProcessEvent at 0x00567A20. Our hook runs on the game
// thread, drains pending trace requests, then calls the original PE.
#include "minhook/include/MinHook.h"

namespace PEHook {

static constexpr int MAX_TRACED_NPCS  = 128;
static constexpr int TRACE_QUEUE_SIZE = 128;
static constexpr int TRACES_PER_TICK  = 8;
static constexpr DWORD TRACE_REFRESH_MS = 150;

struct TraceRequest {
    uintptr_t actorAddr;
    Vec3      traceStart;
    Vec3      traceEnd;
    int       npcIndex;
};

struct TraceResult {
    volatile long visible;
    volatile long hasResult;
    DWORD         lastUpdateTick;
};

// ---- global state ----
static volatile long g_hookInstalled   = 0;
static uintptr_t     g_traceFunc       = 0;
static volatile long g_tracesExecuted  = 0;
static volatile long g_processingTraces = 0;
static volatile long g_hookCallCount   = 0;
static volatile long g_traceLogCounter = 0;

// ProcessEvent is __thiscall: this in ECX, func+params on stack, callee-cleanup.
// MinGW can't do __thiscall on free functions. Use __fastcall trick:
// __fastcall puts first 2 args in ECX, EDX — maps to __thiscall (ECX=this, EDX=unused).
typedef void (__fastcall *ProcessEventFn)(void* thisPtr, void* edx, void* func, void* params, void* result);
static ProcessEventFn g_originalPE = nullptr;

// SPSC ring buffer
static TraceRequest  g_traceQueue[TRACE_QUEUE_SIZE];
static volatile long g_queueHead = 0;
static volatile long g_queueTail = 0;

static TraceResult   g_traceResults[MAX_TRACED_NPCS];

// ---- overlay-thread API ----

inline bool QueueTrace(uintptr_t actor, const Vec3& start, const Vec3& end, int npcIdx) {
    if (npcIdx < 0 || npcIdx >= MAX_TRACED_NPCS) return false;
    long head = g_queueHead;
    long next = (head + 1) % TRACE_QUEUE_SIZE;
    if (next == g_queueTail) return false;
    g_traceQueue[head].actorAddr  = actor;
    g_traceQueue[head].traceStart = start;
    g_traceQueue[head].traceEnd   = end;
    g_traceQueue[head].npcIndex   = npcIdx;
    MemoryBarrier();
    InterlockedExchange(&g_queueHead, next);
    return true;
}

inline bool NeedsRefresh(int npcIdx) {
    if (npcIdx < 0 || npcIdx >= MAX_TRACED_NPCS) return true;
    if (!g_traceResults[npcIdx].hasResult) return true;
    return (GetTickCount() - g_traceResults[npcIdx].lastUpdateTick) > TRACE_REFRESH_MS;
}

inline bool GetTraceResult(int npcIdx, bool& outVisible) {
    if (npcIdx < 0 || npcIdx >= MAX_TRACED_NPCS) return false;
    if (!g_traceResults[npcIdx].hasResult) return false;
    outVisible = (g_traceResults[npcIdx].visible != 0);
    return true;
}

// ---- Trace params struct (AActor::Trace, iNative=26650) ----
struct TraceParams {
    Vec3     HitLocation;     // 0x00
    Vec3     HitNormal;       // 0x0C
    Vec3     TraceEnd;        // 0x18
    Vec3     TraceStart;      // 0x24
    uint32_t bTraceActors;    // 0x30
    Vec3     Extent;          // 0x34
    uint8_t  HitInfo[0x1C];   // 0x40
    int32_t  ExtraTraceFlags; // 0x5C
    uint32_t ReturnValue;     // 0x60 (AActor pointer)
};

// ---- command queue for non-game-thread PE calls ----
static DWORD g_gameThreadId = 0;
static CRITICAL_SECTION g_cmdCS;
static bool g_cmdCSInit = false;
static std::vector<std::function<void()>>* g_pendingCmds = nullptr;

inline bool IsGameThread() {
    return g_gameThreadId != 0 && GetCurrentThreadId() == g_gameThreadId;
}

inline void InitCommandQueue() {
    if (!g_cmdCSInit) {
        InitializeCriticalSection(&g_cmdCS);
        g_pendingCmds = new std::vector<std::function<void()>>();
        g_cmdCSInit = true;
    }
}

inline void QueueCommand(std::function<void()> fn) {
    InitCommandQueue();
    EnterCriticalSection(&g_cmdCS);
    g_pendingCmds->push_back(std::move(fn));
    LeaveCriticalSection(&g_cmdCS);
}

static void DrainCommands() {
    if (!g_cmdCSInit || !g_pendingCmds || g_pendingCmds->empty()) return;
    std::vector<std::function<void()>> cmds;
    EnterCriticalSection(&g_cmdCS);
    cmds.swap(*g_pendingCmds);
    LeaveCriticalSection(&g_cmdCS);
    for (auto& c : cmds) {
        c();
    }
}

// ---- game-thread trace execution ----

static void __attribute__((noinline)) ProcessPendingTraces() {
    if (!g_traceFunc || !g_originalPE) return;
    if (InterlockedCompareExchange(&g_processingTraces, 1, 0) != 0) return;

    if (g_queueHead == g_queueTail) {
        InterlockedExchange(&g_processingTraces, 0);
        return;
    }

    int processed = 0;
    while (processed < TRACES_PER_TICK) {
        long tail = g_queueTail;
        if (tail == g_queueHead) break;
        MemoryBarrier();

        auto& req = g_traceQueue[tail];
        uintptr_t actor = req.actorAddr;

        if (actor > 0x10000) {
            uint64_t oflags = *(uint64_t*)(actor + 0x08);
            if (!(oflags & 0x20000000)) {
                int idx = req.npcIndex;
                long traceNum = g_tracesExecuted;

                // Direct call to UWorld::SingleLineCheck at 0x0064E7A0
                // ECX = GWorld from [0x01449888], 7 stack params
                // Returns TRUE = clear path (visible), FALSE = blocked
                typedef int (__fastcall *SingleLineCheckFn)(
                    void* world, void* edx,
                    void* checkResult,
                    void* sourceActor,
                    const Vec3* traceEnd,
                    const Vec3* traceStart,
                    uint32_t traceFlags,
                    const Vec3* extent,
                    uint32_t extraFlags
                );
                auto slcFn = (SingleLineCheckFn)0x0064E7A0;
                void* world = *(void**)0x01449888;

                uint8_t checkResult[0x50] = {};
                Vec3 extent = {0.0f, 0.0f, 0.0f};

                bool vis = true;
                if (world) {
                    int clear = slcFn(world, nullptr, checkResult, (void*)actor,
                                      &req.traceEnd, &req.traceStart, 0x05, &extent, 0);
                    vis = (clear != 0);
                }

                if (idx >= 0 && idx < MAX_TRACED_NPCS) {
                    g_traceResults[idx].visible        = vis ? 1 : 0;
                    g_traceResults[idx].lastUpdateTick  = GetTickCount();
                    InterlockedExchange(&g_traceResults[idx].hasResult, 1);
                }
                InterlockedIncrement(&g_tracesExecuted);

                long cnt = InterlockedIncrement(&g_traceLogCounter);
                if (cnt <= 5 || (cnt % 200) == 0) {
                    LogToFile("Trace[%ld]: actor=0x%08X idx=%d hit=0x%08X vis=%d start=(%.0f,%.0f,%.0f) end=(%.0f,%.0f,%.0f)",
                              cnt, (uint32_t)actor, idx, (uint32_t)0, vis?1:0,
                              req.traceStart.X, req.traceStart.Y, req.traceStart.Z,
                              req.traceEnd.X, req.traceEnd.Y, req.traceEnd.Z);
                }
            }
        }

        InterlockedExchange(&g_queueTail, (tail + 1) % TRACE_QUEUE_SIZE);
        processed++;
    }

    InterlockedExchange(&g_processingTraces, 0);
}

// ---- MinHook detour (called as __thiscall by the game) ----

static void __fastcall HookedProcessEvent(void* thisPtr, void* edx, void* func, void* params, void* result) {
    (void)edx;
    if (g_gameThreadId == 0) g_gameThreadId = GetCurrentThreadId();
    g_originalPE(thisPtr, nullptr, func, params, result);
    // Drain trace queue
    long cc = InterlockedIncrement(&g_hookCallCount);
    if (cc == 1 || cc == 1000 || cc == 10000) {
        LogToFile("PEHook: call #%ld this=0x%08X qH=%ld qT=%ld",
                  cc, (uint32_t)thisPtr, g_queueHead, g_queueTail);
    }
    ProcessPendingTraces();
    // Drain queued commands from overlay thread
    DrainCommands();
}

// ---- find "Trace" UFunction (direct memory read) ----

static uintptr_t FindTraceFuncDirect(uintptr_t actor) {
    if (!actor || actor < 0x10000) return 0;
    uintptr_t cls = *(uintptr_t*)(actor + 0x30);
    if (!cls || cls < 0x10000) return 0;
    uintptr_t namesArr = *(uintptr_t*)(Off::GNames);
    if (!namesArr) return 0;
    int namesCount = *(int*)(Off::GNames + 4);

    uintptr_t curClass = cls;
    for (int depth = 0; curClass && curClass > 0x10000 && depth < 30; depth++) {
        uintptr_t field = *(uintptr_t*)(curClass + 0x48);
        for (int fc = 0; field && field > 0x10000 && fc < 500; fc++) {
            int32_t nameIdx = *(int32_t*)(field + 0x28);
            if (nameIdx >= 0 && nameIdx < namesCount) {
                uintptr_t entry = *(uintptr_t*)(namesArr + nameIdx * 4);
                if (entry) {
                    const char* name = (const char*)(entry + 0x10);
                    if (name && strcmp(name, "Trace") == 0) return field;
                }
            }
            field = *(uintptr_t*)(field + 0x38);
        }
        curClass = *(uintptr_t*)(curClass + 0x44);
    }
    return 0;
}

// ---- lifecycle ----

static bool g_mhInitialized = false;

static bool Install() {
    if (g_hookInstalled) return true;

    uintptr_t pawn = *(uintptr_t*)(Off::LocalPawn);
    if (!pawn || pawn < 0x10000) return false;
    uintptr_t vtable = *(uintptr_t*)pawn;
    if (!vtable || vtable < 0x400000) return false;
    uintptr_t peAddr = *(uintptr_t*)(vtable + 59 * 4);
    if (!peAddr || peAddr < 0x10000) return false;

    if (!g_traceFunc) {
        g_traceFunc = FindTraceFuncDirect(pawn);
        if (!g_traceFunc) {
            LogToFile("PEHook::Install: Trace func not found");
            return false;
        }
        LogToFile("PEHook::Install: Trace=0x%08X iNative=%d",
                  (uint32_t)g_traceFunc, *(int32_t*)(g_traceFunc + 0x88));
    }

    InitCommandQueue();

    if (!g_mhInitialized) {
        if (MH_Initialize() != MH_OK) {
            LogToFile("PEHook::Install: MH_Initialize failed");
            return false;
        }
        g_mhInitialized = true;
        LogToFile("PEHook::Install: MH_Initialize OK");
    }

    MH_STATUS st = MH_CreateHook(
        (LPVOID)peAddr,
        (LPVOID)&HookedProcessEvent,
        (LPVOID*)&g_originalPE
    );
    if (st != MH_OK) {
        LogToFile("PEHook::Install: MH_CreateHook failed (%d)", (int)st);
        return false;
    }

    st = MH_EnableHook((LPVOID)peAddr);
    if (st != MH_OK) {
        LogToFile("PEHook::Install: MH_EnableHook failed (%d)", (int)st);
        return false;
    }

    memset((void*)g_traceResults, 0, sizeof(g_traceResults));
    g_queueHead = 0;
    g_queueTail = 0;
    g_tracesExecuted = 0;
    g_hookCallCount = 0;
    g_traceLogCounter = 0;

    InterlockedExchange(&g_hookInstalled, 1);
    LogToFile("PEHook::Install: OK PE=0x%08X trampoline=0x%08X",
              (uint32_t)peAddr, (uint32_t)g_originalPE);
    return true;
}

static void Uninstall() {
    if (!g_hookInstalled) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
    InterlockedExchange(&g_hookInstalled, 0);
    g_originalPE = nullptr;
    LogToFile("PEHook::Uninstall: done (traces executed: %ld)", g_tracesExecuted);
}

static bool Validate() {
    if (!g_hookInstalled) return false;
    uintptr_t pawn = *(uintptr_t*)(Off::LocalPawn);
    if (!pawn || pawn < 0x10000) { Uninstall(); return false; }
    return true;
}

static void ResetResults() {
    for (int i = 0; i < MAX_TRACED_NPCS; i++)
        InterlockedExchange(&g_traceResults[i].hasResult, 0);
    g_queueHead = 0;
    g_queueTail = 0;
}

} // namespace PEHook

class GameReader {
public:
    HANDLE hProcess = nullptr;
    DWORD processId = 0;
    HWND gameWindow = nullptr;
    float cachedFOV = 80.0f;
    int parentIndexOffset = -1;
    DWORD lastPawnScan = 0;
    std::string probeLog;
    std::vector<int> pawnIndices;
    std::unordered_map<int, std::string> nameCache;

    bool Attach() {
        gameWindow = FindWindowA(nullptr, "Dishonored");
        if (!gameWindow) return false;

        GetWindowThreadProcessId(gameWindow, &processId);
        if (!processId) return false;

        hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, processId);
        return hProcess != nullptr;
    }

    bool AttachInternal() {
        hProcess = GetCurrentProcess();
        processId = GetCurrentProcessId();
        gameWindow = FindWindowA(nullptr, "Dishonored");
        return gameWindow != nullptr;
    }

    void Detach() {
        if (hProcess) { CloseHandle(hProcess); hProcess = nullptr; }
        processId = 0;
        gameWindow = nullptr;
    }

    bool IsValid() {
        if (!hProcess || !processId) return false;
        DWORD code = 0;
        if (!GetExitCodeProcess(hProcess, &code)) return false;
        return code == STILL_ACTIVE;
    }

    RECT GetWindowRect_() {
        RECT r = {};
        if (!gameWindow) return r;
        RECT client = {};
        GetClientRect(gameWindow, &client);
        POINT topLeft = {0, 0};
        ClientToScreen(gameWindow, &topLeft);
        r.left = topLeft.x;
        r.top = topLeft.y;
        r.right = topLeft.x + client.right;
        r.bottom = topLeft.y + client.bottom;
        return r;
    }

    template<typename T>
    bool RPM(uintptr_t addr, T& out) {
        return ReadProcessMemory(hProcess, (LPCVOID)addr, &out, sizeof(T), nullptr);
    }

    bool RPMBytes(uintptr_t addr, void* buf, size_t size) {
        return ReadProcessMemory(hProcess, (LPCVOID)addr, buf, size, nullptr);
    }

    template<typename T>
    bool WPM(uintptr_t addr, const T& val) {
        return WriteProcessMemory(hProcess, (LPVOID)addr, &val, sizeof(T), nullptr);
    }

    bool WPMBytes(uintptr_t addr, const void* buf, size_t size) {
        return WriteProcessMemory(hProcess, (LPVOID)addr, buf, size, nullptr);
    }

    const char* GetNameByIndex(int32_t index) {
        static char nameBuf[256];
        auto it = nameCache.find(index);
        if (it != nameCache.end()) return it->second.c_str();

        uintptr_t dataPtr = 0; int count = 0;
        if (!RPM(Off::GNames, dataPtr) || !RPM(Off::GNames + 4, count)) return "??";
        if (index < 0 || index >= count) return "??";

        uintptr_t entryPtr = 0;
        if (!RPM(dataPtr + index * 4, entryPtr) || !entryPtr) return "??";
        if (!RPMBytes(entryPtr + 0x10, nameBuf, 255)) return "??";
        nameBuf[255] = 0;

        nameCache[index] = nameBuf;
        return nameCache[index].c_str();
    }

    std::string camDebug;

    bool GetCameraData(Vec3& outPos, Rotator& outRot) {
        char dbg[512];

        // Path: LocalPawn → Controller (PC) → PlayerCamera → ViewTarget.POV
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) {
            camDebug = "no local pawn";
            return false;
        }

        uintptr_t pc = 0;
        if (!RPM(pawn + 0x026C, pc) || !pc) {
            camDebug = "no controller";
            return false;
        }

        uintptr_t camera = 0;
        RPM(pc + 0x0384, camera);

        if (camera && camera > 0x10000) {
            // ACamera::CameraCache.POV (final rendered position)
            // CameraCache at 0x032C, TimeStamp +0x00, POV at +0x04
            if (RPMBytes(camera + 0x0330, &outPos, 12) &&
                !std::isnan(outPos.X) && fabsf(outPos.X) < 1e8f &&
                !(outPos.X == 0.0f && outPos.Y == 0.0f && outPos.Z == 0.0f))
            {
                RPMBytes(camera + 0x033C, &outRot, 12);
                float fov = 0;
                if (RPM(camera + 0x0348, fov) && fov > 10.0f && fov < 170.0f)
                    cachedFOV = fov;
                sprintf(dbg, "cam CC pos=(%.0f,%.0f,%.0f) rot=(%d,%d,%d) fov=%.0f",
                        outPos.X, outPos.Y, outPos.Z,
                        outRot.Pitch, outRot.Yaw, outRot.Roll, cachedFOV);
                camDebug = dbg;
                return true;
            }

            // Fallback: ACamera::ViewTarget.POV
            if (RPMBytes(camera + 0x0374, &outPos, 12) &&
                !std::isnan(outPos.X) && fabsf(outPos.X) < 1e8f &&
                !(outPos.X == 0.0f && outPos.Y == 0.0f && outPos.Z == 0.0f))
            {
                RPMBytes(camera + 0x0380, &outRot, 12);
                float fov = 0;
                if (RPM(camera + 0x038C, fov) && fov > 10.0f && fov < 170.0f)
                    cachedFOV = fov;
                sprintf(dbg, "cam VT pos=(%.0f,%.0f,%.0f) fov=%.0f",
                        outPos.X, outPos.Y, outPos.Z, cachedFOV);
                camDebug = dbg;
                return true;
            }

            sprintf(dbg, "cam ptr=0x%08X but POV invalid", (uint32_t)camera);
            camDebug = dbg;
        }

        // Fallback: PC.Rotation + Pawn.Location + EyeHeight
        RPMBytes(pawn + 0x00C4, &outPos, 12);
        float eyeH = 0;
        RPM(pawn + 0x032C, eyeH);
        if (eyeH < 1.0f || eyeH > 200.0f) eyeH = 64.0f;
        outPos.Z += eyeH;
        RPMBytes(pc + 0x00D0, &outRot, 12);

        float fov = 80.0f;
        cachedFOV = fov;

        sprintf(dbg, "fallback pc rot pos=(%.0f,%.0f,%.0f) rot=(%d,%d,%d)",
                outPos.X, outPos.Y, outPos.Z, outRot.Pitch, outRot.Yaw, outRot.Roll);
        camDebug = dbg;
        return !(outPos.X == 0.0f && outPos.Y == 0.0f && outPos.Z == 0.0f);
    }

    void BuildVPM(const Vec3& camPos, const Rotator& camRot,
                  float scrW, float scrH, Matrix4& outVPM)
    {
        const float PI = 3.14159265358979f;
        float pitch = (float)camRot.Pitch * (PI / 32768.0f);
        float yaw   = (float)camRot.Yaw   * (PI / 32768.0f);
        float roll  = (float)camRot.Roll   * (PI / 32768.0f);

        float cp = cosf(pitch), sp = sinf(pitch);
        float cy = cosf(yaw),   sy = sinf(yaw);
        float cr = cosf(roll),  sr = sinf(roll);

        Vec3 fwd, right, up;
        fwd.X   = cp * cy;   fwd.Y   = cp * sy;   fwd.Z   = sp;
        right.X = sr*sp*cy - cr*sy; right.Y = sr*sp*sy + cr*cy; right.Z = -sr*cp;
        up.X    = -(cr*sp*cy + sr*sy); up.Y = cy*sr - cr*sp*sy; up.Z = cr*cp;

        float view[4][4] = {};
        view[0][0] = right.X; view[0][1] = up.X; view[0][2] = fwd.X;
        view[1][0] = right.Y; view[1][1] = up.Y; view[1][2] = fwd.Y;
        view[2][0] = right.Z; view[2][1] = up.Z; view[2][2] = fwd.Z;
        view[3][0] = -(right.X*camPos.X + right.Y*camPos.Y + right.Z*camPos.Z);
        view[3][1] = -(up.X*camPos.X + up.Y*camPos.Y + up.Z*camPos.Z);
        view[3][2] = -(fwd.X*camPos.X + fwd.Y*camPos.Y + fwd.Z*camPos.Z);
        view[3][3] = 1.0f;

        float fovRad = cachedFOV * (PI / 180.0f);
        float aspect = scrW / scrH;
        float tanHalf = tanf(fovRad * 0.5f);

        float proj[4][4] = {};
        proj[0][0] = 1.0f / tanHalf;
        proj[1][1] = aspect / tanHalf;
        proj[2][2] = 100000.0f / 99999.0f;
        proj[2][3] = 1.0f;
        proj[3][2] = -(1.0f * 100000.0f) / 99999.0f;

        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                outVPM.M[i][j] = 0;
                for (int k = 0; k < 4; k++)
                    outVPM.M[i][j] += view[i][k] * proj[k][j];
            }
    }

    std::vector<uintptr_t> pawnAddresses;
    std::vector<uintptr_t> itemAddresses;
    std::vector<std::string> itemClassNames;
    std::vector<uintptr_t> interactAddresses;
    std::vector<std::string> interactClassNames;
    std::vector<uintptr_t> doorAddresses;

    void ScanLevelActors(uintptr_t level, uintptr_t localPawn) {
        uintptr_t actorsData = 0; int actorsCount = 0;
        if (!RPM(level + 0x0038, actorsData) || !actorsData) return;
        if (!RPM(level + 0x003C, actorsCount)) return;
        if (actorsCount <= 0 || actorsCount > 10000) return;

        constexpr int CHUNK = 512;
        uintptr_t chunk[CHUNK];

        for (int base = 0; base < actorsCount; base += CHUNK) {
            int n = (actorsCount - base < CHUNK) ? (actorsCount - base) : CHUNK;
            if (!RPMBytes(actorsData + base * 4, chunk, n * 4)) continue;

            for (int j = 0; j < n; j++) {
                uintptr_t obj = chunk[j];
                if (!obj || obj < 0x10000 || obj == localPawn) continue;

                uintptr_t classPtr = 0;
                if (!RPM(obj + 0x30, classPtr) || !classPtr || classPtr < 0x10000) continue;

                int32_t classNameIdx = 0;
                if (!RPM(classPtr + 0x28, classNameIdx)) continue;

                const char* cls = GetNameByIndex(classNameIdx);
                if (!cls) continue;

                int32_t objNameIdx = 0;
                if (!RPM(obj + 0x28, objNameIdx)) continue;
                const char* objName = GetNameByIndex(objNameIdx);
                if (objName && strstr(objName, "Default__")) continue;

                if (strstr(cls, "Pawn") && !strstr(cls, "Factory") && !strstr(cls, "Tweaks")) {
                    bool dup = false;
                    for (auto a : pawnAddresses) if (a == obj) { dup = true; break; }
                    if (!dup) pawnAddresses.push_back(obj);
                }
                else if (strstr(cls, "Pickup") || strstr(cls, "Rune") || strstr(cls, "BoneCharm") ||
                         strstr(cls, "Elixir") || strstr(cls, "Key_")) {
                    itemAddresses.push_back(obj);
                    itemClassNames.push_back(cls);
                }
                if (strstr(cls, "DisDoor")) {
                    bool dup = false;
                    for (auto a : doorAddresses) if (a == obj) { dup = true; break; }
                    if (!dup) doorAddresses.push_back(obj);
                }
                if (((strstr(cls, "UsableObject") || strstr(cls, "WhaleOilReceptacle") ||
                    strstr(cls, "WhaleOilBattery") || strstr(cls, "AlarmBell") ||
                    strstr(cls, "WaterSource") || strstr(cls, "Tripwire") ||
                    strstr(cls, "SpringRazor") || strstr(cls, "ArcMine")) &&
                    !strstr(cls, "DisDoor") &&
                    !(objName && (strstr(objName, "TrashBin") || strstr(objName, "sink") || strstr(objName, "Sink"))))) {
                    interactAddresses.push_back(obj);
                    interactClassNames.push_back(cls);
                }
            }
        }
    }

    void ScanActors() {
        pawnAddresses.clear();
        itemAddresses.clear();
        itemClassNames.clear();
        interactAddresses.clear();
        interactClassNames.clear();
        doorAddresses.clear();

        uintptr_t localPawn = 0;
        RPM(Off::LocalPawn, localPawn);
        if (!localPawn) return;

        // Scan persistent level (player's level)
        uintptr_t level = 0;
        if (RPM(localPawn + 0x0024, level) && level)
            ScanLevelActors(level, localPawn);

        // Scan all streaming sublevels via WorldInfo.StreamingLevels
        uintptr_t worldInfo = 0;
        if (!RPM(localPawn + 0x0160, worldInfo) || !worldInfo) return;
        uintptr_t slData = 0; int slCount = 0;
        if (!RPM(worldInfo + 0x0314, slData) || !slData) return;
        if (!RPM(worldInfo + 0x0318, slCount)) return;
        if (slCount <= 0 || slCount > 200) return;

        uintptr_t slPtrs[200];
        if (!RPMBytes(slData, slPtrs, slCount * 4)) return;
        for (int i = 0; i < slCount; i++) {
            if (!slPtrs[i] || slPtrs[i] < 0x10000) continue;
            uintptr_t loadedLevel = 0;
            if (!RPM(slPtrs[i] + 0x0040, loadedLevel) || !loadedLevel) continue;
            if (loadedLevel == level) continue;
            ScanLevelActors(loadedLevel, localPawn);
        }
    }


    // 0=friendly (same faction, not attackable), 1=passive (civilian), 2=enemy (guard etc)
    int GetRelation(uintptr_t npc, const char* className) {
        (void)className;
        uintptr_t fac = 0;
        if (!RPM(npc + 0x050C, fac) || !fac) return 2;

        uintptr_t lp = 0;
        if (RPM(Off::LocalPawn, lp) && lp) {
            uintptr_t pf = 0;
            if (RPM(lp + 0x050C, pf) && pf == fac) return 0;
        }

        int32_t fi = 0;
        if (!RPM(fac + 0x28, fi)) return 2;
        const char* fn = GetNameByIndex(fi);
        if (!fn) return 2;

        if (strstr(fn, "Civilian") || strstr(fn, "Neutral"))
            return 1;
        if (strstr(fn, "Loyalist") || strstr(fn, "Corvo"))
            return 0;
        return 2;
    }

    bool bUseLineTrace = false;
    bool bLineTraceWorks = false;

    bool CheckVisibility(uintptr_t pawn) {
        // Actor.LastRenderTime at 0x016C (SDK: Engine.Actor.LastRenderTime)
        // NOTE: In UE3, LastRenderTime tracks frustum visibility (whether the
        // actor was submitted to the renderer), NOT occlusion. Actors in the
        // camera frustum but behind walls will still appear "visible". This is
        // a known UE3 limitation -- true occlusion checks would require calling
        // line trace functions which are unsafe from a non-game thread.
        float lastRender = 0;
        if (!RPM(pawn + 0x016C, lastRender)) return true;

        // Debug log for first NPC only, once per second max
        DWORD now = GetTickCount();
        if (now - visDebugLastLog > 1000) {
            visDebugLastLog = now;
            LogToFile("CheckVisibility: pawn=0x%08X lastRender=%.4f cachedTime=%.4f diff=%.4f",
                      (uint32_t)pawn, lastRender, cachedTimeSeconds,
                      cachedTimeSeconds - lastRender);
        }

        if (lastRender <= 0.0f) return false;
        if (cachedTimeSeconds > 0.0f)
            return (cachedTimeSeconds - lastRender) < 0.1f;
        return true;
    }

    // Trace-aware visibility: uses PE hook results when available,
    // falls back to LastRenderTime otherwise.
    bool CheckVisibilityTraced(uintptr_t pawn, int npcIndex, const Vec3& camPos) {
        if (PEHook::g_hookInstalled && npcIndex >= 0 && npcIndex < PEHook::MAX_TRACED_NPCS) {
            bool vis;
            if (PEHook::GetTraceResult(npcIndex, vis)) {
                // Queue a refresh if the result is stale
                if (PEHook::NeedsRefresh(npcIndex)) {
                    Vec3 npcLoc;
                    if (RPMBytes(pawn + Off::ActorLocation, &npcLoc, 12)) {
                        npcLoc.Z += 80.0f; // trace to chest height
                        PEHook::QueueTrace(pawn, camPos, npcLoc, npcIndex);
                    }
                }
                return vis;
            }
            // No result yet -- queue first trace, fall through to LastRenderTime
            Vec3 npcLoc;
            if (RPMBytes(pawn + Off::ActorLocation, &npcLoc, 12)) {
                npcLoc.Z += 80.0f;
                PEHook::QueueTrace(pawn, camPos, npcLoc, npcIndex);
            }
        }
        return CheckVisibility(pawn);
    }

    // Call PE for native functions. Do NOT clear iNative/FUNC_Native flags --
    // clearing them forces PE into the bytecode interpreter, but native functions
    // have empty bytecode so nothing executes. Keep flags intact so PE dispatches
    // through GNatives[iNative] which IS the real native C++ function.
    void CallProcessEventNative(uintptr_t obj, uintptr_t func, void* params) {
        if (!obj || !func || obj < 0x10000 || func < 0x10000) return;
        CallProcessEvent(obj, func, params);
    }

    // UE3 AActor::FastTrace — true = line is clear (no hit)
    // Params from SDK: TraceEnd(0x00), TraceStart(0x0C), BoxExtent(0x18), bTraceBullet(0x24), ReturnValue(0x28)
    uintptr_t cachedFastTraceFunc = 0;
    bool FastTrace(uintptr_t actor, const Vec3& start, const Vec3& end) {
        if (!actor || actor < 0x10000) return false;

        if (!cachedFastTraceFunc) {
            cachedFastTraceFunc = FindFunction(actor, "FastTrace");
            if (!cachedFastTraceFunc) { bLineTraceWorks = false; return false; }
        }

        struct {
            Vec3 TraceEnd;          // 0x00
            Vec3 TraceStart;        // 0x0C
            Vec3 BoxExtent;         // 0x18
            uint32_t bTraceBullet;  // 0x24
            uint32_t ReturnValue;   // 0x28
        } params;
        memset(&params, 0, sizeof(params));
        params.TraceEnd = end;
        params.TraceStart = start;

        CallProcessEventNative(actor, cachedFastTraceFunc, &params);
        return params.ReturnValue != 0;
    }

    void TestProcessEvent() {
        LogToFile("=== TestProcessEvent START ===");
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn || pawn < 0x10000) {
            spawnDebug = "no pawn for PE test";
            return;
        }

        cachedFastTraceFunc = FindFunction(pawn, "FastTrace");
        if (!cachedFastTraceFunc) {
            spawnDebug = "FastTrace func not found";
            bLineTraceWorks = false;
            return;
        }

        Vec3 loc;
        if (!RPMBytes(pawn + Off::ActorLocation, &loc, 12)) {
            spawnDebug = "can't read pawn loc";
            return;
        }

        if (!PEHook::g_hookInstalled || !PEHook::g_originalPE) {
            spawnDebug = "hook not installed - cannot test PE";
            return;
        }

        // Queue the actual PE call to the game thread
        uintptr_t func = cachedFastTraceFunc;
        Vec3 testLoc = loc;
        // Store pointer to member vars for result reporting
        volatile bool* pLineTraceWorks = &bLineTraceWorks;
        std::string* pSpawnDebug = &spawnDebug;

        PEHook::QueueCommand([pawn, func, testLoc, pLineTraceWorks, pSpawnDebug]() {
            Vec3 end = testLoc;
            end.Z += 10.f;
            struct {
                Vec3 TraceEnd;
                Vec3 TraceStart;
                Vec3 BoxExtent;
                uint32_t bTraceBullet;
                uint32_t ReturnValue;
            } params;
            memset(&params, 0, sizeof(params));
            params.TraceEnd = end;
            params.TraceStart = testLoc;
            params.ReturnValue = 0xDEAD;

            PEHook::g_originalPE((void*)pawn, nullptr, (void*)func, &params, nullptr);

            char dbg[128];
            snprintf(dbg, sizeof(dbg), "FastTrace@0x%08X ret=%u (game thread)", (uint32_t)func, params.ReturnValue);
            *pSpawnDebug = dbg;
            if (params.ReturnValue != 0xDEAD) {
                *pLineTraceWorks = true;
                LogToFile("TestPE SUCCESS: line trace works, ret=%u", params.ReturnValue);
            } else {
                *pLineTraceWorks = false;
                LogToFile("TestPE FAIL: ReturnValue unchanged");
            }
        });
        spawnDebug = "TestPE queued to game thread";
    }

    float cachedTimeSeconds = 0.0f;
    DWORD visDebugLastLog = 0;
    void UpdateWorldTime() {
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) return;
        // AActor.WorldInfo is at offset 0x0160 directly on the actor.
        // BUG FIX: previously read Outer(0x24)->Level then Level+0x0160,
        // but ULevel does NOT have WorldInfo at 0x0160 -- that offset is
        // AActor.WorldInfo. Reading it directly from the pawn works.
        uintptr_t wi = 0;
        if (!RPM(pawn + 0x0160, wi) || !wi) return;
        float ts = 0;
        if (RPM(wi + 0x038C, ts) && ts > 0.0f)
            cachedTimeSeconds = ts;
    }

    bool IsPawnAlive(uintptr_t pawn) {
        uint64_t flags = 0;
        if (RPM(pawn + 0x08, flags) && (flags & 0x20000000)) return false;
        // unconscious/dead NPCs have 0 hp
        int32_t hp = 0;
        if (RPM(pawn + 0x0344, hp) && hp <= 0) return false;
        // ragdolled = unconscious/dead (PHYS_RigidBody = 10)
        uint8_t phys = 0;
        if (RPM(pawn + 0x0104, phys) && phys == 10) return false;
        return true;
    }

    void ProbeBoneParentOffset(uintptr_t meshPtr) {
        if (parentIndexOffset >= 0) return;

        uintptr_t skelMesh = 0;
        if (!RPM(meshPtr + Off::MeshSkelMesh, skelMesh) || !skelMesh) return;

        uintptr_t refData = 0; int refCount = 0;
        if (!RPM(skelMesh + Off::SkelRefSkel, refData) || !refData) return;
        if (!RPM(skelMesh + Off::SkelRefSkel + 4, refCount)) return;
        if (refCount < 10 || refCount > 300) return;

        // Bone[0] = root (parent should be -1 or 0)
        // Bone[1] = Root_jnt (parent should be 0)
        // Bone[2] = spine_0_jnt (parent should be 1)
        // Find offset where these values appear

        uint8_t bone0[80], bone1[80], bone2[80];
        if (!RPMBytes(refData, bone0, 80)) return;
        if (!RPMBytes(refData + 80, bone1, 80)) return;
        if (!RPMBytes(refData + 160, bone2, 80)) return;

        probeLog = "Probing FMeshBone parent offset (size=80):\n";

        for (int off = 8; off <= 76; off += 4) {
            int32_t v0 = *(int32_t*)(bone0 + off);
            int32_t v1 = *(int32_t*)(bone1 + off);
            int32_t v2 = *(int32_t*)(bone2 + off);

            char buf[128];
            sprintf(buf, "  +0x%02X: b0=%d b1=%d b2=%d\n", off, v0, v1, v2);
            probeLog += buf;

            // Root parent = -1 or 0, bone1 parent = 0, bone2 parent = 1
            if ((v0 == -1 || v0 == 0) && v1 == 0 && v2 == 1) {
                parentIndexOffset = off;
                char msg[128];
                sprintf(msg, "  >>> Found ParentIndex at +0x%02X <<<\n", off);
                probeLog += msg;
            }
        }

        if (parentIndexOffset < 0) {
            probeLog += "  !!! ParentIndex NOT found - using fallback connections\n";
        }
    }

    bool boneDumpDone = false;
    void DumpBoneNames(uintptr_t pawn) {
        if (boneDumpDone) return;
        uintptr_t meshPtr = 0;
        if (!RPM(pawn + Off::PawnMesh, meshPtr) || !meshPtr) return;
        uintptr_t skelMesh = 0;
        if (!RPM(meshPtr + Off::MeshSkelMesh, skelMesh) || !skelMesh) return;
        uintptr_t refData = 0; int refCount = 0;
        if (!RPM(skelMesh + Off::SkelRefSkel, refData) || !refData) return;
        if (!RPM(skelMesh + Off::SkelRefSkel + 4, refCount)) return;
        FILE* f = fopen("bone_dump.txt", "w");
        if (!f) return;
        int limit = (refCount < 150) ? refCount : 150;
        for (int i = 0; i < limit; i++) {
            int32_t nameIdx = 0, parentIdx = -1;
            RPM(refData + i * FMESHBONE_SIZE, nameIdx);
            if (parentIndexOffset >= 0)
                RPM(refData + i * FMESHBONE_SIZE + parentIndexOffset, parentIdx);
            const char* bname = GetNameByIndex(nameIdx);
            fprintf(f, "[%3d] parent=%3d  %s\n", i, parentIdx, bname ? bname : "??");
        }
        fclose(f);
        boneDumpDone = true;
    }

    struct BoneMapping { const char* name; int mappedParent; };

    std::vector<std::pair<int,int>> cachedSkelConnections;
    int cachedHeadBoneIdx = -1;
    int cachedHeadEndBoneIdx = -1;
    uintptr_t cachedSkelMeshAddr = 0;

    int FindBoneByName(uintptr_t refData, int refCount, int maxBones, const char* target) {
        int limit = (refCount < maxBones) ? refCount : maxBones;
        for (int i = 0; i < limit; i++) {
            int32_t nameIdx = 0;
            if (RPM(refData + i * FMESHBONE_SIZE, nameIdx)) {
                const char* bname = GetNameByIndex(nameIdx);
                if (bname && strcmp(bname, target) == 0) return i;
            }
        }
        return -1;
    }

    void GetBoneData(uintptr_t pawn, std::vector<Vec3>& bones,
                     std::vector<std::pair<int,int>>& connections)
    {
        uintptr_t meshPtr = 0;
        if (!RPM(pawn + Off::PawnMesh, meshPtr) || !meshPtr) return;

        Matrix4 l2w;
        if (!RPMBytes(meshPtr + Off::MeshL2W, &l2w, sizeof(Matrix4))) return;
        if (std::isnan(l2w.M[0][0]) || fabsf(l2w.M[0][0]) > 1e6f) return;

        if (l2w.M[0][0] == 0.0f && l2w.M[1][1] == 0.0f && l2w.M[2][2] == 0.0f) {
            if (!RPMBytes(pawn + Off::ActorCachedL2W, &l2w, sizeof(Matrix4))) return;
            if (l2w.M[0][0] == 0.0f && l2w.M[1][1] == 0.0f) return;
        }

        uintptr_t basesData = 0; int basesCount = 0;
        if (!RPM(meshPtr + Off::MeshSpaceBases, basesData) || !basesData) return;
        if (!RPM(meshPtr + Off::MeshSpaceBases + 4, basesCount)) return;
        if (basesCount <= 0 || basesCount > 300) return;

        ProbeBoneParentOffset(meshPtr);

        int maxBones = (basesCount < 150) ? basesCount : 150;
        std::vector<BoneAtom> atoms(maxBones);
        if (!RPMBytes(basesData, atoms.data(), maxBones * sizeof(BoneAtom))) return;

        // Read bone names from RefSkeleton for filtering
        uintptr_t skelMesh = 0;
        uintptr_t refData = 0;
        int refCount = 0;
        bool hasRef = false;
        if (RPM(meshPtr + Off::MeshSkelMesh, skelMesh) && skelMesh) {
            if (RPM(skelMesh + Off::SkelRefSkel, refData) && refData &&
                RPM(skelMesh + Off::SkelRefSkel + 4, refCount) && refCount > 0)
                hasRef = true;
        }

        (void)0; // bone names read above

        for (int i = 0; i < maxBones; i++) {
            auto& a = atoms[i];
            if (std::isnan(a.Translation.X) || fabsf(a.Translation.X) > 1e6f) {
                bones.push_back({0,0,0});
                continue;
            }
            Vec3 p;
            p.X = a.Translation.X*l2w.M[0][0]+a.Translation.Y*l2w.M[1][0]+a.Translation.Z*l2w.M[2][0]+l2w.M[3][0];
            p.Y = a.Translation.X*l2w.M[0][1]+a.Translation.Y*l2w.M[1][1]+a.Translation.Z*l2w.M[2][1]+l2w.M[3][1];
            p.Z = a.Translation.X*l2w.M[0][2]+a.Translation.Y*l2w.M[1][2]+a.Translation.Z*l2w.M[2][2]+l2w.M[3][2];
            if (std::isnan(p.X) || fabsf(p.X) > 1e8f) p = {0,0,0};
            bones.push_back(p);
        }

        // Build skeleton connections by name lookup (cached)
        if (hasRef && cachedSkelMeshAddr == skelMesh && !cachedSkelConnections.empty()) {
            connections = cachedSkelConnections;
        } else if (hasRef) {
            cachedSkelMeshAddr = skelMesh;
            cachedSkelConnections.clear();
            const char* chain[][2] = {
                {"spine_0_jnt", "spine_1_jnt"},
                {"spine_1_jnt", "spine_2_jnt"},
                {"spine_2_jnt", "spine_3_jnt"},
                {"spine_3_jnt", "neck_jnt"},
                {"neck_jnt", "head_jnt"},
                // Left arm
                {"spine_3_jnt", "shoulder_L_jnt"},
                {"shoulder_L_jnt", "upper_arm_L_jnt"},
                {"upper_arm_L_jnt", "lower_arm_L_jnt"},
                {"lower_arm_L_jnt", "hand_L_jnt"},
                // Right arm
                {"spine_3_jnt", "shoulder_R_jnt"},
                {"shoulder_R_jnt", "upper_arm_R_jnt"},
                {"upper_arm_R_jnt", "lower_arm_R_jnt"},
                {"lower_arm_R_jnt", "hand_R_jnt"},
                // Left leg
                {"spine_0_jnt", "upper_leg_L_jnt"},
                {"upper_leg_L_jnt", "lower_leg_L_jnt"},
                {"lower_leg_L_jnt", "foot_L_jnt"},
                // Right leg
                {"spine_0_jnt", "upper_leg_R_jnt"},
                {"upper_leg_R_jnt", "lower_leg_R_jnt"},
                {"lower_leg_R_jnt", "foot_R_jnt"},
                {nullptr, nullptr}
            };
            for (int c = 0; chain[c][0]; c++) {
                int a = FindBoneByName(refData, refCount, maxBones, chain[c][0]);
                int b = FindBoneByName(refData, refCount, maxBones, chain[c][1]);
                if (a >= 0 && b >= 0)
                    connections.push_back(std::make_pair(b, a));
            }
            cachedHeadBoneIdx = FindBoneByName(refData, refCount, maxBones, "head_jnt");
            cachedHeadEndBoneIdx = FindBoneByName(refData, refCount, maxBones, "head_end_jnt");
            cachedSkelConnections = connections;
        }

        // Fallback: chain
        if (connections.empty() && maxBones > 1)
            for (int i = 1; i < maxBones; i++)
                connections.push_back(std::make_pair(i, i-1));
    }

    std::vector<ActorInfo> GatherActors(const Vec3& camPos, float maxDist) {
        DWORD now = GetTickCount();
        if (now - lastPawnScan > 2000 || pawnAddresses.empty()) {
            ScanActors();
            lastPawnScan = now;
            PEHook::ResetResults(); // indices may have changed
        }

        std::vector<ActorInfo> result;

        for (size_t pidx = 0; pidx < pawnAddresses.size(); pidx++) {
            uintptr_t obj = pawnAddresses[pidx];
            if (!obj) continue;

            Vec3 loc;
            if (!RPMBytes(obj + Off::ActorLocation, &loc, 12)) continue;
            if (loc.X == 0.0f && loc.Y == 0.0f && loc.Z == 0.0f) continue;
            if (std::isnan(loc.X) || fabsf(loc.X) > 1e8f) continue;

            float dist = (loc - camPos).Length() / 100.0f;
            if (dist > maxDist || dist < 1.0f) continue;
            if (!IsPawnAlive(obj)) continue;

            // read class name for relation check
            uintptr_t classPtr2 = 0;
            int32_t clsIdx2 = 0;
            const char* clsName = nullptr;
            if (RPM(obj + 0x30, classPtr2) && classPtr2)
                if (RPM(classPtr2 + 0x28, clsIdx2))
                    clsName = GetNameByIndex(clsIdx2);

            ActorInfo info{};
            info.Address = obj;
            info.Location = loc;
            info.Distance = dist;
            info.relation = GetRelation(obj, clsName);
            int traceIdx = ((int)pidx < PEHook::MAX_TRACED_NPCS) ? (int)pidx : -1;
            info.bIsVisible = true;
            // Read instance name + number for display
            int32_t nameIdx = 0, nameNum = 0;
            if (RPM(obj + 0x28, nameIdx) && RPM(obj + 0x2C, nameNum)) {
                const char* baseName = GetNameByIndex(nameIdx);
                if (baseName && baseName[0]) {
                    // Shorten "DishonoredNPCPawn" → "NPC"
                    std::string display = baseName;
                    if (display.find("DishonoredNPCPawn") != std::string::npos)
                        display = "NPC";
                    else if (display.find("DishonoredPawn") != std::string::npos)
                        display = "Pawn";
                    if (nameNum > 0) {
                        char buf[32];
                        sprintf(buf, " #%d", nameNum);
                        display += buf;
                    }
                    info.Name = display;
                } else {
                    info.Name = "??";
                }
            } else {
                info.Name = "??";
            }

            // Health
            int32_t hp = 0, hpMax = 0;
            RPM(obj + 0x0344, hp);
            RPM(obj + 0x0348, hpMax);
            info.healthPct = (hpMax > 0) ? (float)hp / (float)hpMax : 1.0f;
            if (info.healthPct < 0.0f) info.healthPct = 0.0f;
            if (info.healthPct > 1.0f) info.healthPct = 1.0f;

            // Pawn->Controller(0x26C)->AIBrain(0x380)->combatFlags(0x3C)/suspicion(0x41)
            info.aiState = AI_Unknown;
            uintptr_t ctrl = 0;
            if (RPM(obj + 0x26C, ctrl) && ctrl) {
                uintptr_t brain = 0;
                if (RPM(ctrl + 0x0380, brain) && brain) {
                    uint32_t flags = 0;
                    uint8_t susp = 0;
                    RPM(brain + 0x003C, flags);
                    RPM(brain + 0x0041, susp);
                    if (flags & 0x01) info.aiState = AI_Combat;
                    else if (susp >= 1) info.aiState = AI_Suspicious;
                    else info.aiState = AI_Unaware;
                }
            }

            if (dist < maxDist) {
                GetBoneData(obj, info.Bones, info.BoneConnections);
                info.headBoneIdx = cachedHeadBoneIdx;
                info.headEndBoneIdx = cachedHeadEndBoneIdx;
            }

            result.push_back(std::move(info));
        }
        return result;
    }

    void DumpLevelActors(FILE* f, uintptr_t level, int& pickupCount) {
        uintptr_t actorsData = 0; int actorsCount = 0;
        if (!RPM(level + 0x0038, actorsData) || !actorsData) return;
        if (!RPM(level + 0x003C, actorsCount)) return;
        if (actorsCount <= 0 || actorsCount > 10000) return;
        constexpr int CHUNK = 512;
        uintptr_t chunk[CHUNK];
        for (int base = 0; base < actorsCount; base += CHUNK) {
            int n = (actorsCount - base < CHUNK) ? (actorsCount - base) : CHUNK;
            if (!RPMBytes(actorsData + base * 4, chunk, n * 4)) continue;
            for (int j = 0; j < n; j++) {
                uintptr_t obj = chunk[j];
                if (!obj || obj < 0x10000) continue;
                uintptr_t classPtr = 0;
                if (!RPM(obj + 0x30, classPtr) || !classPtr) continue;
                int32_t ci = 0;
                if (!RPM(classPtr + 0x28, ci)) continue;
                const char* cls = GetNameByIndex(ci);
                if (!cls) continue;
                int32_t ni = 0; RPM(obj + 0x28, ni);
                const char* nm = GetNameByIndex(ni);
                if (nm && strstr(nm, "Default__")) continue;
                bool isPickupClass = (strstr(cls, "Pickup") || strstr(cls, "Rune") || strstr(cls, "BoneCharm") || strstr(cls, "Elixir") || strstr(cls, "Key_"));
                bool skip = (strstr(cls, "Pawn") || strstr(cls, "Controller") || strstr(cls, "Camera") || strstr(cls, "Light") || strstr(cls, "Emitter") || strstr(cls, "Sound") || strstr(cls, "Trigger") || strstr(cls, "Volume") || strstr(cls, "Info") || strstr(cls, "NavigationPoint") || strstr(cls, "DecalActor") || strstr(cls, "Brush") || strstr(cls, "PostProcess") || strstr(cls, "Note") || strstr(cls, "Fog") || strstr(cls, "LensFlare") || strstr(cls, "CoverLink"));
                if (skip) continue;
                uint64_t oflags = 0; RPM(obj + 0x08, oflags);
                uint32_t aflags = 0; RPM(obj + 0x0120, aflags);
                uint32_t pflags = 0; RPM(obj + 0x0388, pflags);
                Vec3 loc; RPMBytes(obj + Off::ActorLocation, &loc, 12);
                const char* status = "OK";
                if (oflags & 0x20000000) status = "PENDINGKILL";
                else if (aflags & 0x02) status = "HIDDEN";
                else if (aflags & 0x08) status = "DELETEME";
                else if (pflags & 0x02) status = "PENDING_DESTROY";
                fprintf(f, "[%s] 0x%08X cls=%-35s name=%-30s aflags=0x%08X pflags=0x%08X loc=(%.0f,%.0f,%.0f) %s\n",
                    isPickupClass ? "PICKUP" : "ACTOR ", (uint32_t)obj, cls, nm?nm:"?",
                    aflags, pflags, loc.X, loc.Y, loc.Z, status);
                if (isPickupClass) pickupCount++;
            }
        }
    }

    void DumpAllItems() {
        uintptr_t localPawn = 0;
        RPM(Off::LocalPawn, localPawn);
        if (!localPawn) return;
        FILE* f = fopen("item_dump.txt", "w");
        if (!f) return;
        int pickupCount = 0, levelCount = 0;

        uintptr_t level = 0;
        if (RPM(localPawn + 0x0024, level) && level) {
            fprintf(f, "=== Persistent Level 0x%08X ===\n", (uint32_t)level);
            DumpLevelActors(f, level, pickupCount);
            levelCount++;
        }

        uintptr_t worldInfo = 0;
        if (RPM(localPawn + 0x0160, worldInfo) && worldInfo) {
            uintptr_t slData = 0; int slCount = 0;
            if (RPM(worldInfo + 0x0314, slData) && slData && RPM(worldInfo + 0x0318, slCount) && slCount > 0 && slCount <= 200) {
                uintptr_t slPtrs[200];
                if (RPMBytes(slData, slPtrs, slCount * 4)) {
                    for (int i = 0; i < slCount; i++) {
                        if (!slPtrs[i] || slPtrs[i] < 0x10000) continue;
                        uintptr_t ll = 0;
                        if (!RPM(slPtrs[i] + 0x0040, ll) || !ll) continue;
                        if (ll == level) continue;
                        int32_t pni = 0; RPM(slPtrs[i] + 0x0038, pni);
                        const char* pkgName = GetNameByIndex(pni);
                        fprintf(f, "\n=== Streaming Level %d: %s (0x%08X) ===\n", i, pkgName?pkgName:"?", (uint32_t)ll);
                        DumpLevelActors(f, ll, pickupCount);
                        levelCount++;
                    }
                }
            }
        }
        fprintf(f, "\nTotal levels scanned: %d, pickup-class actors: %d\n", levelCount, pickupCount);
        fclose(f);
    }

    std::vector<ItemInfo> GatherItems(const Vec3& camPos, float maxDist) {
        std::vector<ItemInfo> result;
        for (size_t i = 0; i < itemAddresses.size(); i++) {
            uintptr_t obj = itemAddresses[i];
            if (!obj) continue;

            Vec3 loc;
            if (!RPMBytes(obj + Off::ActorLocation, &loc, 12)) continue;
            if (loc.X == 0.f && loc.Y == 0.f && loc.Z == 0.f) continue;
            if (std::isnan(loc.X) || fabsf(loc.X) > 1e8f) continue;

            float dist = (loc - camPos).Length() / 100.f;
            if (dist > maxDist || dist < 0.5f) continue;

            // skip non-collectible
            uint64_t oflags = 0;
            if (RPM(obj + 0x08, oflags) && (oflags & 0x20000000)) continue;
            uint32_t aflags = 0;
            if (RPM(obj + 0x0120, aflags) && (aflags & 0x0A)) continue; // bHidden | bDeleteMe
            uint32_t pflags = 0;
            RPM(obj + 0x0388, pflags);
            if (pflags & 0x02) continue; // m_bPendingDestruction
            if (pflags & 0x04) continue; // m_bIsAttachedAsStealable (NPC weapon)

            std::string clsStr = (i < itemClassNames.size()) ? itemClassNames[i] : "?";

            // DishonoredInventoryPickup without m_pItem = display/rack weapon, not collectible
            if (clsStr.find("Inventory") != std::string::npos) {
                uintptr_t pItem = 0;
                RPM(obj + 0x03F4, pItem);
                if (!pItem || pItem < 0x10000) continue;
            }

            ItemInfo info{};
            info.Address = obj;
            info.Location = loc;
            info.Distance = dist;
            info.ClassName = clsStr;

            // read bounding box from CollisionComponent
            info.BoxExtent = {8.f, 8.f, 8.f}; // fallback
            uintptr_t collComp = 0;
            if (RPM(obj + 0x0208, collComp) && collComp > 0x10000) {
                Vec3 origin;
                if (RPMBytes(collComp + 0x00CC, &origin, 12)) {
                    if (!std::isnan(origin.X) && fabsf(origin.X) < 1e8f)
                        info.Location = origin;
                }
                Vec3 ext;
                if (RPMBytes(collComp + 0x00D8, &ext, 12)) {
                    if (ext.X > 1.f && ext.X < 500.f) info.BoxExtent.X = ext.X;
                    if (ext.Y > 1.f && ext.Y < 500.f) info.BoxExtent.Y = ext.Y;
                    if (ext.Z > 1.f && ext.Z < 500.f) info.BoxExtent.Z = ext.Z;
                }
            }

            // read tweaks object name — offset depends on class
            std::string& cn = info.ClassName;
            std::string tweaksName;
            auto readTweaksName = [&](uintptr_t off) {
                uintptr_t tweaks = 0;
                if (!RPM(obj + off, tweaks) || !tweaks || tweaks < 0x10000) return;
                int32_t tni = 0;
                if (!RPM(tweaks + 0x28, tni)) return;
                const char* tn = GetNameByIndex(tni);
                if (!tn || strcmp(tn, "None") == 0) return;
                tweaksName = tn;
                auto pos = tweaksName.rfind("_twk"); if (pos != std::string::npos) tweaksName.erase(pos);
                pos = tweaksName.rfind("_Twk"); if (pos != std::string::npos) tweaksName.erase(pos);
                for (char& c : tweaksName) if (c == '_') c = ' ';
            };
            if (cn.find("Inventory") != std::string::npos) readTweaksName(0x03F8); // DishonoredInventoryPickup
            if (tweaksName.empty() && cn.find("Stat") != std::string::npos) readTweaksName(0x03F0); // DisStatPickup
            if (tweaksName.empty()) readTweaksName(0x03B8); // DisPickup_Base subclasses

            // skip items with no tweaks and no known class (probably invalid)
            if (tweaksName.empty() && cn.find("Abstract") != std::string::npos) continue;
            if (tweaksName.empty() && cn.find("Generic") != std::string::npos) continue;

            if (cn.find("WhaleBoneCharm") != std::string::npos) info.Name = "Bone Charm";
            else if (cn.find("Rune") != std::string::npos) info.Name = "Rune";
            else if (cn.find("ElixirHealth") != std::string::npos) info.Name = "Health Elixir";
            else if (cn.find("ElixirMana") != std::string::npos) info.Name = "Mana Elixir";
            else if (!tweaksName.empty()) info.Name = tweaksName;
            else if (cn.find("Key_") != std::string::npos) info.Name = "Key";
            else if (cn.find("Inventory") != std::string::npos) info.Name = "Weapon";
            else if (cn.find("AudioLog") != std::string::npos) info.Name = "Audio Log";
            else if (cn.find("Note") != std::string::npos) info.Name = "Note";
            else if (cn.find("Stat") != std::string::npos) info.Name = "Loot";
            else info.Name = cn;

            result.push_back(std::move(info));
        }
        return result;
    }

    std::vector<InteractInfo> GatherInteractables(const Vec3& camPos, float maxDist) {
        std::vector<InteractInfo> result;
        for (size_t i = 0; i < interactAddresses.size(); i++) {
            uintptr_t obj = interactAddresses[i];
            if (!obj) continue;

            Vec3 loc;
            if (!RPMBytes(obj + Off::ActorLocation, &loc, 12)) continue;
            if (loc.X == 0.f && loc.Y == 0.f && loc.Z == 0.f) continue;
            if (std::isnan(loc.X) || fabsf(loc.X) > 1e8f) continue;

            float dist = (loc - camPos).Length() / 100.f;
            if (dist > maxDist || dist < 0.5f) continue;

            uint64_t oflags = 0;
            if (RPM(obj + 0x08, oflags) && (oflags & 0x20000000)) continue;
            uint32_t aflags = 0;
            if (RPM(obj + 0x0120, aflags) && (aflags & 0x0A)) continue;

            InteractInfo info{};
            info.Address = obj;
            info.Location = loc;
            info.Distance = dist;
            info.BoxExtent = {15.f, 15.f, 15.f};
            uintptr_t collComp = 0;
            if (RPM(obj + 0x0208, collComp) && collComp > 0x10000) {
                Vec3 origin;
                if (RPMBytes(collComp + 0x00CC, &origin, 12)) {
                    if (!std::isnan(origin.X) && fabsf(origin.X) < 1e8f)
                        info.Location = origin;
                }
                Vec3 ext;
                if (RPMBytes(collComp + 0x00D8, &ext, 12)) {
                    if (ext.X > 1.f && ext.X < 500.f) info.BoxExtent.X = ext.X;
                    if (ext.Y > 1.f && ext.Y < 500.f) info.BoxExtent.Y = ext.Y;
                    if (ext.Z > 1.f && ext.Z < 500.f) info.BoxExtent.Z = ext.Z;
                }
            }

            std::string cls = (i < interactClassNames.size()) ? interactClassNames[i] : "?";

            // read tweaks name for real display name
            std::string tweaksName;
            auto readTwk = [&](uintptr_t off) {
                uintptr_t tw = 0;
                if (!RPM(obj + off, tw) || !tw || tw < 0x10000) return;
                int32_t tni = 0;
                if (!RPM(tw + 0x28, tni)) return;
                const char* tn = GetNameByIndex(tni);
                if (!tn || strcmp(tn, "None") == 0) return;
                tweaksName = tn;
                auto p = tweaksName.rfind("_twk"); if (p != std::string::npos) tweaksName.erase(p);
                p = tweaksName.rfind("_Twk"); if (p != std::string::npos) tweaksName.erase(p);
                for (char& c : tweaksName) if (c == '_') c = ' ';
            };
            if (cls.find("WhaleOilBattery") != std::string::npos) readTwk(0x0424);
            else if (cls.find("WhaleOilReceptacle") != std::string::npos) readTwk(0x0284);
            else readTwk(0x02A0); // UsableObject base + AlarmBell

            if (!tweaksName.empty()) info.Name = tweaksName;
            else if (cls.find("Tripwire") != std::string::npos) info.Name = "Tripwire";
            else if (cls.find("SpringRazor") != std::string::npos) info.Name = "Spring Razor";
            else if (cls.find("ArcMine") != std::string::npos) info.Name = "Arc Mine";
            else if (cls.find("AlarmBell") != std::string::npos) info.Name = "Alarm Bell";
            else if (cls.find("WhaleOilReceptacle") != std::string::npos) info.Name = "Receptacle";
            else if (cls.find("WhaleOilBattery") != std::string::npos) info.Name = "Whale Oil";
            else if (cls.find("WaterSource") != std::string::npos) info.Name = "Water";
            else if (cls.find("AudioLog") != std::string::npos) info.Name = "Audio Log";
            else {
                int32_t ni = 0;
                if (RPM(obj + 0x28, ni)) {
                    const char* nm = GetNameByIndex(ni);
                    if (nm && nm[0]) {
                        info.Name = nm;
                        for (char& c : info.Name) if (c == '_') c = ' ';
                    } else info.Name = "Usable";
                } else info.Name = "Usable";
            }

            // filter by display name
            if (info.Name.find("sink") != std::string::npos || info.Name.find("Sink") != std::string::npos) continue;
            if (info.Name.find("trash") != std::string::npos || info.Name.find("Trash") != std::string::npos) continue;
            if (info.Name.find("door") != std::string::npos || info.Name.find("Door") != std::string::npos) continue;
            if (info.Name.find("LTD") != std::string::npos) continue;
            if (cls.find("Door") != std::string::npos) continue;

            result.push_back(std::move(info));
        }
        return result;
    }

    std::vector<InteractInfo> GatherWallhackAll(const Vec3& camPos, float maxDist) {
        std::vector<InteractInfo> result;

        // interactables
        auto ints = GatherInteractables(camPos, maxDist);
        result.insert(result.end(), ints.begin(), ints.end());

        // doors
        for (uintptr_t obj : doorAddresses) {
            if (!obj) continue;
            Vec3 loc;
            if (!RPMBytes(obj + Off::ActorLocation, &loc, 12)) continue;
            if (loc.X == 0.f && loc.Y == 0.f && loc.Z == 0.f) continue;
            if (std::isnan(loc.X) || fabsf(loc.X) > 1e8f) continue;
            float dist = (loc - camPos).Length() / 100.f;
            if (dist > maxDist || dist < 0.5f) continue;
            uint64_t oflags = 0;
            if (RPM(obj + 0x08, oflags) && (oflags & 0x20000000)) continue;

            InteractInfo info{};
            info.Address = obj;
            info.Location = loc;
            info.Distance = dist;
            info.BoxExtent = {40.f, 10.f, 60.f};
            uintptr_t collComp = 0;
            if (RPM(obj + 0x0208, collComp) && collComp > 0x10000) {
                Vec3 origin;
                if (RPMBytes(collComp + 0x00CC, &origin, 12)) {
                    if (!std::isnan(origin.X) && fabsf(origin.X) < 1e8f)
                        info.Location = origin;
                }
                Vec3 ext;
                if (RPMBytes(collComp + 0x00D8, &ext, 12)) {
                    if (ext.X > 1.f && ext.X < 500.f) info.BoxExtent.X = ext.X;
                    if (ext.Y > 1.f && ext.Y < 500.f) info.BoxExtent.Y = ext.Y;
                    if (ext.Z > 1.f && ext.Z < 500.f) info.BoxExtent.Z = ext.Z;
                }
            }
            int32_t ni = 0;
            if (RPM(obj + 0x28, ni)) {
                const char* nm = GetNameByIndex(ni);
                if (nm && nm[0]) {
                    info.Name = nm;
                    for (char& c : info.Name) if (c == '_') c = ' ';
                } else info.Name = "Door";
            } else info.Name = "Door";
            result.push_back(std::move(info));
        }

        // items
        auto items = GatherItems(camPos, maxDist);
        for (auto& it : items) {
            InteractInfo info{};
            info.Address = it.Address;
            info.Location = it.Location;
            info.Distance = it.Distance;
            info.BoxExtent = it.BoxExtent;
            info.Name = it.Name;
            result.push_back(std::move(info));
        }

        return result;
    }

    bool TeleportAboveTarget(const Vec3& camPos, const Rotator& camRot, float scrW, float scrH, float maxDist, float heightMeters = 5.f) {
        Matrix4 vpm;
        BuildVPM(camPos, camRot, scrW, scrH, vpm);

        float bestDist = 9999.f;
        int bestIdx = -1;
        Vec3 bestHeadPos;

        for (int i = 0; i < (int)pawnAddresses.size(); i++) {
            uintptr_t obj = pawnAddresses[i];
            if (!obj) continue;

            Vec3 loc;
            if (!RPMBytes(obj + Off::ActorLocation, &loc, 12)) continue;
            float dist = (loc - camPos).Length() / 100.f;
            if (dist > maxDist || dist < 1.0f) continue;
            if (!IsPawnAlive(obj)) continue;

            Vec3 headW;
            if (!GetHeadBonePos(obj, headW)) {
                headW = loc;
                headW.Z += 80.f;
            }
            Vec3 scr;
            if (!WorldToScreen(headW, vpm, scrW, scrH, scr)) continue;

            float dx = scr.X - scrW * 0.5f;
            float dy = scr.Y - scrH * 0.5f;
            float sd = sqrtf(dx*dx + dy*dy);
            if (sd < bestDist) {
                bestDist = sd;
                bestIdx = i;
                bestHeadPos = headW;
            }
        }

        if (bestIdx < 0 || bestDist > 300.f) return false;

        uintptr_t localPawn = 0;
        if (!RPM(Off::LocalPawn, localPawn) || !localPawn) return false;

        Vec3 tpPos = bestHeadPos;
        tpPos.Z += heightMeters * 100.f;
        WPMBytes(localPawn + Off::ActorLocation, &tpPos, 12);
        Vec3 zero = {0.f, 0.f, 0.f};
        WPMBytes(localPawn + 0x01B4, &zero, 12);
        return true;
    }

    int UnlockAll(const Vec3& camPos, float maxDist) {
        int count = 0;
        // unlock UsableObjects (interactables)
        for (size_t i = 0; i < interactAddresses.size(); i++) {
            uintptr_t obj = interactAddresses[i];
            if (!obj) continue;
            Vec3 loc;
            if (!RPMBytes(obj + Off::ActorLocation, &loc, 12)) continue;
            float dist = (loc - camPos).Length() / 100.f;
            if (dist > maxDist) continue;
            uint32_t flags = 0;
            if (!RPM(obj + 0x02B0, flags)) continue;
            if (flags & 0x01) {
                flags &= ~0x01u;
                WPM(obj + 0x02B0, flags);
                count++;
            }
        }
        // unlock doors/safes (DisDoor)
        for (size_t i = 0; i < doorAddresses.size(); i++) {
            uintptr_t obj = doorAddresses[i];
            if (!obj) continue;
            Vec3 loc;
            if (!RPMBytes(obj + Off::ActorLocation, &loc, 12)) continue;
            float dist = (loc - camPos).Length() / 100.f;
            if (dist > maxDist) continue;
            uint32_t flags = 0;
            if (RPM(obj + 0x02B0, flags) && (flags & 0x01)) {
                flags &= ~0x01u;
                WPM(obj + 0x02B0, flags);
                count++;
            }
            // also set m_DoorState to Opened_CW (1) so safes actually open
            uint8_t doorState = 0;
            if (RPM(obj + 0x0311, doorState) && doorState == 0) {
                doorState = 1; // DDS_Opened_CW
                WPM(obj + 0x0311, doorState);
            }
        }
        return count;
    }

    bool GetHeadBonePos(uintptr_t pawn, Vec3& outHead) {
        if (cachedHeadBoneIdx < 0) return false;
        uintptr_t meshPtr = 0;
        if (!RPM(pawn + Off::PawnMesh, meshPtr) || !meshPtr) return false;
        Matrix4 l2w;
        if (!RPMBytes(meshPtr + Off::MeshL2W, &l2w, sizeof(Matrix4))) return false;
        if (l2w.M[0][0] == 0.0f && l2w.M[1][1] == 0.0f && l2w.M[2][2] == 0.0f) {
            if (!RPMBytes(pawn + Off::ActorCachedL2W, &l2w, sizeof(Matrix4))) return false;
            if (l2w.M[0][0] == 0.0f && l2w.M[1][1] == 0.0f) return false;
        }
        uintptr_t basesData = 0; int basesCount = 0;
        if (!RPM(meshPtr + Off::MeshSpaceBases, basesData) || !basesData) return false;
        if (!RPM(meshPtr + Off::MeshSpaceBases + 4, basesCount)) return false;
        if (cachedHeadBoneIdx >= basesCount) return false;
        BoneAtom atom;
        if (!RPMBytes(basesData + cachedHeadBoneIdx * sizeof(BoneAtom), &atom, sizeof(BoneAtom))) return false;
        if (std::isnan(atom.Translation.X)) return false;
        outHead.X = atom.Translation.X*l2w.M[0][0]+atom.Translation.Y*l2w.M[1][0]+atom.Translation.Z*l2w.M[2][0]+l2w.M[3][0];
        outHead.Y = atom.Translation.X*l2w.M[0][1]+atom.Translation.Y*l2w.M[1][1]+atom.Translation.Z*l2w.M[2][1]+l2w.M[3][1];
        outHead.Z = atom.Translation.X*l2w.M[0][2]+atom.Translation.Y*l2w.M[1][2]+atom.Translation.Z*l2w.M[2][2]+l2w.M[3][2];
        return !std::isnan(outHead.X);
    }

    struct AimResult {
        bool found = false;
        Vec3 screenPos;
    };

    AimResult MagicBullet(const Vec3& camPos, const Rotator& camRot, float scrW, float scrH, const std::vector<ActorInfo>& actors, float fovRadius = 9999.f) {
        AimResult aim;
        Matrix4 vpm;
        BuildVPM(camPos, camRot, scrW, scrH, vpm);

        float bestDist = 9999.f;
        uintptr_t bestNPC = 0;
        Vec3 bestBoneWorld;
        Vec3 bestScr;
        float cx = scrW * 0.5f;
        float cy = scrH * 0.5f;

        for (auto& e : actors) {
            if (!e.Address) continue;

            if (!e.Bones.empty() && !e.BoneConnections.empty()) {
                std::vector<bool> usable(e.Bones.size(), false);
                for (auto& [a, b] : e.BoneConnections) {
                    if (a >= 0 && a < (int)usable.size()) usable[a] = true;
                    if (b >= 0 && b < (int)usable.size()) usable[b] = true;
                }
                for (int i = 0; i < (int)e.Bones.size(); i++) {
                    if (!usable[i]) continue;
                    auto& bp = e.Bones[i];
                    if (bp.X == 0.f && bp.Y == 0.f && bp.Z == 0.f) continue;
                    Vec3 scr;
                    if (!WorldToScreen(bp, vpm, scrW, scrH, scr)) continue;
                    float dx = scr.X - cx, dy = scr.Y - cy;
                    float sd = sqrtf(dx*dx + dy*dy);
                    if (sd > fovRadius) continue;
                    if (sd < bestDist) {
                        bestDist = sd;
                        bestNPC = e.Address;
                        bestBoneWorld = bp;
                        bestScr = scr;
                    }
                }
            } else {
                Vec3 fallback = e.Location;
                fallback.Z += 80.f;
                Vec3 scr;
                if (!WorldToScreen(fallback, vpm, scrW, scrH, scr)) continue;
                float dx = scr.X - cx, dy = scr.Y - cy;
                float sd = sqrtf(dx*dx + dy*dy);
                if (sd > fovRadius) continue;
                if (sd < bestDist) {
                    bestDist = sd;
                    bestNPC = e.Address;
                    bestBoneWorld = fallback;
                    bestScr = scr;
                }
            }
        }

        if (!bestNPC) return aim;
        aim.found = true;
        aim.screenPos = bestScr;

        // traverse projectile linked list via GameManager
        // Pawn->WorldInfo(0x0160)->DisGameManager is subclass; find via GObjects or get projMgr
        // Simpler: scan level actors for active projectiles
        // Even simpler: scan all level actors each frame is too slow
        // Use the projectile linked list: DisGameInfo(WorldInfo subclass)->m_pGlobalProjectileManager(0x0414)->m_pProjListHead(0x0038)

        uintptr_t localPawn = 0;
        if (!RPM(Off::LocalPawn, localPawn) || !localPawn) return aim;
        uintptr_t wi = 0;
        if (!RPM(localPawn + 0x0160, wi) || !wi) return aim;
        uintptr_t gameInfo = 0;
        if (!RPM(wi + 0x0410, gameInfo) || !gameInfo || gameInfo < 0x10000) return aim;
        uintptr_t projMgr = 0;
        if (!RPM(gameInfo + 0x0414, projMgr) || !projMgr || projMgr < 0x10000) return aim;
        uintptr_t proj = 0;
        if (!RPM(projMgr + 0x0038, proj)) return aim;

        int maxIter = 32;
        while (proj && proj > 0x10000 && maxIter-- > 0) {
            uintptr_t next = 0;
            RPM(proj + 0x02D4, next);

            uintptr_t srcActor = 0;
            RPM(proj + 0x0278, srcActor);
            if (srcActor == localPawn) {
                uint64_t oflags = 0;
                if (RPM(proj + 0x08, oflags) && !(oflags & 0x20000000)) {
                    Vec3 vel;
                    if (RPMBytes(proj + 0x01B4, &vel, 12)) {
                        float speed = vel.Length();
                        if (speed > 100.f) {
                            Vec3 projLoc;
                            if (RPMBytes(proj + Off::ActorLocation, &projLoc, 12)) {
                                Vec3 diff;
                                diff.X = bestBoneWorld.X - projLoc.X;
                                diff.Y = bestBoneWorld.Y - projLoc.Y;
                                diff.Z = bestBoneWorld.Z - projLoc.Z;
                                float len = diff.Length();
                                if (len > 1.f) {
                                    float nx = diff.X / len, ny = diff.Y / len, nz = diff.Z / len;
                                    // place ~2 frames before target so sweep passes through bone
                                    float offset = speed * 0.033f;
                                    if (offset > len) offset = len * 0.5f;
                                    Vec3 newPos;
                                    newPos.X = bestBoneWorld.X - nx * offset;
                                    newPos.Y = bestBoneWorld.Y - ny * offset;
                                    newPos.Z = bestBoneWorld.Z - nz * offset;
                                    WPMBytes(proj + Off::ActorLocation, &newPos, 12);
                                    Vec3 newVel;
                                    newVel.X = nx * speed;
                                    newVel.Y = ny * speed;
                                    newVel.Z = nz * speed;
                                    WPMBytes(proj + 0x01B4, &newVel, 12);
                                    Vec3 zero = {0, 0, 0};
                                    WPMBytes(proj + 0x01C0, &zero, 12);
                                }
                            }
                        }
                    }
                }
            }

            proj = next;
        }
        return aim;
    }


    struct WallInteractResult {
        bool found = false;
        std::string name;
        float distance = 0.f;
        Vec3 screenPos;
    };

    WallInteractResult InteractThroughWalls(const Vec3& camPos, const Rotator& camRot, float scrW, float scrH) {
        WallInteractResult result;
        uintptr_t localPawn = 0;
        if (!RPM(Off::LocalPawn, localPawn) || !localPawn) return result;
        uintptr_t pc = 0;
        if (!RPM(localPawn + 0x026C, pc) || !pc) return result;

        Matrix4 vpm;
        BuildVPM(camPos, camRot, scrW, scrH, vpm);
        float bestScore = 9999.f;
        uintptr_t bestObj = 0;
        std::string bestName;
        float bestWorldDist = 0.f;
        Vec3 bestScr;

        auto check = [&](uintptr_t obj, const char* fallbackName) {
            Vec3 loc;
            if (!RPMBytes(obj + Off::ActorLocation, &loc, 12)) return;
            Vec3 scr;
            if (!WorldToScreen(loc, vpm, scrW, scrH, scr)) return;
            float dx = scr.X - scrW * 0.5f;
            float dy = scr.Y - scrH * 0.5f;
            float sd = sqrtf(dx*dx + dy*dy);
            if (sd > 300.f) return;
            float ddx = loc.X - camPos.X, ddy = loc.Y - camPos.Y, ddz = loc.Z - camPos.Z;
            float worldDist = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz) / 100.f;
            float score = sd + worldDist * 3.f;
            if (score < bestScore) {
                bestScore = score;
                bestObj = obj;
                bestScr = scr;
                bestWorldDist = worldDist;
                int32_t nameIdx = 0;
                if (RPM(obj + 0x28, nameIdx)) {
                    const char* n = GetNameByIndex(nameIdx);
                    if (n && n[0] && !strstr(n, "Default__")) bestName = n;
                    else bestName = fallbackName;
                } else bestName = fallbackName;
            }
        };

        for (size_t i = 0; i < interactAddresses.size(); i++) {
            if (!interactAddresses[i]) continue;
            const char* fb = (i < interactClassNames.size()) ? interactClassNames[i].c_str() : "Object";
            check(interactAddresses[i], fb);
        }
        for (uintptr_t obj : doorAddresses) {
            if (!obj) continue;
            check(obj, "Door");
        }
        // also check items (pickups)
        for (size_t i = 0; i < itemAddresses.size(); i++) {
            if (!itemAddresses[i]) continue;
            const char* fb = (i < itemClassNames.size()) ? itemClassNames[i].c_str() : "Item";
            check(itemAddresses[i], fb);
        }

        if (bestObj) {
            WPM(pc + 0x069C, bestObj);
            WPM(pc + 0x06A0, bestObj);
            uint32_t flags = 0;
            if (RPM(pc + 0x0610, flags)) {
                flags |= 0x2000;
                WPM(pc + 0x0610, flags);
            }
            result.found = true;
            result.name = bestName;
            result.distance = bestWorldDist;
            result.screenPos = bestScr;
        }
        return result;
    }

    int PatchTraceMasks(bool enableProjectile, bool enableCrosshair) {
        uintptr_t localPawn = 0;
        if (!RPM(Off::LocalPawn, localPawn) || !localPawn) return 0;
        uintptr_t level = 0;
        if (!RPM(localPawn + 0x0024, level) || !level) return 0;
        uintptr_t actorsData = 0; int actorsCount = 0;
        if (!RPM(level + 0x0038, actorsData) || !actorsData) return 0;
        if (!RPM(level + 0x003C, actorsCount)) return 0;
        if (actorsCount <= 0 || actorsCount > 20000) return 0;

        uint32_t clearMask = 0;
        if (enableProjectile) clearMask |= 0x10;
        if (enableCrosshair) clearMask |= 0x08;
        if (!clearMask) return 0;

        int patched = 0;
        constexpr int CHUNK = 512;
        uintptr_t chunk[CHUNK];
        for (int base = 0; base < actorsCount; base += CHUNK) {
            int n = (actorsCount - base < CHUNK) ? (actorsCount - base) : CHUNK;
            if (!RPMBytes(actorsData + base * 4, chunk, n * 4)) continue;
            for (int j = 0; j < n; j++) {
                uintptr_t obj = chunk[j];
                if (!obj || obj < 0x10000) continue;
                // skip pawns and interactables
                uintptr_t classPtr = 0;
                if (!RPM(obj + 0x30, classPtr) || !classPtr || classPtr < 0x10000) continue;
                int32_t classNameIdx = 0;
                if (!RPM(classPtr + 0x28, classNameIdx)) continue;
                const char* cls = GetNameByIndex(classNameIdx);
                if (!cls) continue;
                if (strstr(cls, "Pawn") || strstr(cls, "Pickup") || strstr(cls, "Usable") ||
                    strstr(cls, "Controller") || strstr(cls, "Projectile") || strstr(cls, "Bullet")) continue;

                uintptr_t collComp = 0;
                if (!RPM(obj + 0x0208, collComp) || !collComp || collComp < 0x10000) continue;
                uint32_t traceMask = 0;
                if (!RPM(collComp + 0x0140, traceMask)) continue;
                if (traceMask & clearMask) {
                    traceMask &= ~clearMask;
                    WPM(collComp + 0x0140, traceMask);
                    patched++;
                }
            }
        }

        // also scan streaming sublevels
        uintptr_t worldInfo = 0;
        if (RPM(localPawn + 0x0160, worldInfo) && worldInfo) {
            uintptr_t slData = 0; int slCount = 0;
            if (RPM(worldInfo + 0x00BC, slData) && slData &&
                RPM(worldInfo + 0x00C0, slCount) && slCount > 0 && slCount < 200) {
                for (int i = 0; i < slCount; i++) {
                    uintptr_t slObj = 0;
                    if (!RPM(slData + i * 4, slObj) || !slObj) continue;
                    uintptr_t loadedLevel = 0;
                    if (!RPM(slObj + 0xD8, loadedLevel) || !loadedLevel) continue;
                    uintptr_t subData = 0; int subCount = 0;
                    if (!RPM(loadedLevel + 0x0038, subData) || !subData) continue;
                    if (!RPM(loadedLevel + 0x003C, subCount) || subCount <= 0 || subCount > 20000) continue;
                    for (int sb = 0; sb < subCount; sb += CHUNK) {
                        int sn = (subCount - sb < CHUNK) ? (subCount - sb) : CHUNK;
                        if (!RPMBytes(subData + sb * 4, chunk, sn * 4)) continue;
                        for (int k = 0; k < sn; k++) {
                            uintptr_t sobj = chunk[k];
                            if (!sobj || sobj < 0x10000) continue;
                            uintptr_t scp = 0;
                            if (!RPM(sobj + 0x30, scp) || !scp || scp < 0x10000) continue;
                            int32_t scni = 0;
                            if (!RPM(scp + 0x28, scni)) continue;
                            const char* scls = GetNameByIndex(scni);
                            if (!scls) continue;
                            if (strstr(scls, "Pawn") || strstr(scls, "Pickup") || strstr(scls, "Usable") ||
                                strstr(scls, "Controller") || strstr(scls, "Projectile") || strstr(scls, "Bullet")) continue;
                            uintptr_t scc = 0;
                            if (!RPM(sobj + 0x0208, scc) || !scc || scc < 0x10000) continue;
                            uint32_t stm = 0;
                            if (!RPM(scc + 0x0140, stm)) continue;
                            if (stm & clearMask) {
                                stm &= ~clearMask;
                                WPM(scc + 0x0140, stm);
                                patched++;
                            }
                        }
                    }
                }
            }
        }
        return patched;
    }

    // ===== CHEAT MANAGER FLAGS =====
    uintptr_t GetCheatManager() {
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) return 0;
        uintptr_t pc = 0;
        if (!RPM(pawn + 0x026C, pc) || !pc) return 0;
        uintptr_t cm = 0;
        if (!RPM(pc + 0x045C, cm) || !cm) return 0;
        return cm;
    }

    void SetCheatFlag(uintptr_t cm, int offset, uint32_t mask, bool on) {
        uint32_t val = 0;
        if (!RPM(cm + offset, val)) return;
        if (on) val |= mask; else val &= ~mask;
        WPM(cm + offset, val);
    }

    // All toggle functions try CheatManager first, but work without it too
    void ToggleCheatFlag(int offset, uint32_t mask, bool on) {
        uintptr_t cm = GetCheatManager();
        if (cm) SetCheatFlag(cm, offset, mask, on);
    }
    void ToggleGodMode(bool on)           { ToggleCheatFlag(0x60, 0x00200000, on); }
    void ToggleInfiniteAmmo(bool on)      { ToggleCheatFlag(0x60, 0x01000000, on); }
    void ToggleInfiniteAdrenaline(bool on) { ToggleCheatFlag(0x64, 0x00000001, on); ToggleCheatFlag(0x64, 0x00000002, on); }
    void ToggleAIBlind(bool on)           { ToggleCheatFlag(0x5C, 0x10000000, on); }
    void ToggleAIDeaf(bool on)            { ToggleCheatFlag(0x5C, 0x08000000, on); }
    void ToggleAIOff(bool on)             { ToggleCheatFlag(0x5C, 0x80000000, on); }
    void ToggleAutoDodge(bool on)         { ToggleCheatFlag(0x60, 0x00008000, on); }
    void ToggleBuddhaMode(bool on)        { ToggleCheatFlag(0x60, 0x02000000, on); }
    void ToggleNoKnockdown(bool on)       { ToggleCheatFlag(0x60, 0x00800000, on); }
    void ToggleForceKillCam(bool on)      { ToggleCheatFlag(0x60, 0x20000000, on); }

    // ===== PAWN VALIDATION (survives save/load) =====
    uintptr_t GetValidPawn() {
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn || pawn < 0x10000) return 0;
        // Validate pawn has a valid vtable (not a stale/freed pointer)
        uintptr_t vtable = 0;
        if (!RPM(pawn, vtable) || !vtable || vtable < 0x400000 || vtable > 0x2000000) return 0;
        // Validate maxHP is sane (a real pawn always has maxHP > 0)
        int32_t maxHp = 0;
        if (!RPM(pawn + 0x0348, maxHp) || maxHp <= 0 || maxHp > 100000) return 0;
        return pawn;
    }

    // ===== PER-TICK CHEATS =====

    void StopDarkVision() {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        uintptr_t dvComp = 0;
        if (!RPM(pawn + 0x0AB0, dvComp) || !dvComp || dvComp < 0x10000) return;
        WPM(dvComp + 0x0094, 0.f);
        WPM(dvComp + 0x0098, 0.01f);
    }

    void ForceDarkVision() {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        uintptr_t dvComp = 0;
        if (!RPM(pawn + 0x0AB0, dvComp) || !dvComp || dvComp < 0x10000) return;
        int32_t level = 0;
        RPM(dvComp + 0x0088, level);
        if (level < 2) WPM(dvComp + 0x0088, (int32_t)2);
        WPM(dvComp + 0x0094, 9999.f);
        WPM(dvComp + 0x0098, 0.f);
        // max distance via Tweaks
        uintptr_t tweaks = 0;
        if (RPM(dvComp + 0x008C, tweaks) && tweaks > 0x10000) {
            for (int li = 0; li < 2; li++) {
                uintptr_t base = tweaks + 0x015C + li * 0x38;
                WPM(base + 0x04, 99999.f);
                WPM(base + 0x08, 99999.f);
                WPM(base + 0x0C, 99999.f);
            }
        }
    }

    void InfiniteHealth() {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        int32_t maxHp = 0;
        if (!RPM(pawn + 0x0348, maxHp) || maxHp <= 0) return;
        WPM(pawn + 0x0344, maxHp);
    }

    void InfiniteMana() {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        int32_t maxMana = 0;
        if (!RPM(pawn + 0x0A64, maxMana) || maxMana <= 0) return;
        WPM(pawn + 0x0A60, maxMana);
    }

    void InfiniteAir() {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        float maxAir = 0.f;
        if (!RPMBytes(pawn + 0x0ABC, &maxAir, 4) || maxAir <= 0.f) return;
        WPMBytes(pawn + 0x0AB8, &maxAir, 4);
    }

    void InfiniteElixirs() {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        uintptr_t inv = 0;
        if (!RPM(pawn + 0x059C, inv) || !inv || inv < 0x10000) return;
        int32_t cur1 = -1, cur2 = -1;
        RPM(inv + 0x00D4, cur1);
        RPM(inv + 0x00D8, cur2);
        int32_t maxE = 10;
        if (cur1 >= 0 && cur1 <= 99) WPM(inv + 0x00D4, maxE);
        if (cur2 >= 0 && cur2 <= 99) WPM(inv + 0x00D8, maxE);
    }

    // ===== PER-TICK AMMO REFILL (no CheatManager needed) =====
    void InfiniteAmmoTick() {
        MaxAllAmmo();
        InfiniteElixirs();
    }

    // ===== SPEED HACK =====
    void SetGroundSpeed(float speed) {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        WPMBytes(pawn + 0x02F0, &speed, 4);
    }

    void SetJumpZ(float jump) {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        WPMBytes(pawn + 0x0304, &jump, 4);
    }

    void SetTimeDilation(float td) {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        WPMBytes(pawn + 0x0100, &td, 4);
    }

    // ===== NO WEAPON SPREAD =====
    void NoWeaponSpread() {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        uintptr_t weapon = 0;
        if (!RPM(pawn + 0x03C0, weapon) || !weapon || weapon < 0x10000) return;
        float cur = 0.f;
        if (!RPMBytes(weapon + 0x012C, &cur, 4)) return;
        if (cur < -100.f || cur > 100.f) return; // sanity check
        float zero = 0.f;
        WPMBytes(weapon + 0x012C, &zero, 4);
    }

    // ===== DOOR UNLOCK ALL =====
    void UnlockAllDoors() {
        for (uintptr_t obj : interactAddresses) {
            if (!obj) continue;
            uint32_t flags = 0;
            if (!RPM(obj + 0x02B0, flags)) continue;
            if (flags & 0x01) {
                flags &= ~0x01;
                WPM(obj + 0x02B0, flags);
            }
        }
        for (uintptr_t obj : doorAddresses) {
            if (!obj) continue;
            uint32_t flags = 0;
            if (!RPM(obj + 0x02B0, flags)) continue;
            if (flags & 0x01) {
                flags &= ~0x01;
                WPM(obj + 0x02B0, flags);
            }
        }
    }

    // ===== SET GRAVITY =====
    void SetGravity(float g) {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) return;
        // Pawn->WorldInfo at Actor+0x0068 (AWorldInfo*)
        uintptr_t worldInfo = 0;
        if (!RPM(pawn + 0x0068, worldInfo) || !worldInfo) return;
        WPMBytes(worldInfo + 0x0418, &g, 4); // WorldGravityZ
        WPMBytes(worldInfo + 0x0420, &g, 4); // GlobalGravityZ
    }

    // ===== BLINK RANGE =====
    int cachedBlinkPawnOff = 0;
    int cachedBlinkDistOff = 0;
    void SetBlinkRange(float range) {
        uintptr_t pawn = GetValidPawn();
        if (!pawn) { cachedBlinkPawnOff = 0; cachedBlinkDistOff = 0; return; }
        // Use cached offsets if found before
        if (cachedBlinkPawnOff && cachedBlinkDistOff) {
            uintptr_t blink = 0;
            if (RPM(pawn + cachedBlinkPawnOff, blink) && blink > 0x10000)
                WPMBytes(blink + cachedBlinkDistOff, &range, 4);
            return;
        }
        // Probe once to find correct offsets
        for (int pOff : {0x0AA8, 0x0AAC, 0x0AB0, 0x0AA4, 0x0AA0}) {
            uintptr_t blink = 0;
            if (!RPM(pawn + pOff, blink) || blink < 0x10000) continue;
            for (int dOff : {0x0090, 0x0094, 0x0088, 0x008C}) {
                float cur = 0.f;
                if (RPMBytes(blink + dOff, &cur, 4) && cur > 100.f && cur < 50000.f) {
                    cachedBlinkPawnOff = pOff;
                    cachedBlinkDistOff = dOff;
                    WPMBytes(blink + dOff, &range, 4);
                    return;
                }
            }
        }
    }

    // ===== AMMO MANIPULATION =====
    struct AmmoInfo {
        int32_t count;
        int32_t capacity;
    };

    bool GetAmmoArray(uintptr_t& outData, int& outCount) {
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) return false;
        uintptr_t inv = 0;
        if (!RPM(pawn + 0x059C, inv) || !inv) return false;
        if (!RPM(inv + 0x00BC, outData) || !outData) return false;
        if (!RPM(inv + 0x00BC + 4, outCount)) return false;
        return outCount > 0;
    }

    void MaxAllAmmo() {
        if (!GetValidPawn()) return;
        uintptr_t data = 0; int count = 0;
        if (!GetAmmoArray(data, count)) return;
        for (int i = 0; i < count && i < 16; i++) {
            AmmoInfo ai;
            if (!RPMBytes(data + i * 8, &ai, 8)) continue;
            if (ai.capacity > 0) {
                ai.count = ai.capacity;
                WPMBytes(data + i * 8, &ai, 8);
            }
        }
    }

    // ===== PLAYER STATS READ =====
    struct PlayerStats {
        int32_t health, maxHealth, mana, maxMana;
        float groundSpeed, jumpZ;
    };

    PlayerStats GetPlayerStats() {
        PlayerStats s = {};
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) return s;
        RPM(pawn + 0x0344, s.health);
        RPM(pawn + 0x0348, s.maxHealth);
        RPM(pawn + 0x0A60, s.mana);
        RPM(pawn + 0x0A64, s.maxMana);
        RPMBytes(pawn + 0x02F0, &s.groundSpeed, 4);
        RPMBytes(pawn + 0x0304, &s.jumpZ, 4);
        return s;
    }

    // ===== PROCESSVENT / CONSOLE COMMAND =====
    std::string spawnDebug;

    // Call ProcessEvent via MinHook trampoline (game-thread safe).
    // MUST be called from the game thread when hook is installed.
    void CallProcessEvent(uintptr_t obj, uintptr_t func, void* params) {
        if (!obj || !func || obj < 0x10000 || func < 0x10000) return;
        if (PEHook::g_hookInstalled && PEHook::g_originalPE) {
            PEHook::g_originalPE((void*)obj, nullptr, (void*)func, params, nullptr);
            return;
        }
        // Fallback: inline asm (unreliable calling convention, use only if hook not installed)
        LogToFile("WARNING: CallProcessEvent fallback (hook not installed) obj=0x%08X func=0x%08X", (uint32_t)obj, (uint32_t)func);
        uintptr_t vtable = 0;
        if (!RPM(obj, vtable) || !vtable || vtable < 0x10000) return;
        uintptr_t peAddr = 0;
        if (!RPM(vtable + 59 * 4, peAddr) || !peAddr || peAddr < 0x10000) return;
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)peAddr, &mbi, sizeof(mbi))) return;
        if (!(mbi.Protect & (PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE))) return;
        void* pFunc = (void*)func;
        void* pObj = (void*)obj;
        void* pPE = (void*)peAddr;
        __asm__ __volatile__ (
            "mov  %%esp, %%edi\n\t"
            "push %[parms]\n\t"
            "push %[fn]\n\t"
            "mov  %[thisptr], %%ecx\n\t"
            "call *%[pe]\n\t"
            "mov  %%edi, %%esp\n\t"
            :
            : [parms]   "g" (params),
              [fn]      "g" (pFunc),
              [thisptr] "g" (pObj),
              [pe]      "r" (pPE)
            : "eax", "edx", "ecx", "edi", "memory"
        );
    }

    // Find UFunction by name - all reads via RPM (safe)
    uintptr_t FindFunction(uintptr_t obj, const char* funcName) {
        if (!obj || obj < 0x10000) return 0;
        uintptr_t cls = 0;
        if (!RPM(obj + 0x30, cls) || !cls || cls < 0x10000) return 0;

        uintptr_t curClass = cls;
        int depth = 0;
        while (curClass && curClass > 0x10000 && depth < 30) {
            depth++;
            uintptr_t field = 0;
            if (!RPM(curClass + 0x48, field)) break;
            int fieldCount = 0;
            while (field && field > 0x10000 && fieldCount < 500) {
                fieldCount++;
                int32_t nameIdx = 0;
                if (RPM(field + 0x28, nameIdx)) {
                    const char* n = GetNameByIndex(nameIdx);
                    if (n && strcmp(n, funcName) == 0) return field;
                }
                uintptr_t next = 0;
                if (!RPM(field + 0x38, next)) break;
                field = next;
            }
            uintptr_t super = 0;
            if (!RPM(curClass + 0x44, super)) break;
            curClass = super;
        }
        return 0;
    }

    // Try to ensure CheatManager exists.
    // When called from overlay thread, queues InitCheatManager to game thread.
    // Returns current CM pointer (may be 0 if init was just queued).
    uintptr_t EnsureCheatManager() {
        uintptr_t cm = GetCheatManager();
        if (cm) return cm;
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) return 0;
        uintptr_t pc = 0;
        if (!RPM(pawn + 0x026C, pc) || !pc) return 0;
        uintptr_t func = FindFunction(pc, "InitCheatManager");
        if (!func) return 0;
        if (PEHook::IsGameThread()) {
            CallProcessEvent(pc, func, nullptr);
            RPM(pc + 0x045C, cm);
        } else if (PEHook::g_hookInstalled) {
            // Queue InitCheatManager for next game-thread tick
            PEHook::QueueCommand([pc, func]() {
                if (PEHook::g_originalPE)
                    PEHook::g_originalPE((void*)pc, nullptr, (void*)func, nullptr, nullptr);
            });
            LogToFile("EnsureCheatManager: queued InitCheatManager for game thread");
        }
        return cm;
    }

    // Execute a console command on the PlayerController.
    // Thread-safe: queues to game thread if called from overlay thread.
    bool ExecCommand(const char* cmd) {
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) { spawnDebug = "no pawn"; return false; }
        uintptr_t pc = 0;
        if (!RPM(pawn + 0x026C, pc) || !pc) { spawnDebug = "no PC"; return false; }

        static uintptr_t cachedFunc = 0;
        if (!cachedFunc) cachedFunc = FindFunction(pc, "ConsoleCommand");
        if (!cachedFunc) {
            spawnDebug = "ConsoleCommand not found";
            return false;
        }

        if (PEHook::IsGameThread()) {
            // Direct call on game thread
            int len = (int)strlen(cmd) + 1;
            if (len > 511) len = 511;
            wchar_t wcmd[512];
            for (int i = 0; i < len; i++) wcmd[i] = (wchar_t)cmd[i];
            struct {
                wchar_t* Data; int32_t Count; int32_t Max;
                int32_t bWriteToLog;
                wchar_t* RetData; int32_t RetCount; int32_t RetMax;
            } params;
            params.Data = wcmd; params.Count = len; params.Max = len;
            params.bWriteToLog = 0;
            params.RetData = nullptr; params.RetCount = 0; params.RetMax = 0;
            CallProcessEvent(pc, cachedFunc, &params);
        } else if (PEHook::g_hookInstalled && PEHook::g_originalPE) {
            // Queue to game thread - capture command string by value
            std::string cmdCopy(cmd);
            uintptr_t func = cachedFunc;
            PEHook::QueueCommand([pc, func, cmdCopy]() {
                int len = (int)cmdCopy.size() + 1;
                if (len > 511) len = 511;
                wchar_t wcmd[512];
                for (int i = 0; i < len; i++) wcmd[i] = (wchar_t)cmdCopy[i];
                struct {
                    wchar_t* Data; int32_t Count; int32_t Max;
                    int32_t bWriteToLog;
                    wchar_t* RetData; int32_t RetCount; int32_t RetMax;
                } params;
                params.Data = wcmd; params.Count = len; params.Max = len;
                params.bWriteToLog = 0;
                params.RetData = nullptr; params.RetCount = 0; params.RetMax = 0;
                PEHook::g_originalPE((void*)pc, nullptr, (void*)func, &params, nullptr);
                LogToFile("ExecCommand (queued): '%s' executed on game thread", cmdCopy.c_str());
            });
            spawnDebug = "exec queued to game thread";
        } else {
            spawnDebug = "hook not installed, cannot exec";
            return false;
        }
        return true;
    }

    // ===== CHEAT MANAGER COMMANDS =====
    // Helper: queue a no-param CM command to game thread
    void QueueCMCommand(const char* funcName, const char* label) {
        uintptr_t cm = GetCheatManager();
        uintptr_t pawn = 0;
        RPM(Off::LocalPawn, pawn);
        uintptr_t pc = 0;
        if (pawn) RPM(pawn + 0x026C, pc);

        // Find function on CM (or init CM first)
        uintptr_t func = 0;
        if (cm) func = FindFunction(cm, funcName);

        if (PEHook::g_hookInstalled && PEHook::g_originalPE) {
            std::string fn(funcName), lbl(label);
            PEHook::QueueCommand([pc, cm, func, fn, lbl]() {
                uintptr_t theCM = cm;
                // If no CM, try InitCheatManager first
                if (!theCM && pc) {
                    // Walk class fields to find InitCheatManager
                    uintptr_t cls = 0;
                    if (*(uintptr_t*)(pc + 0x30)) cls = *(uintptr_t*)(pc + 0x30);
                    uintptr_t curClass = cls;
                    uintptr_t initFunc = 0;
                    for (int d = 0; curClass && curClass > 0x10000 && d < 30; d++) {
                        uintptr_t field = *(uintptr_t*)(curClass + 0x48);
                        for (int fc = 0; field && field > 0x10000 && fc < 500; fc++) {
                            int32_t ni = *(int32_t*)(field + 0x28);
                            uintptr_t namesArr = *(uintptr_t*)(Off::GNames);
                            if (namesArr && ni >= 0) {
                                uintptr_t entry = *(uintptr_t*)(namesArr + ni * 4);
                                if (entry && strcmp((const char*)(entry + 0x10), "InitCheatManager") == 0) {
                                    initFunc = field; break;
                                }
                            }
                            field = *(uintptr_t*)(field + 0x38);
                        }
                        if (initFunc) break;
                        curClass = *(uintptr_t*)(curClass + 0x44);
                    }
                    if (initFunc) {
                        PEHook::g_originalPE((void*)pc, nullptr, (void*)initFunc, nullptr, nullptr);
                        theCM = *(uintptr_t*)(pc + 0x045C);
                    }
                }
                if (!theCM) { LogToFile("CM cmd '%s': no CheatManager", fn.c_str()); return; }
                // Find the function on CM (direct memory reads on game thread)
                uintptr_t theFunc = func;
                if (!theFunc) {
                    uintptr_t cls = *(uintptr_t*)(theCM + 0x30);
                    uintptr_t cur = cls;
                    for (int d = 0; cur && cur > 0x10000 && d < 30; d++) {
                        uintptr_t field = *(uintptr_t*)(cur + 0x48);
                        for (int fc = 0; field && field > 0x10000 && fc < 500; fc++) {
                            int32_t ni = *(int32_t*)(field + 0x28);
                            uintptr_t namesArr = *(uintptr_t*)(Off::GNames);
                            if (namesArr && ni >= 0) {
                                uintptr_t entry = *(uintptr_t*)(namesArr + ni * 4);
                                if (entry && strcmp((const char*)(entry + 0x10), fn.c_str()) == 0) {
                                    theFunc = field; break;
                                }
                            }
                            field = *(uintptr_t*)(field + 0x38);
                        }
                        if (theFunc) break;
                        cur = *(uintptr_t*)(cur + 0x44);
                    }
                }
                if (!theFunc) { LogToFile("CM cmd '%s': func not found", fn.c_str()); return; }
                PEHook::g_originalPE((void*)theCM, nullptr, (void*)theFunc, nullptr, nullptr);
                LogToFile("CM cmd '%s': executed on game thread", fn.c_str());
            });
            spawnDebug = std::string(label) + " queued";
        } else {
            spawnDebug = "hook not installed";
        }
    }

    void CheatMaxPowers() {
        if (PEHook::g_hookInstalled) {
            QueueCMCommand("MaxPowers", "MaxPowers");
        } else {
            MaxPowerLevelsDirect();
            spawnDebug = "MaxPowers via memory (no hook)";
        }
    }

    void CheatMaxUpgrades() {
        QueueCMCommand("MaxUpgrades", "MaxUpgrades");
    }

    void CheatGiveMoney(int32_t amount) {
        if (!PEHook::g_hookInstalled || !PEHook::g_originalPE) {
            spawnDebug = "hook not installed - money needs CM"; return;
        }
        uintptr_t cm = GetCheatManager();
        uintptr_t pawn = 0; RPM(Off::LocalPawn, pawn);
        uintptr_t pc = 0; if (pawn) RPM(pawn + 0x026C, pc);
        uintptr_t func = cm ? FindFunction(cm, "GiveMoney") : 0;

        PEHook::QueueCommand([pc, cm, func, amount]() {
            uintptr_t theCM = cm;
            if (!theCM && pc) {
                // InitCheatManager inline
                uintptr_t cls = *(uintptr_t*)(pc + 0x30);
                uintptr_t cur = cls;
                for (int d = 0; cur && cur > 0x10000 && d < 30; d++) {
                    uintptr_t field = *(uintptr_t*)(cur + 0x48);
                    for (int fc = 0; field && field > 0x10000 && fc < 500; fc++) {
                        int32_t ni = *(int32_t*)(field + 0x28);
                        uintptr_t namesArr = *(uintptr_t*)(Game::Off::GNames);
                        if (namesArr && ni >= 0) {
                            uintptr_t entry = *(uintptr_t*)(namesArr + ni * 4);
                            if (entry && strcmp((const char*)(entry + 0x10), "InitCheatManager") == 0) {
                                PEHook::g_originalPE((void*)pc, nullptr, (void*)field, nullptr, nullptr);
                                break;
                            }
                        }
                        field = *(uintptr_t*)(field + 0x38);
                    }
                    cur = *(uintptr_t*)(cur + 0x44);
                }
                theCM = *(uintptr_t*)(pc + 0x045C);
            }
            if (!theCM) { LogToFile("GiveMoney: no CM"); return; }
            // Find GiveMoney
            uintptr_t theFunc = func;
            if (!theFunc) {
                uintptr_t cls = *(uintptr_t*)(theCM + 0x30);
                uintptr_t cur = cls;
                for (int d = 0; cur && cur > 0x10000 && d < 30; d++) {
                    uintptr_t field = *(uintptr_t*)(cur + 0x48);
                    for (int fc = 0; field && field > 0x10000 && fc < 500; fc++) {
                        int32_t ni = *(int32_t*)(field + 0x28);
                        uintptr_t namesArr = *(uintptr_t*)(Game::Off::GNames);
                        if (namesArr && ni >= 0) {
                            uintptr_t entry = *(uintptr_t*)(namesArr + ni * 4);
                            if (entry && strcmp((const char*)(entry + 0x10), "GiveMoney") == 0) {
                                theFunc = field; break;
                            }
                        }
                        field = *(uintptr_t*)(field + 0x38);
                    }
                    if (theFunc) break;
                    cur = *(uintptr_t*)(cur + 0x44);
                }
            }
            if (!theFunc) { LogToFile("GiveMoney: func not found"); return; }
            struct { int32_t Count; } params = { amount };
            PEHook::g_originalPE((void*)theCM, nullptr, (void*)theFunc, &params, nullptr);
            LogToFile("GiveMoney: %d given on game thread", amount);
        });
        spawnDebug = "GiveMoney queued";
    }

    void CheatGiveBoneCharm() {
        QueueCMCommand("GiveBoneCharm", "GiveBoneCharm");
    }

    // Spawn at crosshair position (queued to game thread via ExecCommand)
    void SpawnAtCrosshair(const char* className, const Vec3& camPos, const Rotator& camRot) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "summon %s", className);
        ExecCommand(cmd);
    }

    void MaxPowerLevelsDirect() {
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) { spawnDebug = "no pawn"; return; }
        uintptr_t powComp = 0;
        // Try multiple offsets for PowersComponent
        for (int pcOff : {0x0A38, 0x0A3C, 0x0A40, 0x0A44}) {
            if (RPM(pawn + pcOff, powComp) && powComp && powComp > 0x10000) break;
            powComp = 0;
        }
        if (!powComp) { spawnDebug = "no PowersComponent"; return; }
        // Try to find the TArray of powers
        uintptr_t powData = 0; int powCount = 0;
        for (int arrOff = 0x48; arrOff <= 0x68; arrOff += 4) {
            uintptr_t d = 0; int c = 0;
            if (RPM(powComp + arrOff, d) && d > 0x10000 && RPM(powComp + arrOff + 4, c) && c > 0 && c < 30) {
                powData = d; powCount = c;
                break;
            }
        }
        if (!powData) { spawnDebug = "no power array found"; return; }
        // Try different struct sizes and level offsets
        for (int stride : {0x1C, 0x20, 0x24, 0x28, 0x18}) {
            for (int lvlOff : {0x18, 0x14, 0x10, 0x1C}) {
                if (lvlOff >= stride) continue;
                // Validate: read first entry's level
                int32_t testVal = -1;
                if (RPM(powData + lvlOff, testVal) && testVal >= 0 && testVal <= 10) {
                    for (int i = 0; i < powCount; i++) {
                        int32_t maxLvl = 2;
                        WPM((uintptr_t)(powData + i * stride + lvlOff), maxLvl);
                    }
                    char dbg[128];
                    snprintf(dbg, sizeof(dbg), "powers maxed: %d entries, stride=0x%X lvl@0x%X", powCount, stride, lvlOff);
                    spawnDebug = dbg;
                    return;
                }
            }
        }
        spawnDebug = "could not determine power struct layout";
    }

    // Debug info about CheatManager state
    std::string GetCheatDebugInfo() {
        char buf[512];
        uintptr_t pawn = 0;
        if (!RPM(Off::LocalPawn, pawn) || !pawn) return "no pawn";
        uintptr_t pc = 0;
        if (!RPM(pawn + 0x026C, pc) || !pc) return "no PC";
        uintptr_t cm = 0;
        RPM(pc + 0x045C, cm);
        uintptr_t cmClass = 0;
        RPM(pc + 0x0460, cmClass);

        uintptr_t ccFunc = FindFunction(pc, "ConsoleCommand");
        uintptr_t cmSummon = 0;
        if (cm) cmSummon = FindFunction(cm, "Summon");

        snprintf(buf, sizeof(buf), "PC:0x%08X CM:0x%08X CMClass:0x%08X\nConsoleCmd:0x%08X Summon:0x%08X",
            (uint32_t)pc, (uint32_t)cm, (uint32_t)cmClass, (uint32_t)ccFunc, (uint32_t)cmSummon);
        return buf;
    }

    // ===== SCAN SPAWNABLE CLASSES (from GObjects) =====
    struct SpawnableItem {
        std::string name;
        std::string className;
    };

    std::vector<SpawnableItem> GetSpawnableList() {
        std::vector<SpawnableItem> result;
        uintptr_t objData = 0; int objCount = 0;
        if (!RPM(Off::GObjects, objData) || !objData) return result;
        if (!RPM(Off::GObjects + 4, objCount) || objCount <= 0) return result;

        constexpr int CHUNK = 512;
        uintptr_t chunk[CHUNK];

        for (int base = 0; base < objCount && base < 80000; base += CHUNK) {
            int n = (objCount - base < CHUNK) ? (objCount - base) : CHUNK;
            if (!RPMBytes(objData + base * 4, chunk, n * 4)) continue;
            for (int j = 0; j < n; j++) {
                uintptr_t obj = chunk[j];
                if (!obj || obj < 0x10000) continue;
                int32_t nameIdx = 0;
                if (!RPM(obj + 0x28, nameIdx)) continue;
                const char* name = GetNameByIndex(nameIdx);
                if (!name || strncmp(name, "Default__", 9) != 0) continue;
                uintptr_t cls = 0;
                if (!RPM(obj + 0x30, cls) || !cls) continue;
                int32_t clsIdx = 0;
                if (!RPM(cls + 0x28, clsIdx)) continue;
                const char* clsName = GetNameByIndex(clsIdx);
                if (!clsName) continue;
                if (strstr(clsName, "Pickup") || strstr(clsName, "Pawn") ||
                    strstr(clsName, "Weapon") || strstr(clsName, "Rune") ||
                    strstr(clsName, "BoneCharm") || strstr(clsName, "Elixir") ||
                    strstr(clsName, "Key") || strstr(clsName, "Ammo") ||
                    strstr(clsName, "Rat") || strstr(clsName, "Guard") ||
                    strstr(clsName, "Assassin") || strstr(clsName, "Tallboy") ||
                    strstr(clsName, "Weeper") || strstr(clsName, "Grenade") ||
                    strstr(clsName, "Mine") || strstr(clsName, "Bolt") ||
                    strstr(clsName, "Whale") || strstr(clsName, "Coin") ||
                    strstr(clsName, "Gold") || strstr(clsName, "Door") ||
                    strstr(clsName, "Wire") || strstr(clsName, "Trap") ||
                    strstr(clsName, "Bullet") || strstr(clsName, "Arrow") ||
                    strstr(clsName, "Item") || strstr(clsName, "Hound") ||
                    strstr(clsName, "Daud")) {
                    if (strstr(clsName, "Tweaks") || strstr(clsName, "Factory") ||
                        strstr(clsName, "Info") || strstr(clsName, "Component") ||
                        strstr(clsName, "Seq") || strstr(clsName, "Rule")) continue;

                    SpawnableItem si;
                    si.className = clsName;
                    uintptr_t outer = 0;
                    if (RPM(cls + 0x24, outer) && outer) {
                        int32_t outerIdx = 0;
                        if (RPM(outer + 0x28, outerIdx)) {
                            const char* pkg = GetNameByIndex(outerIdx);
                            if (pkg) si.name = std::string(pkg) + "." + clsName;
                        }
                    }
                    if (si.name.empty()) si.name = clsName;
                    bool dup = false;
                    for (auto& r : result) if (r.name == si.name) { dup = true; break; }
                    if (!dup) result.push_back(si);
                }
            }
        }
        return result;
    }

    void NoclipMove(const Rotator& camRot, float fwd, float right, float up, float speed) {
        uintptr_t localPawn = 0;
        if (!RPM(Off::LocalPawn, localPawn) || !localPawn) return;

        const float PI = 3.14159265358979f;
        float pitch = (float)camRot.Pitch * (PI / 32768.0f);
        float yaw   = (float)camRot.Yaw   * (PI / 32768.0f);

        float cp = cosf(pitch), sp = sinf(pitch);
        float cy = cosf(yaw),   sy = sinf(yaw);

        Vec3 f = {cp * cy, cp * sy, sp};
        Vec3 r = {-sy, cy, 0.f};

        Vec3 loc;
        if (!RPMBytes(localPawn + Off::ActorLocation, &loc, 12)) return;

        loc.X += (f.X * fwd + r.X * right) * speed;
        loc.Y += (f.Y * fwd + r.Y * right) * speed;
        loc.Z += (f.Z * fwd + up) * speed;

        WPMBytes(localPawn + Off::ActorLocation, &loc, 12);

        Vec3 zero = {0.f, 0.f, 0.f};
        WPMBytes(localPawn + 0x01B4, &zero, 12);
    }

};

} // namespace Game
