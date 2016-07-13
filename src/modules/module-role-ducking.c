/***
  This file is part of PulseAudio.

  Copyright 2012 Flavio Ceolin <flavio.ceolin@profusion.mobi>

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

#include <pulse/volume.h>
#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/hook-list.h>
#include <pulsecore/core.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/modargs.h>

#include "module-role-ducking-symdef.h"

PA_MODULE_AUTHOR("Flavio Ceolin <flavio.ceolin@profusion.mobi>");
PA_MODULE_DESCRIPTION("Apply a ducking effect based on streams roles");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);
PA_MODULE_USAGE(
        "trigger_roles=<Comma(and slash) separated list of roles which will trigger a ducking. Slash can divide the roles into groups>"
        "ducking_roles=<Comma(and slash) separated list of roles which will be ducked. Slash can divide the roles into groups>"
        "global=<Should we operate globally or only inside the same device?>"
        "volume=<Volume for the attenuated streams. Default: -20dB. If trigger_roles and ducking_roles are separated by slash, use slash for dividing volume group>"
        "fade_out=<Fade-out duration(ms). If trigger_roles and ducking_roles are separated by slash, use slash for dividing fade group. Default: 0.>"
        "fade_in=<Fade-in duration(ms). If trigger_roles and ducking_roles are separated by slash, use slash for dividing fade group. Default: 0.>"
);

static const char* const valid_modargs[] = {
    "trigger_roles",
    "ducking_roles",
    "global",
    "volume",
    "fade_out",
    "fade_in",
    NULL
};

struct fade_durations {
    long out;
    long in;
};

struct group {
    char *name;
    pa_idxset *trigger_roles;
    pa_idxset *ducking_roles;
    pa_idxset *triggered_inputs;
    pa_idxset *ducked_inputs;
    pa_volume_t volume;
    struct fade_durations fade_durs;
    bool fade_needed;
};

struct userdata {
    pa_core *core;
    const char *name;
    uint32_t n_groups;
    struct group **groups;
    bool global;
    pa_volume_t volume;
    pa_hook_slot
        *sink_input_put_slot,
        *sink_input_unlink_slot,
        *sink_input_move_start_slot,
        *sink_input_move_finish_slot;
};

static bool sink_has_trigger_streams(struct userdata *u, pa_sink *s, pa_sink_input *ignore, struct group *g) {
    pa_sink_input *j;
    uint32_t idx, role_idx, trigger_idx;
    const char *trigger_role;
    void *si;
    bool found = false;

    pa_assert(u);
    pa_sink_assert_ref(s);

    g->fade_needed = true;

    PA_IDXSET_FOREACH(j, s->inputs, idx) {
        const char *role;

        if (j == ignore)
            continue;

        if (!(role = pa_proplist_gets(j->proplist, PA_PROP_MEDIA_ROLE)))
            continue;

        if (g->fade_durs.in || g->fade_durs.out) {
            PA_IDXSET_FOREACH(trigger_role, g->trigger_roles, role_idx) {
                if (pa_streq(role, trigger_role)) {
                    pa_log_debug("Found a '%s' stream(%u) that will trigger the ducking.", trigger_role, j->index);
                    PA_IDXSET_FOREACH(si, g->triggered_inputs, trigger_idx) {
                        if (j == si) {
                            g->fade_needed = false;
                            break;
                        }
                    }
                    found = true;
                    pa_idxset_put(g->triggered_inputs, j, NULL);
                }
            }
        } else {
            PA_IDXSET_FOREACH(trigger_role, g->trigger_roles, role_idx) {
                if (pa_streq(role, trigger_role)) {
                    pa_log_debug("Found a '%s' stream(%u) that will trigger the ducking.", trigger_role, j->index);
                    return true;
                }
            }
        }
    }

    return found;
}

static bool sinks_have_trigger_streams(struct userdata *u, pa_sink *s, pa_sink_input *ignore, struct group *g) {
    bool ret = false;

    pa_assert(u);

    if (u->global) {
        uint32_t idx;
        PA_IDXSET_FOREACH(s, u->core->sinks, idx)
            if ((ret = sink_has_trigger_streams(u, s, ignore, g)))
                break;
    } else
        ret = sink_has_trigger_streams(u, s, ignore, g);

    return ret;
}

static bool stream_is_affected_by_other_groups(struct userdata *u, pa_sink_input *s, struct group *skip_g, pa_volume_t *min_vol) {
    bool ret = false;
    uint32_t i;
    pa_sink_input *si;
    pa_volume_t vol = PA_VOLUME_MAX;

    pa_assert(u);

    for (i = 0; i < u->n_groups; i++) {
        uint32_t idx;
        if (u->groups[i] == skip_g)
            continue;
        PA_IDXSET_FOREACH(si, u->groups[i]->ducked_inputs, idx)
            if (si == s) {
                if (u->groups[i]->volume < vol)
                    vol = u->groups[i]->volume;
                ret = true;
            }
    }
    if (ret)
        *min_vol = vol;

    return ret;
}

static void apply_ducking_to_sink(struct userdata *u, pa_sink *s, pa_sink_input *ignore, bool duck, struct group *g) {
    pa_sink_input *j;
    uint32_t idx, role_idx;
    const char *ducking_role;
    bool trigger = false;
    bool exist = false;
    pa_volume_t min_vol;

    pa_assert(u);
    pa_sink_assert_ref(s);

    PA_IDXSET_FOREACH(j, s->inputs, idx) {
        const char *role;
        pa_sink_input *i;

        if (j == ignore)
            continue;

        if (!(role = pa_proplist_gets(j->proplist, PA_PROP_MEDIA_ROLE)))
            continue;

        PA_IDXSET_FOREACH(ducking_role, g->ducking_roles, role_idx) {
            if ((trigger = pa_streq(role, ducking_role)))
                break;
        }
        if (!trigger)
            continue;

        i = pa_idxset_get_by_data(g->ducked_inputs, j, NULL);
        if (duck && !i) {
            if (g->fade_durs.in || g->fade_durs.out) {
                pa_cvolume_ramp vol_ramp;
                pa_cvolume_ramp_set(&vol_ramp, j->volume.channels, PA_VOLUME_RAMP_TYPE_LINEAR, g->fade_durs.out, g->volume);
                if (!g->fade_needed) {
                    pa_cvolume vol;
                    vol.channels = 1;
                    vol.values[0] = g->volume;
                    pa_log_debug("Found a '%s' stream(%u) that should be ducked by '%s'.", ducking_role, j->index, g->name);

                    pa_sink_input_add_volume_factor(j, g->name, &vol);
                    pa_sink_input_set_volume_ramp(j, &vol_ramp, false, false);
                } else {
                    exist = stream_is_affected_by_other_groups(u, j, g, &min_vol);
                    pa_cvolume_ramp_set(&vol_ramp, j->volume.channels, PA_VOLUME_RAMP_TYPE_LINEAR,
                                        g->fade_durs.out, exist ? MIN(g->volume, min_vol) : g->volume);
                    pa_log_debug("Found a '%s' stream(%u) that should be ducked with fade-out(%lums) by '%s'.",
                                 ducking_role, j->index, g->fade_durs.out, g->name);

                    pa_sink_input_set_volume_ramp(j, &vol_ramp, true, false);
                }
            } else {
                pa_cvolume vol;
                vol.channels = 1;
                vol.values[0] = g->volume;

                pa_log_debug("Found a '%s' stream(%u) that should be ducked by '%s'.", ducking_role, j->index, g->name);
                pa_sink_input_add_volume_factor(j, g->name, &vol);
            }
            pa_idxset_put(g->ducked_inputs, j, NULL);
        } else if (!duck && i) { /* This stream should not longer be ducked */
            if (g->fade_durs.in || g->fade_durs.out) {
                pa_cvolume_ramp vol_ramp;
                pa_log_debug("Found a '%s' stream(%u) that should be unducked with fade-in(%lums) by '%s'",
                             ducking_role, j->index, g->fade_durs.in, g->name);
                pa_sink_input_remove_volume_factor(j, g->name);
                exist = stream_is_affected_by_other_groups(u, j, g, &min_vol);
                pa_cvolume_ramp_set(&vol_ramp, j->volume.channels, PA_VOLUME_RAMP_TYPE_LINEAR,
                                    g->fade_durs.in, exist ? min_vol : PA_VOLUME_NORM);

                pa_sink_input_set_volume_ramp(j, &vol_ramp, true, false);
            } else {
                pa_log_debug("Found a '%s' stream(%u) that should be unducked by '%s'", ducking_role, j->index, g->name);
                pa_sink_input_remove_volume_factor(j, g->name);
            }
            pa_idxset_remove_by_data(g->ducked_inputs, j, NULL);
        }
    }
}

