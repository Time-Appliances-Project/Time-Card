/* SPDX-License-Identifier: BSD-3-Clause */
/* W32Time input provider backed by the native oscillatord sample bridge. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeprov.h>
#include <stdint.h>

#define TC_TIME_MAP_NAME L"Global\\OcpTimeCard.TimeSample.v1"
#define TC_TIME_MAGIC 0x5450434fu
#define TC_TIME_VERSION 1u
#define TC_TIME_SIZE 128u
#define TC_TIME_FLAG_PRESENT      (1u << 0)
#define TC_TIME_FLAG_SYNCHRONIZED (1u << 1)
#define TC_SAMPLE_MAX_AGE_MS 5000ull
#define TC_TIME_JUMP_SETTLE_MS 1000ull
#define TC_MAX_OFFSET_100NS (86400ll * 10000000ll)
#define TC_MAX_UNCERTAINTY_100NS (10ull * 10000000ull)
#define TC_REFID ((DWORD)'O' << 24 | (DWORD)'C' << 16 | (DWORD)'P' << 8)
#define TC_REQUIRED_CALLBACK_SIZE ((DWORD)(FIELD_OFFSET( \
    TimeProvSysCallbacks, pfnAlertSamplesAvail) + sizeof( \
    ((TimeProvSysCallbacks *)0)->pfnAlertSamplesAvail)))

#pragma pack(push, 1)
typedef struct _TC_SHARED_TIME_SAMPLE {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Size;
    volatile uint32_t Sequence;
    uint32_t Flags;
    uint32_t Reserved0;
    int64_t Offset100ns;
    int64_t Delay100ns;
    uint64_t Dispersion100ns;
    uint64_t SystemTickMilliseconds;
    uint64_t CardTimeFileTime100ns;
    uint64_t SystemMidpointFileTime100ns;
    uint64_t UpdatedFileTime100ns;
    uint64_t Reserved[6];
} TC_SHARED_TIME_SAMPLE;
#pragma pack(pop)

typedef struct _TC_PROVIDER_CONTEXT {
    TimeProvSysCallbacks Callbacks;
    HANDLE StopEvent;
    HANDLE Worker;
    volatile LONG Closing;
    volatile LONG64 RejectSamplesThroughTick;
} TC_PROVIDER_CONTEXT;

/*
 * TimeProvClose may overlap an already-dispatched TimeProvCommand.  This
 * process-wide rundown lock prevents the provider context from being freed
 * until such commands have returned.  Close joins the alert worker before it
 * takes the exclusive side, because AlertSamplesAvail may synchronously cause
 * W32Time to issue TPC_GetSamples on that worker thread.
 */
static SRWLOCK TcProviderRundownLock = SRWLOCK_INIT;

static BOOL
tc_read_shared_sample(TC_SHARED_TIME_SAMPLE *sample)
{
    HANDLE mapping;
    const TC_SHARED_TIME_SAMPLE *shared;
    uint32_t before;
    uint32_t after;
    unsigned int attempt;
    BOOL valid = FALSE;

    if (sample == NULL)
        return FALSE;
    mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, TC_TIME_MAP_NAME);
    if (mapping == NULL)
        return FALSE;
    shared = (const TC_SHARED_TIME_SAMPLE *)MapViewOfFile(mapping,
        FILE_MAP_READ, 0, 0, sizeof(*sample));
    if (shared != NULL) {
        for (attempt = 0; attempt < 4u; ++attempt) {
            before = shared->Sequence;
            MemoryBarrier();
            if ((before & 1u) != 0u) {
                SwitchToThread();
                continue;
            }
            CopyMemory(sample, shared, sizeof(*sample));
            MemoryBarrier();
            after = shared->Sequence;
            if (before == after && (after & 1u) == 0u) {
                valid = TRUE;
                break;
            }
        }
        UnmapViewOfFile(shared);
    }
    CloseHandle(mapping);
    return valid;
}

