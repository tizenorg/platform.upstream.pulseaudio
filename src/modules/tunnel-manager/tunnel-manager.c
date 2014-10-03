/***
  This file is part of PulseAudio.

  Copyright 2014 Intel Corporation

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "tunnel-manager.h"

#include <modules/tunnel-manager/tunnel-manager-config.h>

#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>

#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>
#include <pulsecore/parseaddr.h>
#include <pulsecore/shared.h>

#define MAX_DEVICES_PER_SERVER 50

static void remote_server_new(pa_tunnel_manager *manager, pa_tunnel_manager_remote_server_config *config);
static void remote_server_set_up_connection(pa_tunnel_manager_remote_server *server);
static void remote_server_free(pa_tunnel_manager_remote_server *server);
static void remote_server_set_failed(pa_tunnel_manager_remote_server *server, bool failed);

static void remote_device_new(pa_tunnel_manager_remote_server *server, pa_device_type_t type, const void *info);
static void remote_device_free(pa_tunnel_manager_remote_device *device);
static void remote_device_update(pa_tunnel_manager_remote_device *device);

struct device_stub {
    pa_tunnel_manager_remote_server *server;
    pa_device_type_t type;
    uint32_t index;
    bool unlinked;

    pa_operation *get_info_operation;

    /* These are a workaround for the problem that the introspection API's info
     * callbacks are called multiple times, which means that if the userdata
     * needs to be freed during the callbacks, the freeing needs to be
     * postponed until the last call. */
    bool can_free;
    bool dead;
};

static void device_stub_new(pa_tunnel_manager_remote_server *server, pa_device_type_t type, uint32_t idx);
static void device_stub_unlink(struct device_stub *stub);
static void device_stub_free(struct device_stub *stub);

static pa_tunnel_manager *tunnel_manager_new(pa_core *core) {
    pa_tunnel_manager *manager;
    pa_tunnel_manager_config *manager_config;
    pa_tunnel_manager_remote_server_config *server_config;
    void *state;

    pa_assert(core);

    manager = pa_xnew0(pa_tunnel_manager, 1);
    manager->core = core;
    manager->remote_servers = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    manager->refcnt = 1;

    manager_config = pa_tunnel_manager_config_new();

    PA_HASHMAP_FOREACH(server_config, manager_config->remote_servers, state)
        remote_server_new(manager, server_config);

    pa_tunnel_manager_config_free(manager_config);

    pa_shared_set(core, "tunnel_manager", manager);

    return manager;
}

static void tunnel_manager_free(pa_tunnel_manager *manager) {
    pa_assert(manager);
    pa_assert(manager->refcnt == 0);

    pa_shared_remove(manager->core, "tunnel_manager");

    if (manager->remote_servers) {
        pa_tunnel_manager_remote_server *server;

        while ((server = pa_hashmap_first(manager->remote_servers)))
            remote_server_free(server);

        pa_hashmap_free(manager->remote_servers);
    }

    pa_xfree(manager);
}

pa_tunnel_manager *pa_tunnel_manager_get(pa_core *core, bool ref) {
    pa_tunnel_manager *manager;

    pa_assert(core);

    manager = pa_shared_get(core, "tunnel_manager");
    if (manager) {
        if (ref)
            manager->refcnt++;

        return manager;
    }

    if (ref)
        return tunnel_manager_new(core);

    return NULL;
}

void pa_tunnel_manager_unref(pa_tunnel_manager *manager) {
    pa_assert(manager);
    pa_assert(manager->refcnt > 0);

    manager->refcnt--;

    if (manager->refcnt == 0)
        tunnel_manager_free(manager);
}

