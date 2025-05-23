#include "wayland.h"
#include <assert.h>
#include <pixman.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#include "log.h"
#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

void noop() {}

/* layer surface listener */
static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    struct wayland_monitor *mon = data;
    mon->width = w;
    mon->height = h;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface) {
    log_info("layer surface closed");
    zwlr_layer_surface_v1_destroy(surface);
}

struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

/* wl_output listener */
static void output_scale(void *data, struct wl_output *wl_output,
                         int32_t scale) {
    struct wayland_monitor *mon = data;
    mon->scale = scale;

    if (!mon->surface) {
        struct wayland_layer_surface_config conf = mon->wl->ls_config;
        mon->surface = wl_compositor_create_surface(mon->wl->compositor);

        mon->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            mon->wl->layer_shell, mon->surface, mon->output, conf.layer, "wb");
        zwlr_layer_surface_v1_set_size(mon->layer_surface, conf.width,
                                       conf.height);
        zwlr_layer_surface_v1_set_exclusive_zone(mon->layer_surface, conf.zone);
        zwlr_layer_surface_v1_set_anchor(mon->layer_surface, conf.anchor);
        zwlr_layer_surface_v1_set_margin(mon->layer_surface, conf.top,
                                         conf.right, conf.bottom, conf.left);
        zwlr_layer_surface_v1_add_listener(mon->layer_surface,
                                           &layer_surface_listener, mon);

        wl_surface_commit(mon->surface);
        wl_display_roundtrip(mon->wl->display);
    }

    mon->wl->user_scale_callback(mon->wl->user_data, mon, scale);
}

static void output_name(void *data, struct wl_output *wl_output,
                        const char *name) {
    struct wayland_monitor *mon = data;
    free(mon->name);
    mon->name = strdup(name);
}

static void output_done(void *data, struct wl_output *wl_output) {
    struct wayland_monitor *mon = data;
    log_info("monitor %s: scale %d", mon->name, mon->scale);
}

static const struct wl_output_listener output_listener = {
    .geometry = noop,
    .mode = noop,
    .scale = output_scale,
    .name = output_name,
    .description = noop,
    .done = output_done,
};

/* registry listener */
static void registry_global(void *data, struct wl_registry *wl_registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
    // printf("%s %d\n", interface, version);
    struct wayland *wl = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        wl->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        wl->compositor =
            wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wayland_monitor *mon = calloc(1, sizeof(struct wayland_monitor));
        mon->wl = wl;
        mon->output =
            wl_registry_bind(wl_registry, name, &wl_output_interface, 4);
        wl_output_add_listener(mon->output, &output_listener, mon);

        wl_list_insert(&wl->monitors, &mon->link);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        wl->layer_shell =
            wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface,
                             version < 4 ? version : 4);
    }
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry,
                                   uint32_t name) {}

static const struct wl_registry_listener wl_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

struct wayland *wayland_create(struct wayland_layer_surface_config ls_config,
                               scale_callback_t user_scale_callback,
                               void *user_data) {
    struct wayland *wl = calloc(1, sizeof(*wl));
    wl_list_init(&wl->monitors);

    // set user configuration
    wl->ls_config = ls_config;
    wl->user_scale_callback = user_scale_callback;
    wl->user_data = user_data;

    // bind wayland globals
    wl->display = wl_display_connect(NULL);
    wl->fd = wl_display_get_fd(wl->display);
    wl->registry = wl_display_get_registry(wl->display);
    wl_registry_add_listener(wl->registry, &wl_registry_listener, wl);
    wl_display_roundtrip(wl->display);

    // make sure we have the required globals
    assert(wl->compositor);
    assert(wl->layer_shell);
    assert(wl->shm);
    assert(!wl_list_empty(&wl->monitors));

    // roundtrip so listeners added during the registry events are handled
    wl_display_roundtrip(wl->display);

    log_info("wayland initialized");
    return wl;
}

void wayland_destroy(struct wayland *wl) {
    struct wayland_monitor *mon, *tmp;
    wl_list_for_each_safe(mon, tmp, &wl->monitors, link) {
        wl_list_remove(&mon->link);

        wl_output_release(mon->output);
        wl_surface_destroy(mon->surface);
        zwlr_layer_surface_v1_destroy(mon->layer_surface);
        pool_buffer_destroy(&mon->buffer);
        free(mon->name);
        free(mon);
    }

    // globals
    zwlr_layer_shell_v1_destroy(wl->layer_shell);
    wl_registry_destroy(wl->registry);
    wl_shm_destroy(wl->shm);
    wl_compositor_destroy(wl->compositor);

    wl_display_flush(wl->display);
    wl_display_disconnect(wl->display);

    free(wl);

    log_info("wayland destroyed");
}

void render(struct wayland_monitor *mon, draw_callback_t draw,
            void *draw_data) {
    uint32_t width = mon->width * mon->scale;
    uint32_t height = mon->height * mon->scale;

    // check if buffer is out of date of output configuration
    if (width != mon->buffer.width || height != mon->buffer.height) {
        pool_buffer_destroy(&mon->buffer);
        pool_buffer_create(&mon->buffer, mon->wl->shm, width, height);
    }

    assert(mon->buffer.buffer);

    struct render_ctx rctx = {
        .pix = mon->buffer.pix, .width = width, .height = height};

    // call the callback associated with an output
    draw(draw_data, &rctx);

    wl_surface_set_buffer_scale(mon->surface, mon->scale);
    wl_surface_attach(mon->surface, mon->buffer.buffer, 0, 0);
    wl_surface_damage(mon->surface, 0, 0, mon->width, mon->height);
    wl_surface_commit(mon->surface);
}
