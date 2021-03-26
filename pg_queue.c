#include <postgres.h>

#include <access/parallel.h>
//#include <access/printtup.h>
#include <access/xact.h>
//#include <catalog/heap.h>
//#include <catalog/namespace.h>
//#include <catalog/pg_type.h>
#include <commands/async.h>
//#include <commands/dbcommands.h>
#include <commands/extension.h>
#include <commands/prepare.h>
//#include <commands/user.h>
#include <common/hashfn.h>
#include <common/ip.h>
#include <executor/spi.h>
#include <fe_utils/recovery_gen.h>
//#include <fe_utils/string_utils.h>
#include <funcapi.h>
//#include <jit/jit.h>
#include <libpq-fe.h>
#include <libpq/libpq-be.h>
#include <libpq/libpq.h>
#include <libpq/pqformat.h>
//#include <miscadmin.h>
//#include <nodes/makefuncs.h>
//#include <parser/analyze.h>
#include <parser/parse_func.h>
#include <parser/parse_type.h>
#include <pgstat.h>
//#include <postgresql/internal/pqexpbuffer.h>
#include <postmaster/bgworker.h>
#include <postmaster/interrupt.h>
#include <replication/slot.h>
#include <replication/syncrep.h>
//#include <replication/syncrep.h>
#include <replication/walreceiver.h>
#include <replication/walsender_private.h>
#include <storage/ipc.h>
#include <sys/utsname.h>
//#include <tcop/pquery.h>
#include <tcop/utility.h>
//#include <utils/acl.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
//#include <utils/ps_status.h>
#include <utils/regproc.h>
//#include <utils/snapmgr.h>
#include <utils/timeout.h>
//#include <utils/typcache.h>

#define EXTENSION(function) Datum (function)(PG_FUNCTION_ARGS); PG_FUNCTION_INFO_V1(function); Datum (function)(PG_FUNCTION_ARGS)

#define FORMAT_0(fmt, ...) "%s(%s:%d): %s", __func__, __FILE__, __LINE__, fmt
#define FORMAT_1(fmt, ...) "%s(%s:%d): " fmt,  __func__, __FILE__, __LINE__
#define GET_FORMAT(fmt, ...) GET_FORMAT_PRIVATE(fmt, 0, ##__VA_ARGS__, 1, \
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define GET_FORMAT_PRIVATE(fmt, \
      _0,  _1,  _2,  _3,  _4,  _5,  _6,  _7,  _8,  _9, \
     _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, \
     _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, \
     _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, \
     _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, \
     _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, \
     _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, \
     _70, format, ...) FORMAT_ ## format(fmt)

#define D1(fmt, ...) ereport(DEBUG1, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define D2(fmt, ...) ereport(DEBUG2, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define D3(fmt, ...) ereport(DEBUG3, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define D4(fmt, ...) ereport(DEBUG4, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define D5(fmt, ...) ereport(DEBUG5, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define E(fmt, ...) ereport(ERROR, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define F(fmt, ...) ereport(FATAL, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define I(fmt, ...) ereport(INFO, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define L(fmt, ...) ereport(LOG, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define N(fmt, ...) ereport(NOTICE, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))
#define W(fmt, ...) ereport(WARNING, (errmsg(GET_FORMAT(fmt, ##__VA_ARGS__), ##__VA_ARGS__)))

#define countof(array) (sizeof(array)/sizeof(array[0]))

PG_MODULE_MAGIC;

#define NOTIFY_PAYLOAD_MAX_LENGTH (BLCKSZ - NAMEDATALEN - 128)

typedef struct Notification {
    uint16 channel_len; // length of channel-name string
    uint16 payload_len; // length of payload string
    char data[FLEXIBLE_ARRAY_MEMBER]; // null-terminated channel name, then null-terminated payload follow
} Notification;

typedef struct NotificationList {
//    int nestingLevel; // current transaction nesting depth
    List *events; // list of Notification structs
    HTAB *hashtab; // hash of NotificationHash structs, or NULL
//    struct NotificationList *upper; // details for upper transaction levels
} NotificationList;

#define MIN_HASHABLE_NOTIFIES 0 // threshold to build hashtab 16

typedef struct NotificationHash {
    Notification *event; // => the actual Notification struct
} NotificationHash;

typedef struct pg_queue_shmem_t {
    char channel[NAMEDATALEN];
    char payload[NOTIFY_PAYLOAD_MAX_LENGTH];
    int32 pid;
    slock_t mutex;
} pg_queue_shmem_t;

static int pg_queue_size;
static List *listenChannels = NIL;
static NotificationList *pendingNotifies = NULL;
static pg_queue_shmem_t *pg_queue_shmem;
static pqsigfunc pg_queue_signal_original = NULL;
static shmem_startup_hook_type pg_queue_shmem_startup_hook_original = NULL;

