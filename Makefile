EXTENSION = pg_queue
MODULE_big = $(EXTENSION)
OBJS = $(EXTENSION).o async.o
PG_CONFIG = pg_config
DATA = $(EXTENSION)--1.0.sql
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