static void apply_ducking(struct userdata *u, pa_sink *s, pa_sink_input *ignore, bool duck, struct group *g) {
    pa_assert(u);

    if (u->global) {
        uint32_t idx;
        PA_IDXSET_FOREACH(s, u->core->sinks, idx)
            apply_ducking_to_sink(u, s, ignore, duck, g);
    } else
        apply_ducking_to_sink(u, s, ignore, duck, g);
}

static pa_hook_result_t process(struct userdata *u, pa_sink_input *i, bool duck) {
    bool should_duck = false;
    const char *role;
    uint32_t j;

    pa_assert(u);
    pa_sink_input_assert_ref(i);

    if (!i->proplist)
        return PA_HOOK_OK;

    if (!(role = pa_proplist_gets(i->proplist, PA_PROP_MEDIA_ROLE)))
        return PA_HOOK_OK;

    if (!i->sink)
        return PA_HOOK_OK;

    for (j = 0; j < u->n_groups; j++) {
        should_duck = sinks_have_trigger_streams(u, i->sink, duck ? NULL : i, u->groups[j]);
        apply_ducking(u, i->sink, duck ? NULL : i, should_duck, u->groups[j]);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_put_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    return process(u, i, true);
}

static pa_hook_result_t sink_input_unlink_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
    uint32_t j;

    pa_sink_input_assert_ref(i);

    for (j = 0; j < u->n_groups; j++) {
        pa_idxset_remove_by_data(u->groups[j]->ducked_inputs, i, NULL);
        pa_idxset_remove_by_data(u->groups[j]->triggered_inputs, i, NULL);
    }

    return process(u, i, false);
}

