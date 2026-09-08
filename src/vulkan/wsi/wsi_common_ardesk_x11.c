/* SPDX-License-Identifier: MIT */
#include "wsi_common_ardesk.h"
#include <ardesk/tawc-dri.h>
#include <xcb/xcbext.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static xcb_extension_t tawc = { TAWC_DRI_NAME, 0 };
/* Release events are broadcast to this client's selectors on a window, so
 * old/new swapchains must not independently restart their serials at one. */
static atomic_uint next_serial;

static void
trace_buffer(const char *event, struct wsi_ardesk_chain *chain,
             struct wsi_ardesk_image *image)
{
   const char *enabled = getenv("ARDESK_WSI_TRACE");
   if (enabled && !strcmp(enabled, "1"))
      fprintf(stderr, "X11_WSI event=%s window=%u serial=%u buffer=%p\n",
              event, chain->window, image->serial, (void *)image);
}

static VkResult
check(xcb_connection_t *connection, unsigned sequence)
{
   if (!sequence || xcb_connection_has_error(connection))
      return VK_ERROR_SURFACE_LOST_KHR;
   xcb_generic_error_t *error = xcb_request_check(connection,
                                                (xcb_void_cookie_t){ sequence });
   bool failed = error || xcb_connection_has_error(connection);
   free(error);
   return failed ? VK_ERROR_SURFACE_LOST_KHR : VK_SUCCESS;
}

bool
wsi_ardesk_x11_supported(xcb_connection_t *connection, uint32_t visual)
{
   if (!connection || xcb_connection_has_error(connection)) return false;
   struct sockaddr_storage peer;
   socklen_t size = sizeof(peer);
   if (getpeername(xcb_get_file_descriptor(connection), (struct sockaddr *)&peer,
                   &size) || peer.ss_family != AF_UNIX) return false;
   bool compatible = false;
   const xcb_setup_t *setup = xcb_get_setup(connection);
   for (xcb_screen_iterator_t s = xcb_setup_roots_iterator(setup); s.rem;
        xcb_screen_next(&s)) {
      for (xcb_depth_iterator_t d = xcb_screen_allowed_depths_iterator(s.data); d.rem;
           xcb_depth_next(&d)) {
         for (xcb_visualtype_iterator_t v = xcb_depth_visuals_iterator(d.data); v.rem;
              xcb_visualtype_next(&v)) {
            if (v.data->visual_id == visual && v.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR &&
                (d.data->depth == 24 || d.data->depth == 32) &&
                v.data->red_mask == 0xff0000 && v.data->green_mask == 0xff00 &&
                v.data->blue_mask == 0xff) compatible = true;
         }
      }
   }
   const xcb_query_extension_reply_t *extension = xcb_get_extension_data(connection, &tawc);
   if (!compatible || !extension || !extension->present) return false;
   tawc_dri_query_version_req body = { .major_version = 0, .minor_version = 3 };
   struct iovec parts[3] = { [2] = { &body, sizeof(body) } };
   xcb_protocol_request_t request = { 1, &tawc, X_TAWCDRI_QueryVersion, 0 };
   unsigned sequence = xcb_send_request(connection, XCB_REQUEST_CHECKED, parts + 2, &request);
   xcb_generic_error_t *error = NULL;
   tawc_dri_query_version_reply *reply = xcb_wait_for_reply(connection, sequence, &error);
   bool supported = reply && !error && reply->major_version == 0 && reply->minor_version >= 3;
   free(reply);
   free(error);
   return supported;
}

VkResult
wsi_ardesk_x11_extent(xcb_connection_t *connection, uint32_t window, VkExtent2D *extent)
{
   xcb_generic_error_t *error = NULL;
   xcb_get_geometry_reply_t *reply = xcb_get_geometry_reply(connection,
                                     xcb_get_geometry(connection, window), &error);
   VkResult result = reply && !error ? VK_SUCCESS : VK_ERROR_SURFACE_LOST_KHR;
   if (result == VK_SUCCESS) *extent = (VkExtent2D){ reply->width, reply->height };
   free(error);
   free(reply);
   return result;
}

