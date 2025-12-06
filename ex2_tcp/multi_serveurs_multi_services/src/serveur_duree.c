// fichier : ex2_tcp/multi_serveurs_multi_services/src/serveur_duree.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#include "services.h"

#define PORT_DUREE 5004
#define BACKLOG    5
#define BUF_SIZE   1024

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // reap children
    }
}

/*
 * Protocole :
 *   C -> S : "timestamp_session_debut\n" (valeur de time_t envoyée par le client)
 *   S -> C : "Durée de la connexion : Xh Ym Zs\n"
 */
static void handle_client(int sockfd, struct sockaddr_in *cli_addr) {
    printf("[DUREE] Client depuis %s:%d\n",
           inet_ntoa(cli_addr->sin_addr),
           ntohs(cli_addr->sin_port));

    char buf[BUF_SIZE];

    /* Lecture du timestamp envoyé par le client */
    ssize_t n = read_line(sockfd, buf, sizeof(buf));
    if (n <= 0) {
        printf("[DUREE] Aucun timestamp reçu, client parti.\n");
        close(sockfd);
        return;
    }

    long start_sec;
    if (sscanf(buf, "%ld", &start_sec) != 1) {
        const char *msg = "Format invalide pour le temps de début.\n";
        write_ignore(sockfd, msg, strlen(msg));
        close(sockfd);
        printf("[DUREE] Format invalide reçu.\n");
        return;
    }

    time_t start_time = (time_t)start_sec;

    if (service_connection_duration(sockfd, start_time) < 0) {
        printf("[DUREE] Erreur service_connection_duration.\n");
        close(sockfd);
        return;
    }

    close(sockfd);
    printf("[DUREE] Fin de session pour %s:%d\n",
           inet_ntoa(cli_addr->sin_addr),
           ntohs(cli_addr->sin_port));
}

int main(void) {
    int listenfd, connfd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen;

    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) {
        perror("signal");
        exit(EXIT_FAILURE);
    }

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port        = htons(PORT_DUREE);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[DUREE] bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) < 0) {
        perror("[DUREE] listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("[DUREE] Serveur duree en écoute sur le port %d...\n", PORT_DUREE);

    while (1) {
        clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        if (connfd < 0) {
            if (errno == EINTR) continue;
            perror("[DUREE] accept");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // child
            close(listenfd);
            handle_client(connfd, &cli_addr);
            exit(EXIT_SUCCESS);
        } else if (pid > 0) {
            // parent
            close(connfd);
        } else {
            perror("[DUREE] fork");
            close(connfd);
        }
    }

    close(listenfd);
    return 0;
}
