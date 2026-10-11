# Build on the host after sourcing the OpenSTLinux SDK environment-setup
# (it sets CC for aarch64), or natively on the board.
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -I..

m33_link_ping: m33_link_ping.c ../m33_link.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f m33_link_ping
.PHONY: clean
