#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include "services.h"

#define BUF_SIZE      1024
#define NAME_PREVIEW   900   /* Nombre max de caractères d'un chemin affichés dans les messages d'erreur */

/*----------------------------------------------------------------------------
 * write_ignore
 *  Appelle write() et ignore volontairement la valeur de retour.
 *  Utile lorsque l'on envoie un message d'erreur non critique au client.
 *----------------------------------------------------------------------------*/
ssize_t write_ignore(int fd, const void *buf, size_t count) {
    return write(fd, buf, count);
}

/*----------------------------------------------------------------------------
 * read_line
 *  Lit une ligne (jusqu'à '\n' ou EOF) sur la socket.
 *  Empêche les dépassements de tampon en s'arrêtant à maxlen - 1.
 *----------------------------------------------------------------------------*/
ssize_t read_line(int fd, char *buf, size_t maxlen) {
    ssize_t n = 0;
    char c;
    ssize_t rc;

    if (maxlen == 0) return -1;

    while (n < (ssize_t)(maxlen - 1)) {
        rc = read(fd, &c, 1);
        if (rc == 1) {
            if (c == '\n') {
                /* fin de ligne : on s'arrête, mais on ne note pas '\n' dans le buffer */
                break;
            }
            buf[n++] = c;
        } else if (rc == 0) {
            /* Connexion fermée par le pair AVANT toute donnée */
            if (n == 0)
                return 0;   /* EOF réel : socket fermée */
            else
                break;      /* on renvoie la ligne partielle lue avant la fermeture */
        } else {
            if (errno == EINTR)
                continue;   /* Interrompu par un signal, on continue */
            return -1;      /* Erreur réelle */
        }
    }

    buf[n] = '\0';

    /* Cas particulier : ligne vide ("\\n") -> n == 0 ici.
       On NE VEUT PAS confondre ça avec "connexion fermée", donc on
       renvoie un succès (>0) pour que l'appelant ne pense pas à un EOF. */
    if (n == 0) {
        return 1;  /* ligne vide lue avec succès */
    }

    return n;
}

/*----------------------------------------------------------------------------
 * service_datetime
 *  Envoie la date et l'heure actuelles du serveur au client.
 *  Format : "Date et heure du serveur : jj/mm/aaaa HH:MM:SS\n"
 *----------------------------------------------------------------------------*/
int service_datetime(int fd) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char datebuf[128];

    if (!tm_info) {
        snprintf(datebuf, sizeof(datebuf),
                 "Erreur : impossible de récupérer l'heure du serveur.\n");
    } else {
        strftime(datebuf, sizeof(datebuf),
                 "Date et heure du serveur : %d/%m/%Y %H:%M:%S\n",
                 tm_info);
    }

    if (write(fd, datebuf, strlen(datebuf)) < 0) {
        perror("write (service_datetime)");
        return -1;
    }

    return 0;
}

/*----------------------------------------------------------------------------
 * service_list_directory
 *  Lit le chemin d'un répertoire envoyé par le client, puis renvoie la liste
 *  des fichiers de ce répertoire, un par ligne.
 *
 *  Protocole :
 *    C -> S : "chemin_repertoire\n" (vide -> répertoire courant ".")
 *    S -> C : "nom_fichier\n" ... "END_LIST\n"
 *    ou     : "ERREUR : ...\n"
 *----------------------------------------------------------------------------*/
int service_list_directory(int fd) {
    char dirpath[BUF_SIZE];

    /* Lecture du chemin du répertoire */
    ssize_t n = read_line(fd, dirpath, sizeof(dirpath));
	if (n < 0) {
	
        fprintf(stderr, "Erreur : lecture du chemin de répertoire échouée.\n");
        return -1;
    }

    if (dirpath[0] == '\0') {
        /* Répertoire courant si l'utilisateur n'a rien saisi */
        strcpy(dirpath, ".");
    }

    DIR *dp = opendir(dirpath);
    if (!dp) {
        char msg[BUF_SIZE];
        snprintf(msg, sizeof(msg),
                 "ERREUR : impossible d'ouvrir le répertoire '%.*s'\n",
                 NAME_PREVIEW, dirpath);
        write_ignore(fd, msg, strlen(msg));
        return 0;  /* On prévient le client mais on garde la connexion */
    }

    struct dirent *entry;
    char line[BUF_SIZE];

    while ((entry = readdir(dp)) != NULL) {
        /* On ignore les entrées spéciales . et .. */
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        /* On s'assure de ne pas dépasser la taille du tampon */
        snprintf(line, sizeof(line), "%.*s\n",
                 (int)(sizeof(line) - 2), entry->d_name);

        if (write(fd, line, strlen(line)) < 0) {
            perror("write (service_list_directory)");
            closedir(dp);
            return -1;
        }
    }

    closedir(dp);

    /* Marqueur de fin de liste */
    const char *end = "END_LIST\n";
    if (write(fd, end, strlen(end)) < 0) {
        perror("write (END_LIST)");
        return -1;
    }

    return 0;
}

