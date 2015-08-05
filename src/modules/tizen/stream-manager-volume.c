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
#include <math.h>
#include "stream-manager-priv.h"
#include "stream-manager-volume.h"
#include "stream-manager-volume-priv.h"

#define VOLUME_INI_DEFAULT_PATH     "/usr/etc/mmfw_audio_volume.ini"
#define VOLUME_INI_TUNED_PATH       "/opt/system/mmfw_audio_volume.ini"
#define DEFAULT_TABLE               "volumes"

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

static int load_out_volume_conf_file (pa_stream_manager *m) {
    int ret = 0;
    dictionary *dict = NULL;
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
    while ((v = pa_hashmap_iterate(m->volume_infos, &state, (const void**)&vol_type_str))) {
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
                    if (!v->values[STREAM_DIRECTION_OUT].idx_volume_values)
                        v->values[STREAM_DIRECTION_OUT].idx_volume_values = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
                    pa_idxset_put(v->values[STREAM_DIRECTION_OUT].idx_volume_values, vol_value, NULL);
                    token = strtok_r(NULL, delimiter, &ptr);
                }
            } else {
                pa_log_warn("[%s] is not defined, skip it", key);
            }
        } else {
            pa_log_error("failed to pa_xmalloc0()");
            ret = -1;
            goto FAILURE;
        }
        if (key) {
            free(key);
            key = NULL;
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
                if (!m->volume_modifiers)
                    m->volume_modifiers = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                pa_hashmap_put(m->volume_modifiers, (void*)gain_type_str, modifier_gain);
            } else {
                pa_log_warn("[%s] is not defined, skip it", key);
            }
        } else {
            pa_log_error("failed to pa_xmalloc0()");
            ret = -1;
            goto FAILURE;
        }
        if (key) {
            free(key);
            key = NULL;
        }
    }

FAILURE:
    if (key)
        free(key);
    if (dict)
        iniparser_freedict(dict);

    return ret;
}

static int is_hal_volume_by_type(pa_stream_manager *m, const char *volume_type, pa_bool_t *is_hal_volume) {
    volume_info *v = NULL;
    pa_assert(m);
    pa_assert(volume_type);
    pa_assert(is_hal_volume);

    if (m->volume_infos) {
        v = pa_hashmap_get(m->volume_infos, volume_type);
        if (v)
            *is_hal_volume = v->is_hal_volume_type;
        else
            return -1;
    } else
        return -1;

    return 0;
}

