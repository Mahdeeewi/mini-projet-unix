// fichier : ex2_tcp/gui_client/src/gui_main.c
// Client GUI : une fenêtre, un GtkStack avec 3 pages (Login, Menu, Service)
// Utilise de vrais appels TCP vers le serveur central et les serveurs de services.

#include <gtk/gtk.h>

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

/* === Constantes réseau === */

#define BUF_SIZE     1024
#define PORT_CENTRAL 5000
#define MAX_SERVICES 10

typedef struct {
    int  id;
    char name[32];
    int  port;
} ServiceInfo;

/* === Structure globale de l'application === */

typedef struct {
    GtkWidget   *stack;
    GtkWidget   *entry_ip;
    GtkWidget   *entry_login;
    GtkWidget   *entry_password;
    GtkWidget   *label_status;
    GtkWidget   *textview_output;

    /* Infos réseau / session */
    gchar       *server_ip;                 /* IP du serveur central / services */
    ServiceInfo  services[MAX_SERVICES];
    int          nb_services;
    time_t       session_start;             /* moment de l'authentification */
} AppWidgets;

/* =========================================================================
 *                       FONCTIONS UTILITAIRES RÉSEAU
 * ========================================================================= */

/* Lecture d'une ligne terminée par '\n', comme dans services.c (version corrigée) */
static ssize_t read_line_fd(int fd, char *buf, size_t maxlen) {
    ssize_t n = 0;
    char c;
    ssize_t rc;

    if (maxlen == 0) return -1;

    while (n < (ssize_t)(maxlen - 1)) {
        rc = read(fd, &c, 1);
        if (rc == 1) {
            buf[n++] = c;
            if (c == '\n')
                break;
        } else if (rc == 0) {
            /* connexion fermée */
            if (n == 0)
                return 0;  /* aucune donnée lue */
            break;         /* on renvoie ce qu'on a déjà lu */
        } else {
            if (errno == EINTR)
                continue;
            return -1;
        }
    }

    /* On retire éventuellement le '\n' final */
    if (n > 0 && buf[n - 1] == '\n')
        n--;

    buf[n] = '\0';
    return n;
}

/* Connexion générique à un service ip + port */
static int connect_to_service(const char *ip, int port, char *errbuf, size_t errlen) {
    int sockfd;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        snprintf(errbuf, errlen, "socket() a échoué : %s", strerror(errno));
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons((unsigned short)port);

    if (inet_aton(ip, &serv_addr.sin_addr) == 0) {
        snprintf(errbuf, errlen, "Adresse IP invalide : %s", ip);
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        snprintf(errbuf, errlen, "connect() a échoué : %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/* Récupère le port pour un service donné ("DATE", "LS", "CAT", "DUREE") */
static int get_service_port(AppWidgets *app, const char *name) {
    for (int i = 0; i < app->nb_services; i++) {
        if (strcmp(app->services[i].name, name) == 0)
            return app->services[i].port;
    }
    return -1;
}

/* Petit helper pour afficher un texte complet dans la textview */
static void set_textview_text(GtkWidget *textview, const char *text) {
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_set_text(buffer, text ? text : "", -1);
}

/* =========================================================================
 *                    DIALOGUE DE SAISIE (pour LS / CAT)
 * ========================================================================= */

/* Demande une chaîne de caractères à l'utilisateur via une boîte de dialogue */
static gchar *prompt_for_text(GtkWindow *parent,
                              const gchar *title,
                              const gchar *message) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Annuler", GTK_RESPONSE_CANCEL,
        "_Valider", GTK_RESPONSE_OK,
        NULL
    );

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *vbox    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *label   = gtk_label_new(message);
    GtkWidget *entry   = gtk_entry_new();

    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(content), vbox);
    gtk_widget_show_all(dialog);

    gchar *result = NULL;

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text)
            result = g_strdup(text);
    }

    gtk_widget_destroy(dialog);
    return result;  /* peut être NULL si Annuler */
}

/* =========================================================================
 *                     COMMUNICATION AVEC LE SERVEUR CENTRAL
 * ========================================================================= */

/*
 * Connexion au serveur central, envoi login/password,
 * vérification de la réponse, puis lecture de la liste des services.
 * Remplit app->services[] et app->nb_services.
 * Retourne TRUE en cas de succès, FALSE sinon + message d'erreur.
 */