static void remote_server_new(pa_tunnel_manager *manager, pa_tunnel_manager_remote_server_config *config) {
    int r;
    pa_parsed_address parsed_address;
    pa_tunnel_manager_remote_server *server = NULL;

    pa_assert(manager);
    pa_assert(config);

    if (!config->address) {
        pa_log("No address configured for remote server %s.", config->name);
        return;
    }

    r = pa_parse_address(config->address->value, &parsed_address);
    if (r < 0) {
        pa_log("[%s:%u] Invalid address: \"%s\"", config->address->filename, config->address->lineno, config->address->value);
        return;
    }

    pa_xfree(parsed_address.path_or_host);

    server = pa_xnew0(pa_tunnel_manager_remote_server, 1);
    server->manager = manager;
    server->name = pa_xstrdup(config->name);
    server->address = pa_xstrdup(config->address->value);
    server->devices = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    server->device_stubs = pa_hashmap_new(NULL, NULL);

    pa_assert_se(pa_hashmap_put(manager->remote_servers, server->name, server) >= 0);

    pa_log_debug("Created remote server %s.", server->name);
    pa_log_debug("    Address: %s", server->address);
    pa_log_debug("    Failed: %s", pa_boolean_to_string(server->failed));

    remote_server_set_up_connection(server);
}

static const char *device_type_to_string(pa_device_type_t type) {
    switch (type) {
        case PA_DEVICE_TYPE_SINK:   return "sink";
        case PA_DEVICE_TYPE_SOURCE: return "source";
    }

    pa_assert_not_reached();
}