static int get_volume_value(pa_stream_manager *m, stream_type_t stream_type, pa_bool_t is_hal_volume, const char *volume_type, uint32_t volume_level, double *volume_value) {
    int ret = 0;
    double volume_linear = 1.0;
    pa_assert(m);
    pa_assert(volume_type);
    pa_assert(volume_value);

    /* Get volume value by type & level */
    if (is_hal_volume) {
        /* Get value from HAL */
        if (pa_hal_manager_get_volume_value(m->hal, volume_type, NULL, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level, &volume_linear)) {
            ret = -1;
            goto FAILURE;
        }
    } else {
        volume_info *v = NULL;
        void *volumes = m->volume_infos;
        /* Get value from stream-manager */
        if (volumes) {
            v = pa_hashmap_get(volumes, volume_type);
            if (v && v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].idx_volume_values) {
                double *value = NULL;
                value = pa_idxset_get_by_index(v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].idx_volume_values, volume_level);
                if (value) {
                    volume_linear = *value;
                    /* Apply master volume */
                    v = pa_hashmap_get(volumes, MASTER_VOLUME_TYPE);
                    if (v)
                        volume_linear *= (double)(v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].current_level)/100.0;
                } else {
                    pa_log_error("failed to pa_idxset_get_by_index()");
                    ret = -1;
                    goto FAILURE;
                }
            } else {
                pa_log_error("could not get volume value for stream_type[%d], volume_type[%s], level[%u]", stream_type, volume_type, volume_level);
                ret = -1;
                goto FAILURE;
            }
        } else {
            pa_log_error("could not get volumes in volume infos, stream_type(%d), volume_type(%s)", stream_type, volume_type);
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

int32_t set_volume_level_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t volume_level) {
    pa_bool_t is_hal_volume = FALSE;
    volume_info *v = NULL;
    double volume_linear = 1.0;
    double *modifier_gain_value = NULL;
    uint32_t idx = 0;
    const char *volume_type_str = NULL;
    const char *modifier_gain = NULL;
    void *s = NULL;
    pa_hashmap *volumes = NULL;
    pa_cvolume cv;
    pa_assert(m);
    pa_assert(volume_type);

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), volume_type(%s)", volume_type);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_volume_level(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
            return -1;

    volumes = m->volume_infos;
    if (volumes) {
        v = pa_hashmap_get(volumes, volume_type);
        if (v) {
            if (pa_streq(volume_type, MASTER_VOLUME_TYPE) && MASTER_VOLUME_LEVEL_MAX < volume_level) {
                pa_log_error("could not set volume level of MASTER type, out of range(%u)", volume_level);
                return -1;
            }
            v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].current_level = volume_level;
            if (is_hal_volume && pa_streq(volume_type, MASTER_VOLUME_TYPE)) {
                /* no need to update the value of pulseaudio stream */
                return 0;
            }
        } else {
            pa_log_error("could not get volume_info, stream_type(%d), volume_type(%s)", stream_type, volume_type);
            return -1;
        }
    } else {
        pa_log_error("could not get volumes in volume infos, stream_type(%d), volume_type(%s)", stream_type, volume_type);
        return -1;
    }

    if (!pa_streq(volume_type, MASTER_VOLUME_TYPE)) {
        if (get_volume_value(m, stream_type, is_hal_volume, volume_type, volume_level, &volume_linear))
            return -1;

        PA_IDXSET_FOREACH(s, stream_type==STREAM_SINK_INPUT?m->core->sink_inputs:m->core->source_outputs, idx) {
            if ((volume_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
                /* Get modifier for gain */
                modifier_gain = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_GAIN_TYPE);
            } else {
                continue;
            }
            /* Update volume level of stream if it has requested the volume type */
            if (pa_streq(volume_type_str, volume_type)) {
                if (modifier_gain) {
                    if (m->volume_modifiers) {
                       modifier_gain_value = pa_hashmap_get(m->volume_modifiers, modifier_gain);
                        if (modifier_gain_value) {
                            volume_linear *= (*modifier_gain_value);
                            pa_log_info("set_volume_level_by_type() : apply the modifier for the gain value(%s=>%f), result volume_linear(%f)",
                                modifier_gain, *modifier_gain_value, volume_linear);
                        }
                    }
                }
                pa_cvolume_set(&cv, GET_STREAM_SAMPLE_SPEC(s, stream_type).channels, pa_sw_volume_from_linear(volume_linear));
                if (stream_type == STREAM_SINK_INPUT)
                    pa_sink_input_set_volume((pa_sink_input*)s, &cv, TRUE, TRUE);
                else if (stream_type == STREAM_SOURCE_OUTPUT)
                    pa_source_output_set_volume((pa_source_output*)s, &cv, TRUE, TRUE);
            }
        }
    } else {
        PA_IDXSET_FOREACH(s, stream_type==STREAM_SINK_INPUT?m->core->sink_inputs:m->core->source_outputs, idx) {
            if ((volume_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
                /* Get modifier for gain */
                modifier_gain = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_GAIN_TYPE);
                /* Check if it is related to HAL volume */
                if (is_hal_volume_by_type(m, volume_type_str, &is_hal_volume)) {
                    pa_log_error("failed to is_hal_volume_by_type(), volume_type(%s)", volume_type_str);
                    continue;
                 }
                /* Get volume level of this type */
                v = pa_hashmap_get(volumes, volume_type_str);
                if (v)
                    volume_level = v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].current_level;
                else
                    continue;
            } else {
                continue;
            }
            if (get_volume_value(m, stream_type, is_hal_volume, volume_type_str, volume_level, &volume_linear))
                continue;

            if (modifier_gain) {
                if (m->volume_modifiers) {
                   modifier_gain_value = pa_hashmap_get(m->volume_modifiers, modifier_gain);
                    if (modifier_gain_value) {
                        volume_linear *= (*modifier_gain_value);
                        pa_log_info("set_volume_level_by_type() : apply the modifier for the gain value(%s=>%f), result volume_linear(%f)",
                            modifier_gain, *modifier_gain_value, volume_linear);
                    }
                }
            }
            pa_cvolume_set(&cv, GET_STREAM_SAMPLE_SPEC(s, stream_type).channels, pa_sw_volume_from_linear(volume_linear));
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

int32_t get_volume_level_by_type(pa_stream_manager *m, pa_volume_get_command_t command, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level) {
    int32_t ret = 0;
    pa_bool_t is_hal_volume = FALSE;
    pa_hashmap *volumes = NULL;
    pa_assert(m);
    pa_assert(volume_type);

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), volume_type(%s)", volume_type);
        return -1;
    }

    if (command == GET_VOLUME_CURRENT_LEVEL) {
        /* Get level */
        if (is_hal_volume) {
            /* from HAL */
            if (pa_hal_manager_get_volume_level(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
                ret = -1;
        } else {
            /* from stream-manager */
            volumes = m->volume_infos;
            if (volumes) {
                volume_info *v = pa_hashmap_get(volumes, volume_type);
                if (v)
                    *volume_level = v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].current_level;
                else {
                    pa_log_error("could not get volume_info, stream_type(%d), volume_type(%s)", stream_type, volume_type);
                    return -1;
                }
            } else {
                pa_log_error("could not get volumes in volume infos, stream_type(%d), volume_type(%s)", stream_type, volume_type);
                return -1;
            }
        }
    } else if (command == GET_VOLUME_MAX_LEVEL) {
        /* If it is the master volume type */
        if (pa_streq(volume_type, MASTER_VOLUME_TYPE)) {
            *volume_level = MASTER_VOLUME_LEVEL_MAX;
        } else {
            /* Get max level */
            if (is_hal_volume) {
                /* from HAL */
                if (pa_hal_manager_get_volume_level_max(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
                    ret = -1;
            } else {
                /* from stream-manager */
                volumes = m->volume_infos;
                if (volumes) {
                    volume_info *v = pa_hashmap_get(volumes, volume_type);
                    if (v && v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].idx_volume_values)
                        *volume_level = pa_idxset_size(v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].idx_volume_values);
                    else {
                        pa_log_error("could not get volume_info, stream_type(%d), volume_type(%s)", stream_type, volume_type);
                        return -1;
                    }
                } else {
                    pa_log_error("could not get volumes in volume infos, stream_type(%d), volume_type(%s)", stream_type, volume_type);
                    return -1;
                }
            }
        }
    }

    return ret;
}

