# SPDX-License-Identifier: MIT
BIN := kraken-hypersense-charging

PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin
UNITDIR ?= $(PREFIX)/lib/systemd/system
SYSCONFDIR ?= /etc
DOCDIR ?= $(PREFIX)/share/doc/$(BIN)

CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=c17 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc

LIB_SRC := src/kv3p.c src/log.c src/frame.c src/gate.c src/config.c src/trigger.c \
	   src/sysfs.c src/usbfs.c src/borrow.c
SRC := $(LIB_SRC) src/main.c
OBJ := $(SRC:.c=.o)
DEPS := $(wildcard src/*.h)

TEST_SRC := $(wildcard tests/test_*.c)
TEST_BIN := $(TEST_SRC:.c=)
# The tests run the same sources under ASan/UBSan; they touch no hardware.
TEST_CFLAGS := -std=c17 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc -g -O1 -fsanitize=address,undefined

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

check: $(TEST_BIN)
	@fail=0; for t in $(TEST_BIN); do \
		printf '== %s\n' "$$t"; ./$$t || fail=1; \
	done; \
	if [ $$fail -eq 0 ]; then echo "all tests passed"; else echo "FAILURES"; exit 1; fi

tests/test_%: tests/test_%.c $(LIB_SRC) $(DEPS)
	$(CC) $(TEST_CFLAGS) -o $@ $< $(LIB_SRC) $(LDFLAGS)

install: $(BIN)
	install -d $(DESTDIR)$(SBINDIR) $(DESTDIR)$(UNITDIR) $(DESTDIR)$(SYSCONFDIR) $(DESTDIR)$(DOCDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(SBINDIR)/$(BIN)
	install -m 0644 systemd/$(BIN).service $(DESTDIR)$(UNITDIR)/$(BIN).service
	install -m 0644 systemd/$(BIN)-resume.service $(DESTDIR)$(UNITDIR)/$(BIN)-resume.service
	install -m 0644 README.md FINDINGS.md $(DESTDIR)$(DOCDIR)/
	@if [ -f $(DESTDIR)$(SYSCONFDIR)/$(BIN).conf ]; then \
		echo "keeping the existing $(DESTDIR)$(SYSCONFDIR)/$(BIN).conf"; \
	else \
		install -m 0644 config/$(BIN).conf $(DESTDIR)$(SYSCONFDIR)/$(BIN).conf; \
	fi
	@echo
	@echo "installed. Next:"
	@echo "  sudo systemctl daemon-reload"
	@echo "  sudo systemctl enable --now $(BIN).service"

uninstall:
	rm -f $(DESTDIR)$(SBINDIR)/$(BIN)
	rm -f $(DESTDIR)$(UNITDIR)/$(BIN).service
	rm -f $(DESTDIR)$(UNITDIR)/$(BIN)-resume.service
	rm -rf $(DESTDIR)$(DOCDIR)
	@echo "left $(DESTDIR)$(SYSCONFDIR)/$(BIN).conf in place; remove it by hand if you want it gone"

clean:
	rm -f $(OBJ) $(BIN) $(TEST_BIN)

.PHONY: all check install uninstall clean
