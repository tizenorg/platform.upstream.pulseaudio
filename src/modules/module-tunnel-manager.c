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

#include "module-tunnel-manager-symdef.h"

#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>

#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/namereg.h>

PA_MODULE_AUTHOR("Tanu Kaskinen");
PA_MODULE_DESCRIPTION(_("Manage tunnels to other PulseAudio servers"));
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

#define MAX_PROXY_DEVICES 50
#define SYSTEM_INSTANCE_ADDRESS "unix:" PA_SYSTEM_RUNTIME_PATH PA_PATH_SEP PA_NATIVE_DEFAULT_UNIX_SOCKET
#define TUNNEL_SINK_MODULE_NAME "module-tunnel-sink-new"
#define TUNNEL_SOURCE_MODULE_NAME "module-tunnel-source-new"

struct proxy_device {
    struct userdata *userdata;

    pa_device_type_t type;
    uint32_t remote_index;

    pa_operation *get_info_operation;
    pa_module *tunnel_module;
};

static void proxy_device_free(struct proxy_device *device);
static void proxy_device_create_tunnel(struct proxy_device *device, const void *info);

struct userdata {
    pa_core *core;
    pa_module *module;
    char *remote_address;
    pa_context *context;
    unsigned n_devices;
    pa_hashmap *sinks; /* remote index -> struct proxy_device */
    pa_hashmap *sources; /* remote index -> struct proxy_device */
    pa_operation *list_sinks_operation;
    pa_operation *list_sources_operation;
    pa_hook_slot *module_unload_slot;
};

static const char *device_type_to_string(pa_device_type_t type) {
    switch (type) {
        case PA_DEVICE_TYPE_SINK:   return "sink";
        case PA_DEVICE_TYPE_SOURCE: return "source";
    }

    pa_assert_not_reached();
}

static int proxy_device_new(struct userdata *u, pa_device_type_t type, uint32_t idx, struct proxy_device **_r) {
    struct proxy_device *device = NULL;
    int r = 0;

    pa_assert(u);
    pa_assert(_r);

    /* TODO: This check should be done in libpulse. */
    if (idx == PA_INVALID_INDEX) {
        pa_log_debug("[%s %s (invalid)] Invalid index. Can't create a proxy device.", u->remote_address,
                     device_type_to_string(type));
        return -PA_ERR_INVALID;
    }

    if (u->n_devices >= MAX_PROXY_DEVICES) {
        pa_log_debug("[%s %s %u] Maximum number of proxy devices reached, can't create a new one.", u->remote_address,
                     device_type_to_string(type), idx);
        return -PA_ERR_TOOLARGE;
    }

    device = pa_xnew0(struct proxy_device, 1);
    device->userdata = u;
    device->type = type;
    device->remote_index = idx;

    switch (type) {
        case PA_DEVICE_TYPE_SINK:
            r = pa_hashmap_put(u->sinks, PA_UINT32_TO_PTR(idx), device);
            break;

        case PA_DEVICE_TYPE_SOURCE:
            r = pa_hashmap_put(u->sources, PA_UINT32_TO_PTR(idx), device);
            break;
    }

    if (r < 0) {
        pa_log_debug("[%s %s %u] Device already exists.", u->remote_address, device_type_to_string(type), idx);
        r = -PA_ERR_EXIST;
        goto fail;
    }

    u->n_devices++;

    *_r = device;
    return 0;

fail:
    if (device)
        proxy_device_free(device);

    return r;
}

static void proxy_device_free(struct proxy_device *device) {
    pa_assert(device);

    if (device->tunnel_module)
        pa_module_unload(device->tunnel_module->core, device->tunnel_module, true);

    if (device->get_info_operation) {
        pa_operation_cancel(device->get_info_operation);
        pa_operation_unref(device->get_info_operation);
    }

    pa_xfree(device);
}

static void get_info_cb(pa_context *context, void *info, int is_last, void *userdata) {
    struct proxy_device *device = userdata;

    pa_assert(context);
    pa_assert(device);

    if (device->get_info_operation) {
        pa_operation_unref(device->get_info_operation);
        device->get_info_operation = NULL;
    }

    if (is_last < 0) {
        pa_log_debug("[%s %s %u] Getting info failed: %s", device->userdata->remote_address,
                     device_type_to_string(device->type), device->remote_index,
                     pa_strerror(pa_context_errno(device->userdata->context)));
        return;
    }

    if (is_last > 0)
        return;

    pa_assert(info);

    if (device->type == PA_DEVICE_TYPE_SOURCE) {
        const pa_source_info *source_info = info;

        if (source_info->monitor_of_sink != PA_INVALID_INDEX) {
            pa_hashmap_remove_and_free(device->userdata->sources, PA_UINT32_TO_PTR(device->remote_index));
            return;
        }
    }

    proxy_device_create_tunnel(device, info);
}

