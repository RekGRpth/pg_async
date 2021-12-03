#ifndef _INCLUDE_H_
#define _INCLUDE_H_

#define D1(...) ereport(DEBUG1, (errmsg(__VA_ARGS__)))
#define D2(...) ereport(DEBUG2, (errmsg(__VA_ARGS__)))
#define D3(...) ereport(DEBUG3, (errmsg(__VA_ARGS__)))
#define D4(...) ereport(DEBUG4, (errmsg(__VA_ARGS__)))
#define D5(...) ereport(DEBUG5, (errmsg(__VA_ARGS__)))
#define E(...) ereport(ERROR, (errmsg(__VA_ARGS__)))
#define F(...) ereport(FATAL, (errmsg(__VA_ARGS__)))
#define I(...) ereport(INFO, (errmsg(__VA_ARGS__)))
#define L(...) ereport(LOG, (errmsg(__VA_ARGS__)))
#define N(...) ereport(NOTICE, (errmsg(__VA_ARGS__)))
#define W(...) ereport(WARNING, (errmsg(__VA_ARGS__)))

#define countof(array) (sizeof(array)/sizeof(array[0]))

#include <postgres.h>

#include <access/xact.h>
#include <commands/async.h>
#include <miscadmin.h>
#include <pgstat.h>
#include <storage/ipc.h>
#if PG_VERSION_NUM >= 140000
#include <storage/lwlock.h>
#include <storage/shmem.h>
#endif
#include <tcop/utility.h>
#include <utils/builtins.h>

extern Size AsyncShmemSizeMy(void);
extern void AsyncShmemInitMy(void);
extern void NotifyMyFrontEndMy(const char *channel, const char *payload, int32 srcPid);
extern void Async_Notify_My(const char *channel, const char *payload);
extern void Async_Listen_My(const char *channel);
extern void Async_Unlisten_My(const char *channel);
extern void Async_UnlistenAll_My(void);
extern void PreCommit_Notify_My(void);
extern void AtCommit_Notify_My(void);
extern void AtAbort_Notify_My(void);
extern void AtSubCommit_Notify_My(void);
extern void AtSubAbort_Notify_My(void);
extern void AtPrepare_Notify_My(void);
extern void ProcessCompletedNotifiesMy(void);
extern void HandleNotifyInterruptMy(void);
extern void ProcessNotifyInterruptMy(bool flush);
extern Datum pg_listening_channels_my(PG_FUNCTION_ARGS);
extern Datum pg_notify_my(PG_FUNCTION_ARGS);
extern Datum pg_notification_queue_usage_my(PG_FUNCTION_ARGS);

#endif // _INCLUDE_H_
