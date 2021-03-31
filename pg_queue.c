#include <include.h>

#define EXTENSION(function) Datum (function)(PG_FUNCTION_ARGS); PG_FUNCTION_INFO_V1(function); Datum (function)(PG_FUNCTION_ARGS)

PG_MODULE_MAGIC;

static pqsigfunc pg_queue_signal_original = NULL;
static ProcessUtility_hook_type pg_queue_ProcessUtility_hook_original = NULL;
static shmem_startup_hook_type pg_queue_shmem_startup_hook_original = NULL;

static void CheckRestrictedOperation(const char *cmdname) {
    if (InSecurityRestrictedOperation()) ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE), errmsg("cannot execute %s within security-restricted operation", cmdname)));
}

static void pg_queue_ProcessUtility_hook(PlannedStmt *pstmt, const char *queryString, ProcessUtilityContext context, ParamListInfo params, QueryEnvironment *queryEnv, DestReceiver *dest, QueryCompletion *qc) {
    Node *parsetree = pstmt->utilityStmt;
    if (!XactReadOnly) return pg_queue_ProcessUtility_hook_original ? pg_queue_ProcessUtility_hook_original(pstmt, queryString, context, params, queryEnv, dest, qc) : standard_ProcessUtility(pstmt, queryString, context, params, queryEnv, dest, qc);
    check_stack_depth();
    switch (nodeTag(parsetree)) {
        case T_ListenStmt: {
            ListenStmt *stmt = (ListenStmt *)parsetree;
            CheckRestrictedOperation("LISTEN");
            Async_Listen_My(stmt->conditionname);
        } break;
        case T_NotifyStmt: {
            NotifyStmt *stmt = (NotifyStmt *)parsetree;
            Async_Notify_My(stmt->conditionname, stmt->payload);
        } break;
        case T_UnlistenStmt: {
            UnlistenStmt *stmt = (UnlistenStmt *)parsetree;
            CheckRestrictedOperation("UNLISTEN");
            if (stmt->conditionname) Async_Unlisten_My(stmt->conditionname);
            else Async_UnlistenAll_My();
        } break;
        default: return pg_queue_ProcessUtility_hook_original ? pg_queue_ProcessUtility_hook_original(pstmt, queryString, context, params, queryEnv, dest, qc) : standard_ProcessUtility(pstmt, queryString, context, params, queryEnv, dest, qc);
    }
    CommandCounterIncrement();
}

static void pg_queue_shmem_startup_hook(void) {
    if (pg_queue_shmem_startup_hook_original) pg_queue_shmem_startup_hook_original();
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
    AsyncShmemInitMy();
    LWLockRelease(AddinShmemInitLock);
}

void _PG_fini(void); void _PG_fini(void) {
    ProcessUtility_hook = pg_queue_ProcessUtility_hook_original;
    shmem_startup_hook = pg_queue_shmem_startup_hook_original;
}

void _PG_init(void); void _PG_init(void) {
    if (!process_shared_preload_libraries_in_progress) return;
    pg_queue_ProcessUtility_hook_original = ProcessUtility_hook;
    ProcessUtility_hook = pg_queue_ProcessUtility_hook;
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
    if (!XactReadOnly) Async_Listen(channel); else {
        if (!pg_queue_signal_original) pg_queue_signal_original = pqsignal(SIGUSR1, pg_queue_signal);
        Async_Listen_My(channel);
        PreCommit_Notify_My();
        AtCommit_Notify_My();
    }
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_listening_channels) {
    Datum datum;
    if (!XactReadOnly) return pg_listening_channels(fcinfo);
    datum = pg_listening_channels_my(fcinfo);
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    return datum;
}

EXTENSION(pg_queue_notification_queue_usage) {
    Datum datum;
    if (!XactReadOnly) return pg_notification_queue_usage(fcinfo);
    datum = pg_notification_queue_usage_my(fcinfo);
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    return datum;
}

EXTENSION(pg_queue_notify) {
    if (!XactReadOnly) return pg_notify(fcinfo);
    pg_notify_my(fcinfo);
    PreCommit_Notify_My();
    AtCommit_Notify_My();
    ProcessCompletedNotifiesMy();
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_unlisten_all) {
    if (!XactReadOnly) Async_UnlistenAll(); else {
        Async_UnlistenAll_My();
        PreCommit_Notify_My();
        AtCommit_Notify_My();
        if (pg_queue_signal_original) {
            pqsignal(SIGUSR1, pg_queue_signal_original);
            pg_queue_signal_original = NULL;
        }
    }
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_unlisten) {
    const char *channel = PG_ARGISNULL(0) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(0));
    if (!XactReadOnly) Async_Unlisten(channel); else {
        Async_Unlisten_My(channel);
        PreCommit_Notify_My();
        AtCommit_Notify_My();
    }
    PG_RETURN_VOID();
}
