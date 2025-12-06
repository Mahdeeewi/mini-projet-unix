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

#define PORT_CAT 5003
#define BACKLOG  5

static void sigchld_handler(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // reap children
    }
}

/* Pour chaque client : appeler service_file_content() puis fermer */
static void handle_client(int sockfd, struct sockaddr_in *cli_addr) {
    printf("[CAT] Client depuis %s:%d\n",
           inet_ntoa(cli_addr->sin_addr),
           ntohs(cli_addr->sin_port));

    if (service_file_content(sockfd) < 0) {
        printf("[CAT] Erreur service_file_content.\n");
    }

    close(sockfd);
    printf("[CAT] Fin de session pour %s:%d\n",
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
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port        = htons(PORT_CAT);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[CAT] bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) < 0) {
        perror("[CAT] listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("[CAT] Serveur cat en écoute sur le port %d...\n", PORT_CAT);

    while (1) {
        clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        if (connfd < 0) {
            if (errno == EINTR) continue;
            perror("[CAT] accept");
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
            perror("[CAT] fork");
            close(connfd);
        }
    }

    close(listenfd);
    return 0;
}
