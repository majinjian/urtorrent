CFLAGS ?= -g
CFLAGS += -Wall
CPPFLAGS += -DBE_DEBUG

%.o: %.c
	gcc $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

../lib/libbencode.a: bencode.o
	ar rc $@ $<
	ranlib $@
