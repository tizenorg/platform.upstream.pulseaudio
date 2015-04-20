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

#include <iniparser.h>
#include "stream-manager-priv.h"
#include "stream-manager-volume.h"
#include "stream-manager-volume-priv.h"

typedef enum {
    GET_VOLUME_CURRENT_LEVEL,
    GET_VOLUME_MAX_LEVEL
} pa_volume_get_command_t;

#define VOLUME_INI_DEFAULT_PATH     "/usr/etc/mmfw_audio_volume.ini"
#define VOLUME_INI_TUNED_PATH       "/opt/system/mmfw_audio_volume.ini"
#define DEFAULT_TABLE               "speaker"

/* TODO : after preparing gain map, we can remove it */
static const char *__get_gain_type_string_by_idx (uint32_t gain_type_idx) {
    switch (gain_type_idx) {
    case AUDIO_GAIN_TYPE_DEFAULT:           return "default";
    case AUDIO_GAIN_TYPE_DIALER:            return "dialer";
    case AUDIO_GAIN_TYPE_TOUCH:             return "touch";
    case AUDIO_GAIN_TYPE_AF:                return "af";
    case AUDIO_GAIN_TYPE_SHUTTER1:          return "shutter1";
    case AUDIO_GAIN_TYPE_SHUTTER2:          return "shutter2";
    case AUDIO_GAIN_TYPE_CAMCODING:         return "camcording";
    case AUDIO_GAIN_TYPE_MIDI:              return "midi";
    case AUDIO_GAIN_TYPE_BOOTING:           return "booting";
    case AUDIO_GAIN_TYPE_VIDEO:             return "video";
    case AUDIO_GAIN_TYPE_TTS:               return "tts";
    default:                                return "invalid";
    }
}

static int load_out_volume_and_gain_table_from_ini (pa_stream_manager *m) {
    int ret = 0;
    dictionary *dict = NULL;
    uint32_t vol_type_idx = 0;
    uint32_t gain_type_idx = 0;
    int size = 0;
    const char delimiter[] = ", ";
    char *key = NULL;
    char *list_str = NULL;
    char *token = NULL;
    char *ptr = NULL;
    const char *table_str = DEFAULT_TABLE;
    volume_info* v = NULL;
    void *state = NULL;
    const char *vol_type_str = NULL;

    pa_assert(m);

    dict = iniparser_load(VOLUME_INI_TUNED_PATH);
    if (!dict) {
        pa_log_warn("Loading tuned volume & gain table from ini file failed");
        dict = iniparser_load(VOLUME_INI_DEFAULT_PATH);
        if (!dict) {
            pa_log_warn("Loading default volume & gain table from ini file failed");
            ret = -1;
            goto FAILURE;
        }
    }

    /* Load volume table */
    while ((v = pa_hashmap_iterate(m->volume_map.out_volumes, &state, &vol_type_str))) {
        size = strlen(table_str) + strlen(vol_type_str) + 2;
        key = pa_xmalloc0(size);
        if (key) {
            snprintf(key, size, "%s:%s", table_str, vol_type_str);
            list_str = iniparser_getstring(dict, key, NULL);
            if (list_str) {
                token = strtok_r(list_str, delimiter, &ptr);
                while (token) {
                    /* convert dB volume to linear volume */
                    double *vol_value = pa_xmalloc0(sizeof(double));
                    *vol_value = 0.0f;
                    if(strncmp(token, "0", strlen(token)))
                        *vol_value = pow(10.0, (atof(token) - 100) / 20.0);
                    pa_idxset_put(v->idx_volume_values, vol_value, NULL);
                    token = strtok_r(NULL, delimiter, &ptr);
                }
            } else {
                pa_log_error("failed to parse [%s]", key);
                ret = -1;
                goto FAILURE;
            }
        } else {
            pa_log_error("failed to pa_xmalloc0()");
            ret = -1;
            goto FAILURE;
        }
    }

    /* Load gain table */
    for (gain_type_idx = AUDIO_GAIN_TYPE_DEFAULT + 1; gain_type_idx < AUDIO_GAIN_TYPE_MAX; gain_type_idx++) {
        const char *gain_type_str = __get_gain_type_string_by_idx(gain_type_idx);
        size = strlen(table_str) + strlen("gain") + strlen(gain_type_str) + 3;
        key = pa_xmalloc0(size);
        if (key) {
            snprintf(key, size, "%s:gain_%s", table_str, gain_type_str);
            token = iniparser_getstring(dict, key, NULL);
            if (token) {
                double *modifier_gain = pa_xmalloc0(sizeof(double));
                *modifier_gain = atof(token);
                if (!m->volume_map.out_modifier_gains)
                    m->volume_map.out_modifier_gains = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                pa_hashmap_put(m->volume_map.out_modifier_gains, gain_type_str, modifier_gain);
            } else {
                pa_log_error("failed to parse [%s]", key);
                ret = -1;
                goto FAILURE;
            }
        } else {
            pa_log_error("failed to pa_xmalloc0()");
            ret = -1;
            goto FAILURE;
        }
    }

FAILURE:
    if (key)
        free(key);
    if (dict)
        iniparser_freedict(dict);

    return ret;
}