/*----------------------------------------------------------------------------
 * service_file_content
 *  Lit le chemin d'un fichier envoyé par le client puis renvoie son contenu
 *  ligne par ligne, suivi de "EOF\n" lorsque la fin du fichier est atteinte.
 *
 *  Protocole :
 *    C -> S : "chemin_fichier\n"
 *    S -> C : lignes du fichier...
 *             "EOF\n"
 *    ou     : "ERREUR : ...\n"
 *----------------------------------------------------------------------------*/
int service_file_content(int fd) {
    char filepath[BUF_SIZE];

    /* Lecture du chemin de fichier :
       n < 0  => vraie erreur de lecture
       n == 0 => ligne vide (client a juste appuyé sur Entrée) */
    ssize_t n = read_line(fd, filepath, sizeof(filepath));
    if (n < 0) {
        fprintf(stderr, "Erreur : lecture du chemin de fichier échouée.\n");
        return -1;   /* on indique au serveur qu'il y a une erreur fatale */
    }

    /* Si l'utilisateur n'a rien saisi (juste Entrée) */
    if (filepath[0] == '\0') {
        const char *msg = "ERREUR : aucun chemin de fichier fourni.\n";
        write_ignore(fd, msg, strlen(msg));
        return 0;    /* on signale l'erreur au client mais on garde la connexion */
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        char msg[BUF_SIZE];
        snprintf(msg, sizeof(msg),
                 "ERREUR : impossible d'ouvrir le fichier '%.*s'\n",
                 NAME_PREVIEW, filepath);
        write_ignore(fd, msg, strlen(msg));
        return 0;    /* erreur logique, mais pas de fermeture de la connexion */
    }

    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (write(fd, line, len) < 0) {
            perror("write (service_file_content)");
            fclose(f);
            return -1;
        }
    }

    fclose(f);

    /* Marqueur de fin de fichier pour le client */
    const char *eof = "EOF\n";
    if (write(fd, eof, strlen(eof)) < 0) {
        perror("write (EOF)");
        return -1;
    }

    return 0;
}

/*----------------------------------------------------------------------------
 * service_connection_duration
 *  Calcule et envoie au client la durée écoulée depuis le début de la
 *  connexion, à partir du time_t start_time fourni par le serveur.
 *----------------------------------------------------------------------------*/
int service_connection_duration(int fd, time_t start_time) {
    if (start_time == (time_t)-1) {
        const char *err = "Erreur : temps de connexion non disponible.\n";
        write_ignore(fd, err, strlen(err));
        return 0;
    }

    time_t now = time(NULL);
    long diff = now - start_time;
    if (diff < 0) diff = 0;

    long h = diff / 3600;
    long m = (diff % 3600) / 60;
    long s = diff % 60;

    char msg[BUF_SIZE];
    snprintf(msg, sizeof(msg),
             "Durée de la connexion : %ldh %ldm %lds\n", h, m, s);

    if (write(fd, msg, strlen(msg)) < 0) {
        perror("write (service_connection_duration)");
        return -1;
    }

    return 0;
}

/*----------------------------------------------------------------------------
 * check_credentials
 *  Vérifie les identifiants (login / mot de passe) dans un fichier texte.
 *  Format du fichier :
 *     login:motdepasse
 *
 *  Retour :
 *    1 si une ligne correspond exactement (login & mot de passe),
 *    0 sinon.
 *----------------------------------------------------------------------------*/
int check_credentials(const char *login, const char *password,
                      const char *users_file) {
    FILE *f = fopen(users_file, "r");
    if (!f) {
        perror("fopen (users_file)");
        return 0;
    }

    char line[BUF_SIZE];

    while (fgets(line, sizeof(line), f)) {
        /* On enlève le \n final éventuel */
        line[strcspn(line, "\r\n")] = '\0';

        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        const char *file_login = line;
        const char *file_pass  = colon + 1;

        if (!strcmp(file_login, login) && !strcmp(file_pass, password)) {
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    return 0;
}

