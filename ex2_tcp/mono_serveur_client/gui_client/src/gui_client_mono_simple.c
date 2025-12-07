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

#include "services.h"   /* <- use shared read_line(), write_ignore, etc. */

#define BUF_SIZE 1024

typedef struct {
    GtkWidget *stack;
    GtkWidget *entry_ip;
    GtkWidget *entry_port;
    GtkWidget *entry_login;
    GtkWidget *entry_password;
    GtkWidget *label_status;
    GtkWidget *textview_output;

    int        sockfd;        /* socket TCP vers le serveur */
    gboolean   authenticated; /* TRUE après OK */

} AppWidgets;

/* =========================================================================
 *                       FONCTIONS UTILITAIRES RÉSEAU
 * ========================================================================= */

/* Connexion au serveur (IP + port) */
static int connect_to_server(const char *ip, int port, char *errbuf, size_t errlen) {
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

/* Affiche du texte dans la textview */
static void set_textview_text(GtkWidget *textview, const char *text) {
    GtkTextBuffer *buffer =
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_set_text(buffer, text ? text : "", -1);
}

/* =========================================================================
 *                       DIALOGUES DE SAISIE (LS / CAT)
 * ========================================================================= */

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
    return result;
}

/* =========================================================================
 *                   APPELS DE SERVICES SUR LA MÊME SOCKET
 * ========================================================================= */

/* Envoie un choix numérique (1..4 ou 0) au serveur */
static gboolean send_choice(AppWidgets *app, int choice, char *errbuf, size_t errlen) {
    if (app->sockfd < 0 || !app->authenticated) {
        snprintf(errbuf, errlen, "Non connecté au serveur.");
        return FALSE;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d\n", choice);

    if (write(app->sockfd, buf, strlen(buf)) < 0) {
        snprintf(errbuf, errlen, "Erreur d'envoi du choix : %s", strerror(errno));
        return FALSE;
    }
    return TRUE;
}

/* --- Service 1 : DATE --- */
static void do_service_date(AppWidgets *app) {
    char err[256];
    char buf[BUF_SIZE];

    if (!send_choice(app, 1, err, sizeof(err))) {
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    if (read_line(app->sockfd, buf, sizeof(buf)) <= 0) {
        set_textview_text(app->textview_output,
                          "[DATE] Erreur de lecture ou connexion fermée.");
    } else {
        char out[BUF_SIZE + 32];
        snprintf(out, sizeof(out), "[DATE] %s\n", buf);
        set_textview_text(app->textview_output, out);
    }

    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
}

/* --- Service 2 : LS --- */
static void do_service_ls(AppWidgets *app) {
    char err[256];
    char buf[BUF_SIZE];

    GtkWindow *parent =
        GTK_WINDOW(gtk_widget_get_toplevel(app->stack));
    gchar *dirpath = prompt_for_text(parent,
                                     "Service LS",
                                     "Chemin du répertoire sur le serveur (vide = répertoire courant) :");
    if (!dirpath)
        return;

    if (!send_choice(app, 2, err, sizeof(err))) {
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(dirpath);
        return;
    }

    /* Envoi du chemin */
    snprintf(buf, sizeof(buf), "%.*s\n",
             (int)(sizeof(buf) - 2), dirpath);
    if (write(app->sockfd, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err),
                 "[LS] Erreur d'envoi du chemin : %s", strerror(errno));
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(dirpath);
        return;
    }

    GString *gs = g_string_new("[LS] Contenu du répertoire :\n");

    while (1) {
        ssize_t n = read_line(app->sockfd, buf, sizeof(buf));
        if (n <= 0) {
            g_string_append(gs, "\n[LS] Connexion fermée ou erreur.\n");
            break;
        }

        if (strcmp(buf, "END_LIST") == 0)
            break;

        if (strncmp(buf, "ERREUR", 6) == 0) {
            g_string_append_printf(gs, "%s\n", buf);
            break;
        }

        g_string_append_printf(gs, "  %s\n", buf);
    }

    set_textview_text(app->textview_output, gs->str);
    g_string_free(gs, TRUE);

    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
    g_free(dirpath);
}

/* --- Service 3 : CAT --- */
static void do_service_cat(AppWidgets *app) {
    char err[256];
    char buf[BUF_SIZE];

    GtkWindow *parent =
        GTK_WINDOW(gtk_widget_get_toplevel(app->stack));
    gchar *filepath = prompt_for_text(parent,
                                      "Service CAT",
                                      "Chemin du fichier sur le serveur :");
    if (!filepath)
        return;

    if (!send_choice(app, 3, err, sizeof(err))) {
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(filepath);
        return;
    }

    /* Envoi du chemin */
    snprintf(buf, sizeof(buf), "%.*s\n",
             (int)(sizeof(buf) - 2), filepath);
    if (write(app->sockfd, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err),
                 "[CAT] Erreur d'envoi du chemin : %s", strerror(errno));
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(filepath);
        return;
    }

    GString *gs = g_string_new("[CAT] Contenu du fichier :\n");

    while (1) {
        ssize_t n = read_line(app->sockfd, buf, sizeof(buf));
        if (n <= 0) {
            break; /* fin de connexion ou erreur => on arrête de lire */
        }

        if (strcmp(buf, "EOF") == 0)
            break;

        if (strncmp(buf, "ERREUR", 6) == 0) {
            g_string_append_printf(gs, "%s\n", buf);
            break;
        }

        g_string_append_printf(gs, "%s\n", buf);
    }

    set_textview_text(app->textview_output, gs->str);
    g_string_free(gs, TRUE);

    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
    g_free(filepath);
}

