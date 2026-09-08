/* SPDX-License-Identifier: MIT
 * Turnip/KGSL WSI: shared Android buffers, with small X11/Wayland adapters.
 */
#include "wsi_common_ardesk.h"
#include "wsi_common_entrypoints.h"
#include "vk_device.h"
#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_util.h"
#include "util/os_time.h"
#include <X11/Xlib-xcb.h>
#include <fcntl.h>
#include <unistd.h>

static bool
is_wayland(VkIcdSurfaceBase *surface)
{
   return surface->platform == VK_ICD_WSI_PLATFORM_WAYLAND;
}

static xcb_connection_t *
x_connection(VkIcdSurfaceBase *surface)
{
   return surface->platform == VK_ICD_WSI_PLATFORM_XLIB ?
      XGetXCBConnection(((VkIcdSurfaceXlib *)surface)->dpy) :
      ((VkIcdSurfaceXcb *)surface)->connection;
}

static uint32_t
x_window(VkIcdSurfaceBase *surface)
{
   return surface->platform == VK_ICD_WSI_PLATFORM_XLIB ?
      ((VkIcdSurfaceXlib *)surface)->window : ((VkIcdSurfaceXcb *)surface)->window;
}

static bool
has_allocator(struct wl_display *display)
{
   struct wsi_ardesk_chain chain = {0};
   VkResult result = wsi_ardesk_connect(&chain, display);
   wsi_ardesk_disconnect(&chain);
   return result == VK_SUCCESS;
}

static VkResult
get_support(VkIcdSurfaceBase *surface, struct wsi_device *wsi, uint32_t queue,
            VkBool32 *supported)
{
   *supported = false;
   struct wsi_ardesk_formats formats;
   VkResult result = wsi_ardesk_get_formats(wsi, &formats);
   if (result != VK_SUCCESS) return result;
   if (!formats.count) return VK_SUCCESS;
   if (queue >= wsi->queue_family_count || !(wsi->queue_supports_blit & BITFIELD64_BIT(queue)))
      return VK_SUCCESS;
   if (is_wayland(surface)) {
      *supported = has_allocator(((VkIcdSurfaceWayland *)surface)->display);
   } else {
      xcb_connection_t *connection = x_connection(surface);
      xcb_generic_error_t *error = NULL;
      xcb_get_window_attributes_reply_t *attrs = xcb_get_window_attributes_reply(connection,
         xcb_get_window_attributes(connection, x_window(surface)), &error);
      if (attrs && !error)
         *supported = wsi_ardesk_x11_supported(connection, attrs->visual) && has_allocator(NULL);
      free(attrs);
      free(error);
   }
   return VK_SUCCESS;
}

static VkResult
get_capabilities(VkIcdSurfaceBase *surface, struct wsi_device *wsi,
                 const void *info_next, VkSurfaceCapabilities2KHR *caps)
{
   struct wsi_ardesk_formats supported;
   VkResult result = wsi_ardesk_get_formats(wsi, &supported);
   if (result != VK_SUCCESS) return result;
   if (!supported.count) return VK_ERROR_SURFACE_LOST_KHR;
   VkExtent2D extent = { UINT32_MAX, UINT32_MAX };
   if (!is_wayland(surface)) {
      VkResult result = wsi_ardesk_x11_extent(x_connection(surface), x_window(surface), &extent);
      if (result != VK_SUCCESS) return result;
   }
   caps->surfaceCapabilities = (VkSurfaceCapabilitiesKHR) {
      .minImageCount = 3, .maxImageCount = ARDESK_MAX_IMAGES,
      .currentExtent = extent,
      .minImageExtent = {1, 1},
      .maxImageExtent = supported.maximum,
      .maxImageArrayLayers = 1,
      .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .supportedUsageFlags = supported.usage,
   };
   vk_foreach_struct(sType, ext, caps->pNext) {
      switch (sType) {
      case VK_STRUCTURE_TYPE_SURFACE_PROTECTED_CAPABILITIES_KHR:
         ((VkSurfaceProtectedCapabilitiesKHR *)ext)->supportsProtected = false;
         break;
      case VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR: {
         VkSurfacePresentScalingCapabilitiesKHR *scaling = ext;
         scaling->supportedPresentScaling = 0;
         scaling->supportedPresentGravityX = scaling->supportedPresentGravityY = 0;
         scaling->minScaledImageExtent = caps->surfaceCapabilities.minImageExtent;
         scaling->maxScaledImageExtent = caps->surfaceCapabilities.maxImageExtent;
         break;
      }
      case VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR: {
         VkSurfacePresentModeCompatibilityKHR *compat = ext;
         if (!compat->pPresentModes) compat->presentModeCount = 1;
         else if (compat->presentModeCount) {
            compat->presentModeCount = 1;
            compat->pPresentModes[0] = VK_PRESENT_MODE_FIFO_KHR;
         }
         break;
      }
      case VK_STRUCTURE_TYPE_IMAGE_USAGE_FLAGS_2_CREATE_INFO_KHR:
         ((VkImageUsageFlags2CreateInfoKHR *)ext)->usage = caps->surfaceCapabilities.supportedUsageFlags;
         break;
      default: break;
      }
   }
   return VK_SUCCESS;
}

