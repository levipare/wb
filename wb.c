#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <pthread.h>
#include <unistd.h>

#include "log.h"
#include "wl.h"

char status[100];
char workspaces[10][2] = {"1", "2", "3", "4", "5", "6", "8", "9", "10"};

static void draw_bar(struct wb_output *output) {
    // Fill background with a color
    cairo_t *cr = output->buffer.cairo;
    cairo_set_source_rgb(cr, 0.2, 0.3, 0.35);

    cairo_rectangle(cr, 0, 0, output->width, output->height);
    cairo_fill(cr);

    // setup pango to render to cairo
    PangoContext *pango = pango_cairo_create_context(cr);

    PangoLayout *layout = pango_layout_new(pango);
    PangoFontDescription *font_desc =
        pango_font_description_from_string("monospace 10");
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    // workspaces

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // White text
    for (int i = 0; i < sizeof(workspaces) / sizeof(workspaces[0]); i++) {
        pango_layout_set_text(layout, workspaces[i], 2);
        int width, height;
        pango_layout_get_pixel_size(layout, &width, &height);

        cairo_move_to(cr, 20 + i * 40, (output->height - height) / 2.0);

        pango_cairo_show_layout(cr, layout);
    }

    // right side
    int width, height;
    pango_layout_set_text(layout, status, -1);
    pango_layout_get_pixel_size(layout, &width, &height);
    cairo_move_to(cr, output->width - width - 28,
                  (output->height - height) / 2.0);
    pango_cairo_show_layout(cr, layout);
}

static void *update(void *data) {
    struct wl_ctx *ctx = data;
    while (1) {
        time_t current_time;

        time(&current_time);
        struct tm *time_info = localtime(&current_time);

        char time_string[64];
        strftime(time_string, sizeof(time_string), "%a %b %-d %-I:%M:%S %p",
                 time_info);

        ///
        strcpy(status, time_string);
        render(ctx->output, draw_bar);
        wl_display_flush(ctx->display);
        ///

        // Sleep until the next second boundary
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long sleep_time = 1000000000L - ts.tv_nsec;
        struct timespec req = {0, sleep_time};
        nanosleep(&req, NULL);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    struct wl_ctx *ctx = wl_ctx_create();

    pthread_t th;
    pthread_create(&th, NULL, update, ctx);

    while (wl_display_dispatch(ctx->display)) {
    }

    wl_ctx_destroy(ctx);

    return 0;
}