static BOOL
tc_sample_is_usable(TC_PROVIDER_CONTEXT *context,
                     const TC_SHARED_TIME_SAMPLE *bridge,
                     ULONGLONG *tick_count, LONGLONG *phase_offset)
{
    HRESULT result;
    ULONGLONG bridge_age_clock;
    LONGLONG reject_through;

    if (context == NULL || bridge == NULL || tick_count == NULL ||
        phase_offset == NULL ||
        bridge->Magic != TC_TIME_MAGIC ||
        bridge->Version != TC_TIME_VERSION ||
        bridge->Size != TC_TIME_SIZE ||
        (bridge->Flags & (TC_TIME_FLAG_PRESENT |
                          TC_TIME_FLAG_SYNCHRONIZED)) !=
            (TC_TIME_FLAG_PRESENT | TC_TIME_FLAG_SYNCHRONIZED) ||
        bridge->Offset100ns < -TC_MAX_OFFSET_100NS ||
        bridge->Offset100ns > TC_MAX_OFFSET_100NS ||
        bridge->Delay100ns < 0 ||
        (uint64_t)bridge->Delay100ns > TC_MAX_UNCERTAINTY_100NS ||
        bridge->Dispersion100ns == 0u ||
        bridge->Dispersion100ns > TC_MAX_UNCERTAINTY_100NS)
        return FALSE;
    if (context->Callbacks.pfnGetTimeSysInfo == NULL)
        return FALSE;
    bridge_age_clock = GetTickCount64();
    if (bridge_age_clock < bridge->SystemTickMilliseconds ||
        bridge_age_clock - bridge->SystemTickMilliseconds >
            TC_SAMPLE_MAX_AGE_MS)
        return FALSE;
    reject_through = InterlockedCompareExchange64(
        &context->RejectSamplesThroughTick, 0, 0);
    if (reject_through > 0) {
        /*
         * TPC_TimeJumped invalidates the offset-to-system-time association.
         * Accept only a bridge generation published after a short settling
         * interval, then clear exactly the barrier that was observed.  The
         * compare/exchange cannot erase a newer concurrent time-jump barrier.
         */
        if (bridge->SystemTickMilliseconds <= (uint64_t)reject_through)
            return FALSE;
        (VOID)InterlockedCompareExchange64(
            &context->RejectSamplesThroughTick, 0, reject_through);
    }
    result = context->Callbacks.pfnGetTimeSysInfo(TSI_TickCount,
                                                   tick_count);
    if (FAILED(result))
        return FALSE;
    result = context->Callbacks.pfnGetTimeSysInfo(TSI_PhaseOffset,
                                                   phase_offset);
    return SUCCEEDED(result);
}

static HRESULT
tc_get_samples(TC_PROVIDER_CONTEXT *context, TpcGetSamplesArgs *arguments)
{
    TC_SHARED_TIME_SAMPLE bridge;
    TimeSample sample;
    ULONGLONG tick_count = 0;
    LONGLONG phase_offset = 0;

    if (arguments == NULL)
        return E_INVALIDARG;
    arguments->dwSamplesReturned = 0;
    arguments->dwSamplesAvailable = 0;
    ZeroMemory(&bridge, sizeof(bridge));
    if (!tc_read_shared_sample(&bridge) ||
        !tc_sample_is_usable(context, &bridge, &tick_count, &phase_offset))
        return S_OK;

    arguments->dwSamplesAvailable = 1;
    if (arguments->pbSampleBuf == NULL ||
        arguments->cbSampleBuf < sizeof(sample))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    ZeroMemory(&sample, sizeof(sample));
    sample.dwSize = sizeof(sample);
    sample.dwRefid = TC_REFID;
    /* W32Time offsets follow the NTP convention: reference minus local. */
    sample.toOffset = bridge.Offset100ns;
    sample.toDelay = bridge.Delay100ns;
    sample.tpDispersion = bridge.Dispersion100ns;
    sample.nSysTickCount = tick_count;
    sample.nSysPhaseOffset = phase_offset;
    sample.nLeapFlags = 0;
    sample.nStratum = 0;
    sample.dwTSFlags = TSF_Hardware;
    lstrcpynW(sample.wszUniqueName,
        L"OCP Time Card PHC (native oscillatord)",
        ARRAYSIZE(sample.wszUniqueName));
    CopyMemory(arguments->pbSampleBuf, &sample, sizeof(sample));
    arguments->dwSamplesReturned = 1;
    return S_OK;
}

static DWORD WINAPI
tc_alert_worker(LPVOID parameter)
{
    TC_PROVIDER_CONTEXT *context = (TC_PROVIDER_CONTEXT *)parameter;
    uint32_t previous_sequence = 0u;

    while (WaitForSingleObject(context->StopEvent, 500) == WAIT_TIMEOUT) {
        TC_SHARED_TIME_SAMPLE bridge;
        ZeroMemory(&bridge, sizeof(bridge));
        if (tc_read_shared_sample(&bridge) &&
            bridge.Sequence != previous_sequence &&
            (bridge.Sequence & 1u) == 0u &&
            (bridge.Flags & (TC_TIME_FLAG_PRESENT |
                             TC_TIME_FLAG_SYNCHRONIZED)) ==
                (TC_TIME_FLAG_PRESENT | TC_TIME_FLAG_SYNCHRONIZED)) {
            previous_sequence = bridge.Sequence;
            if (context->Callbacks.pfnAlertSamplesAvail != NULL)
                context->Callbacks.pfnAlertSamplesAvail();
        }
    }
    return 0;
}

