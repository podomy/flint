#ifndef WL_H
#define WL_H

#include <stdbool.h>
#include <wayland-client.h>

// Wl holds the needed wayland connections.
struct Wl {
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_compositor* compositor;
    struct xdg_wm_base* wm_base;
    struct wl_surface* surface;
    struct xdg_surface* xdg_surface;
    struct xdg_toplevel* xdg_toplevel;

    // Current window size. Starts at a default
    // picked by wl_init and is updated whenever
    // the compositor sends a different size via
    // xdg_toplevel's configure event.
    int32_t width;
    int32_t height;

    // Set once the compositor has sent the
    // first xdg_surface configure and we have
    // acked it. Nothing may be presented before
    // this is true.
    bool configured;

    // Set once the toplevel has been asked to
    // close, or the connection has died. The
    // caller's main loop should exit.
    bool closed;

    // Set whenever the compositor requests a
    // different size than the one currently in
    // use. The renderer should recreate its
    // swapchain and clear this once done.
    bool resized;
};

// wl_init connects, and binds the compositor,
// xdg wm base, and creates the surface.
// Returns true on success and false on failure.
bool wl_init(struct Wl* wl);

// wl_dispatch pumps one non-blocking round of
// Wayland events: replies to pings, applies
// toplevel configure/close, and flushes
// outgoing requests. Meant to be called once
// per iteration of a render loop. Returns false
// if the connection has died.
bool wl_dispatch(struct Wl* wl);

// wl_finish disconnects and destroys the surface.
void wl_finish(struct Wl* wl);

#endif