static gboolean central_auth_and_get_services(AppWidgets *app,
                                              const char *ip,
                                              const char *login,
                                              const char *passwd,
                                              char *errbuf,
                                              size_t errlen) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buf[BUF_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        snprintf(errbuf, errlen, "socket() a échoué : %s", strerror(errno));
        return FALSE;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(PORT_CENTRAL);

    if (inet_aton(ip, &serv_addr.sin_addr) == 0) {
        snprintf(errbuf, errlen, "Adresse IP invalide : %s", ip);
        close(sockfd);
        return FALSE;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        snprintf(errbuf, errlen, "connect() au serveur central a échoué : %s",
                 strerror(errno));
        close(sockfd);
        return FALSE;
    }

    /* Envoi login + password (format "<texte>\n") */
    snprintf(buf, sizeof(buf), "%.*s\n", (int)(sizeof(buf) - 2), login);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        snprintf(errbuf, errlen, "write(login) a échoué : %s", strerror(errno));
        close(sockfd);
        return FALSE;
    }

    snprintf(buf, sizeof(buf), "%.*s\n", (int)(sizeof(buf) - 2), passwd);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        snprintf(errbuf, errlen, "write(password) a échoué : %s", strerror(errno));
        close(sockfd);
        return FALSE;
    }

    /* Réponse "OK" ou "ERR" */
    if (read_line_fd(sockfd, buf, sizeof(buf)) <= 0) {
        snprintf(errbuf, errlen,
                 "Pas de réponse du serveur central lors de l'authentification.");
        close(sockfd);
        return FALSE;
    }

    if (strcmp(buf, "OK") != 0) {
        snprintf(errbuf, errlen,
                 "Authentification refusée par le serveur central.");
        close(sockfd);
        return FALSE;
    }

    /* Auth OK : on mémorise l'instant de début de session */
    app->session_start = time(NULL);

    /* Lecture liste des services */
    app->nb_services = 0;

    while (1) {
        ssize_t n = read_line_fd(sockfd, buf, sizeof(buf));
        if (n <= 0) {
            snprintf(errbuf, errlen,
                     "Connexion fermée pendant la réception des services.");
            close(sockfd);
            return FALSE;
        }

        if (strcmp(buf, "END_SERVICES") == 0)
            break;

        if (strcmp(buf, "SERVICES") == 0)
            continue;  /* ligne d'entête */

        int id, port;
        char name[32];

        if (sscanf(buf, "%d %31s %d", &id, name, &port) == 3) {
            if (app->nb_services < MAX_SERVICES) {
                app->services[app->nb_services].id   = id;
                app->services[app->nb_services].port = port;
                snprintf(app->services[app->nb_services].name,
                         sizeof(app->services[app->nb_services].name),
                         "%s", name);
                app->nb_services++;
            }
        }
        /* En cas de ligne invalide, on ignore silencieusement pour simplifier */
    }

    close(sockfd);

    if (app->nb_services == 0) {
        snprintf(errbuf, errlen,
                 "Aucun service fourni par le serveur central.");
        return FALSE;
    }

    return TRUE;
}

/* =========================================================================
 *                          APPELS AUX SERVICES
 * ========================================================================= */

/* --- Service DATE --- */
static void do_service_date(AppWidgets *app) {
    char err[256];
    int port = get_service_port(app, "DATE");
    if (port < 0) {
        set_textview_text(app->textview_output,
                          "[DATE] Service non disponible (port introuvable).");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    int sockfd = connect_to_service(app->server_ip, port, err, sizeof(err));
    if (sockfd < 0) {
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    char buf[BUF_SIZE];
    if (read_line_fd(sockfd, buf, sizeof(buf)) > 0) {
        char out[BUF_SIZE + 64];
        snprintf(out, sizeof(out), "[DATE] %s", buf);
        set_textview_text(app->textview_output, out);
    } else {
        set_textview_text(app->textview_output,
                          "[DATE] Erreur de lecture ou serveur fermé.");
    }

    close(sockfd);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
}

/* --- Service LS --- */
static void do_service_ls(AppWidgets *app) {
    char err[256];
    int port = get_service_port(app, "LS");
    if (port < 0) {
        set_textview_text(app->textview_output,
                          "[LS] Service non disponible (port introuvable).");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    GtkWindow *parent =
        GTK_WINDOW(gtk_widget_get_toplevel(app->stack));
    gchar *dirpath = prompt_for_text(parent,
                                     "Répertoire pour LS",
                                     "Chemin du répertoire sur le serveur (laisser vide pour '.') :");
    if (!dirpath) {
        /* utilisateur a annulé */
        return;
    }

    int sockfd = connect_to_service(app->server_ip, port, err, sizeof(err));
    if (sockfd < 0) {
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(dirpath);
        return;
    }

    char buf[BUF_SIZE];
    /* Envoi du chemin */
    snprintf(buf, sizeof(buf), "%s\n", dirpath);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err),
                 "[LS] Erreur d'envoi du chemin : %s", strerror(errno));
        set_textview_text(app->textview_output, err);
        close(sockfd);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(dirpath);
        return;
    }

    /* Lecture de la liste */
    GString *gs = g_string_new("[LS] Contenu du répertoire :\n");

    while (1) {
        ssize_t n = read_line_fd(sockfd, buf, sizeof(buf));
        if (n <= 0) {
            g_string_append(gs, "\n[LS] Le serveur a fermé la connexion.\n");
            break;
        }

        if (strcmp(buf, "END_LIST") == 0)
            break;

        g_string_append_printf(gs, "  %s\n", buf);
    }

    set_textview_text(app->textview_output, gs->str);
    g_string_free(gs, TRUE);

    close(sockfd);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
    g_free(dirpath);
}