/* --- Service 4 : DUREE --- */
static void do_service_duree(AppWidgets *app) {
    char err[256];
    char buf[BUF_SIZE];

    if (!send_choice(app, 4, err, sizeof(err))) {
        set_textview_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    if (read_line(app->sockfd, buf, sizeof(buf)) <= 0) {
        set_textview_text(app->textview_output,
                          "[DUREE] Erreur de lecture ou connexion fermée.");
    } else {
        char out[BUF_SIZE + 32];
        snprintf(out, sizeof(out), "[DUREE] %s\n", buf);
        set_textview_text(app->textview_output, out);
    }

    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
}

/* --- Envoi du choix 0 pour fermer proprement côté serveur --- */
static void send_quit_and_close(AppWidgets *app) {
    if (app->sockfd >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "0\n");
        /* on ignore les erreurs d'écriture ici */
        write(app->sockfd, buf, strlen(buf));
        close(app->sockfd);
        app->sockfd = -1;
    }
    app->authenticated = FALSE;
}

/* =========================================================================
 *                                CALLBACKS GUI
 * ========================================================================= */

static void on_login_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;

    const char *ip     = gtk_entry_get_text(GTK_ENTRY(app->entry_ip));
    const char *port_s = gtk_entry_get_text(GTK_ENTRY(app->entry_port));
    const char *login  = gtk_entry_get_text(GTK_ENTRY(app->entry_login));
    const char *passwd = gtk_entry_get_text(GTK_ENTRY(app->entry_password));

    if (ip[0] == '\0' || port_s[0] == '\0' ||
        login[0] == '\0' || passwd[0] == '\0') {
        gtk_label_set_text(GTK_LABEL(app->label_status),
                           "Veuillez remplir IP, port, login et mot de passe.");
        return;
    }

    int port = atoi(port_s);
    if (port <= 0 || port > 65535) {
        gtk_label_set_text(GTK_LABEL(app->label_status),
                           "Port invalide.");
        return;
    }

    char err[256];
    int sockfd = connect_to_server(ip, port, err, sizeof(err));
    if (sockfd < 0) {
        gtk_label_set_text(GTK_LABEL(app->label_status), err);
        return;
    }

    app->sockfd = sockfd;

    char buf[BUF_SIZE];

    /* Envoi login + password */
    snprintf(buf, sizeof(buf), "%.*s\n", (int)(sizeof(buf) - 2), login);
    if (write(app->sockfd, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err), "Erreur write(login) : %s", strerror(errno));
        gtk_label_set_text(GTK_LABEL(app->label_status), err);
        send_quit_and_close(app);
        return;
    }

    snprintf(buf, sizeof(buf), "%.*s\n", (int)(sizeof(buf) - 2), passwd);
    if (write(app->sockfd, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err), "Erreur write(password) : %s", strerror(errno));
        gtk_label_set_text(GTK_LABEL(app->label_status), err);
        send_quit_and_close(app);
        return;
    }

    /* Lecture OK / ERR */
    if (read_line(app->sockfd, buf, sizeof(buf)) <= 0) {
        gtk_label_set_text(GTK_LABEL(app->label_status),
                           "Pas de réponse du serveur pendant l'authentification.");
        send_quit_and_close(app);
        return;
    }

    if (strcmp(buf, "OK") != 0) {
        gtk_label_set_text(GTK_LABEL(app->label_status),
                           "Authentification refusée.");
        send_quit_and_close(app);
        return;
    }

    app->authenticated = TRUE;

    gtk_label_set_text(GTK_LABEL(app->label_status),
                       "Authentification réussie, connecté au serveur.");
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

