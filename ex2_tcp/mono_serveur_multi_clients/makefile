# --- Compilateur et options ---
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I$(INC_DIR_1)
# -I$(INC_DIR_1) permet de réutiliser les headers de la partie 1 (services.h)

# --- Dossiers du projet ---
SRC_DIR_2 = src
INC_DIR_1 = ../mono_serveur_client/include
SRC_DIR_1 = ../mono_serveur_client/src
BIN_DIR   = bin

# --- Exécutables produits ---
SERVER = $(BIN_DIR)/serveurTCP-multi
CLIENT = $(BIN_DIR)/clientTCP-multi

# --- Fichiers sources ---
# Le serveur et le client réutilisent services.c de la partie 1
SERVER_SRC = $(SRC_DIR_2)/serveurTCP-multi.c $(SRC_DIR_1)/services.c
CLIENT_SRC = $(SRC_DIR_2)/clientTCP-multi.c  $(SRC_DIR_1)/services.c

# ============================================================================
#                                 RÈGLES PRINCIPALES
# ============================================================================

all: $(SERVER) $(CLIENT)

# --- Compilation du serveur multi-client ---
$(SERVER): $(SERVER_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)
	@echo ">>> Serveur TCP multi-client compilé : $(SERVER)"

# --- Compilation du client multi ---
$(CLIENT): $(CLIENT_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)
	@echo ">>> Client TCP multi compilé : $(CLIENT)"

server: $(SERVER)
client: $(CLIENT)

# ============================================================================
#                            Commandes d'exécution
# ============================================================================


run-serveurTCP-multi: server
	@if [ -z "$(PORT)" ]; then \
		echo "Utilisation : make run-serveurTCP-multi PORT=<port>"; \
		exit 1; \
	fi
	@echo ">>> Exécution du serveur TCP multi-client sur le port $(PORT)..."
	@$(SERVER) $(PORT)


run-clientTCP-multi: client
	@if [ -z "$(IP)" ] || [ -z "$(PORT)" ]; then \
		echo "Utilisation : make run-clientTCP-multi IP=<adresse> PORT=<port>"; \
		exit 1; \
	fi
	@echo ">>> Exécution du client TCP multi -> connexion à $(IP):$(PORT)..."
	@$(CLIENT) $(IP) $(PORT)

# ============================================================================
#                                  NETTOYAGE
# ============================================================================

clean:
	rm -rf $(BIN_DIR)
	@echo ">>> Nettoyage effectué."

.PHONY: all server client clean run-serveurTCP-multi run-clientTCP-multi
