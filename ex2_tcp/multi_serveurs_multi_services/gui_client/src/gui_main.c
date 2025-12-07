// ================================================================
//   GUI CLIENT — MULTI-SERVERS / MULTI-SERVICES VERSION
//   With "0) Quitter" button and warnings fixed
// ================================================================

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

#include "services.h"

#define BUF_SIZE     1024
#define PORT_CENTRAL 5000
#define MAX_SERVICES 10

typedef struct {
    int  id;
    char name[32];
    int  port;
} ServiceInfo;

/* GUI + session info */
typedef struct {
    GtkWidget   *stack;
    GtkWidget   *entry_ip;
    GtkWidget   *entry_login;
    GtkWidget   *entry_password;
    GtkWidget   *label_status;
    GtkWidget   *textview_output;

    gchar       *server_ip;
    ServiceInfo  services[MAX_SERVICES];
    int          nb_services;
    time_t       session_start;

} AppWidgets;

/* ============================================================
   Connect to a service
   ============================================================ */
static int connect_to_service(const char *ip, int port, char *errbuf, size_t errlen) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        snprintf(errbuf, errlen, "socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);

    if (inet_aton(ip, &addr.sin_addr) == 0) {
        snprintf(errbuf, errlen, "Invalid IP: %s", ip);
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        snprintf(errbuf, errlen, "connect() failed: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

/* Look up a service port */
static int get_service_port(AppWidgets *app, const char *name) {
    for (int i = 0; i < app->nb_services; i++)
        if (strcmp(app->services[i].name, name) == 0)
            return app->services[i].port;
    return -1;
}

static void set_text(GtkWidget *textview, const char *txt) {
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    gtk_text_buffer_set_text(b, txt ? txt : "", -1);
}

/* ============================================================
   Dialog prompt for LS / CAT
   ============================================================ */
static gchar *prompt(GtkWindow *parent, const char *title, const char *msg) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        title, parent,
        GTK_DIALOG_MODAL,
        "_Annuler", GTK_RESPONSE_CANCEL,
        "_Valider", GTK_RESPONSE_OK,
        NULL
    );

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *lbl = gtk_label_new(msg);
    GtkWidget *entry = gtk_entry_new();

    gtk_box_pack_start(GTK_BOX(v), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(v), entry, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(box), v);

    gtk_widget_show_all(dlg);

    gchar *res = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        const gchar *t = gtk_entry_get_text(GTK_ENTRY(entry));
        if (t) res = g_strdup(t);
    }
    gtk_widget_destroy(dlg);
    return res;
}

/* ============================================================
   Central server authentication + service list
   ============================================================ */
static gboolean central_auth(AppWidgets *app,
                             const char *ip,
                             const char *login,
                             const char *pass,
                             char *err, size_t errlen)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        snprintf(err, errlen, "socket failed: %s", strerror(errno));
        return FALSE;
    }

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons(PORT_CENTRAL);

    if (inet_aton(ip, &a.sin_addr) == 0) {
        snprintf(err, errlen, "Invalid IP: %s", ip);
        close(sock);
        return FALSE;
    }

    if (connect(sock, (struct sockaddr*)&a, sizeof(a)) < 0) {
        snprintf(err, errlen, "connect failed: %s", strerror(errno));
        close(sock);
        return FALSE;
    }

    char buf[BUF_SIZE];

    /* Send login */
    snprintf(buf, sizeof(buf), "%s\n", login);
    if (write(sock, buf, strlen(buf)) < 0) {
        snprintf(err, errlen, "write(login) failed: %s", strerror(errno));
        close(sock);
        return FALSE;
    }

    /* Send password */
    snprintf(buf, sizeof(buf), "%s\n", pass);
    if (write(sock, buf, strlen(buf)) < 0) {
        snprintf(err, errlen, "write(password) failed: %s", strerror(errno));
        close(sock);
        return FALSE;
    }

    /* Read response */
    if (read_line(sock, buf, sizeof(buf)) <= 0) {
        snprintf(err, errlen, "No response during authentication.");
        close(sock);
        return FALSE;
    }

    if (strcmp(buf, "OK") != 0) {
        snprintf(err, errlen, "Authentication failed.");
        close(sock);
        return FALSE;
    }

    app->session_start = time(NULL);
    app->nb_services   = 0;

    /* Read service list */
    while (1) {
        ssize_t n = read_line(sock, buf, sizeof(buf));
        if (n <= 0) {
            snprintf(err, errlen, "Connection closed while reading services.");
            close(sock);
            return FALSE;
        }

        if (strcmp(buf, "END_SERVICES") == 0)
            break;

        if (strcmp(buf, "SERVICES") == 0)
            continue;

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
    }

    close(sock);
    return TRUE;
}

/* ============================================================
   Service implementations
   ============================================================ */
