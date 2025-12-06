

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "services.h"

#define BUF_SIZE     1024
#define PORT_CENTRAL 5000   // le même que dans serveur_central.c
#define MAX_SERVICES 10

typedef struct {
    int  id;
    char name[32];
    int  port;
} ServiceInfo;

/* Temps de début de session (après authentification) */
static time_t g_session_start = 0;

/* Connexion générique à un service donné (ip + port) */
static int connect_to_service(const char *ip, int port) {
    int sockfd;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons((unsigned short)port);

    if (inet_aton(ip, &serv_addr.sin_addr) == 0) {
        fprintf(stderr, "Adresse IP invalide : %s\n", ip);
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/* === Service 1 : DATE === */
static void do_service_date(const char *server_ip, int port) {
    int sockfd = connect_to_service(server_ip, port);
    if (sockfd < 0) return;

    char buf[BUF_SIZE];

    /* Protocole serveur_date : il envoie directement la date puis ferme. */
    if (read_line(sockfd, buf, sizeof(buf)) > 0) {
        printf("\n[DATE] %s\n", buf);
    } else {
        printf("[DATE] Erreur ou serveur fermé.\n");
    }

    close(sockfd);
}

/* === Service 2 : LS === */
static void do_service_ls(const char *server_ip, int port) {
    int sockfd = connect_to_service(server_ip, port);
    if (sockfd < 0) return;

    char buf[BUF_SIZE];
    char dirpath[BUF_SIZE];

    printf("\n[LS] Chemin du répertoire sur le serveur (vide = répertoire courant) : ");
    if (!fgets(dirpath, sizeof(dirpath), stdin)) {
        fprintf(stderr, "[LS] Erreur de saisie.\n");
        close(sockfd);
        return;
    }
    dirpath[strcspn(dirpath, "\n")] = '\0';

    /* Envoi du chemin au serveur (protocole identique à service_list_directory) */
    snprintf(buf, sizeof(buf), "%.*s\n", (int)(sizeof(buf) - 2), dirpath);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        perror("[LS] write (dirpath)");
        close(sockfd);
        return;
    }

    printf("[LS] Contenu du répertoire :\n");
    while (1) {
        ssize_t n = read_line(sockfd, buf, sizeof(buf));
        if (n <= 0) {
            printf("[LS] Le serveur a fermé la connexion.\n");
            break;
        }

        if (strcmp(buf, "END_LIST") == 0)
            break;

        if (strncmp(buf, "ERREUR", 6) == 0) {
            printf("%s\n", buf);
            break;
        }

        printf("  %s\n", buf);
    }

    close(sockfd);
}

/* === Service 3 : CAT === */
static void do_service_cat(const char *server_ip, int port) {
    int sockfd = connect_to_service(server_ip, port);
    if (sockfd < 0) return;

    char buf[BUF_SIZE];
    char filepath[BUF_SIZE];

    printf("\n[CAT] Chemin du fichier sur le serveur : ");
    if (!fgets(filepath, sizeof(filepath), stdin)) {
        fprintf(stderr, "[CAT] Erreur de saisie.\n");
        close(sockfd);
        return;
    }
    filepath[strcspn(filepath, "\n")] = '\0';

    /* Envoi du chemin au serveur (protocole identique à service_file_content) */
    snprintf(buf, sizeof(buf), "%.*s\n", (int)(sizeof(buf) - 2), filepath);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        perror("[CAT] write (filepath)");
        close(sockfd);
        return;
    }

    printf("[CAT] Contenu du fichier :\n");
    while (1) {
        ssize_t n = read_line(sockfd, buf, sizeof(buf));
        if (n <= 0) {
            printf("[CAT] Le serveur a fermé la connexion.\n");
            break;
        }

        if (strcmp(buf, "EOF") == 0)
            break;

        if (strncmp(buf, "ERREUR", 6) == 0) {
            printf("%s\n", buf);
            break;
        }

        printf("%s\n", buf);
    }

    close(sockfd);
}

/* === Service 4 : DUREE (durée depuis l'authentification) === */
static void do_service_duree(const char *server_ip, int port) {
    int sockfd = connect_to_service(server_ip, port);
    if (sockfd < 0) return;

    char buf[BUF_SIZE];

    if (g_session_start == 0) {
        printf("[DUREE] Erreur : temps de début de session inconnu.\n");
        close(sockfd);
        return;
    }

    /* On envoie le time_t de début de session au serveur de durée */
    snprintf(buf, sizeof(buf), "%ld\n", (long)g_session_start);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        perror("[DUREE] write (timestamp)");
        close(sockfd);
        return;
    }

    /* Lecture de la durée calculée par le serveur */
    if (read_line(sockfd, buf, sizeof(buf)) > 0) {
        printf("\n[DUREE] %s\n", buf);
    } else {
        printf("[DUREE] Erreur ou serveur fermé.\n");
    }

    close(sockfd);
}

