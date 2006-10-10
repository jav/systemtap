bin_PROGRAMS = lket-b2a
lket_b2a_SOURCES = lket_b2a.c
lket_b2a_DEPENDENCIES = lket_b2a.h
lket_b2a_LDFLAGS = `pkg-config --libs glib-2.0`
lket_b2a_CFLAGS = -O2 -D_GNU_SOURCE -Wall `pkg-config --cflags glib-2.0`

if HAS_MYSQL
lket_b2a_LDFLAGS += `mysql_config --libs`
lket_b2a_CFLAGS += `mysql_config --cflags`
lket_b2a_CFLAGS += -DHAS_MYSQL
endif
