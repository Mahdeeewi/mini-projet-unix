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

#include "services.h"

#define BUF_SIZE   1024

#define PORT_CENTRAL 5000
#define BACKLOG      5

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // reap children
    }
}

/* Gère une connexion client : auth + envoi de la liste des services */
static void handle_client(int sockfd, struct sockaddr_in *cli_addr) {
    char login[BUF_SIZE], password[BUF_SIZE];

    printf("Client connecté depuis %s:%d\n",
           inet_ntoa(cli_addr->sin_addr),
           ntohs(cli_addr->sin_port));

    /* Lecture login + password */
    if (read_line(sockfd, login, sizeof(login)) <= 0 ||
        read_line(sockfd, password, sizeof(password)) <= 0) {
        fprintf(stderr, "Erreur lecture identifiants.\n");
        close(sockfd);
        return;
    }

    if (check_credentials(login, password, USERS_FILE_PATH)) {
        write_ignore(sockfd, "OK\n", 3);
        printf("Authentification OK pour '%s'\n", login);
    } else {
        write_ignore(sockfd, "ERR\n", 4);
        printf("Authentification échouée pour '%s'\n", login);
        close(sockfd);
        return;
    }

    /* Envoi de la liste des services (statique pour l’instant) */
    const char *services =
        "SERVICES\n"
        "1 DATE 5001\n"
        "2 LS 5002\n"
        "3 CAT 5003\n"
        "4 DUREE 5004\n"
        "END_SERVICES\n";

    write_ignore(sockfd, services, strlen(services));

    close(sockfd);
    printf("Session centrale terminée pour %s:%d\n",
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
    serv_addr.sin_port        = htons(PORT_CENTRAL);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) < 0) {
        perror("listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("Serveur CENTRAL en écoute sur le port %d...\n", PORT_CENTRAL);

    while (1) {
        clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        if (connfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // fils
            close(listenfd);
            handle_client(connfd, &cli_addr);
            exit(EXIT_SUCCESS);
        } else if (pid > 0) {
            // père
            close(connfd);
        } else {
            perror("fork");
            close(connfd);
        }
    }

    close(listenfd);
    return 0;
}
