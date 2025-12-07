# --- Compiler & Flags ---
CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -I$(INC_DIR)

# --- Directory Structure ---
SRC_DIR        = src
INC_DIR        = ../common/include
COMMON_SRC     = ../common/src/services.c
BIN_DIR        = bin

# --- Executables ---
CLIENT          = $(BIN_DIR)/clientTCP-multiservices
SERVEUR_CENTRAL = $(BIN_DIR)/serveur_central
SERVEUR_DATE    = $(BIN_DIR)/serveur_date
SERVEUR_LS      = $(BIN_DIR)/serveur_ls
SERVEUR_CAT     = $(BIN_DIR)/serveur_cat
SERVEUR_DUREE   = $(BIN_DIR)/serveur_duree

# --- Sources ---
CLIENT_SRC          = $(SRC_DIR)/clientTCP-multiservices.c $(COMMON_SRC)
SERVEUR_CENTRAL_SRC = $(SRC_DIR)/serveur_central.c         $(COMMON_SRC)
SERVEUR_DATE_SRC    = $(SRC_DIR)/serveur_date.c            $(COMMON_SRC)
SERVEUR_LS_SRC      = $(SRC_DIR)/serveur_ls.c              $(COMMON_SRC)
SERVEUR_CAT_SRC     = $(SRC_DIR)/serveur_cat.c             $(COMMON_SRC)
SERVEUR_DUREE_SRC   = $(SRC_DIR)/serveur_duree.c           $(COMMON_SRC)

# --- Build Rules ---
all: $(CLIENT) $(SERVEUR_CENTRAL) $(SERVEUR_DATE) $(SERVEUR_LS) $(SERVEUR_CAT) $(SERVEUR_DUREE)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(CLIENT): $(CLIENT_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_SRC)
	@echo ">>> Client multiservices compiled : $@"

$(SERVEUR_CENTRAL): $(SERVEUR_CENTRAL_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVEUR_CENTRAL_SRC)

$(SERVEUR_DATE): $(SERVEUR_DATE_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVEUR_DATE_SRC)

$(SERVEUR_LS): $(SERVEUR_LS_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVEUR_LS_SRC)

$(SERVEUR_CAT): $(SERVEUR_CAT_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVEUR_CAT_SRC)

$(SERVEUR_DUREE): $(SERVEUR_DUREE_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SERVEUR_DUREE_SRC)

# ============================================================================
#                           COMMANDES D’EXÉCUTION
# ============================================================================

run-central: $(SERVEUR_CENTRAL)
	@echo ">>> Lancement du serveur central..."
	@./bin/serveur_central

run-date: $(SERVEUR_DATE)
	@echo ">>> Lancement du serveur DATE..."
	@./bin/serveur_date

run-ls: $(SERVEUR_LS)
	@echo ">>> Lancement du serveur LS..."
	@./bin/serveur_ls

run-cat: $(SERVEUR_CAT)
	@echo ">>> Lancement du serveur CAT..."
	@./bin/serveur_cat

run-duree: $(SERVEUR_DUREE)
	@echo ">>> Lancement du serveur DUREE..."
	@./bin/serveur_duree

run-client: $(CLIENT)
	@if [ -z "$(IP)" ]; then \
		echo "Utilisation : make run-client IP=<adresse_ip>"; \
		exit 1; \
	fi
	@echo ">>> Lancement du client vers $(IP)..."
	@./bin/clientTCP-multiservices $(IP)

# ============================================================================
#                           RUN ALL + STOP ALL
# ============================================================================

run-all: $(SERVEUR_CENTRAL) $(SERVEUR_DATE) $(SERVEUR_LS) $(SERVEUR_CAT) $(SERVEUR_DUREE)
	@echo ">>> Lancement de tous les serveurs en arrière-plan..."
	@./bin/serveur_central &  echo "  - serveur_central lancé (5000)"
	@./bin/serveur_date &     echo "  - serveur_date lancé (5001)"
	@./bin/serveur_ls &       echo "  - serveur_ls lancé (5002)"
	@./bin/serveur_cat &      echo "  - serveur_cat lancé (5003)"
	@./bin/serveur_duree &    echo "  - serveur_duree lancé (5004)"
	@echo ">>> Tous les serveurs sont actifs."

stop-all:
	@echo ">>> Arrêt de tous les serveurs..."
	@pkill -f serveur_central || true
	@pkill -f serveur_date || true
	@pkill -f serveur_ls || true
	@pkill -f serveur_cat || true
	@pkill -f serveur_duree || true
	@echo ">>> Tous les serveurs ont été arrêtés."

# ============================================================================
#                                  CLEAN
# ============================================================================

clean:
	rm -rf $(BIN_DIR)
	@echo ">>> Nettoyage complet effectué."

.PHONY: all clean run-central run-date run-ls run-cat run-duree run-client run-all stop-all