static pa_hook_result_t sink_input_move_start_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    return process(u, i, false);
}

static pa_hook_result_t sink_input_move_finish_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    return process(u, i, true);
}

int pa__init(pa_module *m) {
    pa_modargs *ma = NULL;
    struct userdata *u;
    const char *roles;
    const char *volumes;
    const char *times;
    uint32_t group_count_tr = 0;
    uint32_t group_count_du = 0;
    uint32_t group_count_vol = 0;
    uint32_t group_count_fd_out = 0;
    uint32_t group_count_fd_in = 0;
    uint32_t i = 0;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);

    u->core = m->core;
    u->name = m->name;

    roles = pa_modargs_get_value(ma, "trigger_roles", NULL);
    if (roles) {
        const char *split_state = NULL;
        char *n = NULL;
        while ((n = pa_split(roles, "/", &split_state))) {
            group_count_tr++;
            pa_xfree(n);
        }
    }
    roles = pa_modargs_get_value(ma, "ducking_roles", NULL);
    if (roles) {
        const char *split_state = NULL;
        char *n = NULL;
        while ((n = pa_split(roles, "/", &split_state))) {
            group_count_du++;
            pa_xfree(n);
        }
    }
    volumes = pa_modargs_get_value(ma, "volume", NULL);
    if (volumes) {
        const char *split_state = NULL;
        char *n = NULL;
        while ((n = pa_split(volumes, "/", &split_state))) {
            group_count_vol++;
            pa_xfree(n);
        }
    }
    times = pa_modargs_get_value(ma, "fade_out", NULL);
    if (times) {
        const char *split_state = NULL;
        char *n = NULL;
        while ((n = pa_split(times, "/", &split_state))) {
            group_count_fd_out++;
            pa_xfree(n);
        }
    }
    times = pa_modargs_get_value(ma, "fade_in", NULL);
    if (times) {
        const char *split_state = NULL;
        char *n = NULL;
        while ((n = pa_split(times, "/", &split_state))) {
            group_count_fd_in++;
            pa_xfree(n);
        }
    }

    if ((group_count_tr > 1 || group_count_du > 1 || group_count_vol > 1) &&
        ((group_count_tr != group_count_du) || (group_count_tr != group_count_vol) ||
        (group_count_fd_out > group_count_tr) || (group_count_fd_in > group_count_tr))) {
        pa_log("Invalid number of groups");
        goto fail;
    }

    if (group_count_tr > 0)
        u->n_groups = group_count_tr;
    else
        u->n_groups = 1;

    u->groups = pa_xmalloc0(u->n_groups * sizeof(struct group*));
    for (i = 0; i < u->n_groups; i++) {
        u->groups[i] = pa_xmalloc0(sizeof(struct group));
        u->groups[i]->name = pa_sprintf_malloc("%s_group_%u", u->name, i);
        u->groups[i]->trigger_roles = pa_idxset_new(NULL, NULL);
        u->groups[i]->ducking_roles = pa_idxset_new(NULL, NULL);
        u->groups[i]->triggered_inputs = pa_idxset_new(NULL, NULL);
        u->groups[i]->ducked_inputs = pa_idxset_new(NULL, NULL);
    }

    roles = pa_modargs_get_value(ma, "trigger_roles", NULL);
    if (roles) {
        const char *group_split_state = NULL;
        char *roles_in_group = NULL;
        i = 0;
        while ((roles_in_group = pa_split(roles, "/", &group_split_state))) {
            if (roles_in_group[0] != '\0') {
                const char *split_state = NULL;
                char *n = NULL;
                while ((n = pa_split(roles_in_group, ",", &split_state))) {
                    if (n[0] != '\0')
                        pa_idxset_put(u->groups[i]->trigger_roles, n, NULL);
                    else {
                        pa_log("empty trigger role");
                        pa_xfree(n);
                        goto fail;
                    }
                }
                i++;
            } else {
                pa_log("empty trigger roles");
                pa_xfree(roles_in_group);
                goto fail;
            }
        }
    }
    if (pa_idxset_isempty(u->groups[0]->trigger_roles)) {
        pa_log_debug("Using role 'phone' as trigger role.");
        pa_idxset_put(u->groups[0]->trigger_roles, pa_xstrdup("phone"), NULL);
    }

    roles = pa_modargs_get_value(ma, "ducking_roles", NULL);
    if (roles) {
        const char *group_split_state = NULL;
        char *roles_in_group = NULL;
        i = 0;
        while ((roles_in_group = pa_split(roles, "/", &group_split_state))) {
            if (roles_in_group[0] != '\0') {
                const char *split_state = NULL;
                char *n = NULL;
                while ((n = pa_split(roles_in_group, ",", &split_state))) {
                    if (n[0] != '\0')
                        pa_idxset_put(u->groups[i]->ducking_roles, n, NULL);
                    else {
                        pa_log("empty ducking role");
                        pa_xfree(n);
                        goto fail;
                     }
                }
                i++;
            } else {
                pa_log("empty ducking roles");
                pa_xfree(roles_in_group);
                goto fail;
            }
        }
    }
    if (pa_idxset_isempty(u->groups[0]->ducking_roles)) {
        pa_log_debug("Using roles 'music' and 'video' as ducking roles.");
        pa_idxset_put(u->groups[0]->ducking_roles, pa_xstrdup("music"), NULL);
        pa_idxset_put(u->groups[0]->ducking_roles, pa_xstrdup("video"), NULL);
    }

    u->groups[0]->volume = pa_sw_volume_from_dB(-20);
    volumes = pa_modargs_get_value(ma, "volume", NULL);
    if (volumes) {
        const char *group_split_state = NULL;
        char *n = NULL;
        i = 0;
        while ((n = pa_split(volumes, "/", &group_split_state))) {
            if (n[0] != '\0') {
                if (pa_parse_volume(n, &(u->groups[i++]->volume)) == -1) {
                    pa_log("Failed to parse volume");
                    pa_xfree(n);
                    goto fail;
                }
            } else {
                pa_log("empty volume");
                pa_xfree(n);
                goto fail;
            }
            pa_xfree(n);
        }
    }

    times = pa_modargs_get_value(ma, "fade_out", NULL);
    if (times) {
        const char *group_split_state = NULL;
        char *time_in_group = NULL;
        i = 0;
        while ((time_in_group = pa_split(times, "/", &group_split_state))) {
            if (time_in_group[0] != '\0') {
                if (pa_atol(time_in_group, &(u->groups[i++]->fade_durs.out))) {
                    pa_log("Failed to pa_atol() for fade-out duration");
                    pa_xfree(time_in_group);
                    goto fail;
                }
            } else {
                pa_log("empty fade-out duration");
                pa_xfree(time_in_group);
                goto fail;
            }
            pa_xfree(time_in_group);
        }
    }

    times = pa_modargs_get_value(ma, "fade_in", NULL);
    if (times) {
        const char *group_split_state = NULL;
        char *time_in_group = NULL;
        i = 0;
        while ((time_in_group = pa_split(times, "/", &group_split_state))) {
            if (time_in_group[0] != '\0') {
                if (pa_atol(time_in_group, &(u->groups[i++]->fade_durs.in))) {
                    pa_log("Failed to pa_atol() for fade-in duration");
                    pa_xfree(time_in_group);
                    goto fail;
                }
            } else {
                pa_log("empty fade-in duration");
                pa_xfree(time_in_group);
                goto fail;
            }
            pa_xfree(time_in_group);
        }
    }

    u->global = false;
    if (pa_modargs_get_value_boolean(ma, "global", &u->global) < 0) {
        pa_log("Failed to parse a boolean parameter: global");
        goto fail;
    }

    u->sink_input_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT], PA_HOOK_LATE, (pa_hook_cb_t) sink_input_put_cb, u);
    u->sink_input_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK], PA_HOOK_LATE, (pa_hook_cb_t) sink_input_unlink_cb, u);
    u->sink_input_move_start_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_START], PA_HOOK_LATE, (pa_hook_cb_t) sink_input_move_start_cb, u);
    u->sink_input_move_finish_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_FINISH], PA_HOOK_LATE, (pa_hook_cb_t) sink_input_move_finish_cb, u);

    pa_modargs_free(ma);

    return 0;

