# --- Compiler & Flags ---
CC      = gcc
CFLAGS  = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0`
LDFLAGS = `pkg-config --libs gtk+-3.0`

SRC_DIR = src
BIN_DIR = bin

GUI_SRC = $(SRC_DIR)/gui_main.c
GUI_BIN = $(BIN_DIR)/clientGUI

all: $(GUI_BIN)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(GUI_BIN): $(GUI_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(GUI_SRC) $(LDFLAGS)
	@echo ">>> GUI client compiled : $(GUI_BIN)"

clean:
	rm -rf $(BIN_DIR)
	@echo ">>> Nettoyage GUI effectué."

.PHONY: all clean
