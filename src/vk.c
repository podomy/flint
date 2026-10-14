#include "vk.h"
#include "wl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_wayland.h>

// vk_init() depends on wl_init() having succeeded because it needs wl.display and wl.surface It then creates: VkInstance -> VkSurfaceKHR (Wayland Surface) -> graphics + presentation queue family -> VkDevice -> VkQueue

// Flat background color for the one frame this demo clears to.
static const VkClearColorValue kClearColor = {
    .float32 = {0.05f, 0.05f, 0.08f, 1.0f},
};

bool vk_init(struct Vk* vk, struct Wl* wl) {
    memset(vk, 0, sizeof(*vk));

    // Surface extensions the instance needs. The generic surface extension plus the Wayland binding. Surface creation is unavailable without both enabled.
    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };

    // App metadata for the loader. sType tags the struct type. Names and versions are informational, apiVersion sets the API contract the code is written against.
    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Flint";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Flint";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    // Instance request. Links the app metadata and enables the extensions above. A NULL allocator selects the default allocator.
    VkInstanceCreateInfo create_info = {0};
    create_info.sType =
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = 2;
    create_info.ppEnabledExtensionNames = exts;

    // NULL in the pAllocator parameter selects Vulkan's default host memory allocator
    // We can pass our own host memory allocator to allocate memory in DRAM/Heap by providing a VkAllocationCallbacks structure whose callbacks forward to malloc, realloc, and free
    // We can allocate memory in VRAM/GPU visible memory by calling vkAllocateMemory directly with the needed requirements (the vkAllocateMemory function needs appropriate vulkan memory type and a binded buffer)
    VkResult result =
        vkCreateInstance(&create_info, NULL, &vk->inst);
    if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "vkCreateInstance failed with error code %d\n",
            result);
        return false;
    }

    // Vulkan Wayland surface

    // This must happen before physical-device selection, because we want to test queue-family presentation support against this actual VkSurfaceKHR.
    VkWaylandSurfaceCreateInfoKHR surface_info = {0};
    surface_info.sType =
        VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    surface_info.display = wl->display;
    surface_info.surface = wl->surface;

    result = vkCreateWaylandSurfaceKHR(
        vk->inst, &surface_info, NULL, &vk->surf);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkCreateWaylandSurfaceKHR failed with "
                "error code %d\n",
                result);
        return false;
    }

    // Physical devices

    uint32_t ndev = 0;

    result =
        vkEnumeratePhysicalDevices(vk->inst, &ndev, NULL);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkEnumeratePhysicalDevices failed with "
                "error code %d\n",
                result);
        return false;
    }

    if (ndev == 0) {
        fprintf(stderr, "No physical devices were found\n");
        return false;
    }

    if (ndev > 8) {
        ndev = 8;
    }
    VkPhysicalDevice devices[8] = {0};

    result = vkEnumeratePhysicalDevices(vk->inst, &ndev,
                                        devices);

    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkEnumeratePhysicalDevices failed with "
                "error code %d\n",
                result);
        return false;
    }

    // Find physical device + queue family

    uint32_t picked = 0;
    uint32_t qfam = 0;
    bool found = false;

    for (uint32_t i = 0; i < ndev && !found; i++) {
        uint32_t nfam = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(
            devices[i], &nfam, NULL);

        if (nfam > 8)
            nfam = 8;

        VkQueueFamilyProperties fams[8] = {0};

        vkGetPhysicalDeviceQueueFamilyProperties(
            devices[i], &nfam, fams);

        for (uint32_t f = 0; f < nfam; f++) {

            // We need this to find a graphics-capable queue
            if (!(fams[f].queueFlags &
                  VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }

            // Check whether this queue family can present to our actual Wayland VkSurfaceKHR
            VkBool32 present_supported = VK_FALSE;

            result = vkGetPhysicalDeviceSurfaceSupportKHR(
                devices[i], f, vk->surf,
                &present_supported);

            if (result != VK_SUCCESS) {
                fprintf(stderr,
                        "vkGetPhysicalDeviceSurfaceSupportK"
                        "HR failed with error code %d\n",
                        result);
                return false;
            }

            if (!present_supported) {
                continue;
            }

            picked = i;
            qfam = f;
            found = true;
            break;
        }
    }

    if (!found) {
        fprintf(stderr,
                "No physical device with a graphics + "
                "present queue was found\n");
        return false;
    }

    vk->phys = devices[picked];
    vk->qfam = qfam;

    // Logical device

    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_info = {0};
    queue_info.sType =
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = qfam;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    // The swapchain extension is required for presenting Vulkan images to VkSurfaceKHR (wayland surface)
    const char* device_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkDeviceCreateInfo device_info = {0};
    device_info.sType =
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount =
        sizeof(device_exts) / sizeof(device_exts[0]);
    device_info.ppEnabledExtensionNames = device_exts;

    result = vkCreateDevice(vk->phys, &device_info, NULL,
                            &vk->dev);
    if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "vkCreateDevice failed with error code %d\n",
            result);
        return false;
    }

    // Get the queue from the selected queue family
    vkGetDeviceQueue(vk->dev, qfam, 0, &vk->queue);

    // Command pool. One pool, one primary command buffer per swapchain image, re-recorded every frame since all we do right now is clear + present.
    VkCommandPoolCreateInfo pool_info = {0};
    pool_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags =
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = vk->qfam;

    result = vkCreateCommandPool(vk->dev, &pool_info,
                                  NULL, &vk->cmd_pool);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkCreateCommandPool failed with error "
                "code %d\n",
                result);
        return false;
    }

    // Frame sync objects. Flint keeps exactly one frame in flight, so one set is enough. in_flight starts signaled so the first wait in vk_draw_frame doesn't block forever.
    VkSemaphoreCreateInfo sem_info = {0};
    sem_info.sType =
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    result = vkCreateSemaphore(vk->dev, &sem_info, NULL,
                                &vk->image_available);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkCreateSemaphore failed with error "
                "code %d\n",
                result);
        return false;
    }

    result = vkCreateSemaphore(vk->dev, &sem_info, NULL,
                                &vk->render_finished);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkCreateSemaphore failed with error "
                "code %d\n",
                result);
        return false;
    }

    VkFenceCreateInfo fence_info = {0};
    fence_info.sType =
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    result = vkCreateFence(vk->dev, &fence_info, NULL,
                            &vk->in_flight);
    if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "vkCreateFence failed with error code %d\n",
            result);
        return false;
    }

    return vk_swapchain_create(vk, wl);
}

