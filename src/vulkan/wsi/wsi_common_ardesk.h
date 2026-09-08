/* SPDX-License-Identifier: MIT */
#ifndef WSI_COMMON_ARDESK_H
#define WSI_COMMON_ARDESK_H

#include "wsi_common_private.h"
#include <wayland-client.h>
#include <xcb/xcb.h>

#define ARDESK_MAX_IMAGES 8
#define ARDESK_MAX_FDS 32
#define ARDESK_MAX_INTS 128
/* CPU access requests linear Qualcomm gralloc storage; no pixels cross the CPU. */
#define ARDESK_BUFFER_USAGE 0x333u /* HW_RENDER | HW_TEXTURE | SW_READ/WRITE_OFTEN */

struct wsi_ardesk_chain;
struct android_wlegl;

struct wsi_ardesk_image {
   struct wsi_image base;
   struct wsi_ardesk_chain *chain;
   struct wl_buffer *buffer;
   int fds[ARDESK_MAX_FDS];
   int32_t ints[ARDESK_MAX_INTS];
   unsigned num_fds, num_ints;
   uint32_t stride, format, serial;
   enum { ARDESK_FREE, ARDESK_ACQUIRED, ARDESK_PRESENTED } state;
};

/* One image pool and one lifecycle for both window protocols. */
struct wsi_ardesk_chain {
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
   bool owns_display;
   struct wl_surface *surface; /* wrapper on the application's surface */
   struct wl_callback *frame;

   xcb_connection_t *connection;
   xcb_window_t window;
   uint32_t eid, serial;
   xcb_special_event_t *events;

   struct wsi_ardesk_image images[ARDESK_MAX_IMAGES];
};

VkResult wsi_ardesk_connect(struct wsi_ardesk_chain *chain,
                           struct wl_display *display);
void wsi_ardesk_disconnect(struct wsi_ardesk_chain *chain);
VkResult wsi_ardesk_alloc_buffer(struct wsi_ardesk_chain *chain,
                                struct wsi_ardesk_image *image);
void wsi_ardesk_free_buffer(struct wsi_ardesk_image *image);
/* All event dispatch is serialized by chain->lock after initialization. */
VkResult wsi_ardesk_dispatch(struct wsi_ardesk_chain *chain, uint64_t timeout);
VkResult wsi_ardesk_wayland_present(struct wsi_ardesk_chain *chain,
                                   struct wsi_ardesk_image *image);

bool wsi_ardesk_x11_supported(xcb_connection_t *connection, uint32_t visual);
VkResult wsi_ardesk_x11_init(struct wsi_ardesk_chain *chain);
void wsi_ardesk_x11_finish(struct wsi_ardesk_chain *chain);
void wsi_ardesk_x11_drain(struct wsi_ardesk_chain *chain);
VkResult wsi_ardesk_x11_present(struct wsi_ardesk_chain *chain,
                               struct wsi_ardesk_image *image);
VkResult wsi_ardesk_x11_extent(xcb_connection_t *connection, uint32_t window,
                              VkExtent2D *extent);
#endif