static void proxy_device_get_info(struct proxy_device *device) {
    pa_assert(device);

    if (device->get_info_operation)
        return;

    switch (device->type) {
        case PA_DEVICE_TYPE_SINK:
            device->get_info_operation = pa_context_get_sink_info_by_index(device->userdata->context, device->remote_index,
                                                                           (pa_sink_info_cb_t) get_info_cb, device);
            break;

        case PA_DEVICE_TYPE_SOURCE:
            device->get_info_operation = pa_context_get_source_info_by_index(device->userdata->context, device->remote_index,
                                                                             (pa_source_info_cb_t) get_info_cb, device);
            break;
    }

    if (!device->get_info_operation)
        pa_log_debug("[%s %s %u] pa_context_get_%s_info_by_index() failed: %s", device->userdata->remote_address,
                     device_type_to_string(device->type), device->remote_index, device_type_to_string(device->type),
                     pa_strerror(pa_context_errno(device->userdata->context)));
}

static void proxy_device_create_tunnel(struct proxy_device *device, const void *info) {
    const char *remote_name = NULL;
    const pa_sample_spec *sample_spec = NULL;
    const pa_channel_map *channel_map = NULL;
    const char *module_name = NULL;
    const char *type_str;
    char *args;
    char map_buf[PA_CHANNEL_MAP_SNPRINT_MAX];

    pa_assert(device);
    pa_assert(!device->tunnel_module);
    pa_assert(info);

    switch (device->type) {
        case PA_DEVICE_TYPE_SINK: {
            const pa_sink_info *sink_info = info;

            remote_name = sink_info->name;
            sample_spec = &sink_info->sample_spec;
            channel_map = &sink_info->channel_map;
            module_name = TUNNEL_SINK_MODULE_NAME;
            break;
        }

        case PA_DEVICE_TYPE_SOURCE: {
            const pa_source_info *source_info = info;

            remote_name = source_info->name;
            sample_spec = &source_info->sample_spec;
            channel_map = &source_info->channel_map;
            module_name = TUNNEL_SOURCE_MODULE_NAME;
            break;
        }
    }

    type_str = device_type_to_string(device->type);

    /* TODO: This check should be in libpulse. */
    if (!remote_name || !pa_namereg_is_valid_name(remote_name)) {
        pa_log_debug("[%s %s %u] Invalid device name.", device->userdata->remote_address, type_str, device->remote_index);
        return;
    }

    /* TODO: This check should be in libpulse. */
    if (!pa_sample_spec_valid(sample_spec)) {
        pa_log_debug("[%s %s %u] Invalid sample spec.", device->userdata->remote_address, type_str, device->remote_index);
        return;
    }

    /* TODO: This check should be in libpulse. */
    if (!pa_channel_map_valid(channel_map)) {
        pa_log_debug("[%s %s %u] Invalid channel map.", device->userdata->remote_address, type_str, device->remote_index);
        return;
    }

    args = pa_sprintf_malloc("server=%s "
                             "%s=%s "
                             "%s_name=system.%s "
                             "format=%s "
                             "channels=%u "
                             "channel_map=%s "
                             "rate=%u",
                             device->userdata->remote_address,
                             type_str, remote_name,
                             type_str, remote_name,
                             pa_sample_format_to_string(sample_spec->format),
                             sample_spec->channels,
                             pa_channel_map_snprint(map_buf, sizeof(map_buf), channel_map),
                             sample_spec->rate);
    device->tunnel_module = pa_module_load(device->userdata->core, module_name, args);
    pa_xfree(args);
}

