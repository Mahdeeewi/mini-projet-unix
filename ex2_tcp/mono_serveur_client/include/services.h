#ifndef SERVICES_H
#define SERVICES_H

#include <sys/types.h>
#include <stddef.h>
#include <time.h>

/*----------------------------------------------------------------------------
 * write_ignore
 *  Petite fonction utilitaire qui encapsule write().
 *  Elle permet d'ignorer explicitement la valeur de retour de write()
 *  sans générer d'avertissement du compilateur.
 *----------------------------------------------------------------------------*/
ssize_t write_ignore(int fd, const void *buf, size_t count);

/*----------------------------------------------------------------------------
 * read_line
 *  Lit une ligne terminée par '\n' sur un descripteur de fichier (socket).
 *  - fd      : socket de communication
 *  - buf     : tampon de destination
 *  - maxlen  : taille maximale du tampon
 *  Retour :
 *    > 0 : nombre d'octets lus (hors '\n')
 *      0 : connexion fermée
 *     -1 : erreur
 *----------------------------------------------------------------------------*/
ssize_t read_line(int fd, char *buf, size_t maxlen);

/*----------------------------------------------------------------------------
 * Services offerts par le serveur
 *  Chaque fonction prend en paramètre le descripteur de socket du client,
 *  et envoie la réponse directement sur cette socket.
 *
 *  Retour :
 *    0  : succès
 *   <0  : erreur fatale (le serveur peut décider de fermer la connexion)
 *----------------------------------------------------------------------------*/

/* Service 1 : envoi de la date/heure système du serveur */
int service_datetime(int fd);

/* Service 2 : envoi de la liste des fichiers d’un répertoire */
int service_list_directory(int fd);

/* Service 3 : envoi du contenu d’un fichier texte */
int service_file_content(int fd);

/* Service 4 : envoi de la durée écoulée depuis le début de la connexion */
int service_connection_duration(int fd, time_t start_time);

/*----------------------------------------------------------------------------
 * check_credentials
 *  Vérifie les identifiants d'un utilisateur à partir d'un fichier texte.
 *  Format du fichier users.txt :
 *      login:motdepasse
 *  à raison d'un utilisateur par ligne.
 *
 *  Retour :
 *    1 : identifiant valide
 *    0 : identifiant invalide ou erreur d'ouverture du fichier
 *----------------------------------------------------------------------------*/
int check_credentials(const char *login, const char *password,
                      const char *users_file);

#endif

