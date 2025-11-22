# --- Compilateur et options ---
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I$(INC_DIR)
# -Wall -Wextra : active les avertissements standard
# -O2 : optimisation normale
# -I$(INC_DIR) : ajoute le dossier include/ au chemin des headers

# --- Dossiers du projet ---
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin

# --- Exécutables produits ---
SERVER = $(BIN_DIR)/serveurTCP
CLIENT = $(BIN_DIR)/clientTCP

# --- Fichiers sources ---
# Le serveur et le client utilisent services.c
SERVER_SRC = $(SRC_DIR)/serveurTCP.c $(SRC_DIR)/services.c
CLIENT_SRC = $(SRC_DIR)/clientTCP.c  $(SRC_DIR)/services.c

# ============================================================================
#                                 RÈGLES PRINCIPALES
# ============================================================================

# La règle par défaut : compilation du client ET du serveur
all: $(SERVER) $(CLIENT)

# --- Compilation du serveur ---
$(SERVER): $(SERVER_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC)
	@echo ">>> Serveur TCP compilé : $(SERVER)"

# --- Compilation du client ---
$(CLIENT): $(CLIENT_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC)
	@echo ">>> Client TCP compilé : $(CLIENT)"

# Raccourcis :
server: $(SERVER)
client: $(CLIENT)


# ============================================================================
#                            Commandes d'exécution
# ============================================================================

# L'utilisateur DOIT fournir un port :  make run-serveurTCP PORT=5000
run-serveurTCP: server
	@if [ -z "$(PORT)" ]; then \
		echo "Utilisation : make run-serveurTCP PORT=<port>"; \
		exit 1; \
	fi
	@echo ">>> Exécution du serveur TCP sur le port $(PORT)..."
	@$(SERVER) $(PORT)

# L’utilisateur doit fournir l’IP et le port : make run-clientTCP IP=127.0.0.1 PORT=5000
run-clientTCP: client
	@if [ -z "$(IP)" ] || [ -z "$(PORT)" ]; then \
		echo "Utilisation : make run-clientTCP IP=<adresse> PORT=<port>"; \
		exit 1; \
	fi
	@echo ">>> Exécution du client TCP -> connexion à $(IP):$(PORT)..."
	@$(CLIENT) $(IP) $(PORT)


# ============================================================================
#                                  NETTOYAGE
# ============================================================================

# Suppression des exécutables et objets
clean:
	rm -rf $(BIN_DIR)
	@echo ">>> Nettoyage effectué."

.PHONY: all server client clean run-serveurTCP run-clientTCP

