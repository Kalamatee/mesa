/*
 * Copyright © 2016 The AROS Dev Team
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "anv_wsi.h"

#include "util/hash_table.h"

struct wsi_aros_connection {
};

struct wsi_aros {
   struct anv_wsi_interface base;

   pthread_mutex_t                              mutex;
   /* Hash table of wsi_aros_connection mappings */
   struct hash_table *connections;
};

static struct wsi_aros_connection *
wsi_aros_connection_create(struct anv_physical_device *device)
{
   struct wsi_aros_connection *wsi_conn =
      anv_alloc(&instance->alloc, sizeof(*wsi_conn), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!wsi_conn)
      return NULL;

   return wsi_conn;
}

static void
wsi_aros_connection_destroy(struct anv_physical_device *device,
                           struct wsi_aros_connection *conn)
{
   anv_free(&instance->alloc, conn);
}

static struct wsi_aros_connection *
wsi_aros_get_connection(struct anv_physical_device *device)
{
   struct wsi_aros *wsi =
      (struct wsi_aros *)device->wsi[VK_ICD_WSI_PLATFORM_AROS];

   pthread_mutex_lock(&wsi->mutex);

   struct hash_entry *entry = _mesa_hash_table_search(wsi->connections, conn);
   if (!entry) {
      /* We're about to make a bunch of blocking calls.  Let's drop the
       * mutex for now so we don't block up too badly.
       */
      pthread_mutex_unlock(&wsi->mutex);

      struct wsi_aros_connection *wsi_conn =
         wsi_aros_connection_create(device);

      pthread_mutex_lock(&wsi->mutex);

      entry = _mesa_hash_table_search(wsi->connections, conn);
      if (entry) {
         /* Oops, someone raced us to it */
         wsi_aros_connection_destroy(device, wsi_conn);
      } else {
         entry = _mesa_hash_table_insert(wsi->connections, conn, wsi_conn);
      }
   }

   pthread_mutex_unlock(&wsi->mutex);

   return entry->data;
}

static const VkSurfaceFormatKHR formats[] = {
   { .format = VK_FORMAT_B8G8R8A8_SRGB, },
};

static const VkPresentModeKHR present_modes[] = {
   VK_PRESENT_MODE_MAILBOX_KHR,
};

VkBool32 anv_GetPhysicalDeviceAROSPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    xcb_connection_t*                           connection,
    xcb_visualid_t                              visual_id)
{
   ANV_FROM_HANDLE(anv_physical_device, device, physicalDevice);

   struct wsi_aros_connection *wsi_conn =
      wsi_aros_get_connection(device);

   return true;
}

static VkResult
aros_surface_get_support(VkIcdSurfaceBase *icd_surface,
                        struct anv_physical_device *device,
                        uint32_t queueFamilyIndex,
                        VkBool32* pSupported)
{
   VkIcdSurfaceAROS *surface = (VkIcdSurfaceAROS *)icd_surface;

   struct wsi_aros_connection *wsi_conn =
      wsi_aros_get_connection(device);
   if (!wsi_conn)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   *pSupported = true;
   return VK_SUCCESS;
}

static VkResult
aros_surface_get_capabilities(VkIcdSurfaceBase *icd_surface,
                             struct anv_physical_device *device,
                             VkSurfaceCapabilitiesKHR *caps)
{
   VkIcdSurfaceAROS *surface = (VkIcdSurfaceAROS *)icd_surface;

   caps->minImageCount = 2;
   caps->maxImageCount = 4;
   caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->maxImageArrayLayers = 1;
   caps->supportedUsageFlags =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   return VK_SUCCESS;
}

static VkResult
aros_surface_get_formats(VkIcdSurfaceBase *surface,
                        struct anv_physical_device *device,
                        uint32_t *pSurfaceFormatCount,
                        VkSurfaceFormatKHR *pSurfaceFormats)
{
   if (pSurfaceFormats == NULL) {
      *pSurfaceFormatCount = ARRAY_SIZE(formats);
      return VK_SUCCESS;
   }

   assert(*pSurfaceFormatCount >= ARRAY_SIZE(formats));
   typed_memcpy(pSurfaceFormats, formats, *pSurfaceFormatCount);
   *pSurfaceFormatCount = ARRAY_SIZE(formats);

   return VK_SUCCESS;
}

static VkResult
aros_surface_get_present_modes(VkIcdSurfaceBase *surface,
                              struct anv_physical_device *device,
                              uint32_t *pPresentModeCount,
                              VkPresentModeKHR *pPresentModes)
{
   if (pPresentModes == NULL) {
      *pPresentModeCount = ARRAY_SIZE(present_modes);
      return VK_SUCCESS;
   }

   assert(*pPresentModeCount >= ARRAY_SIZE(present_modes));
   typed_memcpy(pPresentModes, present_modes, *pPresentModeCount);
   *pPresentModeCount = ARRAY_SIZE(present_modes);

   return VK_SUCCESS;
}

static VkResult
aros_surface_create_swapchain(VkIcdSurfaceBase *surface,
                             struct anv_device *device,
                             const VkSwapchainCreateInfoKHR* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             struct anv_swapchain **swapchain);

