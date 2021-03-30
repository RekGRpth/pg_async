#include <include.h>

#define EXTENSION(function) Datum (function)(PG_FUNCTION_ARGS); PG_FUNCTION_INFO_V1(function); Datum (function)(PG_FUNCTION_ARGS)

PG_MODULE_MAGIC;

static pqsigfunc pg_queue_signal_original = NULL;
static shmem_startup_hook_type pg_queue_shmem_startup_hook_original = NULL;

static void pg_queue_shmem_startup_hook(void) {
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
    AsyncShmemInitMy();
    LWLockRelease(AddinShmemInitLock);
}

void _PG_fini(void); void _PG_fini(void) {
    shmem_startup_hook = pg_queue_shmem_startup_hook_original;
}

void _PG_init(void); void _PG_init(void) {
    if (!process_shared_preload_libraries_in_progress) return;
    pg_queue_shmem_startup_hook_original = shmem_startup_hook;
    shmem_startup_hook = pg_queue_shmem_startup_hook;
    RequestAddinShmemSpace(AsyncShmemSizeMy());
}

static void pg_queue_signal(SIGNAL_ARGS) {
    HandleNotifyInterruptMy();
    if (notifyInterruptPending) ProcessNotifyInterruptMy();
    pg_queue_signal_original(postgres_signal_arg);
}

EXTENSION(pg_queue_listen) {
    const char *channel = PG_ARGISNULL(0) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(0));
    if (!pg_queue_signal_original) pg_queue_signal_original = pqsignal(SIGUSR1, pg_queue_signal);
    Async_Listen_My(channel);
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_listening_channels) {
    Datum datum = pg_listening_channels_my(fcinfo);
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    return datum;
}

EXTENSION(pg_queue_notification_queue_usage) {
    Datum datum = pg_notification_queue_usage_my(fcinfo);
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    return datum;
}

EXTENSION(pg_queue_notify) {
    pg_notify_my(fcinfo);
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    ProcessCompletedNotifiesMy();
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_unlisten_all) {
    if (pg_queue_signal_original) {
        pqsignal(SIGUSR1, pg_queue_signal_original);
        pg_queue_signal_original = NULL;
    }
    Async_UnlistenAll_My();
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_unlisten) {
    const char *channel = PG_ARGISNULL(0) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(0));
    Async_Unlisten_My(channel);
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    PG_RETURN_VOID();
}