static int is_hal_volume_by_type(pa_stream_manager *m, const char *volume_type, stream_type stream_type, pa_bool_t *is_hal_volume) {
    volume_info *v = NULL;
    pa_assert(m);
    pa_assert(is_hal_volume);

    if ((stream_type==STREAM_SINK_INPUT)?m->volume_map.out_volumes:m->volume_map.in_volumes) {
        v = pa_hashmap_get((stream_type==STREAM_SINK_INPUT)?m->volume_map.out_volumes:m->volume_map.in_volumes, volume_type);
        if (v)
            *is_hal_volume = v->is_hal_volume_type;
    } else
        return -1;

    return 0;
}

#ifdef PRIMARY_VOLUME
int _set_primary_volume(pa_stream_manager *m, void* key, int volumetype, int is_new) {
    const int NO_INSTANCE = -1;
    const int CAPURE_ONLY = -2; /* check mm_sound.c */

    int ret = -1;
    int default_primary_vol = NO_INSTANCE;
    int default_primary_vol_prio = NO_INSTANCE;

    struct primary_volume_type_info* p_volume = NULL;
    struct primary_volume_type_info* n_p_volume = NULL;
    struct primary_volume_type_info* new_volume = NULL;

    /* descending order */
    int priority[] = {
        AUDIO_PRIMARY_VOLUME_TYPE_SYSTEM,
        AUDIO_PRIMARY_VOLUME_TYPE_NOTIFICATION,
        AUDIO_PRIMARY_VOLUME_TYPE_ALARM,
        AUDIO_PRIMARY_VOLUME_TYPE_RINGTONE,
        AUDIO_PRIMARY_VOLUME_TYPE_MEDIA,
        AUDIO_PRIMARY_VOLUME_TYPE_VOICE,
        AUDIO_PRIMARY_VOLUME_TYPE_CALL,
        AUDIO_PRIMARY_VOLUME_TYPE_VOIP,
        AUDIO_PRIMARY_VOLUME_TYPE_FIXED,
        AUDIO_PRIMARY_VOLUME_TYPE_MAX /* for capture handle */
    };

    if(is_new) {
        new_volume = pa_xnew0(struct primary_volume_type_info, 1);
        new_volume->key = key;
        new_volume->volumetype = volumetype;
        new_volume->priority = priority[volumetype];

        /* no items */
        if(m->primary_volume == NULL) {
            PA_LLIST_PREPEND(struct primary_volume_type_info, m->primary_volume, new_volume);
        } else {
            /* already added */
            PA_LLIST_FOREACH_SAFE(p_volume, n_p_volume, m->primary_volume) {
                if(p_volume->key == key) {
                    ret = 0;
                    pa_xfree(new_volume);
                    goto exit;
                }
            }

            /* add item */
            PA_LLIST_FOREACH_SAFE(p_volume, n_p_volume, m->primary_volume) {
                if(p_volume->priority <= priority[volumetype]) {
                    PA_LLIST_INSERT_AFTER(struct primary_volume_type_info, m->primary_volume, p_volume, new_volume);
                    break;
                } else if(p_volume->priority > priority[volumetype]) {
                    PA_LLIST_PREPEND(struct primary_volume_type_info, m->primary_volume, new_volume);
                    break;
                }
            }
        }
        pa_log_info("add volume data to primary volume list. volumetype(%d), priority(%d)", new_volume->volumetype, new_volume->priority);
    } else { /* remove(unlink) */
        PA_LLIST_FOREACH_SAFE(p_volume, n_p_volume, m->primary_volume) {
            if(p_volume->key == key) {
                PA_LLIST_REMOVE(struct primary_volume_type_info, m->primary_volume, p_volume);
                pa_log_info("remove volume data from primary volume list. volumetype(%d), priority(%d)", p_volume->volumetype, p_volume->priority);
                pa_xfree(p_volume);
                break;
            }
        }
    }

    if(m->primary_volume) {
        if(m->primary_volume->volumetype == AUDIO_PRIMARY_VOLUME_TYPE_MAX) {
            default_primary_vol = CAPURE_ONLY;
            default_primary_vol_prio = CAPURE_ONLY;
        } else {
            default_primary_vol = m->primary_volume->volumetype;
            default_primary_vol_prio = m->primary_volume->priority;
        }
    }
    pa_log_info("current primary volumetype(%d), priority(%d)", default_primary_vol, default_primary_vol_prio);

    if(vconf_set_int(VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE, default_primary_vol) < 0) {
        ret = -1;
        pa_log_info("VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE set failed default_primary_vol(%d)", default_primary_vol);
    }

exit:

    return ret;
}
#endif

