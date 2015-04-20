/***
  This file is part of PulseAudio.

  Copyright 2013 João Paulo Rechi Vita

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

#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/module.h>
#ifdef BLUETOOTH_APTX_SUPPORT
#include <pulsecore/modargs.h>
//#include "bluez5-util.h"
#include <dlfcn.h>
#endif

#include "module-bluetooth-discover-symdef.h"

PA_MODULE_AUTHOR("João Paulo Rechi Vita");
PA_MODULE_DESCRIPTION("Detect available Bluetooth daemon and load the corresponding discovery module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

#ifdef BLUETOOTH_APTX_SUPPORT
PA_MODULE_USAGE("aptx_lib_name=<name of aptx library name>");
#endif

#ifdef BLUETOOTH_APTX_SUPPORT
static const char* const valid_modargs[] = {
    "aptx_lib_name",
#ifdef __TIZEN_BT__
    "enable_scmst",
#endif
    NULL
};
#endif

struct userdata {
    uint32_t bluez5_module_idx;
    uint32_t bluez4_module_idx;
};
#ifdef BLUETOOTH_APTX_SUPPORT
int pa_load_aptx(const char *aptx_lib_name);
int pa_unload_aptx(void);
void* pa_aptx_get_handle(void);
#endif

#ifdef BLUETOOTH_APTX_SUPPORT
static void *aptx_handle = NULL;

int pa_unload_aptx(void)
{
	if (aptx_handle == NULL) {
		pa_log_warn("Unable to unload apt-X library");
		return -1;
	}

	dlclose(aptx_handle);
	aptx_handle = NULL;

	pa_log_debug("unloaded apt-X library successfully");
	return 0;
}

int pa_load_aptx(const char *aptx_lib_name)
{
	char* lib_path = NULL ;

        if(aptx_lib_name == NULL)
		return -1;

        lib_path = pa_sprintf_malloc("%s/%s", PA_DLSEARCHPATH, aptx_lib_name);

	if (!lib_path)
		return -1;

	pa_log_info("aptx_lib_path = [%s]", lib_path);

	aptx_handle = dlopen(lib_path, RTLD_LAZY);
	if (aptx_handle == NULL) {
		pa_log_warn("Unable to load apt-X library [%s]", dlerror());
		pa_xfree(lib_path);
		return -1;
	}

	pa_log_debug("loaded apt-X library successfully");
	pa_xfree(lib_path);

	return 0;
}

void* pa_aptx_get_handle(void)
{
	return aptx_handle;
}
#endif

int pa__init(pa_module* m) {
    struct userdata *u;
    pa_module *mm;

#ifdef BLUETOOTH_APTX_SUPPORT
    pa_modargs *ma = NULL;
    const char *aptx_lib_name = NULL;
#endif

    pa_assert(m);

#ifdef BLUETOOTH_APTX_SUPPORT
    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        pa__done(m);
        return -1;
    }

    if (pa_modargs_get_value(ma, "async", NULL))
        pa_log_warn("The 'async' argument is deprecated and does nothing.");


    aptx_lib_name = pa_modargs_get_value(ma, "aptx_lib_name", NULL);
    if (aptx_lib_name)
        pa_load_aptx(aptx_lib_name);
    else
        pa_log("Failed to parse aptx_lib_name argument.");
#endif

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->bluez5_module_idx = PA_INVALID_INDEX;
    u->bluez4_module_idx = PA_INVALID_INDEX;

    if (pa_module_exists("module-bluez5-discover")) {
        mm = pa_module_load(m->core, "module-bluez5-discover",  NULL);
        if (mm)
            u->bluez5_module_idx = mm->index;
    }

    if (pa_module_exists("module-bluez4-discover")) {
        mm = pa_module_load(m->core, "module-bluez4-discover",  NULL);
        if (mm)
            u->bluez4_module_idx = mm->index;
    }

    if (u->bluez5_module_idx == PA_INVALID_INDEX && u->bluez4_module_idx == PA_INVALID_INDEX) {
        pa_xfree(u);
#ifdef BLUETOOTH_APTX_SUPPORT
	     pa_modargs_free(ma);
#endif
        return -1;
    }
#ifdef BLUETOOTH_APTX_SUPPORT
    pa_modargs_free(ma);
#endif

    return 0;
}

void pa__done(pa_module* m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->bluez5_module_idx != PA_INVALID_INDEX)
        pa_module_unload_by_index(m->core, u->bluez5_module_idx, true);

    if (u->bluez4_module_idx != PA_INVALID_INDEX)
        pa_module_unload_by_index(m->core, u->bluez4_module_idx, true);

#ifdef BLUETOOTH_APTX_SUPPORT
    pa_unload_aptx();
#endif

    pa_xfree(u);
}