static VkResult
get_formats(VkIcdSurfaceBase *surface, struct wsi_device *wsi,
            uint32_t *count, VkSurfaceFormatKHR *out_formats)
{
   struct wsi_ardesk_formats supported;
   VkResult result = wsi_ardesk_get_formats(wsi, &supported);
   if (result != VK_SUCCESS) return result;
   VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormatKHR, out, out_formats, count);
   for (unsigned i = 0; i < supported.count; i++) {
      vk_outarray_append_typed(VkSurfaceFormatKHR, &out, f) {
         *f = supported.formats[i];
      }
   }
   return vk_outarray_status(&out);
}

static VkResult
get_formats2(VkIcdSurfaceBase *surface, struct wsi_device *wsi, const void *info_next,
             uint32_t *count, VkSurfaceFormat2KHR *out_formats)
{
   struct wsi_ardesk_formats supported;
   VkResult result = wsi_ardesk_get_formats(wsi, &supported);
   if (result != VK_SUCCESS) return result;
   VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormat2KHR, out, out_formats, count);
   for (unsigned i = 0; i < supported.count; i++) {
      vk_outarray_append_typed(VkSurfaceFormat2KHR, &out, f) {
         f->surfaceFormat = supported.formats[i];
      }
   }
   return vk_outarray_status(&out);
}

static VkResult
get_present_modes(VkIcdSurfaceBase *surface, struct wsi_device *wsi,
                  uint32_t *count, VkPresentModeKHR *modes)
{
   VK_OUTARRAY_MAKE_TYPED(VkPresentModeKHR, out, modes, count);
   vk_outarray_append_typed(VkPresentModeKHR, &out, mode) { *mode = VK_PRESENT_MODE_FIFO_KHR; }
   return vk_outarray_status(&out);
}

