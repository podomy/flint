#include "wl.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------
 * xdg_wm_base listener
 * ------------------------------------------------------------
 */

static void wm_base_ping(void* data,
                         struct xdg_wm_base* wm_base,
                         uint32_t serial) {
    (void)data;

    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener =
    {
        .ping = wm_base_ping,
};

/* ------------------------------------------------------------
 * Registry listener
 * ------------------------------------------------------------
 */

static void registry_global(void* data,
                            struct wl_registry* registry,
                            uint32_t name,
                            const char* interface,
                            uint32_t version) {

    struct Wl* wl = data;

    if (strcmp(interface, wl_compositor_interface.name) ==
        0) {
        /*
         * Version 1 is sufficient for creating a basic
         * wl_surface. Limit the version to what we actually
         * use.
         */
        uint32_t bind_version = version < 1 ? version : 1;

        wl->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface,
            bind_version);

    } else if (strcmp(interface,
                      xdg_wm_base_interface.name) == 0) {
        /*
         * xdg_wm_base currently only needs version 1 for
         * the functionality used here.
         */
        uint32_t bind_version = version < 1 ? version : 1;

        wl->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface,
            bind_version);

        if (wl->wm_base != NULL) {
            xdg_wm_base_add_listener(wl->wm_base,
                                     &wm_base_listener, wl);
        }
    }
}

static void
registry_global_remove(void* data,
                       struct wl_registry* registry,
                       uint32_t name) {

    (void)data;
    (void)registry;
    (void)name;

    /*
     * Nothing to do for this basic implementation.
     *
     * A production client should track global removal if it
     * depends on globals remaining available.
     */
}

static const struct wl_registry_listener registry_listener =
    {
        .global = registry_global,
        .global_remove = registry_global_remove,
};

/* ------------------------------------------------------------
 * xdg_surface / xdg_toplevel listeners
 * ------------------------------------------------------------
 */

static void
xdg_surface_configure(void* data,
                      struct xdg_surface* xdg_surface,
                      uint32_t serial) {

    struct Wl* wl = data;

    /*
     * Every xdg_surface.configure must be acknowledged.
     */
    xdg_surface_ack_configure(xdg_surface, serial);

    wl->configured = true;
}

static const struct xdg_surface_listener
    xdg_surface_listener = {
        .configure = xdg_surface_configure,
};

static void
xdg_toplevel_configure(void* data,
                       struct xdg_toplevel* toplevel,
                       int32_t width, int32_t height,
                       struct wl_array* states) {

    struct Wl* wl = data;

    (void)toplevel;
    (void)states;

    /*
     * A compositor may send zero for width/height, meaning
     * the client should choose an appropriate size.
     */
    if (width > 0)
        wl->width = width;

    if (height > 0)
        wl->height = height;
}

static void
xdg_toplevel_close(void* data,
                   struct xdg_toplevel* toplevel) {

    struct Wl* wl = data;

    (void)toplevel;

    wl->should_close = true;
}

static const struct xdg_toplevel_listener
    xdg_toplevel_listener = {
        .configure = xdg_toplevel_configure,
        .close = xdg_toplevel_close,
};

/*
 *    Wayland initialization
 *
 * wl_init() does the following:
 *
 *     wl_display_connect()
 *          |
 *     wl_display
 *
 *     wl_display_get_registry()
 *          |
 *     wl_registry
 *
 *     registry roundtrip
 *          |
 *     wl_compositor
 *     xdg_wm_base
 *
 *     wl_compositor_create_surface()
 *          |
 *     wl_surface
 *
 *     xdg_wm_base_get_xdg_surface()
 *          |
 *     xdg_surface
 *
 *     xdg_surface_get_toplevel()
 *          |
 *     xdg_toplevel
 *
 * At the end of wl_init(), the program has a Wayland connection and a wl_surface that Vulkan can present to
 */