static int get_volume_value(pa_stream_manager *m, stream_type stream_type, pa_bool_t is_hal_volume, const char *volume_type, uint32_t volume_level, double *volume_value) {
    int ret = 0;
    double volume_linear = 1.0f;

    pa_assert(m);
    pa_assert(volume_type);
    pa_assert(volume_value);

    /* Get volume value by type & level */
    if (is_hal_volume) {
        /* Get value from HAL */
        if (pa_hal_manager_get_volume_value(m->hal, NULL, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level, &volume_linear)) {
            ret = -1;
            goto FAILURE;
        }
    } else {
        volume_info *v = NULL;
        /* Get value from stream-manager */
        v = pa_hashmap_get((stream_type == STREAM_SINK_INPUT)?m->volume_map.out_volumes:m->volume_map.in_volumes, volume_type);
        if (v && v->idx_volume_values) {
            double *value = NULL;
            value = pa_idxset_get_by_index(v->idx_volume_values, volume_level);
            if (value)
                volume_linear = *value;
            else {
                pa_log_error("failed to pa_idxset_get_by_index()");
                ret = -1;
                goto FAILURE;
            }
        } else {
            pa_log_error("could not get volume value for type[%s],level[%u]", volume_type, volume_level);
            ret = -1;
            goto FAILURE;
        }
    }

    *volume_value = volume_linear;

    pa_log_debug("get_volume_value() : stream_type[%d], volume_type[%s], level[%u], value[%f]",
            stream_type, volume_type, volume_level, volume_linear);
FAILURE:
    return ret;
}

