#include "vk.h"
#include "wl.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_wayland.h>

/*
 * Vulkan initialization
 *
 * vk_init() depends on wl_init() having succeeded because
 * it needs:
 *
 *     wl.display
 *     wl.surface
 *
 * It then creates:
 *
 *     VkInstance
 *          |
 *     VkSurfaceKHR (Wayland Surface)
 *          |
 *     physical device
 *          |
 *     graphics + presentation queue family
 *          |
 *     VkDevice
 *          |
 *     VkQueue
 */

bool vk_init(struct Vk* vk, struct Wl* wl) {
    memset(vk, 0, sizeof(*vk));

    // Surface extensions the instance needs.
    // The generic surface extension plus the
    // Wayland binding. Surface creation is
    // unavailable without both enabled.
    const char* exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };

    // App metadata for the loader. sType tags
    // the struct type. Names and versions are
    // informational, apiVersion sets the API
    // contract the code is written against.
    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Flint";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Flint";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    // Instance request. Links the app metadata
    // and enables the extensions above. A NULL
    // allocator selects the default allocator.
    VkInstanceCreateInfo create_info = {0};
    create_info.sType =
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = 2;
    create_info.ppEnabledExtensionNames = exts;

    VkResult result =
        vkCreateInstance(&create_info, NULL, &vk->inst);
    if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "vkCreateInstance failed with error code %d\n",
            result);
        goto fail;
    }

    /*
     * --------------------------------------------------------
     * Vulkan Wayland surface
     * --------------------------------------------------------
     *
     * This must happen before physical-device selection,
     * because we want to test queue-family presentation
     * support against this actual VkSurfaceKHR.
     */

    VkWaylandSurfaceCreateInfoKHR surface_info = {0};
    surface_info.sType =
        VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    surface_info.display = wl->display;
    surface_info.surface = wl->surface;

    result = vkCreateWaylandSurfaceKHR(
        vk->inst, &surface_info, NULL, &vk->surf);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkCreateWaylandSurfaceKHR failed with error code %d\n",
                result);
        goto fail;
    }

    /*
     * --------------------------------------------------------
     * Physical devices
     * --------------------------------------------------------
     */

    uint32_t ndev = 0;

    result =
        vkEnumeratePhysicalDevices(vk->inst, &ndev, NULL);
    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkEnumeratePhysicalDevices failed with error code %d\n",
                result);
        goto fail;
    }

    if (ndev == 0) {
        fprintf(stderr, "No physical devices were found\n");
        goto fail;
    }

    if (ndev > 8)
        ndev = 8;

    VkPhysicalDevice devices[8] = {0};

    result = vkEnumeratePhysicalDevices(vk->inst, &ndev,
                                        devices);

    if (result != VK_SUCCESS) {
        fprintf(stderr,
                "vkEnumeratePhysicalDevices failed with error code %d\n",
                result);
        goto fail;
    }

    /*
     * --------------------------------------------------------
     * Find physical device + queue family
     * --------------------------------------------------------
     */

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
            /*
             * We need a graphics-capable queue.
             */
            if (!(fams[f].queueFlags &
                  VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }

            /*
             * Check whether this queue family can present
             * to our actual Wayland VkSurfaceKHR.
             */
            VkBool32 present_supported = VK_FALSE;

            result = vkGetPhysicalDeviceSurfaceSupportKHR(
                devices[i], f, vk->surf,
                &present_supported);

            if (result != VK_SUCCESS) {
                fprintf(
                    stderr,
                    "vkGetPhysicalDeviceSurfaceSupportKHR failed with error code %d\n",
                    result);
                goto fail;
            }

            if (!present_supported)
                continue;

            picked = i;
            qfam = f;
            found = true;
            break;
        }
    }

    if (!found) {
        fprintf(stderr,
                "No physical device with a graphics + present queue was found\n");
        goto fail;
    }

    vk->phys = devices[picked];

    /*
     * --------------------------------------------------------
     * Logical device
     * --------------------------------------------------------
     */

    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_info = {0};
    queue_info.sType =
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = qfam;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    /*
     * The swapchain extension is required for presenting
     * Vulkan images to VkSurfaceKHR.
     */
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
        fprintf(stderr,
                "vkCreateDevice failed with error code %d\n",
                result);
        goto fail;
    }
    
    vkGetDeviceQueue(vk->dev, qfam, 0, &vk->queue); // Get the queue from the selected queue family

    return true;

fail:
    vk_finish(vk);
    return false;
}

/* cleanup/destroy resources IN ORDER(VERY IMPORTANT):
* vkDevice
* vkSurfaceKHR
* vkInstance
*/

void vk_finish(struct Vk* vk) {
    if (vk->dev != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vk->dev);
        vkDestroyDevice(vk->dev, NULL);
        vk->dev = VK_NULL_HANDLE;
        vk->queue = VK_NULL_HANDLE;
    }

    if (vk->surf != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vk->inst, vk->surf, NULL);
        vk->surf = VK_NULL_HANDLE;
    }

    if (vk->inst != VK_NULL_HANDLE) {
        vkDestroyInstance(vk->inst, NULL);
        vk->inst = VK_NULL_HANDLE;
    }

    vk->phys = VK_NULL_HANDLE;
}