static VkResult
get_rectangles(VkIcdSurfaceBase *surface, struct wsi_device *wsi,
               uint32_t *count, VkRect2D *rects)
{
   VkSurfaceCapabilities2KHR caps = {.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
   VkResult result = get_capabilities(surface, wsi, NULL, &caps);
   if (result != VK_SUCCESS) return result;
   VK_OUTARRAY_MAKE_TYPED(VkRect2D, out, rects, count);
   vk_outarray_append_typed(VkRect2D, &out, rect) {
      *rect = (VkRect2D){ .extent = caps.surfaceCapabilities.currentExtent };
   }
   return vk_outarray_status(&out);
}

static struct wsi_image *
get_image(struct wsi_swapchain *base, uint32_t index)
{
   return &((struct wsi_ardesk_chain *)base)->images[index].base;
}

static VkResult
acquire(struct wsi_swapchain *base, const VkAcquireNextImageInfoKHR *info, uint32_t *index)
{
   struct wsi_ardesk_chain *chain = (void *)base;
   uint64_t start = os_time_get_nano();
   mtx_lock(&chain->lock);
   if (chain->retired) {
      mtx_unlock(&chain->lock);
      return VK_ERROR_OUT_OF_DATE_KHR;
   }
   for (;;) {
      VkResult result = wsi_ardesk_dispatch(chain, 0);
      if (result != VK_SUCCESS) break;
      for (unsigned i = 0; i < base->image_count; i++) {
         if (chain->images[i].state == ARDESK_FREE) {
            chain->images[i].state = ARDESK_ACQUIRED;
            *index = i;
            mtx_unlock(&chain->lock);
            return VK_SUCCESS;
         }
      }
      uint64_t elapsed = os_time_get_nano() - start;
      if (info->timeout != UINT64_MAX && elapsed >= info->timeout) {
         mtx_unlock(&chain->lock);
         return info->timeout ? VK_TIMEOUT : VK_NOT_READY;
      }
      if (wsi_ardesk_dispatch(chain, info->timeout == UINT64_MAX ? UINT64_MAX :
                             info->timeout - elapsed) != VK_SUCCESS) break;
      /* A presenting thread may need this same lock to submit an already
       * acquired image. Do not hold it for the entire acquire timeout. */
      mtx_unlock(&chain->lock);
      thrd_yield();
      mtx_lock(&chain->lock);
      if (chain->retired) {
         mtx_unlock(&chain->lock);
         return VK_ERROR_OUT_OF_DATE_KHR;
      }
   }
   VkResult result = chain->status;
   mtx_unlock(&chain->lock);
   return result;
}

static VkResult
release_images(struct wsi_swapchain *base, uint32_t count, const uint32_t *indices)
{
   struct wsi_ardesk_chain *chain = (void *)base;
   mtx_lock(&chain->lock);
   for (unsigned i = 0; i < count; i++) {
      if (chain->images[indices[i]].state == ARDESK_ACQUIRED)
         chain->images[indices[i]].state = ARDESK_FREE;
   }
   mtx_unlock(&chain->lock);
   return VK_SUCCESS;
}

static VkResult
present(struct wsi_swapchain *base, uint32_t index, uint64_t present_id,
        const VkPresentRegionKHR *damage)
{
   struct wsi_ardesk_chain *chain = (void *)base;
   /* The two existing protocols carry release events, but no acquire fence.
    * Wait for Mesa's pre-present submit, which consumes the application's
    * semaphores. This is a GPU completion wait, never a pixel readback. */
   VkResult result = base->wsi->WaitForFences(base->device, 1, &base->fences[index],
                                           true, UINT64_MAX);
   mtx_lock(&chain->lock);
   if (result == VK_SUCCESS) result = chain->status;
   if (result == VK_SUCCESS)
      result = chain->connection ? wsi_ardesk_x11_present(chain, &chain->images[index]) :
                                  wsi_ardesk_wayland_present(chain, &chain->images[index]);
   if (result != VK_SUCCESS) chain->status = result;
   mtx_unlock(&chain->lock);
   return result;
}

static VkResult
destroy(struct wsi_swapchain *base, const VkAllocationCallbacks *alloc)
{
   struct wsi_ardesk_chain *chain = (void *)base;
   wsi_ardesk_x11_finish(chain);
   for (unsigned i = 0; i < base->image_count; i++) {
      wsi_destroy_image(base, &chain->images[i].base);
      wsi_ardesk_free_buffer(&chain->images[i]);
   }
   wsi_ardesk_disconnect(chain);
   mtx_destroy(&chain->lock);
   wsi_swapchain_finish(base);
   vk_free(alloc, chain);
   return VK_SUCCESS;
}

/* Import only when the allocation covers the driver's full requirements.
 * A gralloc image can omit the extra rows needed by Turnip's image layout;
 * importing it as a transfer buffer instead preserves its actual row layout. */
static VkResult
import_memory(struct wsi_ardesk_chain *chain, struct wsi_ardesk_image *image,
              const VkMemoryRequirements *reqs, VkImage vk_image, VkBuffer buffer,
              VkDeviceMemory *memory)
{
   struct wsi_swapchain *base = &chain->base;
   const struct wsi_device *wsi = base->wsi;
   VK_FROM_HANDLE(vk_device, device, base->device);
   VkMemoryFdPropertiesKHR props = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
   VkResult result = device->dispatch_table.GetMemoryFdPropertiesKHR(base->device,
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, image->fds[0], &props);
   if (result != VK_SUCCESS) return result;
   uint32_t type = wsi_select_device_memory_type(wsi, reqs->memoryTypeBits & props.memoryTypeBits);
   if (type == UINT32_MAX) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   off_t size = lseek(image->fds[0], 0, SEEK_END);
   if (size < 0 || (uint64_t)size < reqs->size) {
      chain->needs_buffer_blit = size >= 0 && vk_image != VK_NULL_HANDLE;
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
   VkMemoryDedicatedAllocateInfo dedicated = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = vk_image, .buffer = buffer,
   };
   VkImportMemoryFdInfoKHR import = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, .pNext = &dedicated,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = fcntl(image->fds[0], F_DUPFD_CLOEXEC, 0),
   };
   if (import.fd < 0) return VK_ERROR_OUT_OF_HOST_MEMORY;
   VkMemoryAllocateInfo alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &import,
      .allocationSize = reqs->size, .memoryTypeIndex = type,
   };
   result = wsi->AllocateMemory(base->device, &alloc, &base->alloc, memory);
   if (result != VK_SUCCESS) close(import.fd);
   return result;
}

