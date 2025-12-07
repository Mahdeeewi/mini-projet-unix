# --- Compiler & Flags ---
CC      = gcc

# Common header path (services.h)
INC_DIR = ../../common/include

# GTK flags
GTK_CFLAGS  = `pkg-config --cflags gtk+-3.0`
GTK_LIBS    = `pkg-config --libs gtk+-3.0`

CFLAGS  = -Wall -Wextra -g -I$(INC_DIR) $(GTK_CFLAGS)
LDFLAGS = $(GTK_LIBS)

SRC_DIR = src
BIN_DIR = bin

GUI_SRC    = $(SRC_DIR)/gui_client_mono.c
COMMON_SRC = ../../common/src/services.c

GUI_BIN = $(BIN_DIR)/clientGUI-mono

all: $(GUI_BIN)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(GUI_BIN): $(GUI_SRC) $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(GUI_SRC) $(COMMON_SRC) $(LDFLAGS)
	@echo ">>> GUI mono-serveur client compiled : $(GUI_BIN)"

clean:
	rm -rf $(BIN_DIR)
	@echo ">>> Nettoyage GUI mono effectué."

.PHONY: all clean
