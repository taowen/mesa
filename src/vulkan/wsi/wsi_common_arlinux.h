/* SPDX-License-Identifier: MIT */
#ifndef WSI_COMMON_ARLINUX_H
#define WSI_COMMON_ARLINUX_H

#include "wsi_common_private.h"
#include <wayland-client.h>
#include <xcb/xcb.h>

#define ARLINUX_MAX_IMAGES 8
#define ARLINUX_MAX_FDS 32
#define ARLINUX_MAX_INTS 128
/* CPU access requests linear Qualcomm gralloc storage; no pixels cross the CPU. */
#define ARLINUX_BUFFER_USAGE 0x333u /* HW_RENDER | HW_TEXTURE | SW_READ/WRITE_OFTEN */

struct wsi_arlinux_formats {
   VkSurfaceFormatKHR formats[4];
   uint32_t count;
   VkImageUsageFlags usage;
   VkExtent2D maximum;
};
VkResult wsi_arlinux_get_formats(struct wsi_device *, struct wsi_arlinux_formats *);
VkResult wsi_arlinux_image_properties(struct wsi_device *, VkFormat,
                                     VkImageUsageFlags, bool,
                                     VkImageFormatProperties *);

struct wsi_arlinux_chain;
struct android_wlegl;

struct wsi_arlinux_image {
   struct wsi_image base;
   struct wsi_arlinux_chain *chain;
   struct wl_buffer *buffer;
   int fds[ARLINUX_MAX_FDS];
   int32_t ints[ARLINUX_MAX_INTS];
   unsigned num_fds, num_ints;
   uint32_t stride, format, serial;
   uint64_t trace_id;
   enum { ARLINUX_FREE, ARLINUX_ACQUIRED, ARLINUX_PRESENTED } state;
};

/* One image pool and one lifecycle for both window protocols. */
struct wsi_arlinux_chain {
   struct wsi_swapchain base;
   mtx_t lock;
   VkResult status;
   bool retired;
   bool needs_buffer_blit;
   uint32_t width, height, format;
   VkSubresourceLayout layout;
   VkImageDrmFormatModifierExplicitCreateInfoEXT modifier;

   struct wl_display *display;
   struct wl_event_queue *queue;
   struct wl_registry *registry;
   struct android_wlegl *wlegl;
   struct wl_compositor *compositor;
   struct wl_region *opaque_region;
   bool owns_display;
   struct wl_surface *surface; /* wrapper on the application's surface */
   struct wl_callback *frame;

   xcb_connection_t *connection;
   xcb_window_t window;
   uint32_t eid, serial;
   xcb_special_event_t *events;

   struct wsi_arlinux_image images[ARLINUX_MAX_IMAGES];
};

VkResult wsi_arlinux_connect(struct wsi_arlinux_chain *chain,
                           struct wl_display *display);
void wsi_arlinux_disconnect(struct wsi_arlinux_chain *chain);
VkResult wsi_arlinux_alloc_buffer(struct wsi_arlinux_chain *chain,
                                struct wsi_arlinux_image *image);
void wsi_arlinux_free_buffer(struct wsi_arlinux_image *image);
/* All event dispatch is serialized by chain->lock after initialization. */
VkResult wsi_arlinux_dispatch(struct wsi_arlinux_chain *chain, uint64_t timeout);
VkResult wsi_arlinux_wayland_present(struct wsi_arlinux_chain *chain,
                                   struct wsi_arlinux_image *image);

bool wsi_arlinux_x11_supported(xcb_connection_t *connection, uint32_t visual);
VkResult wsi_arlinux_x11_init(struct wsi_arlinux_chain *chain);
void wsi_arlinux_x11_finish(struct wsi_arlinux_chain *chain);
void wsi_arlinux_x11_drain(struct wsi_arlinux_chain *chain);
VkResult wsi_arlinux_x11_present(struct wsi_arlinux_chain *chain,
                               struct wsi_arlinux_image *image);
VkResult wsi_arlinux_x11_extent(xcb_connection_t *connection, uint32_t window,
                              VkExtent2D *extent);
#endif
