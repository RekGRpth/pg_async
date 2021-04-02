#include <include.h>

#define EXTENSION(function) Datum (function)(PG_FUNCTION_ARGS); PG_FUNCTION_INFO_V1(function); Datum (function)(PG_FUNCTION_ARGS)

PG_MODULE_MAGIC;

static ProcessUtility_hook_type pg_async_ProcessUtility_hook_original = NULL;
static shmem_startup_hook_type pg_async_shmem_startup_hook_original = NULL;

static void CheckRestrictedOperation(const char *cmdname) {
    if (InSecurityRestrictedOperation()) ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE), errmsg("cannot execute %s within security-restricted operation", cmdname)));
}

static void pg_async_ProcessUtility_hook(PlannedStmt *pstmt, const char *queryString, ProcessUtilityContext context, ParamListInfo params, QueryEnvironment *queryEnv, DestReceiver *dest, QueryCompletion *qc) {
    Node *parsetree = pstmt->utilityStmt;
    if (!TransactionStartedDuringRecovery()) return pg_async_ProcessUtility_hook_original ? pg_async_ProcessUtility_hook_original(pstmt, queryString, context, params, queryEnv, dest, qc) : standard_ProcessUtility(pstmt, queryString, context, params, queryEnv, dest, qc);
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
            stmt->conditionname ? Async_Unlisten_My(stmt->conditionname) : Async_UnlistenAll_My();
        } break;
        default: return pg_async_ProcessUtility_hook_original ? pg_async_ProcessUtility_hook_original(pstmt, queryString, context, params, queryEnv, dest, qc) : standard_ProcessUtility(pstmt, queryString, context, params, queryEnv, dest, qc);
    }
    CommandCounterIncrement();
}

static void pg_async_shmem_startup_hook(void) {
    if (pg_async_shmem_startup_hook_original) pg_async_shmem_startup_hook_original();
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
    AsyncShmemInitMy();
    LWLockRelease(AddinShmemInitLock);
}

static void pg_async_XactCallback(XactEvent event, void *arg) {
    if (!TransactionStartedDuringRecovery()) return;
    switch (event) {
        case XACT_EVENT_ABORT: AtAbort_Notify_My(); break;
        case XACT_EVENT_COMMIT: AtCommit_Notify_My(); ProcessCompletedNotifiesMy(); break;
        case XACT_EVENT_PRE_COMMIT: PreCommit_Notify_My(); break;
        case XACT_EVENT_PREPARE: AtPrepare_Notify_My(); break;
        default: break;
    }
}

static void pg_async_SubXactCallback(SubXactEvent event, SubTransactionId mySubid, SubTransactionId parentSubid, void *arg) {
    if (!TransactionStartedDuringRecovery()) return;
    switch (event) {
        case SUBXACT_EVENT_ABORT_SUB: AtSubAbort_Notify_My(); break;
        case SUBXACT_EVENT_PRE_COMMIT_SUB: AtSubCommit_Notify_My(); break;
        default: break;
    }
}

void _PG_fini(void); void _PG_fini(void) {
    ProcessUtility_hook = pg_async_ProcessUtility_hook_original;
    shmem_startup_hook = pg_async_shmem_startup_hook_original;
    UnregisterSubXactCallback(pg_async_SubXactCallback, NULL);
    UnregisterXactCallback(pg_async_XactCallback, NULL);
}

void _PG_init(void); void _PG_init(void) {
    if (!process_shared_preload_libraries_in_progress) return;
    pg_async_ProcessUtility_hook_original = ProcessUtility_hook;
    ProcessUtility_hook = pg_async_ProcessUtility_hook;
    pg_async_shmem_startup_hook_original = shmem_startup_hook;
    shmem_startup_hook = pg_async_shmem_startup_hook;
    RequestAddinShmemSpace(AsyncShmemSizeMy());
    RegisterSubXactCallback(pg_async_SubXactCallback, NULL);
    RegisterXactCallback(pg_async_XactCallback, NULL);
}

EXTENSION(pg_async_listen) {
    const char *channel = PG_ARGISNULL(0) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(0));
    !TransactionStartedDuringRecovery() ? Async_Listen(channel) : Async_Listen_My(channel);
    PG_RETURN_VOID();
}

EXTENSION(pg_async_listening_channels) {
    return !TransactionStartedDuringRecovery() ? pg_listening_channels(fcinfo) : pg_listening_channels_my(fcinfo);
}

EXTENSION(pg_async_notification_queue_usage) {
    return !TransactionStartedDuringRecovery() ? pg_notification_queue_usage(fcinfo) : pg_notification_queue_usage_my(fcinfo);
}

EXTENSION(pg_async_notify) {
    return !TransactionStartedDuringRecovery() ? pg_notify(fcinfo) : pg_notify_my(fcinfo);
}

EXTENSION(pg_async_unlisten_all) {
    !TransactionStartedDuringRecovery() ? Async_UnlistenAll() : Async_UnlistenAll_My();
    PG_RETURN_VOID();
}

EXTENSION(pg_async_unlisten) {
    const char *channel = PG_ARGISNULL(0) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(0));
    !TransactionStartedDuringRecovery() ? Async_Unlisten(channel) : Async_Unlisten_My(channel);
    PG_RETURN_VOID();
}
