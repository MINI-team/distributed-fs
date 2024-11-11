# Variables for pkg-config
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)

# Compilation rule
server: server.c dfs.pb-c.c dfs.pb-c.h common.h common.c
	@gcc -o server $(GLIB_CFLAGS) server.c dfs.pb-c.c common.c -lprotobuf-c -lssl -lcrypto $(GLIB_LIBS)