static VkResult
import_image(struct wsi_ardesk_chain *chain, struct wsi_ardesk_image *image)
{
   struct wsi_swapchain *base = &chain->base;
   const struct wsi_device *wsi = base->wsi;
   VkResult result = wsi->CreateImage(base->device, &base->image_info.create,
                                     &base->alloc, &image->base.image);
   if (result != VK_SUCCESS) return result;
   VkMemoryRequirements reqs;
   wsi->GetImageMemoryRequirements(base->device, image->base.image, &reqs);
   if (base->blit.type == WSI_SWAPCHAIN_NO_BLIT) {
      result = import_memory(chain, image, &reqs, image->base.image,
                             VK_NULL_HANDLE, &image->base.memory);
   } else {
      VkMemoryDedicatedAllocateInfo dedicated = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
         .image = image->base.image,
      };
      uint32_t type = wsi_select_device_memory_type(wsi, reqs.memoryTypeBits);
      if (type == UINT32_MAX) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      VkMemoryAllocateInfo memory = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &dedicated,
         .allocationSize = reqs.size, .memoryTypeIndex = type,
      };
      result = wsi->AllocateMemory(base->device, &memory, &base->alloc, &image->base.memory);
   }
   if (result != VK_SUCCESS) return result;
   result = wsi->BindImageMemory(base->device, image->base.image, image->base.memory, 0);
   if (result != VK_SUCCESS || base->blit.type == WSI_SWAPCHAIN_NO_BLIT) return result;

   VkExternalMemoryBufferCreateInfo external = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   VkBufferCreateInfo buffer = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &external,
      .size = base->image_info.linear_size, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   result = wsi->CreateBuffer(base->device, &buffer, &base->alloc, &image->base.blit.buffer);
   if (result != VK_SUCCESS) return result;
   wsi->GetBufferMemoryRequirements(base->device, image->base.blit.buffer, &reqs);
   result = import_memory(chain, image, &reqs, VK_NULL_HANDLE,
                          image->base.blit.buffer, &image->base.blit.memory);
   if (result != VK_SUCCESS) return result;
   result = wsi->BindBufferMemory(base->device, image->base.blit.buffer, image->base.blit.memory, 0);
   if (result != VK_SUCCESS) return result;
   /* Mesa submits this GPU copy after the application's render semaphores,
    * before the same present fence and compositor release used by direct images. */
   return wsi_finish_create_blit_context(base, &base->image_info, &image->base);
}

