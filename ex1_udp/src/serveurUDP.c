#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define NMAX 10      /* n ne doit pas dépasser cette valeur */
#define VAL_MAX 100  /* borne max des valeurs aléatoires envoyées */

static void erreur(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen;
    int n_net;              /* n en ordre réseau */
    int n;                  /* n en ordre hôte */
    int i;
    int buffer[NMAX];       /* tableau des nombres générés */
    ssize_t sent;

    if (argc != 2) {
        fprintf(stderr, "Usage : %s <port_serveur>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Port invalide : %d\n", port);
        exit(EXIT_FAILURE);
    }

    /* Création du socket UDP */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        erreur("socket");

    /* Préparation de l'adresse serveur (INADDR_ANY sur le port donné) */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons((uint16_t)port);

    /* Attachement du socket (bind) */
    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        erreur("bind");

    printf("Serveur UDP en écoute sur le port %d...\n", port);

    /* Initialisation du générateur aléatoire */
    srand((unsigned int)(time(NULL) ^ getpid()));

    /* Boucle infinie : le serveur traite les requêtes les unes après les autres */
    while (1) {
        clilen = sizeof(cliaddr);
        printf("\nServeur : en attente de réception d'un nombre...\n");

        /* Réception de n (un seul entier en ordre réseau) */
        ssize_t recvd = recvfrom(sockfd, &n_net, sizeof(n_net), 0,
                                 (struct sockaddr *)&cliaddr, &clilen);
        if (recvd < 0) {
            perror("recvfrom");
            /* On continue la boucle en cas d'erreur non fatale */
            continue;
        }

        if (recvd != sizeof(n_net)) {
            fprintf(stderr, "Serveur : taille inattendue pour n (%zd octets)\n", recvd);
            /* On ignore ce paquet et on continue */
            continue;
        }

        /* Informations sur le client (pour affichage) */
        printf("Serveur : demande reçue de %s:%d\n",
               inet_ntoa(cliaddr.sin_addr),
               ntohs(cliaddr.sin_port));

        /* Conversion n ordre réseau -> hôte */
        n = ntohl(n_net);
        printf("Serveur : n reçu = %d\n", n);

        if (n < 1) {
            printf("Serveur : n < 1, on le met à 1.\n");
            n = 1;
        } else if (n > NMAX) {
            printf("Serveur : n > NMAX (%d), on le limite à NMAX.\n", NMAX);
            n = NMAX;
        }

        /* Génération de n nombres aléatoires compris entre 0 et VAL_MAX-1 */
        for (i = 0; i < n; i++) {
            int valeur = rand() % VAL_MAX;
            buffer[i] = htonl(valeur);  /* stocker directement en ordre réseau */
        }

        /* Envoi des n nombres au client */
        sent = sendto(sockfd, buffer, n * sizeof(int), 0,
                      (struct sockaddr *)&cliaddr, clilen);
        if (sent < 0) {
            perror("sendto");
            continue;
        }

        printf("Serveur : %d nombres envoyés au client.\n", n);
    }

    /* (Normalement on n'arrive pas ici) */
    close(sockfd);
    return EXIT_SUCCESS;
}