static void svc_date(AppWidgets *app) {
    char err[256], buf[BUF_SIZE];
    int port = get_service_port(app, "DATE");

    if (port < 0) {
        set_text(app->textview_output, "[DATE] Service not available.");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    int sock = connect_to_service(app->server_ip, port, err, sizeof(err));
    if (sock < 0) {
        set_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    if (read_line(sock, buf, sizeof(buf)) > 0) {
        char out[BUF_SIZE + 32];
        snprintf(out, sizeof(out), "[DATE] %s\n", buf);
        set_text(app->textview_output, out);
    } else {
        set_text(app->textview_output, "[DATE] Connection closed.");
    }

    close(sock);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
}

static void svc_ls(AppWidgets *app) {
    char err[256], buf[BUF_SIZE];

    int port = get_service_port(app, "LS");
    if (port < 0) {
        set_text(app->textview_output, "[LS] Service not available.");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(app->stack));
    gchar *dir = prompt(parent, "Service LS",
                        "Chemin du répertoire sur le serveur (vide = .)");
    if (!dir) return;

    int sock = connect_to_service(app->server_ip, port, err, sizeof(err));
    if (sock < 0) {
        set_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(dir);
        return;
    }

    snprintf(buf, sizeof(buf), "%s\n", dir);
    if (write(sock, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err), "[LS] write failed: %s", strerror(errno));
        set_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        close(sock);
        g_free(dir);
        return;
    }

    GString *gs = g_string_new("[LS] Contenu du répertoire:\n");

    while (1) {
        ssize_t n = read_line(sock, buf, sizeof(buf));
        if (n <= 0) break;
        if (strcmp(buf, "END_LIST") == 0) break;
        g_string_append_printf(gs, "  %s\n", buf);
    }

    set_text(app->textview_output, gs->str);
    g_string_free(gs, TRUE);
    close(sock);

    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
    g_free(dir);
}

static void svc_cat(AppWidgets *app) {
    char err[256], buf[BUF_SIZE];

    int port = get_service_port(app, "CAT");
    if (port < 0) {
        set_text(app->textview_output, "[CAT] Service not available.");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(app->stack));
    gchar *file = prompt(parent, "Service CAT",
                         "Chemin du fichier sur le serveur:");
    if (!file) return;

    int sock = connect_to_service(app->server_ip, port, err, sizeof(err));
    if (sock < 0) {
        set_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        g_free(file);
        return;
    }

    snprintf(buf, sizeof(buf), "%s\n", file);
    if (write(sock, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err), "[CAT] write failed: %s", strerror(errno));
        set_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        close(sock);
        g_free(file);
        return;
    }

    GString *gs = g_string_new("[CAT] Contenu du fichier:\n");

    while (1) {
        ssize_t n = read_line(sock, buf, sizeof(buf));
        if (n <= 0) break;
        if (strcmp(buf, "EOF") == 0) break;
        g_string_append_printf(gs, "%s\n", buf);
    }

    set_text(app->textview_output, gs->str);
    g_string_free(gs, TRUE);

    close(sock);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
    g_free(file);
}

static void svc_duree(AppWidgets *app) {
    char err[256], buf[BUF_SIZE];

    int port = get_service_port(app, "DUREE");
    if (port < 0) {
        set_text(app->textview_output, "[DUREE] Service not available.");
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    int sock = connect_to_service(app->server_ip, port, err, sizeof(err));
    if (sock < 0) {
        set_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        return;
    }

    snprintf(buf, sizeof(buf), "%ld\n", (long)app->session_start);
    if (write(sock, buf, strlen(buf)) < 0) {
        snprintf(err, sizeof(err), "[DUREE] write failed: %s", strerror(errno));
        set_text(app->textview_output, err);
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
        close(sock);
        return;
    }

    if (read_line(sock, buf, sizeof(buf)) > 0) {
        char out[BUF_SIZE + 32];
        snprintf(out, sizeof(out), "[DUREE] %s\n", buf);
        set_text(app->textview_output, out);
    } else {
        set_text(app->textview_output, "[DUREE] No response.");
    }

    close(sock);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_service");
}

/* ============================================================
   QUIT BUTTON CALLBACK
   ============================================================ */
static void on_quit_clicked(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    GtkWidget *win = gtk_widget_get_toplevel(GTK_WIDGET(btn));
    gtk_window_close(GTK_WINDOW(win));
}

/* ============================================================
   LOGIN CALLBACK
   ============================================================ */
static void on_login(GtkButton *btn, gpointer data) {
    (void)btn;
    AppWidgets *app = (AppWidgets*)data;

    const char *ip  = gtk_entry_get_text(GTK_ENTRY(app->entry_ip));
    const char *log = gtk_entry_get_text(GTK_ENTRY(app->entry_login));
    const char *pwd = gtk_entry_get_text(GTK_ENTRY(app->entry_password));

    if (!ip[0] || !log[0] || !pwd[0]) {
        gtk_label_set_text(GTK_LABEL(app->label_status),
                           "Veuillez remplir tous les champs.");
        return;
    }

    char err[256];
    if (!central_auth(app, ip, log, pwd, err, sizeof(err))) {
        gtk_label_set_text(GTK_LABEL(app->label_status), err);
        return;
    }

    if (app->server_ip)
        g_free(app->server_ip);
    app->server_ip = g_strdup(ip);

    gtk_label_set_text(GTK_LABEL(app->label_status),
                       "Authentification OK.");
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_menu");
}

/* ============================================================
   MENU CALLBACKS
   ============================================================ */
static void on_svc_date(GtkButton *b, gpointer d){
    (void)b;
    svc_date((AppWidgets*)d);
}
static void on_svc_ls(GtkButton *b, gpointer d){
    (void)b;
    svc_ls((AppWidgets*)d);
}
static void on_svc_cat(GtkButton *b, gpointer d){
    (void)b;
    svc_cat((AppWidgets*)d);
}
static void on_svc_duree(GtkButton *b, gpointer d){
    (void)b;
    svc_duree((AppWidgets*)d);
}

static void on_back(GtkButton *b, gpointer d) {
    (void)b;
    AppWidgets *app = (AppWidgets*)d;
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "page_menu");
}

