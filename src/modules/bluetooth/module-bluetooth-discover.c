/***
  This file is part of PulseAudio.

  Copyright 2008-2009 Joao Paulo Rechi Vita

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <pulse/xmalloc.h>
#include <pulsecore/module.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/dbus-shared.h>

#include "module-bluetooth-discover-symdef.h"
#include "bluetooth-util.h"

PA_MODULE_AUTHOR("Joao Paulo Rechi Vita");
PA_MODULE_DESCRIPTION("Detect available bluetooth audio devices and load bluetooth audio drivers");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_USAGE("sco_sink=<name of sink> "
#ifdef BLUETOOTH_APTX_SUPPORT
                "sco_source=<name of source> "
                "aptx_lib_name=<name of aptx library name>");
#else
                "sco_source=<name of source> ");
#endif
PA_MODULE_LOAD_ONCE(true);

static const char* const valid_modargs[] = {
    "sco_sink",
    "sco_source",
    "async", /* deprecated */
#ifdef BLUETOOTH_APTX_SUPPORT
    "aptx_lib_name",
#endif
    NULL
};

struct userdata {
    pa_module *module;
    pa_modargs *modargs;
    pa_core *core;
    pa_bluetooth_discovery *discovery;
    pa_hook_slot *slot;
    pa_hashmap *hashmap;
};

struct module_info {
    char *path;
    uint32_t module;
};

static pa_hook_result_t load_module_for_device(pa_bluetooth_discovery *y, const pa_bluetooth_device *d, struct userdata *u) {
    struct module_info *mi;

    pa_assert(u);
    pa_assert(d);

    mi = pa_hashmap_get(u->hashmap, d->path);

    if (pa_bluetooth_device_any_audio_connected(d)) {

        if (!mi) {
#ifdef BLUETOOTH_PROFILE_SET
            const char *profile = NULL;

            if ((d->transports[PROFILE_A2DP] && d->transports[PROFILE_A2DP]->state != PA_BLUETOOTH_TRANSPORT_STATE_DISCONNECTED)) {
                profile = "a2dp";
            } if ((d->transports[PROFILE_A2DP_SOURCE] && d->transports[PROFILE_A2DP_SOURCE]->state != PA_BLUETOOTH_TRANSPORT_STATE_DISCONNECTED)) {
                profile = "a2dp_source";
            } else if ((d->transports[PROFILE_HFGW] && d->transports[PROFILE_HFGW]->state != PA_BLUETOOTH_TRANSPORT_STATE_DISCONNECTED)) {
                profile = "hfgw";
            }
            if (!profile)
                return PA_HOOK_OK;
#endif
            pa_module *m = NULL;
            char *args;

            /* Oh, awesome, a new device has shown up and been connected! */

#ifdef BLUETOOTH_PROFILE_SET
            args = pa_sprintf_malloc("address=\"%s\" path=\"%s\" profile=\"%s\"", d->address, d->path, profile);
#else
            args = pa_sprintf_malloc("address=\"%s\" path=\"%s\"", d->address, d->path);
#endif

            if (pa_modargs_get_value(u->modargs, "sco_sink", NULL) &&
                pa_modargs_get_value(u->modargs, "sco_source", NULL)) {
                char *tmp;

                tmp = pa_sprintf_malloc("%s sco_sink=\"%s\" sco_source=\"%s\"", args,
                                        pa_modargs_get_value(u->modargs, "sco_sink", NULL),
                                        pa_modargs_get_value(u->modargs, "sco_source", NULL));
                pa_xfree(args);
                args = tmp;
            }

            pa_log_debug("Loading module-bluetooth-device %s", args);
            m = pa_module_load(u->module->core, "module-bluetooth-device", args);
            pa_xfree(args);

            if (m) {
                mi = pa_xnew(struct module_info, 1);
                mi->module = m->index;
                mi->path = pa_xstrdup(d->path);

                pa_hashmap_put(u->hashmap, mi->path, mi);
            } else
                pa_log_debug("Failed to load module for device %s", d->path);
        }

    } else {

        if (mi) {

            /* Hmm, disconnection? Then the module unloads itself */

            pa_log_debug("Unregistering module for %s", d->path);
            pa_hashmap_remove(u->hashmap, mi->path);
            pa_xfree(mi->path);
            pa_xfree(mi);
        }
    }

    return PA_HOOK_OK;
}

int pa__init(pa_module* m) {
    struct userdata *u;
    pa_modargs *ma = NULL;
#ifdef BLUETOOTH_APTX_SUPPORT
    const char *aptx_lib_name = NULL;
#endif

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    if (pa_modargs_get_value(ma, "async", NULL))
        pa_log_warn("The 'async' argument is deprecated and does nothing.");

#ifdef BLUETOOTH_APTX_SUPPORT
    aptx_lib_name = pa_modargs_get_value(ma, "aptx_lib_name", NULL);
    if (aptx_lib_name)
        pa_load_aptx(aptx_lib_name);
    else
        pa_log("Failed to parse aptx_lib_name argument.");
#endif
    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->module = m;
    u->core = m->core;
    u->modargs = ma;
    ma = NULL;
    u->hashmap = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    if (!(u->discovery = pa_bluetooth_discovery_get(u->core)))
        goto fail;

    u->slot = pa_hook_connect(pa_bluetooth_discovery_hook(u->discovery, PA_BLUETOOTH_HOOK_DEVICE_CONNECTION_CHANGED),
                              PA_HOOK_NORMAL, (pa_hook_cb_t) load_module_for_device, u);

    return 0;

fail:
    pa__done(m);

    if (ma)
        pa_modargs_free(ma);

    return -1;
}

void pa__done(pa_module* m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->slot)
        pa_hook_slot_free(u->slot);

    if (u->discovery)
        pa_bluetooth_discovery_unref(u->discovery);
#ifdef BLUETOOTH_APTX_SUPPORT
	pa_unload_aptx();
#endif

    if (u->hashmap) {
        struct module_info *mi;

        while ((mi = pa_hashmap_steal_first(u->hashmap))) {
            pa_xfree(mi->path);
            pa_xfree(mi);
        }

        pa_hashmap_free(u->hashmap, NULL);
    }

    if (u->modargs)
        pa_modargs_free(u->modargs);

    pa_xfree(u);
}
