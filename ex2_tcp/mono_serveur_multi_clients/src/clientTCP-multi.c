
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "services.h"  // via -I../mono_serveur_client/include

#define BUF_SIZE 1024

/*----------------------------------------------------------------------------
 * clientTCP-multi
 *   - se connecte au serveur (IP + port en argument),
 *   - envoie login + mot de passe,
 *   - si authentification réussie, propose le même menu de services
 *    
 *----------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buf[BUF_SIZE];

    /* Vérification des arguments */
    if (argc != 3) {
        fprintf(stderr, "Utilisation : %s <adresse_IP_serveur> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
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

    /* Préparation de l'adresse du serveur */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons((unsigned short)port);

    if (inet_aton(server_ip, &serv_addr.sin_addr) == 0) {
        fprintf(stderr, "Erreur : adresse IP invalide (%s).\n", server_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Connexion au serveur */
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connecté au serveur %s:%d\n", server_ip, port);

    /* Phase d'authentification */
    char login[BUF_SIZE];
    char password[BUF_SIZE];

    printf("=== Authentification ===\n");
    printf("Nom d'utilisateur : ");
    if (fgets(login, sizeof(login), stdin) == NULL) {
        fprintf(stderr, "Erreur de saisie (login).\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    login[strcspn(login, "\n")] = '\0';

    printf("Mot de passe : ");
    if (fgets(password, sizeof(password), stdin) == NULL) {
        fprintf(stderr, "Erreur de saisie (mot de passe).\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    password[strcspn(password, "\n")] = '\0';

    /* Envoi du login et du mot de passe au serveur */
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

    /* Lecture de la réponse d'authentification : "OK" ou "ERR" */
    if (read_line(sockfd, buf, sizeof(buf)) <= 0) {
        fprintf(stderr, "Erreur : aucune réponse du serveur pendant l'authentification.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (strcmp(buf, "OK") != 0) {
        printf("Authentification refusée par le serveur.\n");
        close(sockfd);
        return 0;
    }

    printf("Authentification réussie.\n");

    /* Boucle principale : menu de services */
    while (1) {
        int choix;

        printf("\n=== Menu des services ===\n");
        printf("1) Afficher la date et l'heure du serveur\n");
        printf("2) Afficher la liste des fichiers d'un répertoire du serveur\n");
        printf("3) Afficher le contenu d'un fichier du serveur\n");
        printf("4) Afficher la durée de la connexion\n");
        printf("0) Quitter\n");
        printf("Votre choix : ");

        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            fprintf(stderr, "Erreur de saisie (choix).\n");
            break;
        }

        choix = atoi(buf);
        snprintf(buf, sizeof(buf), "%d\n", choix);
        if (write(sockfd, buf, strlen(buf)) < 0) {
            perror("write (choix)");
            break;
        }

        if (choix == 0) {
            printf("Fermeture de la connexion...\n");
            break;
        }

        /* Traitement des différents services */
        if (choix == 1) {
            /* Service 1 : date/heure */
            if (read_line(sockfd, buf, sizeof(buf)) <= 0) {
                printf("Le serveur a fermé la connexion.\n");
                break;
            }
            printf("%s\n", buf);
        }
        else if (choix == 2) {
            /* Service 2 : liste des fichiers d'un répertoire */
            char dirpath[BUF_SIZE];

            printf("Chemin du répertoire sur le serveur (laisser vide pour le répertoire courant) : ");
            if (fgets(dirpath, sizeof(dirpath), stdin) == NULL) {
                fprintf(stderr, "Erreur de saisie (chemin de répertoire).\n");
                break;
            }
            dirpath[strcspn(dirpath, "\n")] = '\0';

            snprintf(buf, sizeof(buf), "%.*s\n",
                     (int)(sizeof(buf) - 2), dirpath);
            if (write(sockfd, buf, strlen(buf)) < 0) {
                perror("write (dirpath)");
                break;
            }

            printf("Contenu du répertoire :\n");
            while (1) {
                if (read_line(sockfd, buf, sizeof(buf)) <= 0) {
                    printf("Le serveur a fermé la connexion.\n");
                    goto fin;
                }

                if (strcmp(buf, "END_LIST") == 0)
                    break;

                if (strncmp(buf, "ERREUR", 6) == 0) {
                    printf("%s\n", buf);
                    break;
                }

                printf("  %s\n", buf);
            }
        }
        else if (choix == 3) {
            /* Service 3 : contenu d'un fichier */
            char filepath[BUF_SIZE];

            printf("Chemin du fichier sur le serveur : ");
            if (fgets(filepath, sizeof(filepath), stdin) == NULL) {
                fprintf(stderr, "Erreur de saisie (chemin de fichier).\n");
                break;
            }
            filepath[strcspn(filepath, "\n")] = '\0';

            snprintf(buf, sizeof(buf), "%.*s\n",
                     (int)(sizeof(buf) - 2), filepath);
            if (write(sockfd, buf, strlen(buf)) < 0) {
                perror("write (filepath)");
                break;
            }

            printf("Contenu du fichier :\n");
            while (1) {
                if (read_line(sockfd, buf, sizeof(buf)) <= 0) {
                    printf("Le serveur a fermé la connexion.\n");
                    goto fin;
                }

                if (strcmp(buf, "EOF") == 0)
                    break;

                if (strncmp(buf, "ERREUR", 6) == 0) {
                    printf("%s\n", buf);
                    break;
                }

                printf("%s\n", buf);
            }
        }
        else if (choix == 4) {
            /* Service 4 : durée de la connexion */
            if (read_line(sockfd, buf, sizeof(buf)) <= 0) {
                printf("Le serveur a fermé la connexion.\n");
                break;
            }
            printf("%s\n", buf);
        }
        else {
            /* Choix invalide : on lit éventuellement un message du serveur */
            if (read_line(sockfd, buf, sizeof(buf)) > 0) {
                printf("%s\n", buf);
            } else {
                printf("Le serveur a fermé la connexion.\n");
                break;
            }
        }
    }

fin:
    close(sockfd);
    printf("Client terminé.\n");
    return 0;
}

