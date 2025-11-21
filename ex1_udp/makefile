# --- Variables ---
CC = gcc
CFLAGS = -Wall -Wextra -O2
SRC_DIR = src
BIN_DIR = bin

CLIENT = $(BIN_DIR)/clientUDP
SERVER = $(BIN_DIR)/serveurUDP

CLIENT_SRC = $(SRC_DIR)/clientUDP.c
SERVER_SRC = $(SRC_DIR)/serveurUDP.c

# --- Rules ---

all: $(CLIENT) $(SERVER)

$(CLIENT): $(CLIENT_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)

$(SERVER): $(SERVER_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)

client: $(CLIENT)
server: $(SERVER)

# --- Run commands (arguments required by user) ---

run-serveurUDP: server
	@if [ -z "$(PORT)" ]; then \
		echo "Utilisation : make run-serveurUDP PORT=<port>"; \
		exit 1; \
	fi
	@echo ">>> Exécution du serveur UDP sur le port $(PORT)..."
	@$(SERVER) $(PORT)

run-clientUDP: client
	@if [ -z "$(IP)" ] || [ -z "$(PORT)" ]; then \
		echo "Utilisation : make run-clientUDP IP=<adresse> PORT=<port>"; \
		exit 1; \
	fi
	@echo ">>> Exécution de clientUDP se connectant à $(IP):$(PORT)..."
	@$(CLIENT) $(IP) $(PORT)

# --- Clean ---

clean:
	rm -rf $(BIN_DIR)

.PHONY: all client server clean run-serveurUDP run-clientUDP