void vk_swapchain_destroy(struct Vk* vk) {
    if (vk->dev == VK_NULL_HANDLE) {
        return;
    }

    // Make sure nothing is still reading from or writing to the swapchain's images before tearing it down.
    vkDeviceWaitIdle(vk->dev);

    if (vk->image_count > 0 &&
        vk->cmd_pool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(vk->dev, vk->cmd_pool,
                              vk->image_count,
                              vk->cmd_bufs);
        memset(vk->cmd_bufs, 0, sizeof(vk->cmd_bufs));
    }
    vk->image_count = 0;

    if (vk->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vk->dev, vk->swapchain,
                               NULL);
        vk->swapchain = VK_NULL_HANDLE;
    }
}

bool vk_swapchain_create(struct Vk* vk, struct Wl* wl) {
    // Drop whatever swapchain exists already; this function doubles as both first-time creation and resize recreation.
    vk_swapchain_destroy(vk);

    VkSurfaceCapabilitiesKHR caps = {0};
    VkResult result =
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            vk->phys, vk->surf, &caps);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR "
                "failed with error code %d\n",
                result);
        return false;
    }

    // Surface formats. First call for the count, second to fill the array.
    uint32_t nfmt = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        vk->phys, vk->surf, &nfmt, NULL);
    if (result != VK_SUCCESS || nfmt == 0) {
        fprintf(stderr,
                "vkGetPhysicalDeviceSurfaceFormatsKHR "
                "failed with error code %d\n",
                result);
        return false;
    }
    if (nfmt > 8) {
        nfmt = 8;
    }
    VkSurfaceFormatKHR formats[8] = {0};
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(
        vk->phys, vk->surf, &nfmt, formats);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkGetPhysicalDeviceSurfaceFormatsKHR "
                "failed with error code %d\n",
                result);
        return false;
    }

    // Prefer BGRA8 sRGB; fall back to whatever the surface offers first.
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < nfmt; i++) {
        if (formats[i].format ==
                VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace ==
                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }
    vk->format = chosen.format;

    // Extent: use the surface's current extent when the compositor dictates one. The special value 0xFFFFFFFF means "you decide", in which case fall back to the toplevel's last requested size, clamped into range.
    VkExtent2D extent;
    if (caps.currentExtent.width != 0xFFFFFFFFu) {
        extent = caps.currentExtent;
    } else {
        extent.width = (uint32_t)wl->width;
        extent.height = (uint32_t)wl->height;
    }
    if (extent.width < caps.minImageExtent.width) {
        extent.width = caps.minImageExtent.width;
    }
    if (extent.width > caps.maxImageExtent.width) {
        extent.width = caps.maxImageExtent.width;
    }
    if (extent.height < caps.minImageExtent.height) {
        extent.height = caps.minImageExtent.height;
    }
    if (extent.height > caps.maxImageExtent.height) {
        extent.height = caps.maxImageExtent.height;
    }
    vk->extent = extent;

    // One more than the minimum for a bit of slack, capped at the surface's maximum (0 there means "no maximum").
    uint32_t min_images = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 &&
        min_images > caps.maxImageCount) {
        min_images = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sc_info = {0};
    sc_info.sType =
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sc_info.surface = vk->surf;
    sc_info.minImageCount = min_images;
    sc_info.imageFormat = chosen.format;
    sc_info.imageColorSpace = chosen.colorSpace;
    sc_info.imageExtent = extent;
    sc_info.imageArrayLayers = 1;
    // TRANSFER_DST is what lets vkCmdClearColorImage write directly into the swapchain image, with no render pass or pipeline needed yet.
    sc_info.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sc_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc_info.preTransform = caps.currentTransform;
    sc_info.compositeAlpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // FIFO is the one present mode every Vulkan implementation is required to support.
    sc_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sc_info.clipped = VK_TRUE;

    result = vkCreateSwapchainKHR(vk->dev, &sc_info, NULL,
                                   &vk->swapchain);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkCreateSwapchainKHR failed with error "
                "code %d\n",
                result);
        return false;
    }

    uint32_t nimg = 0;
    result = vkGetSwapchainImagesKHR(
        vk->dev, vk->swapchain, &nimg, NULL);
    if (result != VK_SUCCESS || nimg == 0) {
        fprintf(stderr,
                "vkGetSwapchainImagesKHR failed with "
                "error code %d\n",
                result);
        return false;
    }
    if (nimg > VK_MAX_SWAPCHAIN_IMAGES) {
        nimg = VK_MAX_SWAPCHAIN_IMAGES;
    }
    result = vkGetSwapchainImagesKHR(
        vk->dev, vk->swapchain, &nimg, vk->images);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkGetSwapchainImagesKHR failed with "
                "error code %d\n",
                result);
        return false;
    }
    vk->image_count = nimg;

    // One primary command buffer per swapchain image, allocated up front and re-recorded each time that image is drawn into.
    VkCommandBufferAllocateInfo alloc_info = {0};
    alloc_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = vk->cmd_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = vk->image_count;

    result = vkAllocateCommandBuffers(
        vk->dev, &alloc_info, vk->cmd_bufs);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkAllocateCommandBuffers failed with "
                "error code %d\n",
                result);
        return false;
    }

    return true;
}

