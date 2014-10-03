#ifndef footunnelmanagerhfoo
#define footunnelmanagerhfoo

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

#include <pulse/context.h>

#include <pulsecore/core.h>

typedef struct pa_tunnel_manager pa_tunnel_manager;
typedef struct pa_tunnel_manager_remote_server pa_tunnel_manager_remote_server;
typedef struct pa_tunnel_manager_remote_device pa_tunnel_manager_remote_device;

struct pa_tunnel_manager {
    pa_core *core;
    pa_hashmap *remote_servers; /* name -> pa_tunnel_manager_remote_server */

    unsigned refcnt;
};

/* If ref is true, the reference count of the manager is incremented, and also
 * the manager is created if it doesn't exist yet. If ref is false, the
 * reference count is not incremented, and if the manager doesn't exist, the
 * function returns NULL. */
pa_tunnel_manager *pa_tunnel_manager_get(pa_core *core, bool ref);

void pa_tunnel_manager_unref(pa_tunnel_manager *manager);

struct pa_tunnel_manager_remote_server {
    pa_tunnel_manager *manager;
    char *name;
    char *address;
    pa_hashmap *devices; /* name -> pa_tunnel_manager_remote_device */
    bool failed;

    pa_context *context;
    pa_operation *list_sinks_operation;
    pa_operation *list_sources_operation;
    pa_hashmap *device_stubs; /* struct device_stub -> struct device_stub (hashmap-as-a-set) */
};

enum {
    PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_UNLINKED,
    PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_PROPLIST_CHANGED,
    PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_MAX,
};

struct pa_tunnel_manager_remote_device {
    pa_tunnel_manager_remote_server *server;
    char *name;
    pa_device_type_t type;
    uint32_t index;
    pa_proplist *proplist;
    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    bool is_monitor;
    pa_hook hooks[PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_MAX];

    pa_operation *get_info_operation;

    /* These are a workaround for the problem that the introspection API's info
     * callbacks are called multiple times, which means that if the userdata
     * needs to be freed during the callbacks, the freeing needs to be
     * postponed until the last call. */
    bool can_free;
    bool dead;
};

#endif
