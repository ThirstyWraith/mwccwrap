CC = i686-w64-mingw32-gcc
CFLAGS = -Wall -O2 -std=c99
LDFLAGS = -lversion

all: PluginLib2.dll PluginLib3.dll ASINTPPC.DLL mwccwrap.exe

PluginLib2.dll: pluginlib.c pluginlib2.def cw_types.h host_ctx.h
	$(CC) $(CFLAGS) -shared -o $@ pluginlib.c pluginlib2.def -DPLUGINLIB_VER=2 -Wl,--kill-at,--enable-stdcall-fixup

PluginLib3.dll: pluginlib.c pluginlib3.def cw_types.h host_ctx.h
	$(CC) $(CFLAGS) -shared -o $@ pluginlib.c pluginlib3.def -DPLUGINLIB_VER=3 -Wl,--kill-at,--enable-stdcall-fixup

# PluginLib5.dll: pluginlib.c pluginlib5.def cw_types.h host_ctx.h
# 	$(CC) $(CFLAGS) -shared -o $@ pluginlib.c pluginlib5.def -DPLUGINLIB_VER=5 -Wl,--kill-at,--enable-stdcall-fixup

ASINTPPC.DLL: asintppc.c asintppc.def
	$(CC) $(CFLAGS) -shared -o $@ asintppc.c asintppc.def -Wl,--kill-at,--enable-stdcall-fixup

mwccwrap.exe: mwccwrap.c cw_types.h host_ctx.h
	$(CC) $(CFLAGS) -o $@ mwccwrap.c $(LDFLAGS)

# Quick test - try to compile a minimal C file
test: all test.c
	wibo ./mwccwrap.exe -v -o test.o test.c

# Test with just initialization (no compile)
test-init: all
	wibo ./mwccwrap.exe -v test.c || true

clean:
	rm -f PluginLib2.dll PluginLib3.dll ASINTPPC.DLL mwccwrap.exe test.o

.PHONY: all test test-init clean