static void subscribe_cb(pa_context *context, pa_subscription_event_type_t event_type, uint32_t idx, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;
    pa_device_type_t device_type;
    pa_tunnel_manager_remote_device *device;
    void *state;

    pa_assert(context);
    pa_assert(server);

    if ((event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK)
        device_type = PA_DEVICE_TYPE_SINK;
    else if ((event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE)
        device_type = PA_DEVICE_TYPE_SOURCE;
    else {
        pa_log("[%s] Unexpected event facility: %u", server->name,
               (unsigned) (event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK));
        remote_server_set_failed(server, true);
        return;
    }

    if (idx == PA_INVALID_INDEX) {
        pa_log("[%s] Invalid %s index.", server->name, device_type_to_string(device_type));
        remote_server_set_failed(server, true);
        return;
    }

    if ((event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_NEW) {
        if ((device_type == PA_DEVICE_TYPE_SINK && server->list_sinks_operation)
                || (device_type == PA_DEVICE_TYPE_SOURCE && server->list_sources_operation))
            return;

        device_stub_new(server, device_type, idx);

        return;
    }

    if ((event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
        struct device_stub *stub;

        PA_HASHMAP_FOREACH(device, server->devices, state) {
            if (device->type == device_type && device->index == idx) {
                remote_device_free(device);
                return;
            }
        }

        PA_HASHMAP_FOREACH(stub, server->device_stubs, state) {
            if (stub->type == device_type && stub->index == idx) {
                device_stub_free(stub);
                return;
            }
        }

        return;
    }

    if ((event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_CHANGE) {
        PA_HASHMAP_FOREACH(device, server->devices, state) {
            if (device->type == device_type && device->index == idx) {
                remote_device_update(device);
                return;
            }
        }

        return;
    }
}

static void subscribe_success_cb(pa_context *context, int success, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;

    pa_assert(context);
    pa_assert(server);

    if (!success) {
        pa_log("[%s] Subscribing to device events failed: %s", server->name, pa_strerror(pa_context_errno(context)));
        remote_server_set_failed(server, true);
    }
}

static void get_sink_info_list_cb(pa_context *context, const pa_sink_info *info, int is_last, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;

    pa_assert(context);
    pa_assert(server);

    if (server->list_sinks_operation) {
        pa_operation_unref(server->list_sinks_operation);
        server->list_sinks_operation = NULL;
    }

    if (is_last < 0) {
        pa_log("[%s] Listing sinks failed: %s", server->name, pa_strerror(pa_context_errno(context)));
        remote_server_set_failed(server, true);
        return;
    }

    if (is_last)
        return;

    remote_device_new(server, PA_DEVICE_TYPE_SINK, info);
}

static void get_source_info_list_cb(pa_context *context, const pa_source_info *info, int is_last, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;

    pa_assert(context);
    pa_assert(server);

    if (server->list_sources_operation) {
        pa_operation_unref(server->list_sources_operation);
        server->list_sources_operation = NULL;
    }

    if (is_last < 0) {
        pa_log("[%s] Listing sources failed: %s", server->name, pa_strerror(pa_context_errno(context)));
        remote_server_set_failed(server, true);
        return;
    }

    if (is_last)
        return;

    remote_device_new(server, PA_DEVICE_TYPE_SOURCE, info);
}

static void context_state_cb(pa_context *context, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;
    pa_context_state_t state;

    pa_assert(context);
    pa_assert(server);
    pa_assert(context == server->context);

    state = pa_context_get_state(context);

    switch (state) {
        case PA_CONTEXT_READY: {
            pa_operation *operation;

            pa_context_set_subscribe_callback(context, subscribe_cb, server);
            operation = pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE,
                                             subscribe_success_cb, server);
            if (operation)
                pa_operation_unref(operation);
            else {
                pa_log("[%s] pa_context_subscribe() failed: %s", server->name, pa_strerror(pa_context_errno(context)));
                remote_server_set_failed(server, true);
                return;
            }

            pa_assert(!server->list_sinks_operation);
            pa_assert(!server->list_sources_operation);

            server->list_sinks_operation = pa_context_get_sink_info_list(server->context, get_sink_info_list_cb, server);
            if (!server->list_sinks_operation) {
                pa_log("[%s] pa_context_get_sink_info_list() failed: %s", server->name,
                       pa_strerror(pa_context_errno(context)));
                remote_server_set_failed(server, true);
                return;
            }

            server->list_sources_operation = pa_context_get_source_info_list(server->context, get_source_info_list_cb, server);
            if (!server->list_sources_operation) {
                pa_log("[%s] pa_context_get_source_info_list() failed: %s", server->name,
                       pa_strerror(pa_context_errno(context)));
                remote_server_set_failed(server, true);
                return;
            }

            return;
        }

        case PA_CONTEXT_FAILED:
            pa_log("[%s] Context failed: %s", server->name, pa_strerror(pa_context_errno(context)));
            remote_server_set_failed(server, true);
            return;

        default:
            return;
    }
}

static void remote_server_set_up_connection(pa_tunnel_manager_remote_server *server) {
    pa_assert(server);
    pa_assert(!server->context);

    server->context = pa_context_new(server->manager->core->mainloop, "PulseAudio");
    if (server->context) {
        int r;

        r = pa_context_connect(server->context, server->address, PA_CONTEXT_NOFLAGS, NULL);
        if (r >= 0)
            pa_context_set_state_callback(server->context, context_state_cb, server);
        else {
            pa_log("[%s] pa_context_connect() failed: %s", server->name, pa_strerror(pa_context_errno(server->context)));
            remote_server_set_failed(server, true);
        }
    } else {
        pa_log("[%s] pa_context_new() failed.", server->name);
        remote_server_set_failed(server, true);
    }
}

static void remote_server_tear_down_connection(pa_tunnel_manager_remote_server *server) {
    pa_assert(server);

    if (server->device_stubs) {
        struct device_stub *stub;

        while ((stub = pa_hashmap_first(server->device_stubs)))
            device_stub_free(stub);
    }

    if (server->devices) {
        pa_tunnel_manager_remote_device *device;

        while ((device = pa_hashmap_first(server->devices)))
            remote_device_free(device);
    }

    if (server->list_sources_operation) {
        pa_operation_cancel(server->list_sources_operation);
        pa_operation_unref(server->list_sources_operation);
        server->list_sources_operation = NULL;
    }

    if (server->list_sinks_operation) {
        pa_operation_cancel(server->list_sinks_operation);
        pa_operation_unref(server->list_sinks_operation);
        server->list_sinks_operation = NULL;
    }

    if (server->context) {
        pa_context_disconnect(server->context);
        pa_context_unref(server->context);
        server->context = NULL;
    }
}

static void remote_server_free(pa_tunnel_manager_remote_server *server) {
    pa_assert(server);

    pa_log_debug("Freeing remote server %s.", server->name);

    pa_hashmap_remove(server->manager->remote_servers, server->name);

    remote_server_tear_down_connection(server);

    if (server->device_stubs) {
        pa_assert(pa_hashmap_isempty(server->device_stubs));
        pa_hashmap_free(server->device_stubs);
    }

    if (server->devices) {
        pa_assert(pa_hashmap_isempty(server->devices));
        pa_hashmap_free(server->devices);
    }

    pa_xfree(server->address);
    pa_xfree(server->name);
    pa_xfree(server);
}

static void remote_server_set_failed(pa_tunnel_manager_remote_server *server, bool failed) {
    pa_assert(server);

    if (failed == server->failed)
        return;

    server->failed = failed;

    pa_log_debug("[%s] Failed changed from %s to %s.", server->name, pa_boolean_to_string(!failed),
                 pa_boolean_to_string(failed));

    if (failed)
        remote_server_tear_down_connection(server);
}

static void remote_device_new(pa_tunnel_manager_remote_server *server, pa_device_type_t type, const void *info) {
    const char *name = NULL;
    uint32_t idx = PA_INVALID_INDEX;
    pa_proplist *proplist = NULL;
    const pa_sample_spec *sample_spec = NULL;
    const pa_channel_map *channel_map = NULL;
    bool is_monitor = false;
    pa_tunnel_manager_remote_device *device;
    unsigned i;
    char sample_spec_str[PA_SAMPLE_SPEC_SNPRINT_MAX];
    char channel_map_str[PA_CHANNEL_MAP_SNPRINT_MAX];

    pa_assert(server);
    pa_assert(info);

    switch (type) {
        case PA_DEVICE_TYPE_SINK: {
            const pa_sink_info *sink_info = info;

            name = sink_info->name;
            idx = sink_info->index;
            proplist = sink_info->proplist;
            sample_spec = &sink_info->sample_spec;
            channel_map = &sink_info->channel_map;
            break;
        }

        case PA_DEVICE_TYPE_SOURCE: {
            const pa_source_info *source_info = info;

            name = source_info->name;
            idx = source_info->index;
            proplist = source_info->proplist;
            sample_spec = &source_info->sample_spec;
            channel_map = &source_info->channel_map;
            is_monitor = !!source_info->monitor_of_sink_name;

            break;
        }
    }

    /* TODO: This check should be done in libpulse. */
    if (!name || !pa_namereg_is_valid_name(name)) {
        pa_log("[%s] Invalid remote device name: %s", server->name, pa_strnull(name));
        remote_server_set_failed(server, true);
        return;
    }

    if (pa_hashmap_get(server->devices, name)) {
        pa_log("[%s] Duplicate remote device name: %s", server->name, name);
        remote_server_set_failed(server, true);
        return;
    }

    if (pa_hashmap_size(server->devices) + pa_hashmap_size(server->device_stubs) >= MAX_DEVICES_PER_SERVER) {
        pa_log("[%s] Maximum number of devices reached, can't create a new remote device.", server->name);
        remote_server_set_failed(server, true);
        return;
    }

    /* TODO: This check should be done in libpulse. */
    if (!pa_sample_spec_valid(sample_spec)) {
        pa_log("[%s %s] Invalid sample spec.", server->name, name);
        remote_server_set_failed(server, true);
        return;
    }

    /* TODO: This check should be done in libpulse. */
    if (!pa_channel_map_valid(channel_map)) {
        pa_log("[%s %s] Invalid channel map.", server->name, name);
        remote_server_set_failed(server, true);
        return;
    }

    device = pa_xnew0(pa_tunnel_manager_remote_device, 1);
    device->server = server;
    device->type = type;
    device->name = pa_xstrdup(name);
    device->index = idx;
    device->proplist = pa_proplist_copy(proplist);
    device->sample_spec = *sample_spec;
    device->channel_map = *channel_map;
    device->is_monitor = is_monitor;

    for (i = 0; i < PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_MAX; i++)
        pa_hook_init(&device->hooks[i], device);

    device->can_free = true;

    pa_hashmap_put(server->devices, device->name, device);

    pa_log_debug("[%s] Created remote device %s.", server->name, device->name);
    pa_log_debug("        Type: %s", device_type_to_string(type));
    pa_log_debug("        Index: %u", idx);
    pa_log_debug("        Sample spec: %s", pa_sample_spec_snprint(sample_spec_str, sizeof(sample_spec_str), sample_spec));
    pa_log_debug("        Channel map: %s", pa_channel_map_snprint(channel_map_str, sizeof(channel_map_str), channel_map));
    pa_log_debug("        Is monitor: %s", pa_boolean_to_string(device->is_monitor));
}

static void remote_device_free(pa_tunnel_manager_remote_device *device) {
    unsigned i;

    pa_assert(device);

    pa_log_debug("[%s] Freeing remote device %s.", device->server->name, device->name);

    pa_hashmap_remove(device->server->devices, device->name);
    pa_hook_fire(&device->hooks[PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_UNLINKED], NULL);

    if (device->get_info_operation) {
        pa_operation_cancel(device->get_info_operation);
        pa_operation_unref(device->get_info_operation);
    }

    for (i = 0; i < PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_MAX; i++)
        pa_hook_done(&device->hooks[i]);

    if (device->proplist)
        pa_proplist_free(device->proplist);

    pa_xfree(device->name);
    pa_xfree(device);
}

static void remote_device_set_proplist(pa_tunnel_manager_remote_device *device, pa_proplist *proplist) {
    pa_assert(device);
    pa_assert(proplist);

    if (pa_proplist_equal(proplist, device->proplist))
        return;

    pa_proplist_update(device->proplist, PA_UPDATE_SET, proplist);

    pa_log_debug("[%s %s] Proplist changed.", device->server->name, device->name);

    pa_hook_fire(&device->hooks[PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_PROPLIST_CHANGED], NULL);
}

static void remote_device_get_info_cb(pa_context *context, const void *info, int is_last, void *userdata) {
    pa_tunnel_manager_remote_device *device = userdata;
    pa_proplist *proplist = NULL;

    pa_assert(context);
    pa_assert(device);

    if (device->get_info_operation) {
        pa_operation_unref(device->get_info_operation);
        device->get_info_operation = NULL;
    }

    if (is_last < 0) {
        pa_log_debug("[%s %s] Getting info failed: %s", device->server->name, device->name,
                     pa_strerror(pa_context_errno(context)));
        return;
    }

    if (is_last) {
        device->can_free = true;

        if (device->dead)
            remote_device_free(device);

        return;
    }

    device->can_free = false;

    if (device->dead)
        return;

    switch (device->type) {
        case PA_DEVICE_TYPE_SINK:
            proplist = ((const pa_sink_info *) info)->proplist;
            break;

        case PA_DEVICE_TYPE_SOURCE:
            proplist = ((const pa_source_info *) info)->proplist;
            break;
    }

    remote_device_set_proplist(device, proplist);
}

static void remote_device_update(pa_tunnel_manager_remote_device *device) {
    pa_assert(device);

    if (device->get_info_operation)
        return;

    switch (device->type) {
        case PA_DEVICE_TYPE_SINK:
            device->get_info_operation = pa_context_get_sink_info_by_name(device->server->context, device->name,
                                                                          (pa_sink_info_cb_t) remote_device_get_info_cb,
                                                                          device);
            break;

        case PA_DEVICE_TYPE_SOURCE:
            device->get_info_operation = pa_context_get_source_info_by_name(device->server->context, device->name,
                                                                            (pa_source_info_cb_t) remote_device_get_info_cb,
                                                                            device);
            break;
    }

    if (!device->get_info_operation) {
        pa_log("[%s %s] pa_context_get_%s_info_by_name() failed: %s", device->server->name, device->name,
               device_type_to_string(device->type), pa_strerror(pa_context_errno(device->server->context)));
        remote_server_set_failed(device->server, true);
    }
}

static void device_stub_get_info_cb(pa_context *context, const void *info, int is_last, void *userdata) {
    struct device_stub *stub = userdata;
    uint32_t idx = PA_INVALID_INDEX;

    pa_assert(context);
    pa_assert(stub);

    if (stub->get_info_operation) {
        pa_operation_unref(stub->get_info_operation);
        stub->get_info_operation = NULL;
    }

    if (is_last < 0) {
        pa_log_debug("[%s] Getting info for %s %u failed: %s", stub->server->name, device_type_to_string(stub->type),
                     stub->index, pa_strerror(pa_context_errno(context)));
        device_stub_free(stub);
        return;
    }

    if (is_last) {
        stub->can_free = true;

        /* TODO: libpulse should ensure that the get info operation doesn't
         * return an empty result. Then this check wouldn't be needed. */
        if (!stub->unlinked) {
            pa_log("[%s] No info received for %s %u.", stub->server->name, device_type_to_string(stub->type), stub->index);
            remote_server_set_failed(stub->server, true);
            return;
        }

        device_stub_free(stub);
        return;
    }

    /* This callback will still be called at least once, so we need to keep the
     * stub alive. */
    stub->can_free = false;

    if (stub->dead)
        return;

    /* TODO: libpulse should ensure that the get info operation doesn't return
     * more than one result. Then this check wouldn't be needed. */
    if (stub->unlinked) {
        pa_log("[%s] Multiple info structs received for %s %u.", stub->server->name, device_type_to_string(stub->type),
               stub->index);
        remote_server_set_failed(stub->server, true);
        return;
    }

    /* remote_device_new() checks whether the maximum device limit has been
     * reached, and device stubs count towards that limit. This stub shouldn't
     * any more count towards the limit, so let's remove the stub from the
     * server's accounting. */
    device_stub_unlink(stub);

    switch (stub->type) {
        case PA_DEVICE_TYPE_SINK:
            idx = ((const pa_sink_info *) info)->index;
            break;

        case PA_DEVICE_TYPE_SOURCE:
            idx = ((const pa_source_info *) info)->index;
            break;
    }

    if (idx != stub->index) {
        pa_log("[%s] Index mismatch for %s %u.", stub->server->name, device_type_to_string(stub->type), stub->index);
        remote_server_set_failed(stub->server, true);
        return;
    }

    if (stub->type == PA_DEVICE_TYPE_SOURCE && ((const pa_source_info *) info)->monitor_of_sink != PA_INVALID_INDEX)
        return;

    remote_device_new(stub->server, stub->type, info);
}

static void device_stub_new(pa_tunnel_manager_remote_server *server, pa_device_type_t type, uint32_t idx) {
    pa_tunnel_manager_remote_device *device;
    void *state;
    struct device_stub *stub;

    pa_assert(server);

    PA_HASHMAP_FOREACH(device, server->devices, state) {
        if (device->type == type && device->index == idx) {
            pa_log("[%s] Duplicate %s index %u.", server->name, device_type_to_string(type), idx);
            remote_server_set_failed(server, true);
            return;
        }
    }

    PA_HASHMAP_FOREACH(stub, server->device_stubs, state) {
        if (stub->type == type && stub->index == idx) {
            pa_log("[%s] Duplicate %s index %u.", server->name, device_type_to_string(type), idx);
            remote_server_set_failed(server, true);
            return;
        }
    }

    if (pa_hashmap_size(server->devices) + pa_hashmap_size(server->device_stubs) >= MAX_DEVICES_PER_SERVER) {
        pa_log("[%s] Maximum number of devices reached, can't create a new remote device.", server->name);
        remote_server_set_failed(server, true);
        return;
    }

    stub = pa_xnew0(struct device_stub, 1);
    stub->server = server;
    stub->type = type;
    stub->index = idx;
    stub->can_free = true;

    pa_hashmap_put(server->device_stubs, stub, stub);

    switch (type) {
        case PA_DEVICE_TYPE_SINK:
            stub->get_info_operation = pa_context_get_sink_info_by_index(server->context, idx,
                                                                         (pa_sink_info_cb_t) device_stub_get_info_cb,
                                                                         stub);
            break;

        case PA_DEVICE_TYPE_SOURCE:
            stub->get_info_operation = pa_context_get_source_info_by_index(server->context, idx,
                                                                           (pa_source_info_cb_t) device_stub_get_info_cb,
                                                                           stub);
            break;
    }

    if (!stub->get_info_operation) {
        pa_log("[%s] pa_context_get_%s_info_by_index() failed: %s", server->name, device_type_to_string(type),
               pa_strerror(pa_context_errno(server->context)));
        remote_server_set_failed(server, true);
        return;
    }
}

static void device_stub_unlink(struct device_stub *stub) {
    pa_assert(stub);

    if (stub->unlinked)
        return;

    stub->unlinked = true;

    pa_hashmap_remove(stub->server->device_stubs, stub);

    if (stub->get_info_operation) {
        pa_operation_cancel(stub->get_info_operation);
        pa_operation_unref(stub->get_info_operation);
        stub->get_info_operation = NULL;
    }
}

static void device_stub_free(struct device_stub *stub) {
    pa_assert(stub);

    device_stub_unlink(stub);

    if (stub->can_free)
        pa_xfree(stub);
    else
        stub->dead = true;
}
