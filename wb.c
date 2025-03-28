#include "wb.h"

#include <assert.h>
#include <cairo.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>

#include "log.h"
#include "wl.h"

#define HEX_TO_RGBA(x)                                                         \
    ((x >> 24) & 0xFF) / 255.0, ((x >> 16) & 0xFF) / 255.0,                    \
        ((x >> 8) & 0xFF) / 255.0, (x & 0xFF) / 255.0

#define ALIGNMENT_SEP '\x1f'

static void draw_bar(void *data, struct render_ctx *ctx) {
    struct wb *bar = data;

    // Fill background with a color
    cairo_set_source_rgba(ctx->cr, HEX_TO_RGBA(bar->config.bg_color));
    cairo_rectangle(ctx->cr, 0, 0, ctx->width, ctx->height);
    cairo_fill(ctx->cr);

    PangoLayout *layout = pango_layout_new(ctx->pango);
    PangoFontDescription *font_desc =
        pango_font_description_from_string(bar->config.font);
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);

    // draw text
    cairo_set_source_rgba(ctx->cr, HEX_TO_RGBA(bar->config.fg_color));
    const char *content = bar->content;
    for (int i = 0; i < 3 && *content; ++i) {
        // find position of seperator or null terminator
        const char *end = &content[strcspn(content, "\x1f")];
        // len does not include seperator or null terminator
        size_t len = end - content;

        pango_layout_set_text(layout, content, len);
        int width, height;
        pango_layout_get_pixel_size(layout, &width, &height);

        // calculate alignment
        double x = 0;
        if (i == 1) {
            x = (ctx->width - width) / 2.0;
        } else if (i == 2) {
            x = ctx->width - width;
        }

        cairo_move_to(ctx->cr, x, ((int32_t)ctx->height - height) / 2.0);
        pango_cairo_show_layout(ctx->cr, layout);

        // +1 if we haven't reached the end of the content string
        content += len + (*end == ALIGNMENT_SEP);
    }

    // sanity check to make sure the content pointer always results
    // <= the position of the null terminator of bar->content
    assert(content <= strchr(bar->content, '\0'));
}

void wb_run(struct wb_config config) {
    struct wb *bar = calloc(1, sizeof(*bar));
    bar->config = config;
    bar->wl = wl_ctx_create(config.bottom, config.height, draw_bar, bar);

    // main Loop
    enum { POLL_STDIN, POLL_WL };
    struct pollfd fds[] = {
        [POLL_WL] = {.fd = bar->wl->fd, .events = POLLIN},
        [POLL_STDIN] = {.fd = STDIN_FILENO, .events = POLLIN},
    };
    while (true) {
        int ret = poll(fds, sizeof(fds) / sizeof(fds[0]), -1);
        if (ret < 0) {
            log_fatal("poll error");
        }

        if (fds[POLL_WL].revents & POLLIN) {
            wl_display_dispatch(bar->wl->display);
        }

        if (fds[POLL_STDIN].revents & POLLIN) {
            char buf[4096];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            assert(n > 0);
            buf[n] = '\0';

            // it is possible that multiple events were recieved
            // we only want the most recent one
            char *last_nl = strrchr(buf, '\n');
            if (!last_nl) {
                log_error("input received does not contain newline");
                continue;
            }

            *last_nl = '\0';
            char *second_last_nl = strrchr(buf, '\n');
            if (second_last_nl) {
                strcpy(bar->content, second_last_nl + 1);
            } else {
                strcpy(bar->content, buf);
            }

            schedule_frame(bar->wl);
        }

        if (fds[POLL_STDIN].revents & POLLHUP) {
            fds[POLL_STDIN].fd = -1;
            continue;
        }
    }

    // cleanup
    if (bar->wl) {
        wl_ctx_destroy(bar->wl);
    }

    free(bar);
}
