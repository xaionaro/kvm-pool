
DESTDIR ?= 
PREFIX  ?= /usr
COMPRESS_MAN ?= yes
STRIP_BINARY ?= yes
EXAMPLES ?= yes

CSECFLAGS ?= -fstack-protector-all -Wall --param ssp-buffer-size=4 -D_FORTIFY_SOURCE=2 -fstack-check -DPARANOID -std=gnu99
CFLAGS ?= -pipe -O2
CFLAGS += $(CSECFLAGS) $(shell pkg-config --cflags glib-2.0)
DEBUGCFLAGS ?= -pipe -Wall -ggdb3 -export-dynamic -Wno-error=unused-variable -O0 -pipe $(CSECFLAGS)

CARCHFLAGS ?= -march=native

LIBS :=
LDSECFLAGS ?= -Xlinker -zrelro
LDFLAGS += $(LDSECFLAGS) -pthread $(shell pkg-config --libs glib-2.0)
INC := $(INC)

INSTDIR = $(DESTDIR)$(PREFIX)

objs=\
malloc.o\
error.o\
pthreadex.o\
kvm-pool.o\
main.o\

binary=kvm-pool

.PHONY: doc

all: $(objs)
	$(CC) $(CARCHFLAGS) $(CFLAGS) $(LDFLAGS) $(objs) $(LIBS) -o $(binary)

%.o: %.c
	$(CC) $(CARCHFLAGS) $(CFLAGS) $(INC) $< -c -o $@

debug:
	$(CC) $(CARCHFLAGS) -DDEBUG2 $(DEBUGCFLAGS) $(INC) $(LDFLAGS) *.c $(LIBS) -o $(binary)


clean:
	rm -f $(binary) *.o test

distclean: clean
	rm -f *.orig

doc:
	doxygen .doxygen

install:
	install -d "$(INSTDIR)/bin" "$(INSTDIR)/share/man/man3"
ifeq ($(STRIP_BINARY),yes)
	strip --strip-unneeded -R .comment -R .GCC.command.line -R .note.gnu.gold-version $(binary)
endif
	install -m 755 $(binary) "$(INSTDIR)"/bin/
	install -m 644 man/man1/kvm-pool.1 "$(INSTDIR)"/share/man/man1/
ifeq ($(COMPRESS_MAN),yes)
	rm -f "$(INSTDIR)"/share/man/man1/kvm-pool.1.gz
	gzip  "$(INSTDIR)"/share/man/man1/kvm-pool.1
endif

deinstall:
	rm -f "$(INSTDIR)"/bin/$(binary)

test: all
	$(CC) -O3 test.c -lkvm-pool -L. -o test

debugtest: debug
	$(CC) -O0 -ggdb test.c -lkvm-pool -L. -o test


dotest: test
	LD_LIBRARY_PATH=. ./test

format:
	astyle --style=linux --indent=tab --indent-cases --indent-switches --indent-preproc-define --break-blocks --pad-oper --pad-paren --delete-empty-lines *.c *.h | grep -v Unchanged || true

