#include <gtk/gtk.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "gtk4-layer-shell.h"

// TODO: put global gtk widgets into structs
// consider a module system?

// main bar components
GtkCenterBox *bar;
GtkBox *left;
GtkBox *center;
GtkBox *right;

// info widgets
GtkBox *workspaces;
char workspace_names[10][64] = {"1", "2", "3", "4", "5",
                                "6", "7", "8", "9", "10"};

GtkLabel *activewin;
pthread_t activewin_thread;

GtkLabel *datetime;
pthread_t datetime_thread;

static int socket_create(const char *socket_path) {
    // create file descriptor for use with unix sockets
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("Error creating socket");
        exit(1);
    }

    // create unix socket address struct and copy in socket_path
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // connect to the socket
    if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Error connecting to socket");
        close(socket_fd);
        exit(1);
    }

    return socket_fd;
}

static int socket_readline(int socket_fd, char *buf, size_t size) {
    size_t bytes_read = 0;
    while (1) {
        char c;
        ssize_t nread = recv(socket_fd, &c, 1, 0);

        if (nread == -1) {
            perror("Error while calling recv on socket");
            exit(1);
        }

        if (bytes_read < size - 1) {
            buf[bytes_read] = c;
        }

        if (c == '\n') {
            buf[bytes_read] = '\0';
            break;
        }

        bytes_read += nread;
    }

    return bytes_read;
}

static void *watch_activewin(void *user_data) {
    GtkLabel *label = GTK_LABEL(user_data);

    char *instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (!instance_signature || !xdg_runtime_dir) {
        fprintf(stderr, "Error: Environment variables not set.\n");
        exit(1);
    }

    char socket_path[256];
    snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket2.sock",
             xdg_runtime_dir, instance_signature);

    int socket_fd = socket_create(socket_path);

    char event[256] = {0};
    char curtext[72] = {0};
    size_t nread;
    while ((nread = socket_readline(socket_fd, event, sizeof(event))) > 0) {
        // printf("%s\n", event); // log hyprland events

        // if its an activewindow event
        if (strncmp(event, "activewindow>>", strlen("activewindow>>")) == 0) {
            // copy title of active window
            char *comma_loc = strchr(event, ',');
            strncpy(curtext, comma_loc + 1, sizeof(curtext) - 1);

            // add ellipses to truncate
            curtext[sizeof(curtext) - 4] = '.';
            curtext[sizeof(curtext) - 3] = '.';
            curtext[sizeof(curtext) - 2] = '.';

            gtk_label_set_text(label, curtext);
        } else if (strncmp(event, "workspacev2>>", strlen("workspacev2>>")) ==
                   0) {
            size_t loc = strcspn(event, ">>");
            char *comma_loc = strchr(&event[loc], ',');
            event[loc] = '\0';

            // subtract one since hyrpland ID's are 1 indexed
            // and we store workspace names 0 indexed
            int ws_id = atoi(&event[loc + 2]) - 1;
            char *ws_name = comma_loc + 1;

            GtkWidget *child =
                gtk_widget_get_first_child(GTK_WIDGET(workspaces));
            for (int i = 0; child != NULL; i++) {
                if (i == ws_id) {
                    gtk_widget_add_css_class(child, "active");
                } else {
                    gtk_widget_remove_css_class(child, "active");
                }
                child = gtk_widget_get_next_sibling(child);
            }
        }
    }

    close(socket_fd);

    return NULL;
}

static void *watch_datetime(void *data) {
    GtkLabel *label = GTK_LABEL(data);

    while (1) {
        time_t current_time;
        struct timespec ts;

        time(&current_time);
        struct tm *time_info = localtime(&current_time);

        char time_string[64];
        strftime(time_string, sizeof(time_string), "%a %b %-d %-I:%M:%S %p",
                 time_info);
        gtk_label_set_text(label, time_string);

        // Get current time with nanosecond precision
        clock_gettime(CLOCK_MONOTONIC, &ts);

        // Sleep until the next second boundary
        long sleep_time = 1000000000L - ts.tv_nsec;
        struct timespec req = {0, sleep_time};
        nanosleep(&req, NULL);
    }

    return NULL;
}

static void make_bar() {
    // CONTAINERS
    // main bar
    bar = GTK_CENTER_BOX(gtk_center_box_new());
    gtk_widget_set_name(GTK_WIDGET(bar), "wb");

    // left box
    left = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_set_name(GTK_WIDGET(left), "left");
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(bar), GTK_WIDGET(left));

    // center box
    center = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_set_name(GTK_WIDGET(center), "center");
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(bar), GTK_WIDGET(center));

    // right box
    right = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_set_name(GTK_WIDGET(right), "right");
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(bar), GTK_WIDGET(right));

    // WIDGETS
    workspaces = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_set_name(GTK_WIDGET(workspaces), "workspaces");
    for (int i = 0; i < sizeof(workspace_names) / sizeof(workspace_names[0]);
         ++i) {
        GtkLabel *ws_label = GTK_LABEL(gtk_label_new(workspace_names[i]));
        gtk_box_append(workspaces, GTK_WIDGET(ws_label));
    }
    gtk_box_append(left, GTK_WIDGET(workspaces));

    // active window
    activewin = GTK_LABEL(gtk_label_new(""));
    gtk_box_append(GTK_BOX(center), GTK_WIDGET(activewin));
    pthread_create(&activewin_thread, NULL, watch_activewin, activewin);

    // date & time
    datetime = GTK_LABEL(gtk_label_new(""));
    gtk_widget_set_name(GTK_WIDGET(datetime), "datetime");
    gtk_box_append(GTK_BOX(right), GTK_WIDGET(datetime));
    pthread_create(&datetime_thread, NULL, watch_datetime, datetime);
}

static void activate(GtkApplication *app, void *data) {
    GtkWindow *gtk_window = GTK_WINDOW(gtk_application_window_new(app));

    // setup wayland layer shell
    gtk_layer_init_for_window(gtk_window);
    gtk_layer_set_layer(gtk_window, GTK_LAYER_SHELL_LAYER_TOP);
    gtk_layer_auto_exclusive_zone_enable(gtk_window);
    gtk_layer_set_anchor(gtk_window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(gtk_window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(gtk_window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

    // create the widgets that compose the bar
    make_bar();

    // load css for application
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_file(provider, g_file_new_for_path("./wb.css"));

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);

    // set the primary child of the window
    gtk_window_set_child(gtk_window, GTK_WIDGET(bar));
    gtk_window_present(gtk_window);
}

int main(int argc, char **argv) {
    GtkApplication *app =
        gtk_application_new("io.github.levipare.wb", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