bool vk_draw_frame(struct Vk* vk, bool* resize_needed) {
    *resize_needed = false;

    // Wait for the previous frame using this slot's sync objects to finish before reusing them.
    VkResult result = vkWaitForFences(
        vk->dev, 1, &vk->in_flight, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "vkWaitForFences failed with error code %d\n",
            result);
        return false;
    }

    uint32_t image_index = 0;
    result = vkAcquireNextImageKHR(
        vk->dev, vk->swapchain, UINT64_MAX,
        vk->image_available, VK_NULL_HANDLE,
        &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // The swapchain no longer matches the surface (typically a resize); nothing was submitted, so just ask the caller to recreate and try again next frame.
        *resize_needed = true;
        return true;
    }
    if (result != VK_SUCCESS &&
        result != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr,
                "vkAcquireNextImageKHR failed with "
                "error code %d\n",
                result);
        return false;
    }

    result = vkResetFences(vk->dev, 1, &vk->in_flight);
    if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "vkResetFences failed with error code %d\n",
            result);
        return false;
    }

    VkCommandBuffer cmd = vk->cmd_bufs[image_index];
    result = vkResetCommandBuffer(cmd, 0);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkResetCommandBuffer failed with error "
                "code %d\n",
                result);
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {0};
    begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(cmd, &begin_info);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkBeginCommandBuffer failed with error "
                "code %d\n",
                result);
        return false;
    }

    VkImageSubresourceRange range = {0};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    // UNDEFINED -> TRANSFER_DST so the image can be cleared. UNDEFINED is fine here since we don't care about whatever was in the image before; we're about to overwrite all of it.
    VkImageMemoryBarrier to_clear = {0};
    to_clear.sType =
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_clear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_clear.newLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_clear.srcQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    to_clear.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    to_clear.image = vk->images[image_index];
    to_clear.subresourceRange = range;
    to_clear.dstAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
        NULL, 1, &to_clear);

    vkCmdClearColorImage(
        cmd, vk->images[image_index],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &kClearColor, 1, &range);

    // TRANSFER_DST -> PRESENT_SRC so the presentation engine is allowed to show it.
    VkImageMemoryBarrier to_present = {0};
    to_present.sType =
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.oldLayout =
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout =
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    to_present.image = vk->images[image_index];
    to_present.subresourceRange = range;
    to_present.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL,
        0, NULL, 1, &to_present);

    result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkEndCommandBuffer failed with error "
                "code %d\n",
                result);
        return false;
    }

    // Don't start writing to the image until the presentation engine is actually done reading from it (signaled by image_available).
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkSubmitInfo submit_info = {0};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &vk->image_available;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &vk->render_finished;

    result = vkQueueSubmit(vk->queue, 1, &submit_info,
                            vk->in_flight);
    if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "vkQueueSubmit failed with error code %d\n",
            result);
        return false;
    }

    VkPresentInfoKHR present_info = {0};
    present_info.sType =
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &vk->render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &vk->swapchain;
    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(vk->queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR ||
        result == VK_SUBOPTIMAL_KHR) {
        *resize_needed = true;
        return true;
    }
    if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "vkQueuePresentKHR failed with error code "
            "%d\n",
            result);
        return false;
    }

    return true;
}

