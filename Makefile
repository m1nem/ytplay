# ╔══════════════════════════════════════════════════╗
# ║  ytplay — Makefile                               ║
# ║  Builds on Linux, macOS, and (via nmake) Windows ║
# ╚══════════════════════════════════════════════════╝

CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -std=c99 -pedantic
TARGET  := ytplay
SRC     := ytplay.c

# ── OS detection ──────────────────────────────────────────
UNAME := $(shell uname 2>/dev/null || echo Windows)

ifeq ($(UNAME), Linux)
  INSTALL_DIR := /usr/local/bin
  RM          := rm -f
  OPEN        := xdg-open
endif
ifeq ($(UNAME), Darwin)
  INSTALL_DIR := /usr/local/bin
  RM          := rm -f
  OPEN        := open
endif

# ── Targets ───────────────────────────────────────────────
.PHONY: all clean install uninstall deps help

all: $(TARGET)

$(TARGET): $(SRC)
	@echo "  Building ytplay..."
	$(CC) $(CFLAGS) -o $@ $<
	@echo "  Done! Run: ./ytplay --help"

clean:
	$(RM) $(TARGET) $(TARGET).exe

install: $(TARGET)
	@echo "  Installing to $(INSTALL_DIR)..."
	install -m 755 $(TARGET) $(INSTALL_DIR)/$(TARGET)
	@echo "  Installed! You can now run 'ytplay' from anywhere."

uninstall:
	$(RM) $(INSTALL_DIR)/$(TARGET)
	@echo "  Uninstalled."

# Check / install dependencies
deps:
	@echo "  Checking dependencies..."
	@command -v yt-dlp >/dev/null 2>&1 && echo "  [OK] yt-dlp" || \
		(echo "  [!!] yt-dlp not found – installing via pip..." && \
		pip install --user yt-dlp || echo "  [!!] Please install yt-dlp manually from https://github.com/yt-dlp/yt-dlp")
	@command -v mpv >/dev/null 2>&1 && echo "  [OK] mpv" || \
		echo "  [!!] mpv not found – install: sudo apt install mpv  /  brew install mpv"

help:
	@echo ""
	@echo "  ytplay Makefile targets:"
	@echo "    make          — Build ytplay"
	@echo "    make install  — Install to $(INSTALL_DIR)"
	@echo "    make uninstall— Remove from $(INSTALL_DIR)"
	@echo "    make deps     — Check/install dependencies"
	@echo "    make clean    — Remove build artifacts"
	@echo ""