bool wl_init(struct Wl* wl) {
    memset(wl, 0, sizeof(*wl));

    wl->width = 1280;
    wl->height = 720;

    // Connect to compositor specified by WAYLAND_DISPLAY or default display if it is unset
    wl->display = wl_display_connect(NULL);
    if (wl->display == NULL) {
        fprintf(stderr, "wl_display_connect failed\n");
        goto fail;
    }

    // Obtain the global registry
    wl->registry = wl_display_get_registry(wl->display);
    if (wl->registry == NULL) {
        fprintf(stderr, "wl_display_get_registry failed\n");
        goto fail;
    }

    if (wl_registry_add_listener(
            wl->registry, &registry_listener, wl) < 0) {
        fprintf(stderr,
                "wl_registry_add_listener failed\n");
        goto fail;
    }

    // Roundtrip so the registry_global callbacks run and populate compositor/wm_base
    if (wl_display_roundtrip(wl->display) < 0) {
        fprintf(stderr,
                "initial Wayland roundtrip failed\n");
        goto fail;
    }

    if (wl->compositor == NULL) {
        fprintf(stderr,
                "Wayland compositor not available\n");
        goto fail;
    }

    if (wl->wm_base == NULL) {
        fprintf(stderr, "xdg_wm_base not available\n");
        goto fail;
    }

    
    // Create the actual wl_surface that Vulkan will eventually present into
    wl->surface =
        wl_compositor_create_surface(wl->compositor);
    if (wl->surface == NULL) {
        fprintf(stderr,
                "wl_compositor_create_surface failed\n");
        goto fail;
    }

    
    // Turn the wl_surface into an xdg_surface
    wl->xdg_surface = xdg_wm_base_get_xdg_surface(
        wl->wm_base, wl->surface);

    if (wl->xdg_surface == NULL) {
        fprintf(stderr,
                "xdg_wm_base_get_xdg_surface failed\n");
        goto fail;
    }

    if (xdg_surface_add_listener(wl->xdg_surface,
                                 &xdg_surface_listener,
                                 wl) < 0) {
        fprintf(stderr,
                "xdg_surface_add_listener failed\n");
        goto fail;
    }

    
    // Create the toplevel role
    wl->xdg_toplevel =
        xdg_surface_get_toplevel(wl->xdg_surface);

    if (wl->xdg_toplevel == NULL) {
        fprintf(stderr,
                "xdg_surface_get_toplevel failed\n");
        goto fail;
    }

    if (xdg_toplevel_add_listener(wl->xdg_toplevel,
                                  &xdg_toplevel_listener,
                                  wl) < 0) {
        fprintf(stderr,
                "xdg_toplevel_add_listener failed\n");
        goto fail;
    }

    xdg_toplevel_set_title(wl->xdg_toplevel, "Flint"); // windows metadata

    // We have not committed a buffer yet. The compositor will send the initial xdg_surface.configure after we commit the surface.
    // Vulkan will eventually do the first actual buffer commit.
    if (wl_surface_commit(wl->surface),
        wl_display_roundtrip(wl->display) < 0) {
        fprintf(stderr,
                "initial configure roundtrip failed\n");
        goto fail;
    }

    return true;

fail:
    wl_finish(wl);
    return false;
}

void wl_finish(struct Wl* wl) {
    if (wl == NULL)
        return;

    // Destroy objects in reverse dependency order. THE ORDER IN WHICH WE DESTROY OBJECTS MATTERS AND SHOULD NOT BE ALTERED
    if (wl->xdg_toplevel != NULL) {
        xdg_toplevel_destroy(wl->xdg_toplevel);
        wl->xdg_toplevel = NULL;
    }

    if (wl->xdg_surface != NULL) {
        xdg_surface_destroy(wl->xdg_surface);
        wl->xdg_surface = NULL;
    }

    if (wl->surface != NULL) {
        wl_surface_destroy(wl->surface);
        wl->surface = NULL;
    }

    if (wl->wm_base != NULL) {
        xdg_wm_base_destroy(wl->wm_base);
        wl->wm_base = NULL;
    }

    if (wl->compositor != NULL) {
        wl_compositor_destroy(wl->compositor);
        wl->compositor = NULL;
    }

    if (wl->registry != NULL) {
        wl_registry_destroy(wl->registry);
        wl->registry = NULL;
    }

    if (wl->display != NULL) {
        wl_display_disconnect(wl->display);
        wl->display = NULL;
    }

    wl->configured = false;
    wl->should_close = false;
}
