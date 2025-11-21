#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define NMAX 10           /* valeur max de n (modifiable selon le sujet) */

/* Petite fonction utilitaire pour les erreurs */
static void erreur(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in servaddr;
    int n;                     /* nombre aléatoire à envoyer */
    int n_net;                 /* n en ordre réseau */
    int i;
    socklen_t servlen;
    int buffer[NMAX];          /* pour recevoir les n nombres */
    ssize_t recvd;
    int count;

    if (argc != 3) {
        fprintf(stderr, "Usage : %s <adresse_serveur_IP> <port_serveur>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Port invalide : %d\n", port);
        exit(EXIT_FAILURE);
    }

    /* Création du socket UDP */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        erreur("socket");

    /* Initialisation de la structure d'adresse du serveur */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons((uint16_t)port);

    /* Conversion de l'adresse IP (en notation pointée) vers format binaire */
    if (inet_aton(server_ip, &servaddr.sin_addr) == 0) {
        fprintf(stderr, "Adresse IP invalide : %s\n", server_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Initialisation du générateur de nombres aléatoires */
    srand((unsigned int)(time(NULL) ^ getpid()));

    /* Génération d'un nombre aléatoire entre 1 et NMAX */
    n = (rand() % NMAX) + 1;
    printf("Client : n = %d (nombre aléatoire compris entre 1 et %d)\n", n, NMAX);

    /* Conversion de n en ordre réseau */
    n_net = htonl(n);

    /* Envoi de n au serveur */
    servlen = sizeof(servaddr);
    if (sendto(sockfd, &n_net, sizeof(n_net), 0,
               (struct sockaddr *)&servaddr, servlen) < 0) {
        erreur("sendto");
    }

    printf("Client : n envoyé au serveur %s:%d\n",
           server_ip, port);

    /* Réception des n nombres envoyés par le serveur */
    recvd = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                     (struct sockaddr *)&servaddr, &servlen);
    if (recvd < 0) {
        erreur("recvfrom");
    }

    /* Nombre d'entiers reçus (en supposant qu'ils sont envoyés sous forme de int) */
    if (recvd % sizeof(int) != 0) {
        fprintf(stderr, "Client : taille de réponse inattendue (%zd octets)\n", recvd);
    }

    count = (int)(recvd / sizeof(int));
    printf("Client : reçu %d nombres depuis le serveur :\n", count);

    for (i = 0; i < count; i++) {
        int valeur = ntohl(buffer[i]);  /* conversion ordre réseau -> ordre hôte */
        printf("  [%2d] = %d\n", i, valeur);
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
