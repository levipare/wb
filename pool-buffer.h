#ifndef POOL_BUFFER_H
#define POOL_BUFFER_H

#include <pixman.h>
#include <wayland-client.h>

struct pool_buffer {
    struct wl_buffer *buffer;
    uint32_t width, height;
    size_t size;
    void *data;
    pixman_image_t *pix;
};

void pool_buffer_create(struct pool_buffer *pb, struct wl_shm *shm,
                        uint32_t width, uint32_t height);

void pool_buffer_destroy(struct pool_buffer *buffer);

#endif