int32_t set_volume_level_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t idx, uint32_t volume_level) {
    pa_bool_t is_hal_volume = FALSE;
    void *s = NULL;
    pa_cvolume cv;
    double volume_linear = 1.0;
    const char *volume_type_str = NULL;
    const char *modifier_gain = NULL;
    pa_assert(m);

    s = pa_idxset_get_by_index(stream_type==STREAM_SINK_INPUT?m->core->sink_inputs:m->core->source_outputs, idx);
    if ((volume_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
        /* Get modifier for gain */
        modifier_gain = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_GAIN_TYPE);
    } else {
        pa_log_debug("idx[%u] doesn't have volume type", idx);
        return -1;
    }

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type_str, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), volume_type(%s)", volume_type_str);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_volume_level(m->hal, volume_type_str, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
            return -1;

    if (!get_volume_value(m, stream_type, is_hal_volume, volume_type_str, volume_level, &volume_linear)) {
        if (modifier_gain) {
            double *modifier_gain_value = NULL;
            modifier_gain_value = pa_hashmap_get(m->volume_modifiers, modifier_gain);
            if (modifier_gain_value) {
                volume_linear *= (*modifier_gain_value);
                pa_log_info("set_volume_level_by_idx() : apply the modifier for the gain value(%s=>%f), result volume_linear(%f)",
                    modifier_gain, *modifier_gain_value, volume_linear);
            }
        }
        pa_cvolume_set(&cv, GET_STREAM_SAMPLE_SPEC(s, stream_type).channels, pa_sw_volume_from_linear(volume_linear));
        if (stream_type == STREAM_SINK_INPUT)
            pa_sink_input_set_volume((pa_sink_input*)s, &cv, TRUE, TRUE);
        else if (stream_type == STREAM_SOURCE_OUTPUT)
            pa_source_output_set_volume((pa_source_output*)s, &cv, TRUE, TRUE);
    }
    pa_log_debug("set_volume_level_by_idx() : stream_type[%d], idx[%u]=>volume_type[%s], level[%u], value[%f]",
            stream_type, idx, volume_type_str, volume_level, volume_linear);

    return 0;
}

