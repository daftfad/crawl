# -*- Makefile -*- for stripped down SQLite 3 static lib.

# Modified for Crawl Reference by $Author: dshaligram $ on $Date: 2007-04-20T22:29:34.263715Z $

LIBSQL = libsqlite3.a
AR     = ar rcu
CC     = gcc
RANLIB = ranlib
RM_F   = rm -f

# Omit SQLite features we don't need.
CFLAGS = -DSQLITE_OMIT_AUTHORIZATION \
		 -DSQLITE_OMIT_AUTOVACUUM \
		 -DSQLITE_OMIT_COMPLETE \
		 -DSQLITE_OMIT_BLOB_LITERAL \
		 -DSQLITE_OMIT_COMPOUND_SELECT \
		 -DSQLITE_OMIT_CONFLICT_CLAUSE \
		 -DSQLITE_OMIT_DATETIME_FUNCS \
		 -DSQLITE_OMIT_EXPLAIN \
	     -DSQLITE_OMIT_INTEGRITY_CHECK \
		 -DSQLITE_OMIT_PAGER_PRAGMAS \
		 -DSQLITE_OMIT_PROGRESS_CALLBACK \
		 -DSQLITE_OMIT_SCHEMA_PRAGMAS \
		 -DSQLITE_OMIT_SCHEMA_VERSION_PRAGMAS \
		 -DSQLITE_OMIT_TCL_VARIABLE \
	     -DSQLITE_OMIT_LOAD_EXTENSION \
		 -DTHREADSAFE=0

all: $(LIBSQL)

clean:
	$(RM_F) *.o
	$(RM_F) *.a

$(LIBSQL): sqlite3.o
	$(AR) $@ $^
	$(RANLIB) $@

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<