static VkResult
create_swapchain_for_layout(VkIcdSurfaceBase *surface, VkDevice device, struct wsi_device *wsi,
                 const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *alloc,
                 struct wsi_swapchain **out, bool buffer_blit)
{
   if (info->oldSwapchain) {
      VK_FROM_HANDLE(wsi_swapchain, old_base, info->oldSwapchain);
      struct wsi_ardesk_chain *old = (void *)old_base;
      mtx_lock(&old->lock);
      old->retired = true;
      mtx_unlock(&old->lock);
   }
   if (info->minImageCount > ARDESK_MAX_IMAGES || info->imageArrayLayers != 1 ||
       !info->imageExtent.width || !info->imageExtent.height ||
       info->imageExtent.width > wsi->maxImageDimension2D ||
       info->imageExtent.height > wsi->maxImageDimension2D ||
       info->presentMode != VK_PRESENT_MODE_FIFO_KHR ||
       info->imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
       (info->flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR))
      return VK_ERROR_INITIALIZATION_FAILED;
   struct wsi_ardesk_formats supported;
   VkResult result = wsi_ardesk_get_formats(wsi, &supported);
   if (result != VK_SUCCESS) return result;
   unsigned format;
   for (format = 0; format < supported.count; format++)
      if (info->imageFormat == supported.formats[format].format) break;
   if (format == supported.count || (info->imageUsage & ~supported.usage))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   VkImageFormatProperties properties;
   result = wsi_ardesk_image_properties(wsi, info->imageFormat, info->imageUsage,
                                        buffer_blit, &properties);
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED && !buffer_blit)
      return create_swapchain_for_layout(surface, device, wsi, info, alloc, out, true);
   if (result != VK_SUCCESS) return result;
   if (info->imageExtent.width > properties.maxExtent.width ||
       info->imageExtent.height > properties.maxExtent.height) {
      if (!buffer_blit)
         return create_swapchain_for_layout(surface, device, wsi, info, alloc, out, true);
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }
   struct wsi_ardesk_chain *chain = vk_zalloc(alloc, sizeof(*chain), 8,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!chain) return VK_ERROR_OUT_OF_HOST_MEMORY;
   struct wsi_android_image_params params = {
      .base.image_type = WSI_IMAGE_TYPE_ANDROID, .buffer_blit = buffer_blit,
   };
   result = wsi_swapchain_init(wsi, &chain->base, device, info, &params.base, alloc);
   if (result != VK_SUCCESS) { vk_free(alloc, chain); return result; }
   if (mtx_init(&chain->lock, mtx_plain) != thrd_success) {
      wsi_swapchain_finish(&chain->base);
      vk_free(alloc, chain);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   chain->base.destroy = destroy;
   chain->base.get_wsi_image = get_image;
   chain->base.acquire_next_image = acquire;
   chain->base.queue_present = present;
   chain->base.release_images = release_images;
   chain->base.present_mode = VK_PRESENT_MODE_FIFO_KHR;
   chain->base.image_count = MAX2(info->minImageCount, 3);
   chain->width = info->imageExtent.width;
   chain->height = info->imageExtent.height;
   chain->format = (info->imageFormat == VK_FORMAT_B8G8R8A8_UNORM ||
                    info->imageFormat == VK_FORMAT_B8G8R8A8_SRGB) ? 5 : 1;
   for (unsigned i = 0; i < chain->base.image_count; i++) {
      chain->images[i].base.dma_buf_fd = -1;
      for (unsigned j = 0; j < WSI_ES_COUNT; j++)
         chain->images[i].base.explicit_sync[j].fd = -1;
   }
   result = wsi_ardesk_connect(chain, is_wayland(surface) ?
                              ((VkIcdSurfaceWayland *)surface)->display : NULL);
   if (result != VK_SUCCESS) goto fail;
   if (is_wayland(surface)) {
      chain->surface = wl_proxy_create_wrapper(((VkIcdSurfaceWayland *)surface)->surface);
      if (!chain->surface) { result = VK_ERROR_OUT_OF_HOST_MEMORY; goto fail; }
      wl_proxy_set_queue((struct wl_proxy *)chain->surface, chain->queue);
   } else {
      chain->connection = x_connection(surface);
      chain->window = x_window(surface);
      result = wsi_ardesk_x11_init(chain);
      if (result != VK_SUCCESS) goto fail;
   }
   for (unsigned i = 0; i < chain->base.image_count; i++) {
      struct wsi_ardesk_image *image = &chain->images[i];
      result = wsi_ardesk_alloc_buffer(chain, image);
      if (result != VK_SUCCESS) goto fail;
      if (!i) {
         chain->layout.rowPitch = image->stride * 4;
         if (buffer_blit) {
            chain->base.image_info.linear_stride = chain->layout.rowPitch;
            chain->base.image_info.linear_size = chain->layout.rowPitch * chain->height;
         } else {
            chain->modifier = (VkImageDrmFormatModifierExplicitCreateInfoEXT) {
               .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
               .drmFormatModifier = 0, /* DRM_FORMAT_MOD_LINEAR */
               .drmFormatModifierPlaneCount = 1, .pPlaneLayouts = &chain->layout,
            };
            __vk_append_struct(&chain->base.image_info.create, &chain->modifier);
            chain->base.image_info.create.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         }
      } else if (image->stride * 4 != chain->layout.rowPitch) {
         result = VK_ERROR_FORMAT_NOT_SUPPORTED;
         goto fail;
      }
      result = import_image(chain, image);
      if (result != VK_SUCCESS) goto fail;
   }
   *out = &chain->base;
   return VK_SUCCESS;
fail: {
      bool retry = !buffer_blit && chain->needs_buffer_blit;
      destroy(&chain->base, alloc);
      if (retry)
         return create_swapchain_for_layout(surface, device, wsi, info, alloc, out, true);
      return result;
   }
}