// cleanup/destroy resources IN ORDER(VERY IMPORTANT): swapchain + frame resources -> VkSurfaceKHR -> VkDevice -> VkInstance

void vk_finish(struct Vk* vk) {
    if (vk->dev != VK_NULL_HANDLE) {
        // Make sure no in-flight work still touches anything below before destroying it.
        vkDeviceWaitIdle(vk->dev);
    }

    vk_swapchain_destroy(vk);

    if (vk->in_flight != VK_NULL_HANDLE) {
        vkDestroyFence(vk->dev, vk->in_flight, NULL);
        vk->in_flight = VK_NULL_HANDLE;
    }
    if (vk->render_finished != VK_NULL_HANDLE) {
        vkDestroySemaphore(vk->dev, vk->render_finished,
                            NULL);
        vk->render_finished = VK_NULL_HANDLE;
    }
    if (vk->image_available != VK_NULL_HANDLE) {
        vkDestroySemaphore(vk->dev, vk->image_available,
                            NULL);
        vk->image_available = VK_NULL_HANDLE;
    }
    if (vk->cmd_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vk->dev, vk->cmd_pool, NULL);
        vk->cmd_pool = VK_NULL_HANDLE;
    }

    // Surface before device: legal because the swapchain, the only thing that tied them together, is already gone.
    if (vk->surf != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vk->inst, vk->surf, NULL);
        vk->surf = VK_NULL_HANDLE;
    }

    if (vk->dev != VK_NULL_HANDLE) {
        vkDestroyDevice(vk->dev, NULL);
        vk->dev = VK_NULL_HANDLE;
        vk->queue = VK_NULL_HANDLE;
    }

    if (vk->inst != VK_NULL_HANDLE) {
        vkDestroyInstance(vk->inst, NULL);
        vk->inst = VK_NULL_HANDLE;
    }

    vk->phys = VK_NULL_HANDLE;
}