static int32_t set_volume_level_by_type(pa_stream_manager *m, stream_type stream_type, const char *volume_type, uint32_t volume_level) {
    pa_bool_t is_hal_volume = FALSE;
    volume_info *v = NULL;
    double volume_linear = 1.0f;
    uint32_t idx = 0;
    const char *volume_type_str = NULL;
    void *s = NULL;
    pa_assert(m);
    pa_assert(volume_type);

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type, stream_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), stream_type(%d), volume_type(%s)", stream_type, volume_type);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_volume_level(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
            return -1;

    if (get_volume_value(m, stream_type, is_hal_volume, volume_type, volume_level, &volume_linear))
        return -1;;

    v = pa_hashmap_get((stream_type==STREAM_SINK_INPUT)?m->volume_map.out_volumes:m->volume_map.in_volumes, volume_type);
    if (v && (v->current_level != volume_level))
        v->current_level = volume_level;

    PA_IDXSET_FOREACH(s, stream_type==STREAM_SINK_INPUT?m->core->sink_inputs:m->core->source_outputs, idx) {
        if ((volume_type_str = pa_proplist_gets(stream_type==STREAM_SINK_INPUT?((pa_sink_input*)s)->proplist:((pa_source_output*)s)->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
            /* do nothing */
        } else {
            continue;
        }
        /* Update volume level of stream if it has requested the volume type */
        if (pa_streq(volume_type_str, volume_type)) {
            pa_cvolume cv;
            pa_cvolume_set(&cv, stream_type==STREAM_SINK_INPUT?((pa_sink_input*)s)->sample_spec.channels:((pa_source_output*)s)->sample_spec.channels, pa_sw_volume_from_linear(volume_linear));
            if (stream_type == STREAM_SINK_INPUT)
                pa_sink_input_set_volume((pa_sink_input*)s, &cv, TRUE, TRUE);
            else if (stream_type == STREAM_SOURCE_OUTPUT)
                pa_source_output_set_volume((pa_source_output*)s, &cv, TRUE, TRUE);
        }
    }

    pa_log_debug("set_volume_level_by_type() : stream_type[%d], volume_type[%s], level[%u], value[%f]",
            stream_type, volume_type, volume_level, volume_linear);

    return 0;
}

int32_t set_volume_level_by_idx(pa_stream_manager *m, stream_type stream_type, uint32_t idx, uint32_t volume_level) {
    pa_bool_t is_hal_volume = FALSE;
    void *s = NULL;
    pa_cvolume cv;
    double volume_linear = 1.0f;
    const char *volume_type_str = NULL;

    pa_assert(m);

    s = pa_idxset_get_by_index(stream_type==STREAM_SINK_INPUT?m->core->sink_inputs:m->core->source_outputs, idx);
    if ((volume_type_str = pa_proplist_gets(stream_type==STREAM_SINK_INPUT?((pa_sink_input*)s)->proplist:((pa_source_output*)s)->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
        /* do nothing */
    } else
        return -1;

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type_str, stream_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), stream_type(%d), volume_type(%s)", stream_type, volume_type_str);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_volume_level(m->hal, volume_type_str, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
            return -1;

    if (!get_volume_value(m, stream_type, is_hal_volume, volume_type_str, volume_level, &volume_linear)) {
        pa_cvolume_set(&cv, (stream_type==STREAM_SINK_INPUT)?((pa_sink_input*)s)->sample_spec.channels:((pa_source_output*)s)->sample_spec.channels, pa_sw_volume_from_linear(volume_linear));
        if (stream_type == STREAM_SINK_INPUT)
            pa_sink_input_set_volume((pa_sink_input*)s, &cv, TRUE, TRUE);
        else if (stream_type == STREAM_SOURCE_OUTPUT)
            pa_source_output_set_volume((pa_source_output*)s, &cv, TRUE, TRUE);
    }
    pa_log_debug("set_volume_level_by_idx() : stream_type[%d], idx[%u]=>volume_type[%s], level[%u], value[%f]",
            stream_type, idx, volume_type_str, volume_level, volume_linear);

    return 0;
}

static int32_t get_volume_level_by_type(pa_stream_manager *m, pa_volume_get_command_t command, stream_type stream_type, const char *volume_type, uint32_t *volume_level) {
    int32_t ret = 0;
    pa_bool_t is_hal_volume = FALSE;
    pa_assert(m);
    pa_assert(volume_type);

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type, stream_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), stream_type(%d), volume_type(%s)", stream_type, volume_type);
        return -1;
     }

    if (command == GET_VOLUME_CURRENT_LEVEL) {
        if (is_hal_volume) {
            /* Get level from HAL */
            if (pa_hal_manager_get_volume_level(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
                ret = -1;
        } else {
            /* Get level from stream-manager */
            volume_info *v = pa_hashmap_get((stream_type==STREAM_SINK_INPUT)?m->volume_map.out_volumes:m->volume_map.in_volumes, volume_type);
            if (v)
                *volume_level = v->current_level;
        }
    } else if (command == GET_VOLUME_MAX_LEVEL) {
        if (is_hal_volume) {
            /* Get level from HAL */
            if (pa_hal_manager_get_volume_level_max(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
                ret = -1;
        } else {
            /* Get level from stream-manager */
            volume_info *v = pa_hashmap_get((stream_type==STREAM_SINK_INPUT)?m->volume_map.out_volumes:m->volume_map.in_volumes, volume_type);
            if (v && v->idx_volume_values)
                *volume_level = pa_idxset_size(v->idx_volume_values);
        }
    }

    return ret;
}

static void dump_volume_map (pa_stream_manager *m) {
    volume_info *s = NULL;
    char *volume_type = NULL;
    char *modifier_gain = NULL;
    double *level = NULL;
    double *gain_value = NULL;
    void *state = NULL;
    uint32_t idx = 0;
    pa_assert(m);
    pa_log_debug("==========[START volume-map dump]==========");
    while (m->volume_map.in_volumes && (s = pa_hashmap_iterate(m->volume_map.in_volumes, &state, &volume_type))) {
        if (s) {
            pa_log_debug("[in volume_type : %s]", volume_type);
            pa_log_debug("  - is_hal_volume_type : %d", s->is_hal_volume_type);
            if (s->idx_volume_values) {
                pa_log_debug("  - max level          : %u", pa_idxset_size(s->idx_volume_values));
                PA_IDXSET_FOREACH(level, s->idx_volume_values, idx)
                    pa_log_debug("  - value[%u]          : %f", idx, *level);
            }
        }
    }
    state = NULL;
    while (m->volume_map.out_volumes && (s = pa_hashmap_iterate(m->volume_map.out_volumes, &state, &volume_type))) {
        if (s) {
            pa_log_debug("[out volume_type : %s]", volume_type);
            pa_log_debug("  - is_hal_volume_type : %d", s->is_hal_volume_type);
            if (s->idx_volume_values) {
                pa_log_debug("  - max level          : %u", pa_idxset_size(s->idx_volume_values));
                PA_IDXSET_FOREACH(level, s->idx_volume_values, idx)
                    pa_log_debug("  - value[%u]          : %f", idx, *level);
            }
        }
    }
    state = NULL;
    while (m->volume_map.in_modifier_gains && (gain_value = pa_hashmap_iterate(m->volume_map.in_modifier_gains, &state, &modifier_gain)))
        pa_log_debug("[in modifier gain:%s, value:%f]", modifier_gain, *gain_value);
    state = NULL;
    while (m->volume_map.out_modifier_gains && (gain_value = pa_hashmap_iterate(m->volume_map.out_modifier_gains, &state, &modifier_gain)))
        pa_log_debug("[out modifier gain:%s, value:%f]", modifier_gain, *gain_value);
    pa_log_debug("===========[END stream-map dump]===========");

    return;
}

int init_volume_map (pa_stream_manager *m) {
    int ret = 0;
    int i = 0;
    void *state = NULL;
    stream_info *s = NULL;
    volume_info *v = NULL;
    pa_assert(m);

    m->volume_map.in_volumes = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    m->volume_map.out_volumes = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    if(m->stream_map) {
        PA_HASHMAP_FOREACH(s, m->stream_map, state) {
            for (i = STREAM_DIRECTION_IN; i < STREAM_DIRECTION_MAX; i++) {
                if (s->volume_type[i]) {
                    v = pa_hashmap_get((i==STREAM_DIRECTION_IN)?m->volume_map.in_volumes:m->volume_map.out_volumes, s->volume_type[i]);
                    if (v) {
                        if (s->is_hal_volume[i])
                            v->is_hal_volume_type = s->is_hal_volume[i];
                    } else {
                        v = pa_xmalloc0(sizeof(volume_info));
                        if (v) {
                            v->is_hal_volume_type = s->is_hal_volume[i];
                            v->idx_volume_values = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
                            pa_hashmap_put((i==STREAM_DIRECTION_IN)?m->volume_map.in_volumes:m->volume_map.out_volumes, s->volume_type[i], v);
                        } else {
                            pa_log_error("failed to pa_xmalloc0()");
                            ret = -1;
                            goto FAILURE;
                        }
                    }
                }
            }
        }
    } else {
        pa_log_error("stream_map has not been ready..");
        ret = -1;
        goto FAILURE;
    }
    /* For now, we only care about volumes for the output stream */
    ret = load_out_volume_and_gain_table_from_ini(m);

    dump_volume_map(m);

    /* Apply initial output volume level from vconf volume level */
    {
        #define VCONF_ADDR_LEN 64
        const char *volume_type = NULL;
        void *state = NULL;
        while ((v = pa_hashmap_iterate(m->volume_map.out_volumes, &state, &volume_type))) {
            int level = 5;
            char vconf_vol_type_addr[VCONF_ADDR_LEN] = {0,};
            pa_snprintf(vconf_vol_type_addr, VCONF_ADDR_LEN, "%s%s", VCONFKEY_OUT_VOLUME_PREFIX, volume_type);
            if (vconf_get_int(vconf_vol_type_addr, &level))
            pa_log_error("failed to get volume level of the vconf[%s]",vconf_vol_type_addr);
            set_volume_level_by_type(m, STREAM_SINK_INPUT, volume_type, (uint32_t)level);
            pa_log_debug("type(%s), current level(%u)", volume_type, v->current_level);
        }
    }
#if 0
    /* Apply initial output volume mute from vconf volume mute */
    {

    }
#endif
#ifdef PRIMARY_VOLUME
    vconf_set_int (VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE, -1);
#endif
FAILURE:
    return ret;
}

void deinit_volume_map (pa_stream_manager *m) {
    volume_info *v = NULL;
    void *state = NULL;
    uint32_t idx = 0;
    double *level = NULL;
    double *gain = NULL;
    pa_assert(m);

    if (m->volume_map.in_volumes) {
        PA_HASHMAP_FOREACH(v, m->volume_map.in_volumes, state) {
            PA_IDXSET_FOREACH(level, v->idx_volume_values, idx)
                pa_xfree(level);
            pa_idxset_free(v->idx_volume_values, NULL);
            pa_xfree(v);
        }
        pa_hashmap_free(m->volume_map.in_volumes);
    }
    if (m->volume_map.out_volumes) {
        PA_HASHMAP_FOREACH(v, m->volume_map.out_volumes, state) {
            PA_IDXSET_FOREACH(level, v->idx_volume_values, idx)
                pa_xfree(level);
            pa_idxset_free(v->idx_volume_values, NULL);
            pa_xfree(v);
        }
        pa_hashmap_free(m->volume_map.out_volumes);
    }
    if (m->volume_map.in_modifier_gains) {
        PA_HASHMAP_FOREACH(gain, m->volume_map.in_modifier_gains, state)
            pa_xfree(gain);
        pa_hashmap_free(m->volume_map.in_modifier_gains);
    }
    if (m->volume_map.out_modifier_gains) {
        PA_HASHMAP_FOREACH(gain, m->volume_map.out_modifier_gains, state)
            pa_xfree(gain);
        pa_hashmap_free(m->volume_map.out_modifier_gains);
    }

    return;
}

int32_t pa_stream_manager_volume_get_max_level(pa_stream_manager *m, stream_type stream_type, const char *volume_type, uint32_t *volume_level) {
    int32_t ret = 0;
    pa_assert(m);
    pa_assert(volume_type);

    ret = get_volume_level_by_type(m, GET_VOLUME_MAX_LEVEL, stream_type, volume_type, volume_level);

    pa_log_info("pa_stream_manager_volume_get_max_level, type:%s max_level:%u, ret:%d", volume_type, *volume_level, ret);

    return ret;
}

int32_t pa_stream_manager_volume_get_level(pa_stream_manager *m, stream_type stream_type, const char *volume_type, uint32_t *volume_level) {
    int32_t ret = 0;
    pa_assert(m);
    pa_assert(volume_type);

    ret = get_volume_level_by_type(m, GET_VOLUME_CURRENT_LEVEL, stream_type, volume_type, volume_level);

    pa_log_info("pa_stream_manager_volume_get_level, type:%s level:%u, ret:%d", volume_type, *volume_level, ret);

    return ret;
}

int32_t pa_stream_manager_volume_set_level(pa_stream_manager *m, stream_type stream_type, const char *volume_type, uint32_t volume_level) {
    int32_t ret = 0;
    pa_assert(m);
    pa_assert(volume_type);

    ret = set_volume_level_by_type(m, stream_type, volume_type, volume_level);

    pa_log_info("	pa_stream_manager_volume_set_level, type:%s level:%u, ret:%d", volume_type, volume_level, ret);

    return ret;
}

int32_t pa_stream_manager_volume_get_mute(pa_stream_manager *m, stream_type stream_type, const char *volume_type, pa_bool_t *mute) {
    pa_bool_t is_hal_volume = FALSE;
    pa_sink_input *si = NULL;
    pa_source_output *so = NULL;
    uint32_t idx;
    const char *volume_type_str;

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type, stream_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), stream_type(%d), volume_type(%s)", stream_type, volume_type);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_get_mute(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, (uint32_t*)mute))
            return -1;

    if (stream_type == STREAM_SINK_INPUT) {
        PA_IDXSET_FOREACH(si, m->core->sink_inputs, idx) {
            if ((volume_type_str = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
                /* do nothing */
            } else {
                continue;
            }
            /* Update mute of stream if it has requested the volume type */
            if (pa_streq(volume_type_str, volume_type)) {
                *mute = si->muted;
                break;
            }
        }
    } else if (stream_type == STREAM_SOURCE_OUTPUT) {
        PA_IDXSET_FOREACH(so, m->core->source_outputs, idx) {
            if ((volume_type_str = pa_proplist_gets(so->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
                /* do nothing */
            } else {
                continue;
            }
            /* Update mute of stream if it has requested the volume type */
            if (pa_streq(volume_type_str, volume_type)) {
                *mute = so->muted;
                break;
            }
        }
    }

    pa_log_info("pa_stream_manager_volume_get_mute, stream_type:%d volume_type:%s mute:%d", stream_type, volume_type, *mute);

    return 0;
}

int32_t pa_stream_manager_volume_set_mute(pa_stream_manager *m, stream_type stream_type, const char *volume_type, pa_bool_t mute) {
    pa_bool_t is_hal_volume = FALSE;
    void *s = NULL;
    uint32_t idx;
    const char *volume_type_str;

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type, stream_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), stream_type(%d), volume_type(%s)", stream_type, volume_type);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_mute(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, (uint32_t)mute))
            return -1;

    PA_IDXSET_FOREACH(s, (stream_type==STREAM_SINK_INPUT)?m->core->sink_inputs:m->core->source_outputs, idx) {
        if ((volume_type_str = pa_proplist_gets((stream_type==STREAM_SINK_INPUT)?((pa_sink_input*)s)->proplist:((pa_source_output*)s)->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
            /* do nothing */
        } else {
            continue;
        }
        /* Update mute of stream if it has requested the volume type */
        if (pa_streq(volume_type_str, volume_type))
            if (stream_type == STREAM_SINK_INPUT)
                pa_sink_input_set_mute((pa_sink_input*)s, mute, TRUE);
            else if (stream_type == STREAM_SOURCE_OUTPUT)
                pa_source_output_set_mute((pa_source_output*)s, mute, TRUE);
    }

    pa_log_info("pa_stream_manager_volume_set_mute, stream_type:%d volume_type:%s mute:%d", stream_type, volume_type, mute);

    return 0;
}

int32_t pa_stream_manager_volume_get_mute_by_idx(pa_stream_manager *m, stream_type stream_type, uint32_t stream_idx, pa_bool_t *mute) {
    int32_t ret = 0;
    void *s = NULL;
    uint32_t idx = 0;

    PA_IDXSET_FOREACH(s, (stream_type==STREAM_SINK_INPUT)?m->core->sink_inputs:m->core->source_outputs, idx) {
        /* Update mute of the stream if it has requested idx */
        if (stream_idx == idx) {
            if (stream_type == STREAM_SINK_INPUT)
                *mute = ((pa_sink_input*)s)->muted;
            else if (stream_type == STREAM_SOURCE_OUTPUT)
                *mute = ((pa_source_output*)s)->muted;
            break;
        }
    }
    if (!s)
        ret = -1;

    pa_log_info("pa_stream_manager_volume_get_mute_by_idx, stream_type:%d stream_idx:%u mute:%d, ret:%d", stream_type, stream_idx, *mute, ret);

    return ret;
}

int32_t pa_stream_manager_volume_set_mute_by_idx(pa_stream_manager *m, stream_type stream_type, uint32_t stream_idx, pa_bool_t mute) {
    pa_bool_t is_hal_volume = FALSE;
    void *s = NULL;
    uint32_t idx = 0;
    const char *volume_type_str;

    pa_log_info("pa_stream_manager_volume_set_mute_by_idx, stream_type:%d stream_idx:%u mute:%d", stream_type, stream_idx, mute);

    if (stream_idx != (uint32_t)-1) {
        if ((s = pa_idxset_get_by_index((stream_type==STREAM_SINK_INPUT)?m->core->sink_inputs:m->core->source_outputs, stream_idx))) {
            if ((volume_type_str = pa_proplist_gets((stream_type==STREAM_SINK_INPUT)?((pa_sink_input*)s)->proplist:((pa_source_output*)s)->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
                /* do nothing */
            } else {
                pa_log_debug("stream[%d] doesn't have volume type", stream_idx);
                return -1;
            }
        } else {
            pa_log_warn("stream[%u] doesn't exist", stream_idx);
            return -1;
        }
    }

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type_str, stream_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), stream_type(%d), volume_type(%s)", stream_type, volume_type_str);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_mute(m->hal, volume_type_str, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, mute))
            return -1;

    PA_IDXSET_FOREACH(s, (stream_type==STREAM_SINK_INPUT)?m->core->sink_inputs:m->core->source_outputs, idx) {
        /* Update mute of the stream if it has requested idx */
        if (stream_idx == idx) {
            if (stream_type == STREAM_SINK_INPUT)
                pa_sink_input_set_mute((pa_sink_input*)s, mute, TRUE);
            else if (stream_type == STREAM_SOURCE_OUTPUT)
                pa_source_output_set_mute((pa_source_output*)s, mute, TRUE);
            break;
        }
    }

    return 0;
}
