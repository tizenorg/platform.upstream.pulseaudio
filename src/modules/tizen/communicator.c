/***
  This file is part of PulseAudio.

  Copyright 2015 Sangchul Lee <sc11.lee@samsung.com>

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

#include "communicator.h"
#include <pulsecore/shared.h>

struct _pa_communicator {
    PA_REFCNT_DECLARE;

    pa_core *core;
    pa_hook hooks[PA_COMMUNICATOR_HOOK_MAX];
};

pa_communicator* pa_communicator_get(pa_core *core) {
    pa_communicator *c;
    unsigned i;

    pa_assert(core);

    if ((c = pa_shared_get(core, "communicator")))
        return pa_communicator_ref(c);

    c = pa_xnew0(pa_communicator, 1);
    PA_REFCNT_INIT(c);
    c->core = core;

    for (i = 0; i < PA_COMMUNICATOR_HOOK_MAX; i++)
        pa_hook_init(&c->hooks[i], c);

    pa_shared_set(core, "communicator", c);

    return c;
}

pa_communicator* pa_communicator_ref(pa_communicator *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) > 0);

    PA_REFCNT_INC(c);

    return c;
}

void pa_communicator_unref(pa_communicator *c) {
    unsigned i;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) > 0);

    if (PA_REFCNT_DEC(c) > 0)
        return;

    for (i = 0; i < PA_COMMUNICATOR_HOOK_MAX; i++)
        pa_hook_done(&c->hooks[i]);

    if (c->core)
        pa_shared_remove(c->core, "communicator");

    pa_xfree(c);
}

pa_hook* pa_communicator_hook(pa_communicator *c, pa_communicator_hook_t hook) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) > 0);

    return &c->hooks[hook];
}

