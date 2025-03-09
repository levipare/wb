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
GtkWidget *bar;
GtkWidget *left;
GtkWidget *center;
GtkWidget *right;

// info widgets
GtkWidget *datetime;
pthread_t datetime_thread;

GtkWidget *activewin;
pthread_t activewin_thread;

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

    char buf[72] = {0};
    size_t nread;
    while ((nread = socket_readline(socket_fd, buf, sizeof(buf))) > 0) {
        // add ellipses to truncate
        buf[sizeof(buf) - 4] = '.';
        buf[sizeof(buf) - 3] = '.';
        buf[sizeof(buf) - 2] = '.';

        // if its an activewindow event
        if (strncmp(buf, "activewindow>>", strlen("activewindow>>")) == 0) {
            // printf("'%s'\n", buf);
            char *comma_loc = strchr(buf, ',');
            if (comma_loc)
                gtk_label_set_text(label, comma_loc + 1);
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
    bar = gtk_center_box_new();
    gtk_widget_set_name(bar, "wb");

    // left box
    left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(left, "left");
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(bar), left);

    // center box
    center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(center, "center");
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(bar), center);

    // right box
    right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(right, "right");
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(bar), right);

    // WIDGETS
    // active window
    activewin = gtk_label_new("");
    gtk_box_append(GTK_BOX(center), activewin);
    pthread_create(&activewin_thread, NULL, watch_activewin, activewin);

    // date & time
    datetime = gtk_label_new("");
    gtk_widget_set_name(datetime, "datetime");
    gtk_box_append(GTK_BOX(right), datetime);
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
    gtk_window_set_child(gtk_window, bar);
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