static VkResult
create_swapchain(VkIcdSurfaceBase *surface, VkDevice device, struct wsi_device *wsi,
                 const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *alloc,
                 struct wsi_swapchain **out)
{
   return create_swapchain_for_layout(surface, device, wsi, info, alloc, out, false);
}

static struct wsi_interface interface = {
   .get_support = get_support, .get_capabilities2 = get_capabilities,
   .get_formats = get_formats, .get_formats2 = get_formats2,
   .get_present_modes = get_present_modes, .get_present_rectangles = get_rectangles,
   .create_swapchain = create_swapchain,
};

VkResult
wsi_x11_init_wsi(struct wsi_device *wsi, const VkAllocationCallbacks *alloc,
                const struct driOptionCache *options)
{
   wsi->wsi[VK_ICD_WSI_PLATFORM_XCB] = wsi->wsi[VK_ICD_WSI_PLATFORM_XLIB] = &interface;
   return VK_SUCCESS;
}
void wsi_x11_finish_wsi(struct wsi_device *wsi, const VkAllocationCallbacks *alloc) {}
VkResult
wsi_wl_init_wsi(struct wsi_device *wsi, const VkAllocationCallbacks *alloc, VkPhysicalDevice device)
{
   wsi->wsi[VK_ICD_WSI_PLATFORM_WAYLAND] = &interface;
   return VK_SUCCESS;
}
void wsi_wl_finish_wsi(struct wsi_device *wsi, const VkAllocationCallbacks *alloc) {}

VKAPI_ATTR VkBool32 VKAPI_CALL
wsi_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physical, uint32_t queue,
                                              xcb_connection_t *connection, xcb_visualid_t visual)
{
   VK_FROM_HANDLE(vk_physical_device, device, physical);
   struct wsi_ardesk_formats formats;
   if (wsi_ardesk_get_formats(device->wsi_device, &formats) != VK_SUCCESS || !formats.count)
      return false;
   return queue < device->wsi_device->queue_family_count &&
          (device->wsi_device->queue_supports_blit & BITFIELD64_BIT(queue)) &&
          wsi_ardesk_x11_supported(connection, visual) && has_allocator(NULL);
}
VKAPI_ATTR VkBool32 VKAPI_CALL
wsi_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physical, uint32_t queue,
                                               Display *display, VisualID visual)
{
   return wsi_GetPhysicalDeviceXcbPresentationSupportKHR(physical, queue,
                                                        XGetXCBConnection(display), visual);
}
VKAPI_ATTR VkBool32 VKAPI_CALL
wsi_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physical, uint32_t queue,
                                                  struct wl_display *display)
{
   VK_FROM_HANDLE(vk_physical_device, device, physical);
   struct wsi_ardesk_formats formats;
   if (wsi_ardesk_get_formats(device->wsi_device, &formats) != VK_SUCCESS || !formats.count)
      return false;
   return queue < device->wsi_device->queue_family_count &&
          (device->wsi_device->queue_supports_blit & BITFIELD64_BIT(queue)) && has_allocator(display);
}

#define CREATE_SURFACE(Name, member, platform_name, fields) \
VKAPI_ATTR VkResult VKAPI_CALL \
wsi_Create##Name##SurfaceKHR(VkInstance handle, const Vk##Name##SurfaceCreateInfoKHR *info, \
                             const VkAllocationCallbacks *alloc, VkSurfaceKHR *out) \
{ \
   VK_FROM_HANDLE(vk_instance, instance, handle); \
   VkIcdSurface##Name *surface = vk_alloc2(&instance->alloc, alloc, sizeof(*surface), 8, \
                                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT); \
   if (!surface) return VK_ERROR_OUT_OF_HOST_MEMORY; \
   surface->base.platform = VK_ICD_WSI_PLATFORM_##platform_name; \
   fields \
   *out = VkIcdSurfaceBase_to_handle(&surface->base); \
   return VK_SUCCESS; \
}
CREATE_SURFACE(Xcb, connection, XCB, surface->connection = info->connection; surface->window = info->window;)
CREATE_SURFACE(Xlib, dpy, XLIB, surface->dpy = info->dpy; surface->window = info->window;)
CREATE_SURFACE(Wayland, display, WAYLAND, surface->display = info->display; surface->surface = info->surface;)
#undef CREATE_SURFACE

void
wsi_wl_surface_destroy(VkIcdSurfaceBase *surface, VkInstance handle,
                       const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_instance, instance, handle);
   vk_free2(&instance->alloc, alloc, surface);
}