/* =========================================================================
 *                                MAIN CLIENT
 * ========================================================================= */
int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buf[BUF_SIZE];

    if (argc != 2) {
        fprintf(stderr, "Utilisation : %s <ip_serveur_central>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];

    /* Connexion au serveur central */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(PORT_CENTRAL);

    if (inet_aton(server_ip, &serv_addr.sin_addr) == 0) {
        fprintf(stderr, "Adresse IP invalide : %s\n", server_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connecté au serveur central %s:%d\n", server_ip, PORT_CENTRAL);

    /* === Authentification === */
    char login[BUF_SIZE], password[BUF_SIZE];

    printf("=== Authentification ===\n");
    printf("Login : ");
    if (!fgets(login, sizeof(login), stdin)) {
        fprintf(stderr, "Erreur lecture login\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    login[strcspn(login, "\n")] = '\0';

    printf("Mot de passe : ");
    if (!fgets(password, sizeof(password), stdin)) {
        fprintf(stderr, "Erreur lecture mot de passe\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    password[strcspn(password, "\n")] = '\0';

    /* Envoi login + password  */
    snprintf(buf, sizeof(buf), "%.*s\n", (int)(sizeof(buf) - 2), login);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        perror("write (login)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    snprintf(buf, sizeof(buf), "%.*s\n", (int)(sizeof(buf) - 2), password);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        perror("write (password)");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Lecture réponse : "OK" ou "ERR" */
    if (read_line(sockfd, buf, sizeof(buf)) <= 0) {
        fprintf(stderr, "Pas de réponse du serveur central.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (strcmp(buf, "OK") != 0) {
        printf("Authentification refusée.\n");
        close(sockfd);
        return 0;
    }

    printf("Authentification réussie.\n");
    /* On mémorise le début de la session côté client */
    g_session_start = time(NULL);

    /* === Lecture liste des services === */
    ServiceInfo services[MAX_SERVICES];
    int nb_services = 0;

    printf("\n=== Services disponibles (reçus du serveur central) ===\n");

    while (1) {
        if (read_line(sockfd, buf, sizeof(buf)) <= 0) {
            printf("Connexion fermée par le serveur central.\n");
            close(sockfd);
            return 0;
        }

        if (strcmp(buf, "END_SERVICES") == 0)
            break;

        /* On ignore la ligne "SERVICES" qui sert juste d'entête protocolaire */
        if (strcmp(buf, "SERVICES") == 0) {
            continue;
        }

        int id, port;
        char name[32];

        if (sscanf(buf, "%d %31s %d", &id, name, &port) == 3) {
            if (nb_services < MAX_SERVICES) {
                services[nb_services].id   = id;
                services[nb_services].port = port;
                /* copie sûre, toujours terminée par '\0' */
                snprintf(services[nb_services].name,
                         sizeof(services[nb_services].name),
                         "%s", name);
                nb_services++;
            }
            printf("Service %d : %s (port %d)\n", id, name, port);
        } else {
            printf("Ligne de service invalide : %s\n", buf);
        }
    }

    /* On peut maintenant fermer la connexion centrale (on a les infos) */
    close(sockfd);
    printf("\n✔ Services récupérés depuis le serveur central.\n");
    printf("➡ Passage en mode multiservices : chaque requête utilisera un serveur spécialisé.\n");

    /* === Boucle de menu local côté client === */
    while (1) {
        printf("\n=== Menu des services (multiserveurs) ===\n");
        for (int i = 0; i < nb_services; i++) {
            printf("%d) %s (port %d)\n",
                   services[i].id,
                   services[i].name,
                   services[i].port);
        }
        printf("0) Quitter\n");
        printf("Votre choix : ");

        if (!fgets(buf, sizeof(buf), stdin)) {
            printf("Entrée invalide, fin du client.\n");
            break;
        }

        int choix = atoi(buf);
        if (choix == 0) {
            printf("Fermeture du client multiservices.\n");
            break;
        }

        /* Chercher le service correspondant */
        int port = -1;
        char name[32];

        name[0] = '\0';
        for (int i = 0; i < nb_services; i++) {
            if (services[i].id == choix) {
                port = services[i].port;
                snprintf(name, sizeof(name), "%s", services[i].name);
                break;
            }
        }

        if (port < 0) {
            printf("Choix de service invalide.\n");
            continue;
        }

        /* Appel du service en fonction de son nom */
        if (strcmp(name, "DATE") == 0) {
            do_service_date(server_ip, port);
        } else if (strcmp(name, "LS") == 0) {
            do_service_ls(server_ip, port);
        } else if (strcmp(name, "CAT") == 0) {
            do_service_cat(server_ip, port);
        } else if (strcmp(name, "DUREE") == 0) {
            do_service_duree(server_ip, port);
        } else {
            printf("Service '%s' non pris en charge côté client.\n", name);
        }
    }

    return 0;
}