/* --- Service CAT --- */
/* --- Service CAT --- */
static void do_service_cat(AppWidgets *app) {
    char err[256];
    int port = get_service_port(app, "CAT");
    if (port < 0) {
        set_textview_text(app->textview_output,
                          "[CAT] Service non disponible (port introuvable).");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    GtkWindow *parent =
        GTK_WINDOW(gtk_widget_get_toplevel(app->stack));
    gchar *filepath = prompt_for_text(parent,
                                      "Fichier pour CAT",
                                      "Chemin du fichier sur le serveur :");
    if (!filepath) {
        /* utilisateur a annulé */
        return;
    }

    int sockfd = connect_to_service(app->server_ip, port, err, sizeof(err));
    if (sockfd < 0) {
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(filepath);
        return;
    }

    char buf[BUF_SIZE];
    /* Envoi du chemin du fichier */
    snprintf(buf, sizeof(buf), "%s\n", filepath);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err),
                 "[CAT] Erreur d'envoi du chemin : %s", strerror(errno));
        set_textview_text(app->textview_output, err);
        close(sockfd);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(filepath);
        return;
    }

    /* Lecture du contenu */
    GString *gs = g_string_new("[CAT] Contenu du fichier :\n");

    while (1) {
        ssize_t n = read_line_fd(sockfd, buf, sizeof(buf));
        if (n <= 0) {
            /* fin de connexion : on ne rajoute PAS de message */
            break;
        }

        if (strcmp(buf, "EOF") == 0)
            break;

        g_string_append_printf(gs, "%s\n", buf);
    }

    set_textview_text(app->textview_output, gs->str);
    g_string_free(gs, TRUE);

    close(sockfd);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
    g_free(filepath);
}


/* --- Service DUREE --- */
static void do_service_duree(AppWidgets *app) {
    char err[256];
    int port = get_service_port(app, "DUREE");
    if (port < 0) {
        set_textview_text(app->textview_output,
                          "[DUREE] Service non disponible (port introuvable).");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    if (app->session_start == (time_t)0) {
        set_textview_text(app->textview_output,
                          "[DUREE] Erreur : temps de début de session inconnu.");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    int sockfd = connect_to_service(app->server_ip, port, err, sizeof(err));
    if (sockfd < 0) {
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf), "%ld\n", (long)app->session_start);
    if (write(sockfd, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err),
                 "[DUREE] Erreur d'envoi du timestamp : %s", strerror(errno));
        set_textview_text(app->textview_output, err);
        close(sockfd);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    if (read_line_fd(sockfd, buf, sizeof(buf)) > 0) {
        char out[BUF_SIZE + 64];
        snprintf(out, sizeof(out), "[DUREE] %s", buf);
        set_textview_text(app->textview_output, out);
    } else {
        set_textview_text(app->textview_output,
                          "[DUREE] Erreur de lecture ou serveur fermé.");
    }

    close(sockfd);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
}

/* =========================================================================
 *                                CALLBACKS GUI
 * ========================================================================= */

static void on_login_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;

    const char *ip     = gtk_entry_get_text(GTK_ENTRY(app->entry_ip));
    const char *login  = gtk_entry_get_text(GTK_ENTRY(app->entry_login));
    const char *passwd = gtk_entry_get_text(GTK_ENTRY(app->entry_password));

    if (ip[0] == '\0' || login[0] == '\0' || passwd[0] == '\0') {
        gtk_label_set_text(GTK_LABEL(app->label_status),
                           "Veuillez remplir IP, login et mot de passe.");
        return;
    }

    char err[256];
    if (!central_auth_and_get_services(app, ip, login, passwd, err, sizeof(err))) {
        gtk_label_set_text(GTK_LABEL(app->label_status), err);
        return;
    }

    /* Mémoriser l'IP du serveur pour les services */
    if (app->server_ip)
        g_free(app->server_ip);
    app->server_ip = g_strdup(ip);

    gtk_label_set_text(GTK_LABEL(app->label_status),
                       "Authentification réussie. Services chargés.");
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_menu");
}

