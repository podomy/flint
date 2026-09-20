#include "vk.h"
#include "wl.h"

#include <stdio.h>

/*
 * The initialization order is:
 *
 *     Wayland connection
 *          |
 *     Wayland globals
 *          |
 *     wl_surface
 *          |
 *     Vulkan instance
 *          |
 *     VkSurfaceKHR
 *          |
 *     physical device
 *          |
 *     graphics/present queue family
 *          |
 *     logical device
 *          |
 *        queue
 *
 * Each layer depends on objects created by the layer before it
 */

// terminology (can be found in the book chapter 5)
// presentation is the process of showing vulkan's work to the user. Presentation is generally handled by the OS, but we're using vulkan's Wayland extension because we're on linux
// presentation surface (or just surface) is the object that presents rendered graphics data. Wayland in our case, if we were to use the windows OS our surface would be Windows API

int main(void) {
    struct Wl wl;
    struct Vk vk;
    
    if (!wl_init(&wl)) {
        fprintf(stderr, "Wayland initialization failed\n");
        return 1;
    }

    if (!vk_init(&vk, &wl)) {
        fprintf(stderr, "Vulkan initialization failed\n");
        wl_finish(&wl);

        return 1;
    }

    /*
     * The rendering loop will eventually go here
     *
     * For example:
     *
     *     while (!wl.should_close) {
     *         ...
     *         render();
     *         ...
     *     }
     *
     * The swapchain, command buffers, synchronization objects, and actual frame presentation are not implemented yet
     *
     * So for now, simply perform one Wayland dispatch/roundtrip to demonstrate that the connection is alive
     */

    if (wl_display_roundtrip(wl.display) < 0) {
        fprintf(stderr, "Wayland roundtrip failed\n");
    }

    // We implement finish() functions as a safety net to avoid leaks and unexpected behaviour
    vk_finish(&vk); // Vulkan must be destroyed before Wayland because the VkSurfaceKHR was created using the Wayland objects
    wl_finish(&wl); // Destroy Wayland objects and disconnect the display
    // Destruction happens in the reverse dependency order because you can't destroy the foundation of a 100 story building and expect the whole thing to not fall down
    
    return 0;
}