HRESULT __stdcall
TimeProvOpen(PWSTR name, TimeProvSysCallbacks *callbacks,
             TimeProvHandle *provider)
{
    TC_PROVIDER_CONTEXT *context;
    DWORD copy_size;

    if (name == NULL || callbacks == NULL || provider == NULL)
        return E_INVALIDARG;
    UNREFERENCED_PARAMETER(name);
    *provider = NULL;
    if (callbacks->dwSize < TC_REQUIRED_CALLBACK_SIZE ||
        callbacks->pfnGetTimeSysInfo == NULL ||
        callbacks->pfnAlertSamplesAvail == NULL)
        return E_INVALIDARG;
    context = (TC_PROVIDER_CONTEXT *)HeapAlloc(GetProcessHeap(),
        HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL)
        return E_OUTOFMEMORY;
    copy_size = callbacks->dwSize;
    if (copy_size > sizeof(context->Callbacks))
        copy_size = sizeof(context->Callbacks);
    CopyMemory(&context->Callbacks, callbacks, copy_size);
    context->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (context->StopEvent == NULL) {
        HeapFree(GetProcessHeap(), 0, context);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    context->Worker = CreateThread(NULL, 0, tc_alert_worker, context, 0, NULL);
    if (context->Worker == NULL) {
        CloseHandle(context->StopEvent);
        HeapFree(GetProcessHeap(), 0, context);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *provider = context;
    return S_OK;
}

HRESULT __stdcall
TimeProvCommand(TimeProvHandle provider, TimeProvCmd command,
                TimeProvArgs arguments)
{
    TC_PROVIDER_CONTEXT *context = (TC_PROVIDER_CONTEXT *)provider;
    HRESULT result = S_OK;

    AcquireSRWLockShared(&TcProviderRundownLock);
    if (context == NULL || InterlockedCompareExchange(
        &context->Closing, 0, 0) != 0) {
        result = E_HANDLE;
    } else {
        switch (command) {
        case TPC_GetSamples:
            result = tc_get_samples(context,
                                    (TpcGetSamplesArgs *)arguments);
            break;
        case TPC_Shutdown:
            SetEvent(context->StopEvent);
            break;
        case TPC_TimeJumped:
            /*
             * A system clock step invalidates every offset measured against
             * the old system-time epoch.  Microsoft's provider contract calls
             * for discarding cached samples here; the monotonic barrier also
             * rejects a publication already in flight when the command lands.
             */
            InterlockedExchange64(&context->RejectSamplesThroughTick,
                (LONGLONG)(GetTickCount64() + TC_TIME_JUMP_SETTLE_MS));
            break;
        case TPC_UpdateConfig:
        case TPC_PollIntervalChanged:
        case TPC_NetTopoChange:
        case TPC_Query:
            break;
        default:
            result = E_INVALIDARG;
            break;
        }
    }
    ReleaseSRWLockShared(&TcProviderRundownLock);
    return result;
}

HRESULT __stdcall
TimeProvClose(TimeProvHandle provider)
{
    TC_PROVIDER_CONTEXT *context = (TC_PROVIDER_CONTEXT *)provider;
    DWORD wait_result;

    if (context == NULL)
        return E_HANDLE;
    if (InterlockedCompareExchange(&context->Closing, 1, 0) != 0)
        return E_HANDLE;
    SetEvent(context->StopEvent);
    if (context->Worker != NULL) {
        /* The worker dereferences context and can enter the W32Time callback.
         * Prove it has returned before closing handles or freeing context;
         * a timeout followed by cleanup would be a use-after-free.  The
         * worker has no unbounded operation and the stop event wakes its only
         * wait, so an infinite join is the safe close contract here. */
        wait_result = WaitForSingleObject(context->Worker, INFINITE);
        if (wait_result != WAIT_OBJECT_0)
            return HRESULT_FROM_WIN32(GetLastError());
    }

    AcquireSRWLockExclusive(&TcProviderRundownLock);
    if (context->Worker != NULL)
        CloseHandle(context->Worker);
    CloseHandle(context->StopEvent);
    SecureZeroMemory(context, sizeof(*context));
    HeapFree(GetProcessHeap(), 0, context);
    ReleaseSRWLockExclusive(&TcProviderRundownLock);
    return S_OK;
}

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}