static void on_menu_date_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;
    do_service_date(app);
}

static void on_menu_ls_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;
    do_service_ls(app);
}

static void on_menu_cat_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;
    do_service_cat(app);
}

static void on_menu_duree_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;
    do_service_duree(app);
}

static void on_back_to_menu_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;

    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_menu");
}

/* =========================================================================
 *                      CONSTRUCTION DE L'INTERFACE
 * ========================================================================= */

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    AppWidgets *widgets = g_new0(AppWidgets, 1);

    GtkWidget *window =
        gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Client Multiservices (GTK)");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    // Conteneur vertical principal
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Stack SANS switcher (pas d'onglets visibles)
    widgets->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(widgets->stack),
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(widgets->stack), 250);

    gtk_box_pack_start(GTK_BOX(vbox), widgets->stack, TRUE, TRUE, 0);

    /* === Page 1 : Login === */
    GtkWidget *grid_login = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid_login), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid_login), 5);
    gtk_container_set_border_width(GTK_CONTAINER(grid_login), 10);

    GtkWidget *label_ip   = gtk_label_new("Adresse IP du serveur central :");
    GtkWidget *label_user = gtk_label_new("Login :");
    GtkWidget *label_pass = gtk_label_new("Mot de passe :");

    widgets->entry_ip       = gtk_entry_new();
    widgets->entry_login    = gtk_entry_new();
    widgets->entry_password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(widgets->entry_password), FALSE);

    GtkWidget *btn_login = gtk_button_new_with_label("Se connecter");

    widgets->label_status = gtk_label_new("");

    gtk_grid_attach(GTK_GRID(grid_login), label_ip,   0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->entry_ip, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid_login), label_user, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->entry_login, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid_login), label_pass, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->entry_password, 1, 2, 1, 1);

    gtk_grid_attach(GTK_GRID(grid_login), btn_login,  0, 3, 2, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->label_status, 0, 4, 2, 1);

    gtk_stack_add_titled(GTK_STACK(widgets->stack),
                         grid_login,
                         "page_login",
                         "Login");

    /* === Page 2 : Menu des services === */
    GtkWidget *box_menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box_menu), 10);

    GtkWidget *label_menu = gtk_label_new("Menu des services :");
    GtkWidget *btn_date   = gtk_button_new_with_label("Service 1 : DATE");
    GtkWidget *btn_ls     = gtk_button_new_with_label("Service 2 : LS");
    GtkWidget *btn_cat    = gtk_button_new_with_label("Service 3 : CAT");
    GtkWidget *btn_duree  = gtk_button_new_with_label("Service 4 : DUREE");

    gtk_box_pack_start(GTK_BOX(box_menu), label_menu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_date,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_ls,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_cat,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_duree,  FALSE, FALSE, 0);

    gtk_stack_add_titled(GTK_STACK(widgets->stack),
                         box_menu,
                         "page_menu",
                         "Menu");

    /* === Page 3 : Résultat d'un service === */
    GtkWidget *box_service = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box_service), 10);

    GtkWidget *label_service = gtk_label_new("Résultat du service :");
    widgets->textview_output = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(widgets->textview_output), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(widgets->textview_output),
                                GTK_WRAP_WORD_CHAR);

    GtkWidget *btn_back = gtk_button_new_with_label("Retour au menu");

    gtk_box_pack_start(GTK_BOX(box_service), label_service, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_service), widgets->textview_output,
                       TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box_service), btn_back, FALSE, FALSE, 0);

    gtk_stack_add_titled(GTK_STACK(widgets->stack),
                         box_service,
                         "page_service",
                         "Service");

    /* Connexion des signaux */
    g_signal_connect(btn_login, "clicked",
                     G_CALLBACK(on_login_clicked), widgets);

    g_signal_connect(btn_date, "clicked",
                     G_CALLBACK(on_menu_date_clicked), widgets);
    g_signal_connect(btn_ls, "clicked",
                     G_CALLBACK(on_menu_ls_clicked), widgets);
    g_signal_connect(btn_cat, "clicked",
                     G_CALLBACK(on_menu_cat_clicked), widgets);
    g_signal_connect(btn_duree, "clicked",
                     G_CALLBACK(on_menu_duree_clicked), widgets);

    g_signal_connect(btn_back, "clicked",
                     G_CALLBACK(on_back_to_menu_clicked), widgets);

    // Page initiale : login uniquement
    gtk_stack_set_visible_child_name(GTK_STACK(widgets->stack), "page_login");

    gtk_widget_show_all(window);
}

int main(int argc, char *argv[]) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.example.clientGUI", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
