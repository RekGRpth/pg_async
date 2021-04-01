-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_async" to load this file. \quit

CREATE FUNCTION pg_listen(channel pg_catalog.text default null) RETURNS pg_catalog.void STRICT AS 'MODULE_PATHNAME', 'pg_async_listen' LANGUAGE C;
CREATE FUNCTION pg_listening_channels() RETURNS setof pg_catalog.text STRICT AS 'MODULE_PATHNAME', 'pg_async_listening_channels' LANGUAGE C;
CREATE FUNCTION pg_notification_queue_usage() RETURNS pg_catalog.float8 STRICT AS 'MODULE_PATHNAME', 'pg_async_notification_queue_usage' LANGUAGE C;
CREATE FUNCTION pg_notify(channel pg_catalog.text default null, payload pg_catalog.text default null) RETURNS pg_catalog.void STRICT AS 'MODULE_PATHNAME', 'pg_async_notify' LANGUAGE C;
CREATE FUNCTION pg_unlisten_all() RETURNS pg_catalog.void STRICT AS 'MODULE_PATHNAME', 'pg_async_unlisten_all' LANGUAGE C;
CREATE FUNCTION pg_unlisten(channel pg_catalog.text default null) RETURNS pg_catalog.void STRICT AS 'MODULE_PATHNAME', 'pg_async_unlisten' LANGUAGE C;