/* ============================================================
   GTK UI CONSTRUCTION
   ============================================================ */
static void activate(GtkApplication *app, gpointer data) {
    (void)data;

    AppWidgets *w = g_new0(AppWidgets, 1);

    GtkWidget *win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win),
                         "Client Multi-Services (GTK)");
    gtk_window_set_default_size(GTK_WINDOW(win), 600, 400);

    GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(win), v);

    w->stack = gtk_stack_new();
    gtk_box_pack_start(GTK_BOX(v), w->stack, TRUE, TRUE, 0);

    /* ===========================
       PAGE LOGIN
       =========================== */
    GtkWidget *grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    w->entry_ip       = gtk_entry_new();
    w->entry_login    = gtk_entry_new();
    w->entry_password = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(w->entry_password), FALSE);

    w->label_status = gtk_label_new("");

    GtkWidget *btn_login = gtk_button_new_with_label("Se connecter");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("IP:"), 0,0,1,1);
    gtk_grid_attach(GTK_GRID(grid), w->entry_ip,                         1,0,1,1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Login:"),             0,1,1,1);
    gtk_grid_attach(GTK_GRID(grid), w->entry_login,                      1,1,1,1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Mot de passe:"),      0,2,1,1);
    gtk_grid_attach(GTK_GRID(grid), w->entry_password,                   1,2,1,1);

    gtk_grid_attach(GTK_GRID(grid), btn_login,                           0,3,2,1);
    gtk_grid_attach(GTK_GRID(grid), w->label_status,                     0,4,2,1);

    gtk_stack_add_titled(GTK_STACK(w->stack), grid, "page_login", "Login");

    /* ===========================
       PAGE MENU
       =========================== */
    GtkWidget *menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(menu), 10);

    GtkWidget *btn_date  = gtk_button_new_with_label("1) DATE");
    GtkWidget *btn_ls    = gtk_button_new_with_label("2) LS");
    GtkWidget *btn_cat   = gtk_button_new_with_label("3) CAT");
    GtkWidget *btn_duree = gtk_button_new_with_label("4) DUREE");
    GtkWidget *btn_quit  = gtk_button_new_with_label("0) Quitter");

    gtk_box_pack_start(GTK_BOX(menu), gtk_label_new("Menu des services:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu), btn_date,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu), btn_ls,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu), btn_cat,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu), btn_duree, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menu), btn_quit,  FALSE, FALSE, 0);

    gtk_stack_add_titled(GTK_STACK(w->stack), menu, "page_menu", "Menu");

    /* ===========================
       PAGE SERVICE
       =========================== */
    GtkWidget *svc = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(svc), 10);

    w->textview_output = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->textview_output), FALSE);

    GtkWidget *btn_back = gtk_button_new_with_label("Retour au menu");

    gtk_box_pack_start(GTK_BOX(svc), gtk_label_new("Résultat du service:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(svc), w->textview_output, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(svc), btn_back, FALSE, FALSE, 0);

    gtk_stack_add_titled(GTK_STACK(w->stack), svc, "page_service", "Service");

    /* CONNECT SIGNALS */
    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_login), w);

    g_signal_connect(btn_date,  "clicked", G_CALLBACK(on_svc_date),  w);
    g_signal_connect(btn_ls,    "clicked", G_CALLBACK(on_svc_ls),    w);
    g_signal_connect(btn_cat,   "clicked", G_CALLBACK(on_svc_cat),   w);
    g_signal_connect(btn_duree, "clicked", G_CALLBACK(on_svc_duree), w);

    g_signal_connect(btn_quit, "clicked", G_CALLBACK(on_quit_clicked), w);

    g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back), w);

    gtk_stack_set_visible_child_name(GTK_STACK(w->stack), "page_login");

    gtk_widget_show_all(win);
}

/* ============================================================
   main()
   ============================================================ */
int main(int argc, char *argv[]) {
    GtkApplication *app =
        gtk_application_new("org.example.multiservices.gui", G_APPLICATION_FLAGS_NONE);

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}