/* Bouton "Quitter" : envoie choix 0 puis ferme la fenêtre */
static void on_quit_session_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppWidgets *app = (AppWidgets *)user_data;

    send_quit_and_close(app);

    GtkWidget *win = gtk_widget_get_toplevel(app->stack);
    gtk_window_close(GTK_WINDOW(win));
}

/* =========================================================================
 *                     CONSTRUCTION DE L'INTERFACE GTK
 * ========================================================================= */

static void activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    AppWidgets *widgets = g_new0(AppWidgets, 1);
    widgets->sockfd        = -1;
    widgets->authenticated = FALSE;

    GtkWidget *window =
        gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Client Mono-Serveur (GTK)");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

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

    GtkWidget *label_ip   = gtk_label_new("Adresse IP du serveur :");
    GtkWidget *label_port = gtk_label_new("Port :");
    GtkWidget *label_user = gtk_label_new("Login :");
    GtkWidget *label_pass = gtk_label_new("Mot de passe :");

    widgets->entry_ip       = gtk_entry_new();
    widgets->entry_port     = gtk_entry_new();
    widgets->entry_login    = gtk_entry_new();
    widgets->entry_password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(widgets->entry_password), FALSE);

    GtkWidget *btn_login = gtk_button_new_with_label("Se connecter");

    widgets->label_status = gtk_label_new("");

    gtk_grid_attach(GTK_GRID(grid_login), label_ip,   0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->entry_ip, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid_login), label_port, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->entry_port, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid_login), label_user, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->entry_login, 1, 2, 1, 1);

    gtk_grid_attach(GTK_GRID(grid_login), label_pass, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->entry_password, 1, 3, 1, 1);

    gtk_grid_attach(GTK_GRID(grid_login), btn_login,  0, 4, 2, 1);
    gtk_grid_attach(GTK_GRID(grid_login), widgets->label_status, 0, 5, 2, 1);

    gtk_stack_add_titled(GTK_STACK(widgets->stack),
                         grid_login,
                         "page_login",
                         "Login");

    /* === Page 2 : Menu === */
    GtkWidget *box_menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box_menu), 10);

    GtkWidget *label_menu = gtk_label_new("Menu des services (mono-serveur) :");
    GtkWidget *btn_date   = gtk_button_new_with_label("1) DATE");
    GtkWidget *btn_ls     = gtk_button_new_with_label("2) LS");
    GtkWidget *btn_cat    = gtk_button_new_with_label("3) CAT");
    GtkWidget *btn_duree  = gtk_button_new_with_label("4) DUREE");
    GtkWidget *btn_quit   = gtk_button_new_with_label("0) Quitter la session");

    gtk_box_pack_start(GTK_BOX(box_menu), label_menu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_date,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_ls,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_cat,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_duree,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box_menu), btn_quit,   FALSE, FALSE, 0);

    gtk_stack_add_titled(GTK_STACK(widgets->stack),
                         box_menu,
                         "page_menu",
                         "Menu");

    /* === Page 3 : Résultat service === */
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

    /* Connexion signaux */
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
    g_signal_connect(btn_quit, "clicked",
                     G_CALLBACK(on_quit_session_clicked), widgets);

    g_signal_connect(btn_back, "clicked",
                     G_CALLBACK(on_back_to_menu_clicked), widgets);

    gtk_stack_set_visible_child_name(GTK_STACK(widgets->stack), "page_login");

    gtk_widget_show_all(window);
}

int main(int argc, char *argv[]) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.example.clientGUI.mono_simple", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
