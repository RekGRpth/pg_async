$(OBJS): Makefile
DATA = pg_async--1.0.sql
EXTENSION = pg_async
MODULE_big = $(EXTENSION)
OBJS = $(EXTENSION).o async.o
PG_CONFIG = pg_config
PG_CPPFLAGS = -I$(libpq_srcdir)
PGXS = $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