fail:
    pa__done(m);

    if (ma)
        pa_modargs_free(ma);

    return -1;
}

void pa__done(pa_module *m) {
    struct userdata* u;
    pa_sink_input *i;
    uint32_t j;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->groups) {
        for (j = 0; j < u->n_groups; j++) {
            pa_idxset_free(u->groups[j]->trigger_roles, pa_xfree);
            pa_idxset_free(u->groups[j]->ducking_roles, pa_xfree);
            while ((i = pa_idxset_steal_first(u->groups[j]->ducked_inputs, NULL)))
                pa_sink_input_remove_volume_factor(i, u->groups[j]->name);
            pa_idxset_free(u->groups[j]->ducked_inputs, NULL);
            pa_idxset_free(u->groups[j]->triggered_inputs, NULL);
            pa_xfree(u->groups[j]->name);
            pa_xfree(u->groups[j]);
        }
        pa_xfree(u->groups);
    }

    if (u->sink_input_put_slot)
        pa_hook_slot_free(u->sink_input_put_slot);
    if (u->sink_input_unlink_slot)
        pa_hook_slot_free(u->sink_input_unlink_slot);
    if (u->sink_input_move_start_slot)
        pa_hook_slot_free(u->sink_input_move_start_slot);
    if (u->sink_input_move_finish_slot)
        pa_hook_slot_free(u->sink_input_move_finish_slot);

    pa_xfree(u);
}