int32_t set_volume_level_with_new_data(pa_stream_manager *m, stream_type_t stream_type, void *nd, uint32_t volume_level) {
    pa_bool_t is_hal_volume = FALSE;
    pa_cvolume cv;
    double volume_linear = 1.0;
    const char *volume_type_str = NULL;
    const char *modifier_gain = NULL;
    pa_assert(m);

    if ((volume_type_str = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(nd, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
        /* Get modifier for gain */
        modifier_gain = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(nd, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_GAIN_TYPE);
    } else {
        pa_log_debug("new_data[%p] doesn't have volume type", nd);
        return -1;
    }

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type_str, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), volume_type(%s)", volume_type_str);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_volume_level(m->hal, volume_type_str, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_level))
            return -1;

    if (!get_volume_value(m, stream_type, is_hal_volume, volume_type_str, volume_level, &volume_linear)) {
        if (modifier_gain) {
            double *modifier_gain_value = NULL;
            modifier_gain_value = pa_hashmap_get(m->volume_modifiers, modifier_gain);
            if (modifier_gain_value) {
                volume_linear *= (*modifier_gain_value);
                pa_log_info("set_volume_level_by_idx() : apply the modifier for the gain value(%s=>%f), result volume_linear(%f)",
                    modifier_gain, *modifier_gain_value, volume_linear);
            }
        }
        pa_cvolume_set(&cv, GET_STREAM_NEW_SAMPLE_SPEC(nd, stream_type).channels, pa_sw_volume_from_linear(volume_linear));
        if (stream_type == STREAM_SINK_INPUT)
            pa_sink_input_new_data_set_volume((pa_sink_input_new_data*)nd, &cv, TRUE);
        else if (stream_type == STREAM_SOURCE_OUTPUT)
            pa_source_output_new_data_set_volume((pa_source_output_new_data*)nd, &cv, TRUE);
    }
    pa_log_debug("set_volume_level_with_new_data() : stream_type[%d], volume_type[%s], level[%u], value[%f]",
            stream_type, volume_type_str, volume_level, volume_linear);

    return 0;
}

int32_t set_volume_mute_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, pa_bool_t volume_mute) {
    pa_bool_t is_hal_volume = FALSE;
    volume_info *v = NULL;
    void *s = NULL;
    pa_hashmap *volumes = NULL;
    uint32_t idx;
    const char *volume_type_str = NULL;
    pa_assert(m);
    pa_assert(volume_type);

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), volume_type(%s)", volume_type);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_mute(m->hal, volume_type, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, (uint32_t)volume_mute))
            return -1;

    /* Set mute */
    volumes = m->volume_infos;
    if (volumes) {
        v = pa_hashmap_get(volumes, volume_type);
        if (v)
            v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].is_muted = volume_mute;
        else {
            pa_log_error("could not get volume_info, stream_type(%d), volume_type(%s)", stream_type, volume_type);
            return -1;
        }
    } else {
        pa_log_error("could not get volumes in volume infos, volume_type(%s)", volume_type);
        return -1;
    }

    PA_IDXSET_FOREACH(s, (stream_type==STREAM_SINK_INPUT)?m->core->sink_inputs:m->core->source_outputs, idx) {
        if ((volume_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
            /* do nothing */
        } else {
            continue;
        }
        /* Update mute of stream if it has requested the volume type */
        if (pa_streq(volume_type_str, volume_type)) {
            if (stream_type == STREAM_SINK_INPUT)
                pa_sink_input_set_mute((pa_sink_input*)s, volume_mute, TRUE);
            else if (stream_type == STREAM_SOURCE_OUTPUT)
                pa_source_output_set_mute((pa_source_output*)s, volume_mute, TRUE);
        }
    }

    pa_log_info("pa_stream_manager_volume_set_mute, stream_type:%d volume_type:%s mute:%d", stream_type, volume_type, volume_mute);

    return 0;
}