static void pg_queue_shmem_startup_hook(void) {
    bool found;
    if (pg_queue_shmem_startup_hook_original) pg_queue_shmem_startup_hook_original();
    pg_queue_shmem = NULL;
    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);
    pg_queue_shmem = ShmemInitStruct("pg_queue", sizeof(*pg_queue_shmem), &found);
    if (!found) {
        MemSet(pg_queue_shmem, 0, sizeof(pg_queue_shmem));
        SpinLockInit(&pg_queue_shmem->mutex);
    }
    LWLockRelease(AddinShmemInitLock);
}

void _PG_fini(void); void _PG_fini(void) {
    shmem_startup_hook = pg_queue_shmem_startup_hook_original;
}

void _PG_init(void); void _PG_init(void) {
    if (!process_shared_preload_libraries_in_progress) return;
    DefineCustomIntVariable("pg_queue.size", "pg_queue size", NULL, &pg_queue_size, 1024, 1, INT_MAX, PGC_SIGHUP, 0, NULL, NULL, NULL);
    pg_queue_shmem_startup_hook_original = shmem_startup_hook;
    shmem_startup_hook = pg_queue_shmem_startup_hook;
    RequestAddinShmemSpace(MAXALIGN(sizeof(*pg_queue_shmem)));
}

static bool pg_queue_channel_exists(const char *channel) {
    ListCell *p;
    foreach(p, listenChannels) {
        char *lchan = (char *) lfirst(p);
        if (!strcmp(lchan, channel)) return true;
    }
    return false;
}

static void pg_queue_signal(SIGNAL_ARGS) {
    if (listenChannels != NIL) {
        char *channel;
        char *payload;
        int32 pid;
        SpinLockAcquire(&pg_queue_shmem->mutex);
        channel = pstrdup(pg_queue_shmem->channel);
        payload = pstrdup(pg_queue_shmem->payload);
        pid = pg_queue_shmem->pid;
        SpinLockRelease(&pg_queue_shmem->mutex);
        if (pg_queue_channel_exists(channel)) {
            D1("channel = %s, payload = %s, pid = %i", channel, payload, pid);
            NotifyMyFrontEnd(channel, payload, pid);
            pq_flush();
        }
        pfree(channel);
        pfree(payload);
    }
    pg_queue_signal_original(postgres_signal_arg);
}

EXTENSION(pg_queue_listen) {
    const char *channel = PG_ARGISNULL(0) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(0));
    if (!pg_queue_channel_exists(channel)) {
        MemoryContext oldcontext = MemoryContextSwitchTo(TopMemoryContext);
        listenChannels = lappend(listenChannels, pstrdup(channel));
        MemoryContextSwitchTo(oldcontext);
        if (!pg_queue_signal_original) pg_queue_signal_original = pqsignal(SIGUSR1, pg_queue_signal);
    }
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_listening_channels) {
    FuncCallContext *funcctx;
    if (SRF_IS_FIRSTCALL()) funcctx = SRF_FIRSTCALL_INIT();
    funcctx = SRF_PERCALL_SETUP();
    if (funcctx->call_cntr < list_length(listenChannels)) {
        char *channel = (char *)list_nth(listenChannels, funcctx->call_cntr);
        SRF_RETURN_NEXT(funcctx, CStringGetTextDatum(channel));
    }
    SRF_RETURN_DONE(funcctx);
}

static void pg_queue_kill(void) {
    int num_backends = pgstat_fetch_stat_numbackends();
    for (int curr_backend = 1; curr_backend <= num_backends; curr_backend++) {
        PgBackendStatus *beentry;
        if (!(beentry = pgstat_fetch_stat_beentry(curr_backend))) continue;
        if (kill(beentry->st_procpid, SIGUSR1)) W("kill");
    }
}

static bool AsyncExistsPendingNotify(Notification *n) {
    if (!pendingNotifies) return false;
    if (pendingNotifies->hashtab) {
        if (hash_search(pendingNotifies->hashtab, &n, HASH_FIND, NULL)) return true;
    } else {
        ListCell *l;
        foreach(l, pendingNotifies->events) {
            Notification *oldn = (Notification *)lfirst(l);
            if (n->channel_len == oldn->channel_len && n->payload_len == oldn->payload_len && !memcmp(n->data, oldn->data, n->channel_len + n->payload_len + 2)) return true;
        }
    }
    return false;
}

static uint32 notification_hash(const void *key, Size keysize) {
    const Notification *k = *(const Notification *const *)key;
    Assert(keysize == sizeof(Notification *));
    return DatumGetUInt32(hash_any((const unsigned char *)k->data, k->channel_len + k->payload_len + 1));
}