static void subscribe_cb(pa_context *context, pa_subscription_event_type_t event_type, uint32_t idx, void *userdata) {
    struct userdata *u = userdata;
    pa_device_type_t device_type;

    pa_assert(context);
    pa_assert(u);

    if ((event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK)
        device_type = PA_DEVICE_TYPE_SINK;
    else if ((event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE)
        device_type = PA_DEVICE_TYPE_SOURCE;
    else {
        pa_log_debug("[%s] Unexpected event facility: %u", u->remote_address,
                     (unsigned) (event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK));
        return;
    }

    if ((event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_NEW) {
        int r;
        struct proxy_device *device;

        if ((device_type == PA_DEVICE_TYPE_SINK && u->list_sinks_operation)
                || (device_type == PA_DEVICE_TYPE_SOURCE && u->list_sources_operation))
            return;

        r = proxy_device_new(u, device_type, idx, &device);
        if (r < 0)
            return;

        proxy_device_get_info(device);

        return;
    }

    if ((event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
        switch (device_type) {
            case PA_DEVICE_TYPE_SINK:
                pa_hashmap_remove_and_free(u->sinks, PA_UINT32_TO_PTR(idx));
                break;

            case PA_DEVICE_TYPE_SOURCE:
                pa_hashmap_remove_and_free(u->sources, PA_UINT32_TO_PTR(idx));
                break;
        }

        return;
    }
}

static void subscribe_success_cb(pa_context *context, int success, void *userdata) {
    struct userdata *u = userdata;

    pa_assert(context);
    pa_assert(u);

    if (!success) {
        pa_log_debug("[%s] Subscribing to device events failed: %s", u->remote_address,
                     pa_strerror(pa_context_errno(context)));
        pa_log_debug("[%s] Will be unable to notice new devices.", u->remote_address);
    }
}

static void get_sink_info_list_cb(pa_context *context, const pa_sink_info *info, int is_last, void *userdata) {
    struct userdata *u = userdata;
    int r;
    struct proxy_device *device;

    pa_assert(context);
    pa_assert(u);

    if (u->list_sinks_operation) {
        pa_operation_unref(u->list_sinks_operation);
        u->list_sinks_operation = NULL;
    }

    if (is_last < 0) {
        pa_log_debug("[%s] Listing sinks failed: %s", u->remote_address, pa_strerror(pa_context_errno(context)));
        return;
    }

    if (is_last)
        return;

    pa_assert(info);

    r = proxy_device_new(u, PA_DEVICE_TYPE_SINK, info->index, &device);
    if (r < 0)
        return;

    proxy_device_create_tunnel(device, info);
}

static void get_source_info_list_cb(pa_context *context, const pa_source_info *info, int is_last, void *userdata) {
    struct userdata *u = userdata;
    int r;
    struct proxy_device *device;

    pa_assert(context);
    pa_assert(u);

    if (u->list_sources_operation) {
        pa_operation_unref(u->list_sources_operation);
        u->list_sources_operation = NULL;
    }

    if (is_last < 0) {
        pa_log_debug("[%s] Listing sources failed: %s", u->remote_address, pa_strerror(pa_context_errno(context)));
        return;
    }

    if (is_last)
        return;

    pa_assert(info);

    if (info->monitor_of_sink != PA_INVALID_INDEX)
        return;

    r = proxy_device_new(u, PA_DEVICE_TYPE_SOURCE, info->index, &device);
    if (r < 0)
        return;

    proxy_device_create_tunnel(device, info);
}

static int list_devices(struct userdata *u) {
    pa_assert(u);

    if (!u->list_sinks_operation) {
        u->list_sinks_operation = pa_context_get_sink_info_list(u->context, get_sink_info_list_cb, u);
        if (!u->list_sinks_operation) {
            int r;

            r = pa_context_errno(u->context);
            pa_log_debug("[%s] pa_context_get_sink_info_list() failed: %s", u->remote_address, pa_strerror(r));
            return -r;
        }
    }

    if (!u->list_sources_operation) {
        u->list_sources_operation = pa_context_get_source_info_list(u->context, get_source_info_list_cb, u);
        if (!u->list_sources_operation) {
            int r;

            r = pa_context_errno(u->context);
            pa_log_debug("[%s] pa_context_get_source_info_list() failed: %s", u->remote_address, pa_strerror(r));
            return -r;
        }
    }

    return 0;
}

static void context_state_cb(pa_context *context, void *userdata) {
    struct userdata *u = userdata;
    pa_context_state_t state;

    pa_assert(context);
    pa_assert(u);

    state = pa_context_get_state(context);

    switch (state) {
        case PA_CONTEXT_READY: {
            pa_operation *operation;
            int r;

            pa_context_set_subscribe_callback(context, subscribe_cb, u);
            operation = pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE,
                                             subscribe_success_cb, u);
            if (operation)
                pa_operation_unref(operation);
            else {
                pa_log_debug("[%s] pa_context_subscribe() failed: %s", u->remote_address,
                             pa_strerror(pa_context_errno(context)));
                pa_log_debug("[%s] Will be unable to notice new devices.", u->remote_address);
            }

            r = list_devices(u);
            if (r < 0)
                pa_module_unload(u->core, u->module, true);

            break;
        }

        case PA_CONTEXT_FAILED:
            pa_log_debug("[%s] Context failed: %s", u->remote_address, pa_strerror(pa_context_errno(context)));
            pa_module_unload(u->core, u->module, true);
            break;

        default:
            break;
    }
}

static pa_hook_result_t module_unload_cb(void *hook_data, void *call_data, void *userdata) {
    pa_module *module = call_data;
    struct userdata *u = userdata;
    struct proxy_device *device;
    void *state;

    pa_assert(module);
    pa_assert(u);

    if (pa_streq(module->name, TUNNEL_SINK_MODULE_NAME)) {
        PA_HASHMAP_FOREACH(device, u->sinks, state) {
            if (device->tunnel_module == module) {
                device->tunnel_module = NULL;
                break;
            }
        }

    } else if (pa_streq(module->name, TUNNEL_SOURCE_MODULE_NAME)) {
        PA_HASHMAP_FOREACH(device, u->sources, state) {
            if (device->tunnel_module == module) {
                device->tunnel_module = NULL;
                break;
            }
        }
    }

    return PA_HOOK_OK;
}

int pa__init(pa_module *module) {
    struct userdata *u;
    char *user_name;
    char *client_name;
    int r;

    pa_assert(module);

    if (pa_in_system_mode()) {
        /* TODO: Remove this check once we support also other remotes than just
         * the system instance. */
        pa_log("module-tunnel-manager can not be used in system mode.");
        goto fail;
    }

    u = module->userdata = pa_xnew0(struct userdata, 1);
    u->core = module->core;
    u->module = module;
    u->remote_address = pa_xstrdup(SYSTEM_INSTANCE_ADDRESS);

    user_name = pa_get_user_name_malloc();
    client_name = pa_sprintf_malloc("PulseAudio instance for user \"%s\"", user_name ? user_name : "(unknown)");
    pa_xfree(user_name);
    u->context = pa_context_new(module->core->mainloop, client_name);
    pa_xfree(client_name);

    if (!u->context) {
        pa_log_debug("[%s] pa_context_new() failed.", u->remote_address);
        goto fail;
    }

    u->sinks = pa_hashmap_new_full(NULL, NULL, NULL, (pa_free_cb_t) proxy_device_free);
    u->sources = pa_hashmap_new_full(NULL, NULL, NULL, (pa_free_cb_t) proxy_device_free);

    r = pa_context_connect(u->context, SYSTEM_INSTANCE_ADDRESS, PA_CONTEXT_NOFLAGS, NULL);
    if (r < 0) {
        pa_log_debug("[%s] pa_context_connect() failed: %s", u->remote_address, pa_strerror(pa_context_errno(u->context)));
        goto fail;
    }

    pa_context_set_state_callback(u->context, context_state_cb, u);

    u->module_unload_slot = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_MODULE_UNLOAD], PA_HOOK_NORMAL, module_unload_cb, u);

    return 0;

fail:
    pa__done(module);

    return -1;
}

void pa__done(pa_module *module) {
    struct userdata *u;

    pa_assert(module);

    u = module->userdata;
    if (!u)
        return;

    if (u->module_unload_slot)
        pa_hook_slot_free(u->module_unload_slot);

    if (u->list_sources_operation) {
        pa_operation_cancel(u->list_sinks_operation);
        pa_operation_unref(u->list_sinks_operation);
    }

    if (u->list_sinks_operation) {
        pa_operation_cancel(u->list_sinks_operation);
        pa_operation_unref(u->list_sinks_operation);
    }

    if (u->sources)
        pa_hashmap_free(u->sources);

    if (u->sinks)
        pa_hashmap_free(u->sinks);

    if (u->context) {
        pa_context_disconnect(u->context);
        pa_context_unref(u->context);
    }

    pa_xfree(u->remote_address);
    pa_xfree(u);
}
