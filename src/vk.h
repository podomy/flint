#ifndef VK_H
#define VK_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

struct Wl;

// Flint doesn't negotiate image count beyond
// what fits here; 8 covers every driver seen in
// practice.
#define VK_MAX_SWAPCHAIN_IMAGES 8

struct Vk {
    VkInstance inst;
    VkPhysicalDevice phys;
    VkDevice dev;
    VkQueue queue;
    uint32_t qfam;

    // Wayland surface.
    VkSurfaceKHR surf;

    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;

    uint32_t image_count;
    VkImage images[VK_MAX_SWAPCHAIN_IMAGES];

    // One pool, one primary command buffer per
    // swapchain image. Buffers are re-recorded
    // every frame since all Flint does so far is
    // clear and present.
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_bufs[VK_MAX_SWAPCHAIN_IMAGES];

    // Flint keeps exactly one frame in flight
    // for now, so one set of sync objects is
    // enough.
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
};

// vk_init creates the instance, picks a device
// with Wayland present support, creates the
// logical device and queue, creates the Wayland
// surface, and sets up the command pool and
// frame sync objects. Call vk_finish after any
// return. False on failure.
bool vk_init(struct Vk* vk, struct Wl* wl);

// vk_swapchain_create (re)creates the swapchain
// and its per-image command buffers against
// wl's current window size. Destroys any
// previous swapchain first, so it is safe to
// call again after a resize.
bool vk_swapchain_create(struct Vk* vk, struct Wl* wl);

// vk_swapchain_destroy tears down the swapchain
// and its per-image command buffers, leaving
// the device, surface, and instance intact so
// the swapchain can be recreated.
void vk_swapchain_destroy(struct Vk* vk);

// vk_draw_frame acquires a swapchain image,
// clears it to a flat color, and presents it.
// Returns false on an unrecoverable error. Sets
// *resize_needed when the swapchain is stale
// (e.g. after a resize) and vk_swapchain_create
// should be called before the next frame.
bool vk_draw_frame(struct Vk* vk, bool* resize_needed);

// vk_finish destroys the swapchain, sync
// objects, command pool, surface, device, and
// instance, in that order.
void vk_finish(struct Vk* vk);

#endif