VkResult anv_CreateAROSSurfaceKHR(
    VkInstance                                  _instance,
    const VkXcbSurfaceCreateInfoKHR*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_AROS_SURFACE_CREATE_INFO_KHR);

   VkIcdSurfaceAROS *surface;

   surface = anv_alloc2(&instance->alloc, pAllocator, sizeof *surface, 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (surface == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   surface->base.platform = VK_ICD_WSI_PLATFORM_AROS;
   surface->connection = pCreateInfo->connection;
   surface->window = pCreateInfo->window;

   *pSurface = _VkIcdSurfaceBase_to_handle(&surface->base);

   return VK_SUCCESS;
}

struct aros_image {
   struct anv_image *                        image;
   struct anv_device_memory *                memory;
};

struct aros_swapchain {
   struct anv_swapchain                        base;
   VkExtent2D                                   extent;
   uint32_t                                     image_count;
   uint32_t                                     next_image;
};

static VkResult
aros_get_images(struct anv_swapchain *anv_chain,
               uint32_t* pCount, VkImage *pSwapchainImages)
{
   return VK_SUCCESS;
}

static VkResult
aros_acquire_next_image(struct anv_swapchain *anv_chain,
                       uint64_t timeout,
                       VkSemaphore semaphore,
                       uint32_t *image_index)
{
   return VK_SUCCESS;
}

static VkResult
aros_queue_present(struct anv_swapchain *anv_chain,
                  struct anv_queue *queue,
                  uint32_t image_index)
{
   return VK_SUCCESS;
}

static VkResult
aros_swapchain_destroy(struct anv_swapchain *anv_chain,
                      const VkAllocationCallbacks *pAllocator)
{
   return VK_SUCCESS;
}

static VkResult
aros_surface_create_swapchain(VkIcdSurfaceBase *icd_surface,
                             struct anv_device *device,
                             const VkSwapchainCreateInfoKHR *pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             struct anv_swapchain **swapchain_out)
{
   VkIcdSurfaceAROS *surface = (VkIcdSurfaceAROS *)icd_surface;
   VkResult result;

   int num_images = pCreateInfo->minImageCount;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);

   for (uint32_t i = 0; i < num_images; i++) {
      VkDeviceMemory memory_h;
      VkImage image_h;
      struct anv_image *image;
      struct anv_surface *surface;
      struct anv_device_memory *memory;

      anv_image_create(anv_device_to_handle(device),
         &(struct anv_image_create_info) {
            .isl_tiling_flags = ISL_TILING_X_BIT,
            .stride = 0,
            .vk_info =
         &(VkImageCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = pCreateInfo->imageFormat,
            .extent = {
               .width = pCreateInfo->imageExtent.width,
               .height = pCreateInfo->imageExtent.height,
               .depth = 1
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = 1,
            /* FIXME: Need a way to use X tiling to allow scanout */
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .flags = 0,
         }},
         NULL,
         &image_h);

      image = anv_image_from_handle(image_h);
      assert(anv_format_is_color(image->format));

      surface = &image->color_surface;

      anv_AllocateMemory(anv_device_to_handle(device),
         &(VkMemoryAllocateInfo) {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = image->size,
            .memoryTypeIndex = 0,
         },
         NULL /* XXX: pAllocator */,
         &memory_h);

      memory = anv_device_memory_from_handle(memory_h);

      anv_BindImageMemory(VK_NULL_HANDLE, anv_image_to_handle(image),
                          memory_h, 0);

      int ret = anv_gem_set_tiling(device, memory->bo.gem_handle,
                                   surface->isl.row_pitch, I915_TILING_X);

      if (ret) {
         /* FINISHME: Choose a better error. */
         result = vk_errorf(VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "set_tiling failed: %m");
         goto fail;
      }

      int fd = anv_gem_handle_to_fd(device, memory->bo.gem_handle);
      if (fd == -1) {
         /* FINISHME: Choose a better error. */
         result = vk_errorf(VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "handle_to_fd failed: %m");
         goto fail;
      }
   }

   return VK_SUCCESS;

 fail:
   return result;
}

VkResult
anv_aros_init_wsi(struct anv_physical_device *physical_device)
{
   struct wsi_aros *wsi;
   VkResult result;

   wsi = anv_alloc(&device->instance->alloc, sizeof(*wsi), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!wsi) {
      result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail;
   }

   int ret = pthread_mutex_init(&wsi->mutex, NULL);
   if (ret != 0) {
      if (ret == ENOMEM) {
         result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
      } else {
         /* FINISHME: Choose a better error. */
         result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      goto fail_alloc;
   }

   wsi->connections = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                              _mesa_key_pointer_equal);
   if (!wsi->connections) {
      result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_mutex;
   }

   wsi->base.get_support = aros_surface_get_support;
   wsi->base.get_capabilities = aros_surface_get_capabilities;
   wsi->base.get_formats = aros_surface_get_formats;
   wsi->base.get_present_modes = aros_surface_get_present_modes;
   wsi->base.create_swapchain = aros_surface_create_swapchain;

   device->wsi[VK_ICD_WSI_PLATFORM_AROS] = &wsi->base;

   return VK_SUCCESS;

fail_mutex:
   pthread_mutex_destroy(&wsi->mutex);
fail_alloc:
   anv_free(&&device->instance->alloc, wsi);
fail:
   device->wsi[VK_ICD_WSI_PLATFORM_AROS] = NULL;

   return result;
}

void
anv_aros_finish_wsi(struct anv_physical_device *physical_device)
{
   struct wsi_aros *wsi =
      (struct wsi_aros *)device->wsi[VK_ICD_WSI_PLATFORM_AROS];

   if (wsi) {
      _mesa_hash_table_destroy(wsi->connections, NULL);

      pthread_mutex_destroy(&wsi->mutex);

      anv_free(&&device->instance->alloc, wsi);
   }
}
