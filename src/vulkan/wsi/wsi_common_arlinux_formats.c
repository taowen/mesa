/* SPDX-License-Identifier: MIT */
#include "wsi_common_arlinux.h"
#include "vk_physical_device.h"

static const VkFormat candidates[] = {
   VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
   VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
};

/* Query the backing path, not just the application's ordinary image. The
 * copy path imports a transfer-destination DMA-BUF buffer and renders into
 * an ordinary optimal image; that image must also support transfer source. */
VkResult
wsi_arlinux_image_properties(struct wsi_device *wsi, VkFormat format,
                           VkImageUsageFlags usage, bool buffer_blit,
                           VkImageFormatProperties *properties)
{
   VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
      .drmFormatModifier = 0, /* DRM_FORMAT_MOD_LINEAR */
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   VkPhysicalDeviceExternalImageFormatInfo external = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
      .pNext = &modifier,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   if (buffer_blit) {
      VK_FROM_HANDLE(vk_physical_device, physical, wsi->pdevice);
      if (!physical->dispatch_table.GetPhysicalDeviceExternalBufferProperties)
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
      VkPhysicalDeviceExternalBufferInfo buffer = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
         .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
         .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      };
      VkExternalBufferProperties result = {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES,
      };
      physical->dispatch_table.GetPhysicalDeviceExternalBufferProperties(
         wsi->pdevice, &buffer, &result);
      if (!(result.externalMemoryProperties.externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }
   VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = buffer_blit ? NULL : &external,
      .format = format, .type = VK_IMAGE_TYPE_2D,
      .tiling = buffer_blit ? VK_IMAGE_TILING_OPTIMAL :
                             VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
      .usage = usage | (buffer_blit ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
   };
   VkExternalImageFormatProperties imported = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
   };
   VkImageFormatProperties2 result = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      .pNext = buffer_blit ? NULL : &imported,
   };
   VkResult status = wsi->GetPhysicalDeviceImageFormatProperties2(wsi->pdevice,
                                                                 &info, &result);
   if (status != VK_SUCCESS) return status;
   if ((!buffer_blit && !(imported.externalMemoryProperties.externalMemoryFeatures &
                         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) ||
       !(result.imageFormatProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;
   *properties = result.imageFormatProperties;
   return VK_SUCCESS;
}

static VkResult
format_properties(struct wsi_device *wsi, VkFormat format, VkImageUsageFlags usage,
                  VkImageFormatProperties *properties)
{
   VkResult result = wsi_arlinux_image_properties(wsi, format, usage, false, properties);
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED)
      result = wsi_arlinux_image_properties(wsi, format, usage, true, properties);
   return result;
}

VkResult
wsi_arlinux_get_formats(struct wsi_device *wsi, struct wsi_arlinux_formats *supported)
{
   *supported = (struct wsi_arlinux_formats) {
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .maximum = {wsi->maxImageDimension2D, wsi->maxImageDimension2D},
   };
   const VkImageUsageFlags optional[] = {
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
      VK_IMAGE_USAGE_STORAGE_BIT,
   };
   for (unsigned i = 0; i < ARRAY_SIZE(candidates); i++) {
      VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      VkImageFormatProperties properties;
      VkResult result = format_properties(wsi, candidates[i], usage, &properties);
      if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) continue;
      if (result != VK_SUCCESS) return result;
      supported->formats[supported->count++] =
         (VkSurfaceFormatKHR){candidates[i], VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
      for (unsigned bit = 0; bit < ARRAY_SIZE(optional); bit++) {
         VkImageFormatProperties additional;
         result = format_properties(wsi, candidates[i], usage | optional[bit], &additional);
         if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) continue;
         if (result != VK_SUCCESS) return result;
         usage |= optional[bit];
         properties = additional;
      }
      /* Surface usages are not an intersection of all surface formats: UNORM
       * can support storage images even when its sRGB counterpart cannot.
       * Creation still validates the selected format and complete usage mask
       * against the actual imported-image or buffer-copy backing path. */
      supported->usage |= usage;
      supported->maximum.width = MIN2(supported->maximum.width, properties.maxExtent.width);
      supported->maximum.height = MIN2(supported->maximum.height, properties.maxExtent.height);
   }
   return VK_SUCCESS;
}
