/* SPDX-License-Identifier: MIT */
#include "wsi_common_ardesk.h"
#include "wayland-android-client-protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

static void
global(void *data, struct wl_registry *registry, uint32_t name,
       const char *interface, uint32_t version)
{
   struct wsi_ardesk_chain *chain = data;
   if (!chain->wlegl && version >= 2 && !strcmp(interface, "android_wlegl"))
      chain->wlegl = wl_registry_bind(registry, name, &android_wlegl_interface, 2);
   if (!chain->owns_display && !chain->compositor && !strcmp(interface, "wl_compositor"))
      chain->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = { global, global_remove };

VkResult
wsi_ardesk_connect(struct wsi_ardesk_chain *chain, struct wl_display *display)
{
   chain->owns_display = !display;
   chain->display = display ? display : wl_display_connect(NULL);
   if (!chain->display)
      return VK_ERROR_SURFACE_LOST_KHR;
   chain->queue = wl_display_create_queue(chain->display);
   if (!chain->queue)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   struct wl_display *wrapper = wl_proxy_create_wrapper(chain->display);
   if (!wrapper)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   wl_proxy_set_queue((struct wl_proxy *)wrapper, chain->queue);
   chain->registry = wl_display_get_registry(wrapper);
   wl_proxy_wrapper_destroy(wrapper);
   if (!chain->registry)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   wl_registry_add_listener(chain->registry, &registry_listener, chain);
   if (wl_display_roundtrip_queue(chain->display, chain->queue) < 0)
      return VK_ERROR_SURFACE_LOST_KHR;
   return chain->wlegl ? VK_SUCCESS : VK_ERROR_SURFACE_LOST_KHR;
}

void
wsi_ardesk_disconnect(struct wsi_ardesk_chain *chain)
{
   if (chain->frame) wl_callback_destroy(chain->frame);
   if (chain->opaque_region) wl_region_destroy(chain->opaque_region);
   if (chain->compositor) wl_compositor_destroy(chain->compositor);
   if (chain->surface) wl_proxy_wrapper_destroy(chain->surface);
   if (chain->wlegl) android_wlegl_destroy(chain->wlegl);
   if (chain->registry) wl_registry_destroy(chain->registry);
   if (chain->queue) wl_event_queue_destroy(chain->queue);
   if (chain->owns_display && chain->display) wl_display_disconnect(chain->display);
}

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
   struct wsi_ardesk_image *image = data;
   if (image->state == ARDESK_PRESENTED)
      image->state = ARDESK_FREE;
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void
buffer_fd(void *data, struct android_wlegl_server_buffer_handle *handle, int32_t fd)
{
   struct wsi_ardesk_image *image = data;
   if (image->num_fds == ARDESK_MAX_FDS) {
      close(fd);
      image->chain->status = VK_ERROR_FORMAT_NOT_SUPPORTED;
      return;
   }
   fcntl(fd, F_SETFD, FD_CLOEXEC);
   image->fds[image->num_fds++] = fd;
}

static void
buffer_ints(void *data, struct android_wlegl_server_buffer_handle *handle,
            struct wl_array *ints)
{
   struct wsi_ardesk_image *image = data;
   if (ints->size > sizeof(image->ints) || ints->size % sizeof(int32_t) ||
       image->num_ints) {
      image->chain->status = VK_ERROR_FORMAT_NOT_SUPPORTED;
      return;
   }
   image->num_ints = ints->size / sizeof(int32_t);
   memcpy(image->ints, ints->data, ints->size);
}

static void
buffer_ready(void *data, struct android_wlegl_server_buffer_handle *handle,
             struct wl_buffer *buffer, int32_t format, int32_t stride)
{
   struct wsi_ardesk_image *image = data;
   if (image->buffer) {
      wl_buffer_destroy(buffer);
      image->chain->status = VK_ERROR_SURFACE_LOST_KHR;
      return;
   }
   image->buffer = buffer;
   image->format = format;
   image->stride = stride;
   wl_proxy_set_queue((struct wl_proxy *)buffer, image->chain->queue);
   wl_buffer_add_listener(buffer, &buffer_listener, image);
}

static const struct android_wlegl_server_buffer_handle_listener alloc_listener = {
   buffer_fd, buffer_ints, buffer_ready,
};

VkResult
wsi_ardesk_alloc_buffer(struct wsi_ardesk_chain *chain, struct wsi_ardesk_image *image)
{
   image->chain = chain;
   struct android_wlegl_server_buffer_handle *handle =
      android_wlegl_get_server_buffer_handle(chain->wlegl, chain->width,
         chain->height, chain->format, ARDESK_BUFFER_USAGE);
   if (!handle)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   android_wlegl_server_buffer_handle_add_listener(handle, &alloc_listener, image);
   int result = wl_display_roundtrip_queue(chain->display, chain->queue);
   android_wlegl_server_buffer_handle_destroy(handle);
   if (result < 0)
      return VK_ERROR_SURFACE_LOST_KHR;
   if (chain->status != VK_SUCCESS)
      return chain->status;
   /* Same Qualcomm private-handle test as Mesa's u_gralloc fallback. Never
    * treat an unknown or UBWC layout as a linear image merely because a first
    * FD happens to exist. CPU-readable allocations normally clear UBWC. */
   if (!image->buffer || !image->num_fds || image->num_ints < 2 ||
       image->ints[0] != 0x676d736d || (image->ints[1] & 0x08000000) ||
       image->format != chain->format || image->stride < chain->width ||
       image->stride > UINT32_MAX / 4)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   return VK_SUCCESS;
}

void
wsi_ardesk_free_buffer(struct wsi_ardesk_image *image)
{
   if (image->buffer) wl_buffer_destroy(image->buffer);
   for (unsigned i = 0; i < image->num_fds; i++) close(image->fds[i]);
}

VkResult
wsi_ardesk_dispatch(struct wsi_ardesk_chain *chain, uint64_t timeout)
{
   if (chain->status != VK_SUCCESS) return chain->status;
   while (wl_display_prepare_read_queue(chain->display, chain->queue) != 0) {
      int dispatched = wl_display_dispatch_queue_pending(chain->display, chain->queue);
      if (dispatched < 0)
         return chain->status = VK_ERROR_SURFACE_LOST_KHR;
      if (dispatched) {
         if (chain->connection) wsi_ardesk_x11_drain(chain);
         return chain->status;
      }
   }
   struct pollfd fds[2] = {
      { .fd = wl_display_get_fd(chain->display), .events = POLLIN },
      { .fd = chain->connection ? xcb_get_file_descriptor(chain->connection) : -1,
        .events = POLLIN },
   };
   if (wl_display_flush(chain->display) < 0) {
      if (errno != EAGAIN) {
         wl_display_cancel_read(chain->display);
         return chain->status = VK_ERROR_SURFACE_LOST_KHR;
      }
      fds[0].events |= POLLOUT;
   }
   /* Another app thread may read X events into our special queue. */
   int ms = timeout >= 50000000 ? 50 : (int)((timeout + 999999) / 1000000);
   int result = poll(fds, 2, ms);
   if (result > 0 && (fds[0].revents & POLLIN)) {
      if (wl_display_read_events(chain->display) < 0)
         return chain->status = VK_ERROR_SURFACE_LOST_KHR;
   } else {
      wl_display_cancel_read(chain->display);
   }
   if ((result < 0 && errno != EINTR) ||
       ((fds[0].revents | fds[1].revents) & (POLLERR | POLLHUP | POLLNVAL)) ||
       wl_display_dispatch_queue_pending(chain->display, chain->queue) < 0)
      return chain->status = VK_ERROR_SURFACE_LOST_KHR;
   if (chain->connection) wsi_ardesk_x11_drain(chain);
   return chain->status;
}

static void
frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
   struct wsi_ardesk_chain *chain = data;
   wl_callback_destroy(callback);
   chain->frame = NULL;
}
static const struct wl_callback_listener frame_listener = { frame_done };

VkResult
wsi_ardesk_wayland_present(struct wsi_ardesk_chain *chain,
                          struct wsi_ardesk_image *image)
{
   /* FIFO: never overwrite a commit waiting for the compositor's next frame. */
   while (chain->frame) {
      VkResult result = wsi_ardesk_dispatch(chain, UINT64_MAX);
      if (result != VK_SUCCESS) return result;
   }
   chain->frame = wl_surface_frame(chain->surface);
   if (!chain->frame) return VK_ERROR_OUT_OF_HOST_MEMORY;
   wl_callback_add_listener(chain->frame, &frame_listener, chain);
   image->state = ARDESK_PRESENTED;
   wl_surface_set_opaque_region(chain->surface, chain->opaque_region);
   wl_surface_attach(chain->surface, image->buffer, 0, 0);
   /* damage uses logical surface coordinates; INT32_MAX also covers scaling. */
   wl_surface_damage(chain->surface, 0, 0, INT32_MAX, INT32_MAX);
   wl_surface_commit(chain->surface);
   if (wl_display_flush(chain->display) < 0 && errno != EAGAIN)
      return VK_ERROR_SURFACE_LOST_KHR;
   return VK_SUCCESS;
}
