/* Minimal test-only wlroots virtual pointer client. The caller must explicitly
 * supply the isolated compositor's socket and global logical output extent.
 * Version 1 request signatures match wlr-virtual-pointer-unstable-v1. */
#include <wayland-client.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const struct wl_message pointer_requests[] = {
    {"motion", "uff", NULL}, {"motion_absolute", "uuuuu", NULL},
    {"button", "uuu", NULL}, {"axis", "uuf", NULL}, {"frame", "", NULL},
    {"axis_source", "u", NULL}, {"axis_stop", "uu", NULL},
    {"axis_discrete", "uufi", NULL}, {"destroy", "", NULL},
};
static const struct wl_interface pointer_interface = {
    "zwlr_virtual_pointer_v1", 1, 9, pointer_requests, 0, NULL
};
static const struct wl_interface *create_types[] = {&wl_seat_interface, &pointer_interface};
static const struct wl_message manager_requests[] = {
    {"create_virtual_pointer", "?on", create_types}, {"destroy", "", NULL},
};
static const struct wl_interface manager_interface = {
    "zwlr_virtual_pointer_manager_v1", 1, 2, manager_requests, 0, NULL
};
static struct wl_proxy *manager;
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)data; (void)version;
    if (strcmp(interface, manager_interface.name) == 0)
        manager = wl_registry_bind(registry, name, &manager_interface, 1);
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s SOCKET X Y WIDTH HEIGHT\n", argv[0]);
        return 2;
    }
    struct wl_display *display = wl_display_connect(argv[1]);
    if (!display) return 3;
    struct wl_registry *registry = wl_display_get_registry(display);
    const struct wl_registry_listener listener = {global, removed};
    wl_registry_add_listener(registry, &listener, NULL);
    if (wl_display_roundtrip(display) < 0 || !manager) return 4;
    struct wl_proxy *pointer = wl_proxy_marshal_flags(manager, 0, &pointer_interface, 1, 0, NULL, NULL);
    uint32_t now = (uint32_t)time(NULL) * 1000;
    wl_proxy_marshal_flags(pointer, 1, NULL, 1, 0, now, (uint32_t)atoi(argv[2]), (uint32_t)atoi(argv[3]),
                           (uint32_t)atoi(argv[4]), (uint32_t)atoi(argv[5]));
    wl_proxy_marshal_flags(pointer, 4, NULL, 1, 0);
    if (wl_display_roundtrip(display) < 0) return 5;
    wl_proxy_marshal_flags(pointer, 2, NULL, 1, 0, now + 1, 0x110u, 1u);
    wl_proxy_marshal_flags(pointer, 4, NULL, 1, 0);
    if (wl_display_roundtrip(display) < 0) return 6;
    wl_proxy_marshal_flags(pointer, 2, NULL, 1, 0, now + 2, 0x110u, 0u);
    wl_proxy_marshal_flags(pointer, 4, NULL, 1, 0);
    int result = wl_display_roundtrip(display) < 0;
    wl_proxy_marshal_flags(pointer, 8, NULL, 1, WL_MARSHAL_FLAG_DESTROY);
    wl_proxy_marshal_flags(manager, 1, NULL, 1, WL_MARSHAL_FLAG_DESTROY);
    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);
    return result;
}
