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

#include <modules/tunnel-manager/tunnel-manager.h>

#include <pulsecore/i18n.h>

PA_MODULE_AUTHOR("Tanu Kaskinen");
PA_MODULE_DESCRIPTION(_("Manage tunnels to other PulseAudio servers"));
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

struct userdata {
    pa_tunnel_manager *tunnel_manager;
};

int pa__init(pa_module *module) {
    struct userdata *u;

    pa_assert(module);

    u = module->userdata = pa_xnew0(struct userdata, 1);
    u->tunnel_manager = pa_tunnel_manager_get(module->core, true);

    return 0;
}

void pa__done(pa_module *module) {
    struct userdata *u;

    pa_assert(module);

    u = module->userdata;
    if (!u)
        return;

    if (u->tunnel_manager)
        pa_tunnel_manager_unref(u->tunnel_manager);

    pa_xfree(u);
}