int32_t get_volume_mute_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, pa_bool_t *volume_mute) {
    volume_info *v = NULL;
    pa_hashmap *volumes = NULL;
    pa_assert(m);
    pa_assert(volume_type);
    pa_assert(volume_mute);

    /* Get mute */
    volumes = m->volume_infos;
    if (volumes) {
        v = pa_hashmap_get(volumes, volume_type);
        if (v)
            *volume_mute = v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].is_muted;
        else {
            pa_log_error("could not get volume_info, stream_type(%d), volume_type(%s)", stream_type, volume_type);
            return -1;
        }
    } else {
        pa_log_error("could not get volumes in volume infos, volume_type(%s)", volume_type);
        return -1;
    }

    pa_log_info("pa_stream_manager_volume_get_mute, volume_type:%s mute:%d", volume_type, *volume_mute);

    return 0;
}

int32_t set_volume_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, pa_bool_t volume_mute) {
    void *s = NULL;
    uint32_t idx = 0;
    const char *volume_type_str = NULL;
    volume_info *v = NULL;
    pa_hashmap *volumes = NULL;
    pa_bool_t muted_by_type = FALSE;
    pa_assert(m);

    pa_log_info("set_volume_mute_by_idx, stream_type:%d stream_idx:%u mute:%d", stream_type, stream_idx, volume_mute);

    if (stream_idx != (uint32_t)-1) {
        if ((s = pa_idxset_get_by_index((stream_type==STREAM_SINK_INPUT)?m->core->sink_inputs:m->core->source_outputs, stream_idx))) {
            if ((volume_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
                /* do nothing */
            } else {
                pa_log_warn("stream[%d] doesn't have volume type", stream_idx);
            }
        } else {
            pa_log_warn("stream[%u] doesn't exist", stream_idx);
        }
    }

    /* Get mute state of the volume type of this stream */
    if (volume_type_str) {
        volumes = m->volume_infos;
        if (volumes) {
            v = pa_hashmap_get(volumes, volume_type_str);
            if (v)
                muted_by_type = v->values[(stream_type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].is_muted;
        } else {
            pa_log_error("could not get volumes in volume map, stream_type(%d), volume_type(%s)", stream_type, volume_type_str);
            return -1;
        }
    }

    if (!muted_by_type) {
        PA_IDXSET_FOREACH(s, (stream_type==STREAM_SINK_INPUT)?m->core->sink_inputs:m->core->source_outputs, idx) {
            /* Update mute of the stream if it has requested idx */
            if (stream_idx == idx) {
                if (stream_type == STREAM_SINK_INPUT)
                    pa_sink_input_set_mute((pa_sink_input*)s, volume_mute, TRUE);
                else if (stream_type == STREAM_SOURCE_OUTPUT)
                    pa_source_output_set_mute((pa_source_output*)s, volume_mute, TRUE);
                break;
            }
        }
    }

    return 0;
}

int32_t get_volume_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, pa_bool_t *volume_mute) {
    int32_t ret = 0;
    void *s = NULL;
    uint32_t idx = 0;
    pa_assert(m);
    pa_assert(volume_mute);

    PA_IDXSET_FOREACH(s, (stream_type==STREAM_SINK_INPUT)?m->core->sink_inputs:m->core->source_outputs, idx) {
        /* Update mute of the stream if it has requested idx */
        if (stream_idx == idx) {
            if (stream_type == STREAM_SINK_INPUT)
                *volume_mute = ((pa_sink_input*)s)->muted;
            else if (stream_type == STREAM_SOURCE_OUTPUT)
                *volume_mute = ((pa_source_output*)s)->muted;
            break;
        }
    }
    if (!s)
        ret = -1;

    pa_log_info("get_volume_mute_by_idx, stream_type:%d stream_idx:%u mute:%d, ret:%d", stream_type, stream_idx, *volume_mute, ret);

    return ret;
}

int32_t set_volume_mute_with_new_data(pa_stream_manager *m, stream_type_t stream_type, void *nd, pa_bool_t volume_mute) {
    pa_bool_t is_hal_volume = FALSE;
    const char *volume_type_str = NULL;
    pa_assert(m);

    pa_log_info("set_volume_mute_with_new_data, stream_type:%d mute:%d", stream_type, volume_mute);

    if ((volume_type_str = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(nd, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
        /* do nothing */
    } else {
        pa_log_debug("new_data[%p] doesn't have volume type", nd);
        return -1;
    }

    /* Check if it is related to HAL volume */
    if (is_hal_volume_by_type(m, volume_type_str, &is_hal_volume)) {
        pa_log_error("failed to is_hal_volume_by_type(), volume_type(%s)", volume_type_str);
        return -1;
     }

    if (is_hal_volume)
        if (pa_hal_manager_set_mute(m->hal, volume_type_str, (stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN, volume_mute))
            return -1;

    if (stream_type == STREAM_SINK_INPUT)
        pa_sink_input_new_data_set_muted((pa_sink_input_new_data*)nd, volume_mute);
    else if (stream_type == STREAM_SOURCE_OUTPUT)
        pa_source_output_new_data_set_muted((pa_source_output_new_data*)nd, volume_mute);

    return 0;
}

static void dump_volumes (pa_stream_manager *m) {
    volume_info *s = NULL;
    const char *volume_type = NULL;
    const char *modifier_gain = NULL;
    double *level = NULL;
    double *gain_value = NULL;
    void *state = NULL;
    uint32_t idx = 0;
    pa_assert(m);

    pa_log_debug("==========[START volume infos dump]==========");
    while (m->volume_infos && (s = pa_hashmap_iterate(m->volume_infos, &state, (const void**)&volume_type))) {
        if (s) {
            pa_log_debug("[volume_type : %s]", volume_type);
            pa_log_debug("  - is_hal_volume_type : %d", s->is_hal_volume_type);
            if (s->values[STREAM_DIRECTION_IN].idx_volume_values) {
                pa_log_debug("  - (in)max level          : %u", pa_idxset_size(s->values[STREAM_DIRECTION_IN].idx_volume_values));
                PA_IDXSET_FOREACH(level, s->values[STREAM_DIRECTION_IN].idx_volume_values, idx)
                    pa_log_debug("  - (in)value[%u]          : %f", idx, *level);
            }
            if (s->values[STREAM_DIRECTION_OUT].idx_volume_values) {
                pa_log_debug("  - (out)max level          : %u", pa_idxset_size(s->values[STREAM_DIRECTION_OUT].idx_volume_values));
                PA_IDXSET_FOREACH(level, s->values[STREAM_DIRECTION_OUT].idx_volume_values, idx)
                    pa_log_debug("  - (out)value[%u]          : %f", idx, *level);
            }
        }
    }
    state = NULL;
    while (m->volume_modifiers && (gain_value = pa_hashmap_iterate(m->volume_modifiers, &state, (const void**)&modifier_gain)))
        pa_log_debug("[modifier gain:%s, value:%f]", modifier_gain, *gain_value);
    pa_log_debug("===========[END volume infos dump]===========");

    return;
}

int32_t init_volumes (pa_stream_manager *m) {
    int ret = 0;
    void *state = NULL;
    volume_info *v = NULL;
    pa_assert(m);

    /* For now, we only care about volumes for the output stream */
    ret = load_out_volume_conf_file(m);
    if (ret)
        pa_log_error("failed to load_out_volume_conf_file(), ret[%d]", ret);

    dump_volumes(m);

    /* Apply initial output volume level from vconf volume level */
    {
        #define VCONF_ADDR_LEN 64
        char vconf_vol_type_addr[VCONF_ADDR_LEN] = {0,};
        const char *volume_type = NULL;
        int level = 10;
        state = NULL;
        while ((v = pa_hashmap_iterate(m->volume_infos, &state, (const void**)&volume_type))) {
            memset(vconf_vol_type_addr, 0, VCONF_ADDR_LEN);
            pa_snprintf(vconf_vol_type_addr, VCONF_ADDR_LEN, "%s%s", VCONFKEY_OUT_VOLUME_PREFIX, volume_type);
            if (vconf_get_int(vconf_vol_type_addr, &level))
                pa_log_error("failed to get volume level of the vconf[%s]", vconf_vol_type_addr);
            else {
                set_volume_level_by_type(m, STREAM_SINK_INPUT, volume_type, (uint32_t)level);
                pa_log_debug("type(%s), current level(%u)", volume_type, v->values[STREAM_DIRECTION_OUT].current_level);
            }
        }
    }
#if 0
    /* Apply initial output volume mute from vconf volume mute */
    {

    }
#endif
    if (ret)
        pa_log_error("Failed to initialize volumes");
    return ret;
}

void deinit_volumes (pa_stream_manager *m) {
    volume_info *v = NULL;
    void *state = NULL;
    uint32_t idx = 0;
    double *level = NULL;
    double *gain = NULL;
    int i = 0;
    pa_assert(m);

    if (m->volume_infos) {
        PA_HASHMAP_FOREACH(v, m->volume_infos, state) {
            for (i = 0; i < STREAM_DIRECTION_MAX; i++) {
                if (v->values[i].idx_volume_values) {
                    PA_IDXSET_FOREACH(level, v->values[i].idx_volume_values, idx)
                        pa_xfree(level);
                    pa_idxset_free(v->values[i].idx_volume_values, NULL);
                }
            }
        }
    }
    if (m->volume_modifiers) {
        PA_HASHMAP_FOREACH(gain, m->volume_modifiers, state)
            pa_xfree(gain);
        pa_hashmap_free(m->volume_modifiers);
    }

    return;
}

int32_t pa_stream_manager_volume_get_max_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level) {
    int32_t ret = 0;
    pa_assert(m);
    pa_assert(volume_type);

    ret = get_volume_level_by_type(m, GET_VOLUME_MAX_LEVEL, stream_type, volume_type, volume_level);

    pa_log_info("pa_stream_manager_volume_get_max_level, type:%s max_level:%u, ret:%d", volume_type, *volume_level, ret);

    return ret;
}

int32_t pa_stream_manager_volume_get_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level) {
    int32_t ret = 0;
    pa_assert(m);
    pa_assert(volume_type);

    ret = get_volume_level_by_type(m, GET_VOLUME_CURRENT_LEVEL, stream_type, volume_type, volume_level);

    pa_log_info("pa_stream_manager_volume_get_level, type:%s level:%u, ret:%d", volume_type, *volume_level, ret);

    return ret;
}

int32_t pa_stream_manager_volume_set_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t volume_level) {
    int32_t ret = 0;
    pa_assert(m);
    pa_assert(volume_type);

    ret = set_volume_level_by_type(m, stream_type, volume_type, volume_level);

    pa_log_info("	pa_stream_manager_volume_set_level, type:%s level:%u, ret:%d", volume_type, volume_level, ret);

    return ret;
}

int32_t pa_stream_manager_volume_get_mute(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, pa_bool_t *volume_mute) {
    return get_volume_mute_by_type(m, stream_type, volume_type, volume_mute);
}

int32_t pa_stream_manager_volume_set_mute(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, pa_bool_t volume_mute) {
    return set_volume_mute_by_type(m, stream_type, volume_type, volume_mute);
}

int32_t pa_stream_manager_volume_get_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, pa_bool_t *volume_mute) {
    return get_volume_mute_by_idx(m, stream_type, stream_idx, volume_mute);
}

int32_t pa_stream_manager_volume_set_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, pa_bool_t volume_mute) {
    return set_volume_mute_by_idx(m, stream_type, stream_idx, volume_mute);
}