static int notification_match(const void *key1, const void *key2, Size keysize) {
    const Notification *k1 = *(const Notification *const *)key1;
    const Notification *k2 = *(const Notification *const *)key2;
    Assert(keysize == sizeof(Notification *));
    if (k1->channel_len == k2->channel_len && k1->payload_len == k2->payload_len && !memcmp(k1->data, k2->data, k1->channel_len + k1->payload_len + 2)) return 0; // equal
    return 1; // not equal
}

static void AddEventToPendingNotifies(Notification *n) {
    Assert(pendingNotifies->events != NIL);
    if (list_length(pendingNotifies->events) >= MIN_HASHABLE_NOTIFIES && !pendingNotifies->hashtab) {
        HASHCTL hash_ctl;
        ListCell *l;
        MemSet(&hash_ctl, 0, sizeof(hash_ctl));
        hash_ctl.keysize = sizeof(Notification *);
        hash_ctl.entrysize = sizeof(NotificationHash);
        hash_ctl.hash = notification_hash;
        hash_ctl.match = notification_match;
        hash_ctl.hcxt = CurTransactionContext;
        pendingNotifies->hashtab = hash_create("Pending Notifies", 256L, &hash_ctl, HASH_ELEM | HASH_FUNCTION | HASH_COMPARE | HASH_CONTEXT);
        foreach(l, pendingNotifies->events) {
            Notification *oldn = (Notification *)lfirst(l);
            bool found;
            NotificationHash *hentry = hash_search(pendingNotifies->hashtab, &oldn, HASH_ENTER, &found);
            Assert(!found);
            hentry->event = oldn;
        }
    }
    pendingNotifies->events = lappend(pendingNotifies->events, n);
    if (pendingNotifies->hashtab) {
        bool found;
        NotificationHash *hentry = hash_search(pendingNotifies->hashtab, &n, HASH_ENTER, &found);
        Assert(!found);
        hentry->event = n;
    }
}

EXTENSION(pg_queue_notify) {
    const char *channel = PG_ARGISNULL(0) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(0));
    const char *payload = PG_ARGISNULL(1) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(1));
    size_t channel_len = PG_ARGISNULL(0) ? 0 : strlen(channel);
    size_t payload_len = PG_ARGISNULL(1) ? 0 : strlen(payload);
    MemoryContext oldcontext;
    Notification *n;
    if (IsParallelWorker()) elog(ERROR, "cannot send notifications from a parallel worker");
    if (!channel_len) ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("channel name cannot be empty")));
    if (channel_len >= NAMEDATALEN) ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("channel name too long")));
    if (payload_len >= NOTIFY_PAYLOAD_MAX_LENGTH) ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("payload string too long")));
    oldcontext = MemoryContextSwitchTo(CurTransactionContext);
    n = palloc(offsetof(Notification, data) + channel_len + payload_len + 2);
    n->channel_len = channel_len;
    n->payload_len = payload_len;
    strcpy(n->data, channel);
    if (payload) strcpy(n->data + channel_len + 1, payload);
    else n->data[channel_len + 1] = '\0';
    if (!pendingNotifies) {
        pendingNotifies = MemoryContextAlloc(TopTransactionContext, sizeof(*pendingNotifies));
//        pendingNotifies->nestingLevel = my_level;
        pendingNotifies->events = list_make1(n);
        pendingNotifies->hashtab = NULL;
//        pendingNotifies->upper = pendingNotifies;
    } else {
        if (AsyncExistsPendingNotify(n)) pfree(n);
        else AddEventToPendingNotifies(n);
    }
    MemoryContextSwitchTo(oldcontext);
    SpinLockAcquire(&pg_queue_shmem->mutex);
    strcpy(pg_queue_shmem->channel, channel);
    if (payload) strcpy(pg_queue_shmem->payload, payload);
    else pg_queue_shmem->payload[0] = '\0';
    pg_queue_shmem->pid = MyProcPid;
    SpinLockRelease(&pg_queue_shmem->mutex);
    pg_queue_kill();
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_unlisten_all) {
    list_free_deep(listenChannels);
    listenChannels = NIL;
    if (pg_queue_signal_original) {
        pqsignal(SIGUSR1, pg_queue_signal_original);
        pg_queue_signal_original = NULL;
    }
    PG_RETURN_VOID();
}

EXTENSION(pg_queue_unlisten) {
    const char *channel = PG_ARGISNULL(0) ? "" : text_to_cstring(PG_GETARG_TEXT_PP(0));
    ListCell *q;
    foreach(q, listenChannels) {
        char *lchan = (char *)lfirst(q);
        if (!strcmp(lchan, channel)) {
            listenChannels = foreach_delete_current(listenChannels, q);
            pfree(lchan);
            break;
        }
    }
    if (!list_length(listenChannels) && pg_queue_signal_original) {
        pqsignal(SIGUSR1, pg_queue_signal_original);
        pg_queue_signal_original = NULL;
    }
    PG_RETURN_VOID();
}
