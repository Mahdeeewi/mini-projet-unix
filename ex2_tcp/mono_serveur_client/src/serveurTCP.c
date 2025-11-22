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

#define BUF_SIZE   1024
#define USERS_FILE "users.txt"

/*----------------------------------------------------------------------------
 * serveurTCP
 * 
 *   - accepte une seule connexion client,
 *   - authentifie le client (login + mot de passe),
 *   - propose un menu de services :
 *       1) Date/heure du serveur
 *       2) Liste des fichiers d'un répertoire
 *       3) Contenu d'un fichier
 *       4) Durée de la connexion
 *       0) Fin
 *----------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
    int sockfd, newsockfd;
    int port;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen;

    char buf[BUF_SIZE];
    time_t conn_start;

    /* Vérification des arguments */
    if (argc != 2) {
        fprintf(stderr, "Utilisation : %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[1]);
    if (port <= 0) {
        fprintf(stderr, "Erreur : port invalide.\n");
        exit(EXIT_FAILURE);
    }

    /* Création de la socket TCP IPv4 */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Option pour réutiliser rapidement le port après un crash/restart */
    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval,
                   sizeof(optval)) < 0) {
        perror("setsockopt");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Préparation de l'adresse du serveur (toutes interfaces) */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port        = htons((unsigned short)port);

    /* Attachement de la socket à l'adresse/port */
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Mise en écoute (file d'attente) */
    if (listen(sockfd, 5) < 0) {
        perror("listen");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Serveur en écoute sur le port %d...\n", port);

    /* Acceptation d'un seul client pour cette étape (monoclient) */
    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
    if (newsockfd < 0) {
        perror("accept");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Client connecté depuis %s:%d\n",
           inet_ntoa(cli_addr.sin_addr),
           ntohs(cli_addr.sin_port));

    /* On mémorise l'instant de début de connexion pour le service 4 */
    conn_start = time(NULL);

    /* Phase d'authentification */
    char login[BUF_SIZE], password[BUF_SIZE];

    if (read_line(newsockfd, login, sizeof(login)) <= 0 ||
        read_line(newsockfd, password, sizeof(password)) <= 0) {
        fprintf(stderr, "Erreur : échec de la lecture des identifiants.\n");
        close(newsockfd);
        close(sockfd);
        return 0;
    }

    printf("Tentative de connexion avec login='%s'\n", login);

    if (check_credentials(login, password, USERS_FILE)) {
        if (write(newsockfd, "OK\n", 3) < 0) {
            perror("write (OK)");
            close(newsockfd);
            close(sockfd);
            return 1;
        }
        printf("Authentification réussie.\n");
    } else {
        /* On informe le client et on ferme proprement la connexion */
        write_ignore(newsockfd, "ERR\n", 4);
        printf("Authentification échouée.\n");
        close(newsockfd);
        close(sockfd);
        return 0;
    }

    /* Boucle de service : on traite les requêtes tant que le client reste connecté */
    while (1) {
        ssize_t n = read_line(newsockfd, buf, sizeof(buf));
        if (n <= 0) {
            printf("Client déconnecté ou erreur de lecture.\n");
            break;
        }

        int choix = atoi(buf);
        int rc    = 0;

        printf("Client : choix = %d\n", choix);

        switch (choix) {
            case 0:
                printf("Fin de session demandée par le client.\n");
                goto fin;

            case 1:
                rc = service_datetime(newsockfd);
                break;

            case 2:
                rc = service_list_directory(newsockfd);
                break;

            case 3:
                rc = service_file_content(newsockfd);
                break;

            case 4:
                rc = service_connection_duration(newsockfd, conn_start);
                break;

            default:
                /* Choix invalide, on renvoie un message explicite */
                write_ignore(newsockfd, "Choix inconnu.\n",
                             strlen("Choix inconnu.\n"));
                break;
        }

        if (rc < 0) {
            printf("Erreur lors du traitement du service. Fermeture de la connexion.\n");
            break;
        }
    }

fin:
    close(newsockfd);
    close(sockfd);
    printf("Serveur arrêté.\n");
    return 0;
}

