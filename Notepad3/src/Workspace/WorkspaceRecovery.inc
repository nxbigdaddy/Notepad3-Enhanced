// UI-thread capture; immutable snapshots are encrypted and persisted off-thread.
#include <process.h>
#define WORKSPACE_RECOVERY_TIMER 0x7353
typedef struct WORKSPACE_RECOVERY_JOB {
    NP3_SESSION session;
    HPATHL path;
    HANDLE thread;
    uint32_t error;
    bool success;
} WORKSPACE_RECOVERY_JOB;
typedef struct WORKSPACE_RECOVERY {
    HPATHL directory, file, lockPath, closedPath;
    HANDLE lock;
    WORKSPACE_RECOVERY_JOB* job;
} WORKSPACE_RECOVERY;
static unsigned __stdcall _WorkspaceRecoveryWorker(void* argument)
{
    WORKSPACE_RECOVERY_JOB* job = argument;
    job->success = Session_Save(job->session, Path_Get(job->path), &job->error) != 0;
    return 0;
}
static bool _WorkspaceRecoveryHarvest(WORKSPACE_RECOVERY* recovery, DWORD timeout)
{
    WORKSPACE_RECOVERY_JOB* job = recovery->job;
    if (!job) return true;
    if (WaitForSingleObject(job->thread, timeout) != WAIT_OBJECT_0) return false;
    if (!job->success) {
        WCHAR message[128];
        StringCchPrintfW(message, COUNTOF(message), L"Notepad3 recovery backup failed: %lu\n", (unsigned long)job->error);
        OutputDebugStringW(message);
    }
    CloseHandle(job->thread); Session_Free(job->session); Path_Release(job->path);
    FreeMem(job); recovery->job = NULL;
    return true;
}
static VOID CALLBACK _WorkspaceRecoveryTimer(HWND hwnd, UINT message, UINT_PTR timer, DWORD time)
{
    UNREFERENCED_PARAMETER(hwnd); UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(timer); UNREFERENCED_PARAMETER(time);
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || !workspace->recovery || workspace->transitioning) return;
    WORKSPACE_RECOVERY* recovery = workspace->recovery;
    if (!_WorkspaceRecoveryHarvest(recovery, 0)) return;
    uint32_t error = 0;
    NP3_SESSION session = _WorkspaceSessionCapture(&error);
    if (!session) return;
    WORKSPACE_RECOVERY_JOB* job = AllocMem(sizeof(*job), HEAP_ZERO_MEMORY);
    if (!job) { Session_Free(session); return; }
    job->session = session; job->path = Path_Copy(recovery->file);
    if (job->path) job->thread = (HANDLE)_beginthreadex(NULL, 0, _WorkspaceRecoveryWorker, job, 0, NULL);
    if (!job->thread) { Session_Free(session); Path_Release(job->path); FreeMem(job); return; }
    recovery->job = job;
}
static void _WorkspaceRecoveryClose()
{
    DOCUMENT_WORKSPACE* workspace = _Workspace();
    if (!workspace || !workspace->recovery) return;
    WORKSPACE_RECOVERY* recovery = workspace->recovery;
    KillTimer(Globals.hwndMain, WORKSPACE_RECOVERY_TIMER);
    // The marker is created before releasing ownership. An unfinished worker
    // cannot resurrect documents the user explicitly discarded on normal exit.
    HANDLE marker = Path_CreateFile(recovery->closedPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
    bool const marked = IS_VALID_HANDLE(marker);
    if (marked) { FlushFileBuffers(marker); CloseHandle(marker); }
    bool const finished = _WorkspaceRecoveryHarvest(recovery, marked ? 5000 : INFINITE);
    bool const removed = finished && (!Path_IsExistingFile(recovery->file) || Path_DeleteFile(recovery->file));
    CloseHandle(recovery->lock);
    if (removed) {
        Path_DeleteFile(recovery->lockPath);
        Path_DeleteFile(recovery->closedPath);
    }
    // On timeout the job is retained until process termination; its worker only
    // references this immutable job, never the workspace or editor window.
    Path_Release(recovery->directory); Path_Release(recovery->file);
    Path_Release(recovery->lockPath); Path_Release(recovery->closedPath);
    FreeMem(recovery); workspace->recovery = NULL;
}
static void _WorkspaceRecoveryStart()
{
    if (GetEnvironmentVariableW(L"NP3_WORKSPACE_SELFTEST_DIR", NULL, 0)) return;
    if (!_WorkspaceEnsure() || _Workspace()->recovery) return;
    WCHAR root[MAX_PATH_EXPLICIT];
    DWORD length = GetEnvironmentVariableW(L"NP3_RECOVERY_TEST_DIR", root, COUNTOF(root));
    HPATHL directory = NULL;
    if (length) {
        if (length >= COUNTOF(root)) return;
        directory = Path_Allocate(root);
        HPATHL marker = Path_Copy(directory); Path_Append(marker, L"recovery-test.marker");
        bool const allowed = Path_IsExistingFile(marker); Path_Release(marker);
        if (!allowed) { Path_Release(directory); return; }
    } else {
        PWSTR appData = NULL;
        if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_CREATE, NULL, &appData))) return;
        directory = Path_Allocate(appData); CoTaskMemFree(appData);
        Path_Append(directory, L"Notepad3");
        HRESULT const created = Path_CreateDirectoryEx(directory);
        if (FAILED(created) && created != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) { Path_Release(directory); return; }
        Path_Append(directory, L"Recovery");
    }
    HRESULT const created = Path_CreateDirectoryEx(directory);
    if (FAILED(created) && created != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) { Path_Release(directory); return; }
    WORKSPACE_RECOVERY* recovery = AllocMem(sizeof(*recovery), HEAP_ZERO_MEMORY);
    if (!recovery) { Path_Release(directory); return; }
    FILETIME stamp; GetSystemTimeAsFileTime(&stamp);
    WCHAR leaf[128];
    StringCchPrintfW(leaf, COUNTOF(leaf), L"np3-%lu-%08lx%08lx.np3recovery", GetCurrentProcessId(), stamp.dwHighDateTime, stamp.dwLowDateTime);
    recovery->directory = directory; recovery->file = Path_Copy(directory); Path_Append(recovery->file, leaf);
    recovery->lockPath = Path_Copy(recovery->file); Path_RenameExtension(recovery->lockPath, L".lock");
    recovery->closedPath = Path_Copy(recovery->file); Path_RenameExtension(recovery->closedPath, L".closed");
    recovery->lock = Path_CreateFile(recovery->lockPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (!IS_VALID_HANDLE(recovery->lock)) {
        Path_Release(directory); Path_Release(recovery->file); Path_Release(recovery->lockPath); Path_Release(recovery->closedPath); FreeMem(recovery); return;
    }
    _Workspace()->recovery = recovery;
    HPATHL pattern = Path_Copy(directory); Path_Append(pattern, L"np3-*.np3recovery");
    WIN32_FIND_DATAW found; HANDLE search = Path_FindFirstFile(pattern, &found); Path_Release(pattern);
    if (IS_VALID_HANDLE(search)) {
        do {
            if (found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            HPATHL source = Path_Copy(directory); Path_Append(source, found.cFileName);
            HPATHL lockPath = Path_Copy(source); Path_RenameExtension(lockPath, L".lock");
            HPATHL closed = Path_Copy(source); Path_RenameExtension(closed, L".closed");
            HANDLE claim = Path_CreateFile(lockPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (IS_VALID_HANDLE(claim)) {
                uint32_t error = 0;
                bool const discarded = Path_IsExistingFile(closed);
                bool const transferred = !discarded && _WorkspaceSessionLoad(source, &error) && _WorkspaceSessionSave(recovery->file, &error);
                bool const removed = (discarded || transferred) && Path_DeleteFile(source);
                CloseHandle(claim);
                if (removed) { Path_DeleteFile(lockPath); Path_DeleteFile(closed); }
            }
            Path_Release(source); Path_Release(lockPath); Path_Release(closed);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    SetTimer(Globals.hwndMain, WORKSPACE_RECOVERY_TIMER, 30000, _WorkspaceRecoveryTimer);
    _WorkspaceRecoveryTimer(NULL, 0, 0, 0);
}