static VkResult
select_input(struct wsi_ardesk_chain *chain, uint32_t mask)
{
   tawc_dri_select_input_req body = {
      .eid = chain->eid, .window = chain->window, .event_mask = mask,
   };
   struct iovec parts[3] = { [2] = { &body, sizeof(body) } };
   xcb_protocol_request_t request = { 1, &tawc, X_TAWCDRI_SelectInput, 1 };
   return check(chain->connection, xcb_send_request(chain->connection,
                XCB_REQUEST_CHECKED, parts + 2, &request));
}

VkResult
wsi_ardesk_x11_init(struct wsi_ardesk_chain *chain)
{
   chain->eid = xcb_generate_id(chain->connection);
   chain->events = xcb_register_for_special_xge(chain->connection, &tawc, chain->eid, NULL);
   if (!chain->events) return VK_ERROR_OUT_OF_HOST_MEMORY;
   return select_input(chain, TAWC_DRI_EVENT_MASK_CONFIGURE_NOTIFY |
                               TAWC_DRI_EVENT_MASK_BUFFER_RELEASE);
}

void
wsi_ardesk_x11_finish(struct wsi_ardesk_chain *chain)
{
   if (chain->events) {
      select_input(chain, 0);
      xcb_unregister_for_special_event(chain->connection, chain->events);
   }
}

void
wsi_ardesk_x11_drain(struct wsi_ardesk_chain *chain)
{
   xcb_generic_event_t *event;
   while ((event = xcb_poll_for_special_event(chain->connection, chain->events))) {
      xcb_ge_generic_event_t *ge = (void *)event;
      if (ge->event_type == TAWC_DRI_EVENT_CONFIGURE_NOTIFY) {
         tawc_dri_configure_notify_event *configure = (void *)event;
         if (configure->width != chain->width || configure->height != chain->height)
            chain->status = VK_ERROR_OUT_OF_DATE_KHR;
      } else if (ge->event_type == TAWC_DRI_EVENT_BUFFER_RELEASE) {
         uint32_t serial = ((tawc_dri_buffer_release_event *)event)->serial;
         for (unsigned i = 0; i < chain->base.image_count; i++) {
            struct wsi_ardesk_image *image = &chain->images[i];
            if (image->state == ARDESK_PRESENTED && image->serial == serial) {
               trace_buffer("release", chain, image);
               image->state = ARDESK_FREE;
            }
         }
      }
      free(event);
   }
   if (xcb_connection_has_error(chain->connection))
      chain->status = VK_ERROR_SURFACE_LOST_KHR;
}

VkResult
wsi_ardesk_x11_present(struct wsi_ardesk_chain *chain, struct wsi_ardesk_image *image)
{
   wsi_ardesk_x11_drain(chain);
   if (chain->status != VK_SUCCESS) return chain->status;
   /* A serial is unique among all outstanding buffers, including wraparound. */
   bool collision = true;
   do {
      chain->serial = atomic_fetch_add(&next_serial, 1) + 1;
      if (!chain->serial) continue;
      collision = false;
      for (unsigned i = 0; i < chain->base.image_count; i++)
         collision |= chain->images[i].state == ARDESK_PRESENTED &&
                      chain->images[i].serial == chain->serial;
   } while (collision);
   tawc_dri_present_buffer_req body = {
      .window = chain->window, .num_fds = image->num_fds, .num_ints = image->num_ints,
      .width = chain->width, .height = chain->height, .stride = image->stride,
      .format = image->format, .usage_lo = ARDESK_BUFFER_USAGE, .serial = chain->serial,
   };
   int fds[ARDESK_MAX_FDS];
   for (unsigned i = 0; i < image->num_fds; i++) {
      fds[i] = dup(image->fds[i]);
      if (fds[i] < 0) {
         while (i) close(fds[--i]);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
   struct iovec parts[4] = {
      [2] = { &body, sizeof(body) },
      [3] = { image->ints, image->num_ints * sizeof(int32_t) },
   };
   xcb_protocol_request_t request = { 2, &tawc, X_TAWCDRI_PresentBuffer, 1 };
   /* XCB consumes the duplicate FDs even when sending fails. */
   unsigned sequence = xcb_send_request_with_fds(chain->connection, XCB_REQUEST_CHECKED,
                        parts + 2, &request, image->num_fds, fds);
   VkResult result = check(chain->connection, sequence);
   if (result == VK_SUCCESS) {
      image->state = ARDESK_PRESENTED;
      image->serial = chain->serial;
      trace_buffer("present", chain, image);
   }
   return result;
}
