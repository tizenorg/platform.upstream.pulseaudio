#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <strings.h>
#include <vconf.h> // for mono
#include <iniparser.h>
#include <dlfcn.h>
#include <asoundlib.h>
#include <unistd.h>
#include <pthread.h>

#include <pulse/proplist.h>
#include <pulse/timeval.h>
#include <pulse/util.h>
#include <pulse/rtclock.h>

#include <pulsecore/core.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-scache.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/core-util.h>
#include <pulsecore/mutex.h>
#include <pulsecore/log.h>
#include <pulsecore/namereg.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/protocol-native.h>
#include <pulsecore/pstream-util.h>
#include <pulsecore/strbuf.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/sound-file.h>
#include <pulsecore/play-memblockq.h>
#include <pulsecore/shared.h>

#include "module-policy-symdef.h"
#include "tizen-audio.h"

#define VCONFKEY_SOUND_HDMI_SUPPORT "memory/private/sound/hdmisupport"
#ifdef PRIMARY_VOLUME
#define VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE "memory/private/sound/PrimaryVolumetype"
#endif

//To be changed
#ifndef VCONFKEY_SOUND_CAPTURE_STATUS
#define VCONFKEY_SOUND_CAPTURE_STATUS "memory/Sound/SoundCaptureStatus"
#endif
#define VCONFKEY_CALL_NOISE_REDUCTION_STATE_BOOL "memory/private/call/NoiseReduction"
#define VCONFKEY_CALL_EXTRA_VOLUME_STATE_BOOL "memory/private/call/ExtraVolume"
#define VCONFKEY_CALL_WBAMR_STATE_BOOL "memory/private/call/WBAMRState"



PA_MODULE_AUTHOR("Seungbae Shin");
PA_MODULE_DESCRIPTION("Media Policy module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);
PA_MODULE_USAGE(
        "on_hotplug=<When new device becomes available, recheck streams?> "
        "use_wideband_voice=<Set to 1 to enable wb voice. Default nb>"
        "fragment_size=<fragment size>"
        "tsched_buffer_size=<buffer size when using timer based scheduling> ");

static const char* const valid_modargs[] = {
    "on_hotplug",
    "use_wideband_voice",
    "fragment_size",
    "tsched_buffersize",
    "tsched_buffer_size",
    NULL
};

/* Audio HAL library */
#define LIB_TIZEN_AUDIO "libtizen-audio.so"

/* Tunning Value */
#define DEFAULT_TSCHED_BUFFER_SIZE 16384
#define START_THRESHOLD    4096
#ifdef TIZEN_MICRO
#define DEFAULT_FRAGMENT_SIZE 4096
#else
#define DEFAULT_FRAGMENT_SIZE 8192
#endif

/* Sink & Source names */
#define AEC_SINK            "alsa_output.0.analog-stereo.echo-cancel"
#define AEC_SOURCE          "alsa_input.0.analog-stereo.echo-cancel"
#define SINK_VOIP           "alsa_output.3.analog-stereo"
#define SINK_VIRTUAL        "alsa_output.virtual.analog-stereo"
#define ALSA_VIRTUAL_CARD   "VIRTUALAUDIO"
#define SOURCE_ALSA         "alsa_input.0.analog-stereo"
#define SOURCE_VIRTUAL      "alsa_input.virtual.analog-stereo"
#define SOURCE_VOIP         "alsa_input.3.analog-stereo"
#define SINK_ALSA           "alsa_output.0.analog-stereo"
#define SINK_ALSA_UHQA      "alsa_output.0.analog-stereo-uhqa"
#define SINK_COMBINED       "combined"
#define SINK_MONO_ALSA		"mono_alsa"
#define SINK_MONO_BT		"mono_bt"
#define SINK_MONO_COMBINED	"mono_combined"
#define SINK_HIGH_LATENCY   "alsa_output.4.analog-stereo"
#define SINK_HIGH_LATENCY_UHQA   "alsa_output.4.analog-stereo-uhqa"
#define SINK_HDMI           "alsa_output.1.analog-stereo"
#define SINK_HDMI_UHQA           "alsa_output.1.analog-stereo-uhqa"
#define SOURCE_MIRRORING    "alsa_input.8.analog-stereo"
#define ALSA_MONITOR_SOURCE "alsa_output.0.analog-stereo.monitor"


/* Policies */
#define POLICY_AUTO         "auto"
#define POLICY_AUTO_UHQA    "auto-uhqa"
#define POLICY_PHONE        "phone"
#define POLICY_ALL          "all"
#define POLICY_VOIP         "voip"
#define POLICY_HIGH_LATENCY "high-latency"
#define POLICY_HIGH_LATENCY_UHQA "high-latency-uhqa"
#define POLICY_MIRRORING    "mirroring"
#define POLICY_LOOPBACK    "loopback"

/* API */
#define BLUEZ_API           "bluez"
#define ALSA_API            "alsa"
#define HIGH_LATENCY_API    "high-latency"
#define NULL_SOURCE         "source.null"
#define ALSA_SAUDIOVOIP_CARD "saudiovoip"
#define MONO_KEY 			VCONFKEY_SETAPPL_ACCESSIBILITY_MONO_AUDIO

/* Sink Identify Macros */
#define sink_is_hdmi(sink) !strncmp(sink->name, SINK_HDMI, strlen(SINK_HDMI))
#define sink_is_highlatency(sink) !strncmp(sink->name, SINK_HIGH_LATENCY, strlen(SINK_HIGH_LATENCY))
#define sink_is_alsa(sink) !strncmp(sink->name, SINK_ALSA, strlen(SINK_ALSA))
#define sink_is_voip(sink) !strncmp(sink->name, SINK_VOIP, strlen(SINK_VOIP))

/* Channels */
#define CH_5_1 6
#define CH_7_1 8
#define CH_STEREO 2

/* UHQA */
/**
  * UHQA sampling rate vary from 96 KHz to 192 KHz, currently the plan is to configure sink with highest sampling
  * rate possible i.e. 192 KHz. So that < 192 KHz will be resampled and played. This will avoid creating multiple sinks
  * for multiple rates
  */
#define UHQA_SAMPLING_RATE 192000
#define UHQA_BASE_SAMPLING_RATE 96000

/* PCM Dump */
#define PA_DUMP_INI_DEFAULT_PATH                "/usr/etc/mmfw_audio_pcm_dump.ini"
#define PA_DUMP_INI_TEMP_PATH                   "/opt/system/mmfw_audio_pcm_dump.ini"
#define PA_DUMP_VCONF_KEY                       "memory/private/sound/pcm_dump"
#define PA_DUMP_PLAYBACK_DECODER_OUT            0x00000001
#define PA_DUMP_PLAYBACK_RESAMPLER_IN           0x00000008
#define PA_DUMP_PLAYBACK_RESAMPLER_OUT          0x00000010
#define PA_DUMP_CAPTURE_ENCODER_IN              0x80000000

/* check if this sink is bluez */

/* Vconf Keys */
#define DEFAULT_BOOTING_SOUND_PATH "/usr/share/keysound/poweron.wav"
#define VCONF_BOOTING "memory/private/sound/booting"
#ifdef BURST_SHOT
#define VCONF_SOUND_BURSTSHOT "memory/private/sound/burstshot"
#endif

/* boot sound */
#define BOOTING_SOUND_SAMPLE "booting"


typedef enum pa_hal_event_type {
    PA_HAL_EVENT_LOAD_DEVICE,
    PA_HAL_EVENT_OPEN_DEVICE,
    PA_HAL_EVENT_CLOSE_ALL_DEVICES,
    PA_HAL_EVENT_CLOSE_DEVICE,
    PA_HAL_EVENT_UNLOAD_DEVICE,
} pa_hal_event_type_t;

struct pa_hal_device_event_data {
    audio_device_info_t device_info;
    audio_device_param_info_t params[AUDIO_DEVICE_PARAM_MAX];
};

struct pa_hal_event {
    struct userdata *userdata;

    pa_hal_event_type_t event_type;
    void *event_data;

    pa_cond *cond;
    pa_mutex *mutex;

    PA_LLIST_FIELDS(struct pa_hal_event);
};

#ifdef PRIMARY_VOLUME
struct pa_primary_volume_type_info {
    void* key;
    int volumetype;
    int priority;
    PA_LLIST_FIELDS(struct pa_primary_volume_type_info);
};
#endif

struct userdata {
    pa_core *core;
    pa_module *module;

    pa_hook_slot *sink_input_new_hook_slot,*sink_put_hook_slot;

    pa_hook_slot *sink_input_unlink_slot,*sink_unlink_slot;
    pa_hook_slot *sink_input_put_slot;
    pa_hook_slot *sink_input_unlink_post_slot, *sink_unlink_post_slot;
    pa_hook_slot *sink_input_move_start_slot,*sink_input_move_finish_slot;
    pa_hook_slot *source_output_new_hook_slot, *source_output_unlink_post_slot;
    pa_hook_slot *source_output_put_slot;
    pa_hook_slot *sink_state_changed_slot;
    pa_hook_slot *sink_input_state_changed_slot;

    pthread_t tid;
    pa_defer_event *defer_event;
    PA_LLIST_HEAD(struct pa_hal_event, hal_event_queue);
    struct pa_hal_event *hal_event_last;

    pa_subscription *subscription;

    pa_bool_t on_hotplug:1;
    int bt_off_idx;

    uint32_t session;
    uint32_t subsession;
    uint32_t subsession_opt;
    uint32_t active_device_in;
    uint32_t active_device_out;
    uint32_t active_route_flag;

    int is_mono;
    float balance;
    int muteall;
    int call_muted;
    pa_bool_t wideband;
    int fragment_size;
    int tsched_buffer_size;
	pa_module* module_mono_bt;
    pa_module* module_combined;
    pa_module* module_mono_combined;
    pa_native_protocol *protocol;
//    pa_hook_slot *source_output_new_hook_slot;
#ifdef PRIMARY_VOLUME
    PA_LLIST_HEAD(struct pa_primary_volume_type_info, primary_volume);
#endif

    struct {
        void *dl_handle;
        void *data;
        audio_interface_t intf;
    } audio_mgr;

    struct  { // for burst-shot
        pa_bool_t is_running;
        pa_mutex* mutex;
        int count; /* loop count */
        pa_time_event *time_event;
        pa_scache_entry *e;
        pa_sink_input *i;
        pa_memblockq *q;
        pa_usec_t time_interval;
        pa_usec_t factor; /* timer boosting */
    } audio_sample_userdata;
};

enum {
    CUSTOM_EXT_3D_LEVEL,
    CUSTOM_EXT_BASS_LEVEL,
    CUSTOM_EXT_CONCERT_HALL_VOLUME,
    CUSTOM_EXT_CONCERT_HALL_LEVEL,
    CUSTOM_EXT_CLARITY_LEVEL,
    CUSTOM_EXT_PARAM_MAX
};

enum {
    SUBCOMMAND_TEST,
    SUBCOMMAND_PLAY_SAMPLE,
    SUBCOMMAND_PLAY_SAMPLE_CONTINUOUSLY,
    SUBCOMMAND_MONO,
    SUBCOMMAND_BALANCE,
    SUBCOMMAND_MUTEALL,
    SUBCOMMAND_SET_USE_CASE,
    SUBCOMMAND_SET_SESSION,
    SUBCOMMAND_SET_SUBSESSION,
    SUBCOMMAND_SET_ACTIVE_DEVICE,
    SUBCOMMAND_RESET,
    SUBCOMMAND_GET_VOLUME_LEVEL_MAX,
    SUBCOMMAND_GET_VOLUME_LEVEL,
    SUBCOMMAND_SET_VOLUME_LEVEL,
    SUBCOMMAND_UPDATE_VOLUME,
    SUBCOMMAND_GET_MUTE,
    SUBCOMMAND_SET_MUTE,
    SUBCOMMAND_IS_AVAILABLE_HIGH_LATENCY,
    SUBCOMMAND_UNLOAD_HDMI,

};
typedef enum
{
    DOCK_NONE      = 0,
    DOCK_DESKDOCK  = 1,
    DOCK_CARDOCK   = 2,
    DOCK_AUDIODOCK = 7,
    DOCK_SMARTDOCK = 8
} DOCK_STATUS;

static pa_sink *__get_real_master_sink(pa_sink_input *si);
static pa_source *__get_real_master_source(pa_source_output *so);
static inline int __compare_device_info(audio_device_info_t *device_info1, audio_device_info_t *device_info2);
static audio_return_t __fill_audio_playback_stream_info(pa_proplist *sink_input_proplist, pa_sample_spec *sample_spec, audio_info_t *audio_info);
static audio_return_t __fill_audio_playback_device_info(pa_proplist *sink_proplist, audio_info_t *audio_info);
static audio_return_t __fill_audio_playback_info(pa_sink_input *si, audio_info_t *audio_info);
static audio_return_t __fill_audio_capture_device_info(pa_proplist *source_proplist, audio_info_t *audio_info);
static audio_return_t policy_play_sample(struct userdata *u, pa_native_connection *c, const char *name, uint32_t volume_type, uint32_t gain_type, uint32_t volume_level, uint32_t *stream_idx);
static audio_return_t policy_reset(struct userdata *u);
static audio_return_t policy_set_session(struct userdata *u, uint32_t session, uint32_t start);
static audio_return_t policy_set_active_device(struct userdata *u, uint32_t device_in, uint32_t device_out, uint32_t* need_update);
static audio_return_t policy_get_volume_level_max(struct userdata *u, uint32_t volume_type, uint32_t *volume_level);
static audio_return_t __update_volume(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, uint32_t volume_level);
#ifdef PRIMARY_VOLUME
static int __set_primary_volume(struct userdata *u, void* key, int volumetype, int is_new);
#endif
static audio_return_t policy_get_volume_level(struct userdata *u, uint32_t stream_idx, uint32_t *volume_type, uint32_t *volume_level);
static audio_return_t policy_set_volume_level(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, uint32_t volume_level);
static audio_return_t policy_get_mute(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, uint32_t direction, uint32_t *mute);
static audio_return_t policy_set_mute(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, uint32_t direction, uint32_t mute);
static bool policy_is_filter (pa_sink_input* si);

static const char *__get_session_str(uint32_t session)
{
    switch (session) {
        case AUDIO_SESSION_MEDIA:                       return "media";
        case AUDIO_SESSION_VOICECALL:                   return "voicecall";
        case AUDIO_SESSION_VIDEOCALL:                   return "videocall";
        case AUDIO_SESSION_VOIP:                        return "voip";
        case AUDIO_SESSION_FMRADIO:                     return "fmradio";
        case AUDIO_SESSION_CAMCORDER:                   return "camcorder";
        case AUDIO_SESSION_NOTIFICATION:                return "notification";
        case AUDIO_SESSION_ALARM:                       return "alarm";
        case AUDIO_SESSION_EMERGENCY:                   return "emergency";
        case AUDIO_SESSION_VOICE_RECOGNITION:           return "vr";
        default:                                        return "invalid";
    }
}

static const char *__get_subsession_str(uint32_t subsession)
{
    switch (subsession) {
        case AUDIO_SUBSESSION_NONE:                     return "none";
        case AUDIO_SUBSESSION_VOICE:                    return "voice";
        case AUDIO_SUBSESSION_RINGTONE:                 return "ringtone";
        case AUDIO_SUBSESSION_MEDIA:                    return "media";
        case AUDIO_SUBSESSION_INIT:                     return "init";
        case AUDIO_SUBSESSION_VR_NORMAL:                return "vr_normal";
        case AUDIO_SUBSESSION_VR_DRIVE:                 return "vr_drive";
        case AUDIO_SUBSESSION_STEREO_REC:               return "stereo_rec";
        case AUDIO_SUBSESSION_MONO_REC:                 return "mono_rec";
        default:                                        return "invalid";
    }
}

static const char *__get_device_in_str(uint32_t device_in)
{
    switch (device_in) {
        case AUDIO_DEVICE_IN_NONE:                      return "none";
        case AUDIO_DEVICE_IN_MIC:                       return "mic";
        case AUDIO_DEVICE_IN_WIRED_ACCESSORY:           return "wired";
        case AUDIO_DEVICE_IN_BT_SCO:                    return "bt_sco";
        default:                                        return "invalid";
    }
}

static const char *__get_device_out_str(uint32_t device_out)
{
    switch (device_out) {
        case AUDIO_DEVICE_OUT_NONE:                     return "none";
        case AUDIO_DEVICE_OUT_SPEAKER:                  return "spk";
        case AUDIO_DEVICE_OUT_RECEIVER:                 return "recv";
        case AUDIO_DEVICE_OUT_WIRED_ACCESSORY:          return "wired";
        case AUDIO_DEVICE_OUT_BT_SCO:                   return "bt_sco";
        case AUDIO_DEVICE_OUT_BT_A2DP:                  return "bt_a2dp";
        case AUDIO_DEVICE_OUT_DOCK:                     return "dock";
        case AUDIO_DEVICE_OUT_HDMI:                     return "hdmi";
        case AUDIO_DEVICE_OUT_MIRRORING:                return "mirror";
        case AUDIO_DEVICE_OUT_USB_AUDIO:                return "usb";
        case AUDIO_DEVICE_OUT_MULTIMEDIA_DOCK:          return "multi_dock";
        default:                                        return "invalid";
    }
}

static void __load_dump_config(struct userdata *u)
{
    dictionary * dict = NULL;
    int vconf_dump = 0;

    dict = iniparser_load(PA_DUMP_INI_DEFAULT_PATH);
    if (!dict) {
        pa_log_debug("%s load failed. Use temporary file", PA_DUMP_INI_DEFAULT_PATH);
        dict = iniparser_load(PA_DUMP_INI_TEMP_PATH);
        if (!dict) {
            pa_log_warn("%s load failed", PA_DUMP_INI_TEMP_PATH);
            return;
        }
    }

    vconf_dump |= iniparser_getboolean(dict, "pcm_dump:decoder_out", 0) ? PA_DUMP_PLAYBACK_DECODER_OUT : 0;
    vconf_dump |= iniparser_getboolean(dict, "pcm_dump:resampler_in", 0) ? PA_DUMP_PLAYBACK_RESAMPLER_IN : 0;
    vconf_dump |= iniparser_getboolean(dict, "pcm_dump:resampler_out", 0) ? PA_DUMP_PLAYBACK_RESAMPLER_OUT : 0;
    vconf_dump |= iniparser_getboolean(dict, "pcm_dump:encoder_in", 0) ? PA_DUMP_CAPTURE_ENCODER_IN : 0;
    u->core->dump_sink = (pa_bool_t)iniparser_getboolean(dict, "pcm_dump:pa_sink", 0);
    u->core->dump_sink_input = (pa_bool_t)iniparser_getboolean(dict, "pcm_dump:pa_sink_input", 0);
    u->core->dump_source = (pa_bool_t)iniparser_getboolean(dict, "pcm_dump:pa_source", 0);
    u->core->dump_source_output = (pa_bool_t)iniparser_getboolean(dict, "pcm_dump:pa_source_output", 0);

    iniparser_freedict(dict);

    if (vconf_set_int(PA_DUMP_VCONF_KEY, vconf_dump)) {
        pa_log_warn("vconf_set_int %s=%x failed", PA_DUMP_VCONF_KEY, vconf_dump);
    }
}

static inline pa_bool_t __is_mute_policy(void)
{
    int sound_status = 1;

    /* If sound is mute mode, force ringtone/notification path to headset */
    if (vconf_get_bool(VCONFKEY_SETAPPL_SOUND_STATUS_BOOL, &sound_status)) {
        pa_log_warn("vconf_get_bool for %s failed", VCONFKEY_SETAPPL_SOUND_STATUS_BOOL);
    }

    return (sound_status) ? false : true;
}

static inline pa_bool_t __is_recording(void)
{
    int capture_status = 0;

    /* Check whether audio is recording */
    if (vconf_get_int(VCONFKEY_SOUND_CAPTURE_STATUS, &capture_status)) {
        pa_log_warn("vconf_get_bool for %s failed", VCONFKEY_SOUND_CAPTURE_STATUS);
    }

    return (capture_status) ? true : false;
}

static inline pa_bool_t __is_noise_reduction_on(void)
{
    int noise_reduction_on = 1;

    if (vconf_get_bool(VCONFKEY_CALL_NOISE_REDUCTION_STATE_BOOL, &noise_reduction_on)) {
        pa_log_warn("vconf_get_bool for %s failed", VCONFKEY_CALL_NOISE_REDUCTION_STATE_BOOL);
    }

    return (noise_reduction_on == 1) ? true : false;
}

static bool __is_extra_volume_on(void)
{
    int extra_volume_on = 1;

    if (vconf_get_bool(VCONFKEY_CALL_EXTRA_VOLUME_STATE_BOOL, &extra_volume_on)) {
        pa_log_warn("vconf_get_bool for %s failed", VCONFKEY_CALL_EXTRA_VOLUME_STATE_BOOL);
    }

    return (extra_volume_on == 1) ? true : false;
}

static bool __is_wideband(void)
{
    int wbamr = 1;

    if (vconf_get_bool(VCONFKEY_CALL_WBAMR_STATE_BOOL, &wbamr)) {
        pa_log_warn("vconf_get_bool for %s failed", VCONFKEY_CALL_WBAMR_STATE_BOOL);
    }

    return (wbamr == 1) ? true : false;
}


/* check if this sink is bluez */
static bool policy_is_bluez (pa_sink* sink)
{
	const char* api_name = NULL;

	if (sink == NULL) {
		pa_log_warn ("input param sink is null");
		return false;
	}

    api_name = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_API);
	if (api_name) {
		if (pa_streq (api_name, BLUEZ_API)) {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY][%s] [%s] exists and it is [%s]...true !!", __func__, PA_PROP_DEVICE_API, api_name);
#endif
			return true;
		} else {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY][%s] [%s] exists, but not bluez...false !!", __func__, PA_PROP_DEVICE_API);
#endif
		}
	} else {
#ifdef DEBUG_DETAIL
		pa_log_debug("[POLICY][%s] No [%s] exists...false!!", __func__, PA_PROP_DEVICE_API);
#endif
	}

	return false;
}

/* check if this sink is bluez */
static bool policy_is_usb_alsa (pa_sink* sink)
{
	const char* api_name = NULL;
	const char* device_bus_name = NULL;

	if (sink == NULL) {
		pa_log_warn ("input param sink is null");
		return false;
	}

    api_name = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_API);
	if (api_name) {
		if (pa_streq (api_name, ALSA_API)) {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY][%s] [%s] exists and it is [%s]...true !!", __func__, PA_PROP_DEVICE_API, api_name);
#endif
			device_bus_name = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_BUS);
			if (device_bus_name) {
				if (pa_streq (device_bus_name, "usb")) {
					return true;
				}
			}
		} else {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY][%s] [%s] exists, but not alsa...false !!", __func__, PA_PROP_DEVICE_API);
#endif
		}
	} else {
#ifdef DEBUG_DETAIL
		pa_log_debug("[POLICY][%s] No [%s] exists...false!!", __func__, PA_PROP_DEVICE_API);
#endif
	}

	return false;
}

/* Get sink by name */
static pa_sink* policy_get_sink_by_name (pa_core *c, const char* sink_name)
{
    pa_sink *s = NULL;
    uint32_t idx;

    if (c == NULL || sink_name == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
    }

	PA_IDXSET_FOREACH(s, c->sinks, idx) {
		if (pa_streq (s->name, sink_name)) {
			pa_log_debug ("[POLICY][%s] return [%p] for [%s]\n",  __func__, s, sink_name);
			return s;
		}
	}
	return NULL;
}

/* Get bt sink if available */
static pa_sink* policy_get_bt_sink (pa_core *c)
{
    pa_sink *s = NULL;
    uint32_t idx;

    if (c == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
    }

	PA_IDXSET_FOREACH(s, c->sinks, idx) {
		if (policy_is_bluez (s)) {
			pa_log_debug ("[POLICY][%s] return [%p] for [%s]\n", __func__, s, s->name);
			return s;
		}
	}
	return NULL;
}
/** This function chages the sink from normal sink to UHQA sink if UHQA sink is available*/
static pa_sink* switch_to_uhqa_sink(pa_core *c, const char* policy)
{
    pa_sink_input *si = NULL;
    uint32_t idx;
    pa_sink* sink = NULL;
    pa_sink* uhqa_sink = NULL;
    pa_sink* def = pa_namereg_get_default_sink(c);
    /** If default sink is HDMI sink*/
    if (sink_is_hdmi(def)) {
        uhqa_sink = policy_get_sink_by_name(c, SINK_HDMI_UHQA);
        sink = policy_get_sink_by_name(c, SINK_HDMI);
    /** If high latency UHQA policy means h:0,4 UHQA sink to be selected if h:0,4 UHQA sink already created*/
    } else if (pa_streq(policy, POLICY_HIGH_LATENCY_UHQA)) {
        uhqa_sink = policy_get_sink_by_name(c, SINK_HIGH_LATENCY_UHQA);
        sink = policy_get_sink_by_name(c, SINK_HIGH_LATENCY);
    } else if (pa_streq(policy, POLICY_AUTO_UHQA)) {   /** If UHQA policy choose UHQA sink*/
        pa_log_info ("---------------------------------");

        uhqa_sink = policy_get_sink_by_name(c, SINK_ALSA_UHQA);
        sink = policy_get_sink_by_name(c, SINK_ALSA);
    }
    pa_log_info ("---------------------------------");
    if (uhqa_sink != NULL) {
        if (sink != NULL) {/** if the sink is null due to some reason ,it need to add protect code */

            /** Check if normal sink is in not suspended state then suspend normal sik so that pcm handle is closed*/
            if (PA_SINK_SUSPENDED != pa_sink_get_state(sink) ) {
                pa_sink_suspend(sink, true, PA_SUSPEND_USER);
            }
            pa_log_info ("---------------------------------");

            /** Check any sink input connected to normal sink then move them to UHQA sink*/
            PA_IDXSET_FOREACH (si, sink->inputs, idx) {

                /* Get role (if role is filter, skip it) */
                if (policy_is_filter(si)) {
                    continue;
                }
                pa_sink_input_move_to(si, uhqa_sink, false);
            }
        }
        if (PA_SINK_SUSPENDED == pa_sink_get_state(uhqa_sink)) {
            pa_sink_suspend(uhqa_sink, false, PA_SUSPEND_USER);
        }
        sink = uhqa_sink;
    }

    return sink;
}
/** This function choose normal sink if UHQA sink is in suspended state*/
static pa_sink* switch_to_normal_sink(pa_core *c, const char* policy)
{
    pa_sink_input *si = NULL;
    uint32_t idx;
    pa_sink* sink = NULL;
    pa_sink* uhqa_sink = NULL;
    const char *sink_name  = SINK_ALSA;
    pa_sink* def = NULL;

    def = pa_namereg_get_default_sink(c);

    if (pa_streq(policy, POLICY_PHONE) || pa_streq(policy, POLICY_ALL)) {
      /** Get the UHQA sink */
      uhqa_sink = policy_get_sink_by_name (c, SINK_ALSA_UHQA);
    } else if (sink_is_hdmi(def)) {    /** If default sink is HDMI sink*/
        /** Get the UHQA sink handle, if it exists then suspend it if not in use*/
        uhqa_sink = policy_get_sink_by_name (c, SINK_HDMI_UHQA);
        sink_name  = SINK_HDMI;
    } else if(pa_streq(policy, POLICY_HIGH_LATENCY)) {      /** Choose the normal sink based on policy*/
        /** Get the UHQA sink handle, if it exists then suspend it if not in use*/
        uhqa_sink =  policy_get_sink_by_name(c, SINK_HIGH_LATENCY_UHQA);
        sink_name  = SINK_HIGH_LATENCY;
    } else {
        /** Get the UHQA sink */
        uhqa_sink = policy_get_sink_by_name(c, SINK_ALSA_UHQA);
    }
    sink = uhqa_sink;

    /**
      * If UHQA sink is in used or any UHQA sink is connected to UHQA sink then return UHQA sink else return normal sink
      */
    if ((sink != NULL) && pa_sink_used_by(sink)) {
        sink = uhqa_sink;
    } else {
        sink = policy_get_sink_by_name(c, sink_name);
        if (sink != NULL) {/**if the sink is null ,it need to add protect code*/
            /** Move all sink inputs from UHQA sink to normal sink*/
            if (uhqa_sink != NULL) {
                if (PA_SINK_SUSPENDED != pa_sink_get_state(uhqa_sink)) {
                    pa_sink_suspend(uhqa_sink, true, PA_SUSPEND_USER);
                }
                PA_IDXSET_FOREACH (si, uhqa_sink->inputs, idx) {
                    /* Get role (if role is filter, skip it) */
                    if (policy_is_filter(si)) {
                       continue;
                    }
                   pa_sink_input_move_to(si, sink, false);
               }
            }
            if (PA_SINK_SUSPENDED == pa_sink_get_state(policy_get_sink_by_name(c, sink_name)) ) {
                /** unsuspend this sink */
                pa_sink_suspend( policy_get_sink_by_name(c, sink_name), false, PA_SUSPEND_USER);
            }
        } else {/** if sink is null,it can not move to the normal sink ,still use uhqa sink */
            pa_log_warn ("The %s sink is null", sink_name);
            sink = uhqa_sink;
        }
    }

    return sink;
}
/*Select sink for given condition */
static pa_sink* policy_select_proper_sink (struct userdata *u, const char* policy, pa_sink_input *sink_input, int is_mono)
{
	pa_core *c = u->core;
	pa_sink* sink = NULL;
	pa_sink* bt_sink = NULL;
	pa_sink* def = NULL;
	pa_sink* sink_null;
	uint32_t idx;
	const char *si_policy_str;
	pa_sink_input *si = NULL;
	if (c == NULL || policy == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
	}

	pa_assert (c);

	bt_sink = policy_get_bt_sink(c);
	def = pa_namereg_get_default_sink(c);
	if (def == NULL) {
		pa_log_warn ("POLICY][%s] pa_namereg_get_default_sink() returns null", __func__);
		return NULL;
	}

	pa_log_debug ("[POLICY][%s] policy[%s], is_mono[%d], current default[%s], bt sink[%s]\n",
			__func__, policy, is_mono, def->name, (bt_sink)? bt_sink->name:"null");

    sink_null = (pa_sink *)pa_namereg_get(c, "null", PA_NAMEREG_SINK);
    /* if default sink is set as null sink, we will use null sink */
    if (def == sink_null)
        return def;

	/* Select sink to */
	if (pa_streq(policy, POLICY_ALL)) {
		/* all */
		if (bt_sink) {
			sink = policy_get_sink_by_name(c, (is_mono)? SINK_MONO_COMBINED : SINK_COMBINED);
		} else {
#ifdef TIZEN_MICRO
			sink = policy_get_sink_by_name (c, (is_mono)? SINK_MONO_ALSA : SINK_ALSA);
#else
            sink = switch_to_normal_sink(c, policy);
#endif
		}

	} else if (pa_streq(policy, POLICY_PHONE)) {
		/* phone */
#ifdef TIZEN_MICRO
        if (u->subsession == AUDIO_SUBSESSION_RINGTONE)
            sink = policy_get_sink_by_name(c, AEC_SINK);
        if (!sink)
			sink = policy_get_sink_by_name (c, (is_mono)? SINK_MONO_ALSA : SINK_ALSA);
#else
        sink = switch_to_normal_sink(c, policy);
#endif
	} else if (pa_streq(policy, POLICY_VOIP)) {
		/* VOIP */
		/* NOTE: Check voip sink first, if not available, use AEC sink */
        sink = policy_get_sink_by_name (c,SINK_VOIP);
        if (sink == NULL) {
            pa_log_info ("VOIP sink is not available, try to use AEC sink");
            sink = policy_get_sink_by_name (c, AEC_SINK);
            if (sink == NULL) {
                pa_log_info ("AEC sink is not available, set to default sink");
                sink = def;
            }
        }
	} else {
		/* auto */
		if (policy_is_bluez(def)) {
			sink = (is_mono)? policy_get_sink_by_name (c, SINK_MONO_BT) : def;
		} else if (policy_is_usb_alsa(def)) {
			sink = def;
  		} else if (sink_is_hdmi(def)) {
#ifdef TIZEN_MICRO
            sink = def;
#else
            if (pa_streq(policy, POLICY_AUTO_UHQA) || (pa_streq(policy, POLICY_HIGH_LATENCY_UHQA))) {
                sink = switch_to_uhqa_sink(c,policy);
            }
            else {
                sink = switch_to_normal_sink(c, policy);
            }
#endif
		} else {
            pa_bool_t highlatency_exist = 0;

#ifdef TIZEN_MICRO
            if(pa_streq(policy, POLICY_HIGH_LATENCY)) {
#else
            if ((pa_streq(policy, POLICY_HIGH_LATENCY)) || (pa_streq(policy, POLICY_HIGH_LATENCY_UHQA))) {
#endif
                PA_IDXSET_FOREACH(si, c->sink_inputs, idx) {
                    if ((si_policy_str = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_POLICY))) {
                        if (pa_streq(si_policy_str, POLICY_HIGH_LATENCY) && sink_is_highlatency(si->sink)
                            && (sink_input == NULL || sink_input->index != si->index)) {
                            highlatency_exist = 1;
                            break;
                        }
                    }
                }

#ifdef TIZEN_MICRO
                if (!highlatency_exist) {
                    sink = policy_get_sink_by_name(c, SINK_HIGH_LATENCY);
                }
#else
                /** If high latency UHQA policy means h:0,4 UHQA sink to be selected if h:0,4 UHQA sink already created*/
                if (pa_streq(policy, POLICY_HIGH_LATENCY_UHQA)) {
                    sink = switch_to_uhqa_sink(c,policy);
                }

                /**
                  * If still sink is null means either policy is high-latency or UHQA sink does not exist
                  * Normal sink need to be selected
                  */
                if (!highlatency_exist && (sink == NULL)) {
                    sink = switch_to_normal_sink(c, policy);
                }
#endif
            }
#ifdef TIZEN_MICRO
            if (!sink)
				sink = policy_get_sink_by_name (c, (is_mono)? SINK_MONO_ALSA : SINK_ALSA);

#else
            /** If sink is still null then it is required to choose hw:0,0 UHQA sink or normal sink  based on policy*/
            if (!sink) {
                /** If UHQA policy choose UHQA sink*/
                if (pa_streq(policy, POLICY_AUTO_UHQA)) {
                    sink = switch_to_uhqa_sink(c,policy);
                }

                /** If still no sink selected then select hw:0,0 normal sink this is the default case*/
                if (!sink) {
                    sink = switch_to_normal_sink(c,POLICY_AUTO);
                }
             }
#endif
        }
	}

	pa_log_debug ("[POLICY][%s] selected sink : [%s]\n", __func__, (sink)? sink->name : "null");
	return sink;
}
static bool policy_is_filter (pa_sink_input* si)
{
	const char* role = NULL;

	if (si == NULL) {
		pa_log_warn ("input param sink-input is null");
		return false;
	}

	if ((role = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_ROLE))) {
#ifdef DEBUG_DETAIL
		pa_log_debug("[POLICY][%s] Role of sink input [%d] = %s", __func__, si->index, role);
#endif
		if (pa_streq(role, "filter")) {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY] no need to change of sink for %s", role);
#endif
			return true;
		}
	}

	return false;
}
static pa_sink *__get_real_master_sink(pa_sink_input *si)
{
    const char *master_name;
    pa_sink *s, *sink;

    s = (si->origin_sink) ? si->origin_sink : si->sink;
    master_name = pa_proplist_gets(s->proplist, PA_PROP_DEVICE_MASTER_DEVICE);
    if (master_name)
        sink = pa_namereg_get(si->core, master_name, PA_NAMEREG_SINK);
    else
        sink = s;
    return sink;
}
static pa_source *__get_real_master_source(pa_source_output *so)
{
    const char *master_name;
    pa_source *s, *source;

    s = (so->destination_source) ? so->destination_source : so->source;
    master_name = pa_proplist_gets(s->proplist, PA_PROP_DEVICE_MASTER_DEVICE);
    if (master_name)
        source = pa_namereg_get(so->core, master_name, PA_NAMEREG_SINK);
    else
        source = s;
    return source;
}

static void __free_audio_stream_info (audio_info_t *audio_info)
{
    pa_xfree(audio_info->stream.name);
}
static void __free_audio_device_info (audio_info_t *audio_info)
{
    if (audio_info->device.api == AUDIO_DEVICE_API_ALSA) {
        pa_xfree(audio_info->device.alsa.card_name);
    } else if (audio_info->device.api == AUDIO_DEVICE_API_BLUEZ) {
        pa_xfree(audio_info->device.bluez.protocol);
    }
    pa_xfree(audio_info->device.name);
}
static void __free_audio_info (audio_info_t *audio_info)
{
    __free_audio_stream_info(audio_info);
    __free_audio_device_info(audio_info);
}
static audio_return_t __fill_audio_playback_stream_info(pa_proplist *sink_input_proplist, pa_sample_spec *sample_spec, audio_info_t *audio_info)
{
    const char *si_volume_type_str, *si_gain_type_str;

    memset(&audio_info->stream, 0x00, sizeof(audio_stream_info_t));

    if (!sink_input_proplist) {
        return AUDIO_ERR_PARAMETER;
    }

    audio_info->stream.name = strdup(pa_strnull(pa_proplist_gets(sink_input_proplist, PA_PROP_MEDIA_NAME)));
    audio_info->stream.samplerate = sample_spec->rate;
    audio_info->stream.channels = sample_spec->channels;
    audio_info->stream.gain_type = AUDIO_GAIN_TYPE_DEFAULT;

    /* Get volume type of sink input */
    if ((si_volume_type_str = pa_proplist_gets(sink_input_proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
        pa_atou(si_volume_type_str, &audio_info->stream.volume_type);
    } else {
        pa_xfree(audio_info->stream.name);
        return AUDIO_ERR_UNDEFINED;
    }

    /* Get gain type of sink input */
    if ((si_gain_type_str = pa_proplist_gets(sink_input_proplist, PA_PROP_MEDIA_TIZEN_GAIN_TYPE))) {
        pa_atou(si_gain_type_str, &audio_info->stream.gain_type);
    }

    return AUDIO_RET_OK;
}

static audio_return_t __fill_audio_playback_device_info(pa_proplist *sink_proplist, audio_info_t *audio_info)
{
    const char *s_device_api_str;

    memset(&audio_info->device, 0x00, sizeof(audio_device_info_t));

    if (!sink_proplist) {
        return AUDIO_ERR_PARAMETER;
    }

    /* Get device api */
    if ((s_device_api_str = pa_proplist_gets(sink_proplist, PA_PROP_DEVICE_API))) {
        audio_info->device.name = strdup(pa_strnull(pa_proplist_gets(sink_proplist, PA_PROP_DEVICE_STRING)));
        if (pa_streq(s_device_api_str, "alsa")) {
            const char *card_idx_str, *device_idx_str;

            audio_info->device.api = AUDIO_DEVICE_API_ALSA;
            audio_info->device.direction = AUDIO_DIRECTION_OUT;
            audio_info->device.alsa.card_name = strdup(pa_strnull(pa_proplist_gets(sink_proplist, "alsa.card_name")));
            audio_info->device.alsa.card_idx = 0;
            audio_info->device.alsa.device_idx = 0;
            if ((card_idx_str = pa_proplist_gets(sink_proplist, "alsa.card")))
                pa_atou(card_idx_str, &audio_info->device.alsa.card_idx);
            if ((device_idx_str = pa_proplist_gets(sink_proplist, "alsa.device")))
                pa_atou(device_idx_str, &audio_info->device.alsa.device_idx);
        }
        else if (pa_streq(s_device_api_str, "bluez")) {
            const char *nrec_str;

            audio_info->device.api = AUDIO_DEVICE_API_BLUEZ;
            audio_info->device.bluez.nrec = 0;
            audio_info->device.bluez.protocol = strdup(pa_strnull(pa_proplist_gets(sink_proplist, "bluetooth.protocol")));
            if ((nrec_str = pa_proplist_gets(sink_proplist, "bluetooth.nrec")))
                pa_atou(nrec_str, &audio_info->device.bluez.nrec);
        }
    }

    return AUDIO_RET_OK;
}

static audio_return_t __fill_audio_playback_info(pa_sink_input *si, audio_info_t *audio_info)
{
    audio_return_t audio_ret = AUDIO_RET_OK;
    pa_sink *sink;

    sink = __get_real_master_sink(si);
    if (AUDIO_IS_ERROR((audio_ret = __fill_audio_playback_stream_info(si->proplist, &si->sample_spec, audio_info)))) {
        return audio_ret;
    }
    if (AUDIO_IS_ERROR((audio_ret = __fill_audio_playback_device_info(sink->proplist, audio_info)))) {
        __free_audio_stream_info(audio_info);
        return audio_ret;
    }

    return AUDIO_RET_OK;
}

static audio_return_t __fill_audio_capture_device_info(pa_proplist *source_proplist, audio_info_t *audio_info)
{
    const char *s_device_api_str;

    if (!source_proplist) {
        return AUDIO_ERR_PARAMETER;
    }

    memset(&audio_info->device, 0x00, sizeof(audio_device_info_t));

    /* Get device api */
    if ((s_device_api_str = pa_proplist_gets(source_proplist, PA_PROP_DEVICE_API))) {
        audio_info->device.name = strdup(pa_strnull(pa_proplist_gets(source_proplist, PA_PROP_DEVICE_STRING)));
        if (pa_streq(s_device_api_str, "alsa")) {
            const char *card_idx_str, *device_idx_str;

            audio_info->device.api = AUDIO_DEVICE_API_ALSA;
            audio_info->device.direction = AUDIO_DIRECTION_IN;
            audio_info->device.alsa.card_name = strdup(pa_strnull(pa_proplist_gets(source_proplist, "alsa.card_name")));
            audio_info->device.alsa.card_idx = 0;
            audio_info->device.alsa.device_idx = 0;
            if ((card_idx_str = pa_proplist_gets(source_proplist, "alsa.card")))
                pa_atou(card_idx_str, &audio_info->device.alsa.card_idx);
            if ((device_idx_str = pa_proplist_gets(source_proplist, "alsa.device")))
                pa_atou(device_idx_str, &audio_info->device.alsa.device_idx);
        }
        else if (pa_streq(s_device_api_str, "bluez")) {
            const char *nrec_str;

            audio_info->device.api = AUDIO_DEVICE_API_BLUEZ;
            audio_info->device.bluez.nrec = 0;
            audio_info->device.bluez.protocol = strdup(pa_strnull(pa_proplist_gets(source_proplist, "bluetooth.protocol")));
            if ((nrec_str = pa_proplist_gets(source_proplist, "bluetooth.nrec")))
                pa_atou(nrec_str, &audio_info->device.bluez.nrec);
        }
    }

    return AUDIO_RET_OK;
}

#define PROP_POLICY_CORK "policy_cork_by_device_switch"

static inline int __compare_device_info (audio_device_info_t *device_info1, audio_device_info_t *device_info2)
{
    if (device_info1->direction != device_info2->direction)
        return false;

    if (device_info1->api == AUDIO_DEVICE_API_ALSA) {
        if ((!strcmp(device_info1->alsa.card_name, device_info2->alsa.card_name) || (device_info1->alsa.card_idx == device_info2->alsa.card_idx))
            && (device_info1->alsa.device_idx == device_info2->alsa.device_idx)) {
            return true;
        }
    }
    if (device_info1->api == AUDIO_DEVICE_API_BLUEZ) {
        if (!strcmp(device_info1->bluez.protocol, device_info2->bluez.protocol) && (device_info1->bluez.nrec == device_info2->bluez.nrec)) {
            return true;
        }
    }

    return false;
}

static void __free_event (struct pa_hal_event *hal_event) {
    pa_assert(hal_event);
    pa_assert(hal_event->userdata);

    pa_xfree(hal_event->event_data);
    pa_mutex_free(hal_event->mutex);
    pa_cond_free(hal_event->cond);
    pa_xfree(hal_event);
}

static inline const char *__get_event_type_string (pa_hal_event_type_t hal_event_type)
{
    switch (hal_event_type) {
        case PA_HAL_EVENT_LOAD_DEVICE:          return "load device";
        case PA_HAL_EVENT_OPEN_DEVICE:          return "open device";
        case PA_HAL_EVENT_CLOSE_ALL_DEVICES:    return "close all devices";
        case PA_HAL_EVENT_CLOSE_DEVICE:         return "close device";
        case PA_HAL_EVENT_UNLOAD_DEVICE:        return "unload device";
        default:                                return "undefined";
    }
}
static audio_return_t __load_n_open_device (struct userdata *u, audio_device_info_t *device_info, audio_device_param_info_t *params, pa_hal_event_type_t hal_event_type)
{
    audio_info_t audio_info;
    bool is_module_loaded = false;
    pa_strbuf *name_buf, *args_buf, *prop_buf;
    char *name = NULL, *args = NULL, *prop = NULL;
    pa_source *source;
    pa_sink *sink;
    const char *device_object_str = NULL, *dirction_str = NULL;
    const char *is_manual_corked_str = NULL;
    uint32_t is_manual_corked = 0;
    int i;
    bool is_param_set[AUDIO_DEVICE_PARAM_MAX] = {false, };
    uint32_t idx;

    pa_assert(u);
    pa_assert(device_info);
    pa_assert(device_info->direction == AUDIO_DIRECTION_IN || device_info->direction == AUDIO_DIRECTION_OUT);

     if (device_info->direction == AUDIO_DIRECTION_IN) {
        PA_IDXSET_FOREACH(source, u->core->sources, idx) {
            if (!AUDIO_IS_ERROR(__fill_audio_capture_device_info(source->proplist, &audio_info))) {
                if (__compare_device_info(&audio_info.device, device_info)) {
                    is_module_loaded = true;
                    __free_audio_device_info(&audio_info);
                    break;
                }
                __free_audio_device_info(&audio_info);
            }
        }
        device_object_str = "source";
        dirction_str = "input";
    } else {
        PA_IDXSET_FOREACH(sink, u->core->sinks, idx) {
            if (!AUDIO_IS_ERROR(__fill_audio_playback_device_info(sink->proplist, &audio_info))) {
                if (__compare_device_info(&audio_info.device, device_info)) {
                    is_module_loaded = true;
                    __free_audio_device_info(&audio_info);
                    break;
                }
                __free_audio_device_info(&audio_info);
            }
        }
        device_object_str = "sink";
        dirction_str = "output";
    }

    name_buf = pa_strbuf_new();
    if (device_info->api == AUDIO_DEVICE_API_ALSA) {
        if (device_info->alsa.card_name && !strncmp(device_info->alsa.card_name, ALSA_VIRTUAL_CARD, strlen(ALSA_VIRTUAL_CARD))) {
            pa_strbuf_printf(name_buf, "%s", (device_info->direction == AUDIO_DIRECTION_OUT) ? SINK_VIRTUAL : SOURCE_VIRTUAL);
        } else if (device_info->alsa.card_name && !strncmp(device_info->alsa.card_name, ALSA_SAUDIOVOIP_CARD, strlen(ALSA_SAUDIOVOIP_CARD))) {
            pa_strbuf_printf(name_buf, "%s", (device_info->direction == AUDIO_DIRECTION_OUT) ? SINK_VOIP : SOURCE_VOIP);
        } else {
            pa_strbuf_printf(name_buf, "alsa_%s.%d.analog-stereo", dirction_str, device_info->alsa.device_idx);
        }
    } else if (device_info->api == AUDIO_DEVICE_API_BLUEZ) {
        /* HAL_TODO : do we need to consider BLUEZ here? */
        pa_strbuf_printf(name_buf, "bluez_%s.%s", device_object_str, device_info->bluez.protocol);
    } else {
        pa_strbuf_printf(name_buf, "unknown_%s", device_object_str);
    }
    name = pa_strbuf_tostring_free(name_buf);
    if (!name) {
        pa_log_error("invalid module name");
        return AUDIO_ERR_PARAMETER;
    }

    /* load module if is not loaded */
    if (is_module_loaded == false) {
        args_buf = pa_strbuf_new();
        prop_buf = pa_strbuf_new();

        pa_strbuf_printf(args_buf, "%s_name=\"%s\" ", device_object_str, name);
        if (device_info->name) {
            pa_strbuf_printf(args_buf, "device=\"%s\" ", device_info->name);
        }

        for (i = 0; i < AUDIO_DEVICE_PARAM_MAX; i++) {
            if (params[i].param == AUDIO_DEVICE_PARAM_NONE)
                break;
            is_param_set[params[i].param] = true;
            if (params[i].param == AUDIO_DEVICE_PARAM_CHANNELS) {
                pa_strbuf_printf(args_buf, "channels=\"%d\" ", params[i].u32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_SAMPLERATE) {
                pa_strbuf_printf(args_buf, "rate=\"%d\" ", params[i].u32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_FRAGMENT_SIZE) {
                pa_strbuf_printf(args_buf, "fragment_size=\"%d\" ", params[i].u32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_FRAGMENT_NB) {
                pa_strbuf_printf(args_buf, "fragments=\"%d\" ", params[i].u32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_START_THRESHOLD) {
                pa_strbuf_printf(args_buf, "start_threshold=\"%d\" ", params[i].s32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_USE_MMAP) {
                pa_strbuf_printf(args_buf, "mmap=\"%d\" ", params[i].u32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_USE_TSCHED) {
                pa_strbuf_printf(args_buf, "tsched=\"%d\" ", params[i].u32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_TSCHED_BUF_SIZE) {
                pa_strbuf_printf(args_buf, "tsched_buffer_size=\"%d\" ", params[i].u32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_SUSPEND_TIMEOUT) {
                pa_strbuf_printf(prop_buf, "module-suspend-on-idle.timeout=%d ", params[i].u32_v);
            } else if (params[i].param == AUDIO_DEVICE_PARAM_ALTERNATE_RATE) {
                pa_strbuf_printf(args_buf, "alternate_rate=\"%d\" ", params[i].u32_v);
            }
        }

        if (device_info->direction == AUDIO_DIRECTION_IN) {
            pa_module *module_source = NULL;

            if (!is_param_set[AUDIO_DEVICE_PARAM_FRAGMENT_SIZE]) {
                pa_strbuf_printf(args_buf, "fragment_size=\"%d\" ", (u->fragment_size) ? u->fragment_size : DEFAULT_FRAGMENT_SIZE);
            }
            if ((prop = pa_strbuf_tostring_free(prop_buf))) {
                pa_strbuf_printf(args_buf, "source_properties=\"%s\" ", prop);
            }

            args = pa_strbuf_tostring_free(args_buf);

            if (device_info->api == AUDIO_DEVICE_API_ALSA) {
                module_source = pa_module_load(u->core, "module-alsa-source", args);
            } else if (device_info->api == AUDIO_DEVICE_API_BLUEZ) {
                module_source = pa_module_load(u->core, "module-bluez-source", args);
            }

            if (!module_source) {
                pa_log_error("load source module failed. api:%d args:%s", device_info->api, args);
            }
        } else {
            pa_module *module_sink = NULL;

            if (!is_param_set[AUDIO_DEVICE_PARAM_TSCHED_BUF_SIZE]) {
                pa_strbuf_printf(args_buf, "tsched_buffer_size=\"%d\" ", (u->tsched_buffer_size) ? u->tsched_buffer_size : DEFAULT_TSCHED_BUFFER_SIZE);
            }
            if ((prop = pa_strbuf_tostring_free(prop_buf))) {
                pa_strbuf_printf(args_buf, "sink_properties=\"%s\" ", prop);
            }

            args = pa_strbuf_tostring_free(args_buf);

            if (device_info->api == AUDIO_DEVICE_API_ALSA) {
                module_sink = pa_module_load(u->core, "module-alsa-sink", args);
            } else if (device_info->api == AUDIO_DEVICE_API_BLUEZ) {
                module_sink = pa_module_load(u->core, "module-bluez-sink", args);
            }

            if (!module_sink) {
                pa_log_error("load sink module failed. api:%d args:%s", device_info->api, args);
            }
        }
    }

    if (hal_event_type == PA_HAL_EVENT_LOAD_DEVICE) {
        goto exit;
    }

    if (device_info->direction == AUDIO_DIRECTION_IN) {
        /* set default source */
        pa_source *source_default = pa_namereg_get_default_source(u->core);
        pa_source *source_null = (pa_source *)pa_namereg_get(u->core, "null", PA_NAMEREG_SOURCE);

        if (source_default == source_null || device_info->is_default_device) {
            if ((source = pa_namereg_get(u->core, name, PA_NAMEREG_SOURCE))) {
                pa_source_output *so;

                if (source != source_default) {
                    pa_namereg_set_default_source(u->core, source);
                    pa_log_info("set %s as default source", name);
                }

                PA_IDXSET_FOREACH(so, u->core->source_outputs, idx) {
                    if (!so->source)
                        continue;
                    /* Get role (if role is filter, skip it) */
                    if (policy_is_filter(so->proplist))
                        continue;

                    pa_source_output_move_to(so, source, false);

                    /* UnCork if corked by manually */
                    if ((is_manual_corked_str = pa_proplist_gets(so->proplist, PROP_POLICY_CORK))) {
                        pa_atou(is_manual_corked_str, &is_manual_corked);
                        if (is_manual_corked) {
                            pa_proplist_sets(so->proplist, PROP_POLICY_CORK, "0");
                            pa_source_output_cork(so, false);
                            pa_log_info("<UnCork> for source-input[%d]:source[%s]", so->index, so->source->name);
                        }
                    }
                }
            }
        }
    } else {
        /* set default sink */
        pa_sink *sink_default = pa_namereg_get_default_sink(u->core);
        pa_sink *sink_null = (pa_sink *)pa_namereg_get(u->core, "null", PA_NAMEREG_SINK);

        if ((sink_default == sink_null || device_info->is_default_device)
            && !(device_info->api == AUDIO_DEVICE_API_ALSA && u->active_device_out == AUDIO_DEVICE_OUT_BT_A2DP)) {
            if ((sink = pa_namereg_get(u->core, name, PA_NAMEREG_SINK))) {
                pa_sink_input *si;

                if (sink != sink_default) {
                    pa_namereg_set_default_sink(u->core, sink);
                    pa_log_info("set %s as default sink", name);
                }

                PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
                    if (!si->sink)
                        continue;

                    /* Get role (if role is filter, skip it) */
                    if (policy_is_filter(si->proplist))
                        continue;

                    pa_sink_input_move_to(si, sink, false);

                    /* UnCork if corked by manually */
                    if ((is_manual_corked_str = pa_proplist_gets(si->proplist, PROP_POLICY_CORK))) {
                        pa_atou(is_manual_corked_str, &is_manual_corked);
                        if (is_manual_corked) {
                            pa_proplist_sets(si->proplist, PROP_POLICY_CORK, "0");
                            pa_sink_input_cork(si, false);
                            pa_log_info("<UnCork> for sink-input[%d]:sink[%s]", si->index, si->sink->name);
                        }
                    }
                }
            }
        }
    }

exit:
    pa_xfree(name);
    pa_xfree(args);
    pa_xfree(prop);

    return AUDIO_RET_OK;
}
static audio_return_t __load_n_open_device_callback (void *platform_data, audio_device_info_t *device_info, audio_device_param_info_t *params, pa_hal_event_type_t hal_event_type)
{
    struct userdata *u = (struct userdata *)platform_data;
    struct pa_hal_event *hal_event;
    struct pa_hal_device_event_data *event_data;

    pa_assert(u);
    pa_assert(device_info);
    pa_assert(device_info->direction == AUDIO_DIRECTION_IN || device_info->direction == AUDIO_DIRECTION_OUT);
    pa_assert(hal_event_type == PA_HAL_EVENT_LOAD_DEVICE || hal_event_type == PA_HAL_EVENT_OPEN_DEVICE);

    if (u->tid != pthread_self()) {
        /* called from thread */
        pa_log_debug("%s is called within thread", __get_event_type_string(hal_event_type));

        hal_event = pa_xmalloc(sizeof(struct pa_hal_event));
        hal_event->userdata = u;
        hal_event->event_type = hal_event_type;
        event_data = pa_xmalloc(sizeof(struct pa_hal_device_event_data));
        memcpy(&event_data->device_info, device_info, sizeof(audio_device_info_t));
        memcpy(&event_data->params[0], params, sizeof(audio_device_param_info_t) * AUDIO_DEVICE_PARAM_MAX);
        hal_event->event_data = (void *)event_data;

        hal_event->mutex = pa_mutex_new(true, true);
        hal_event->cond = pa_cond_new();

        pa_mutex_lock(hal_event->mutex);

        PA_LLIST_INSERT_AFTER(struct pa_hal_event, u->hal_event_queue, u->hal_event_last, hal_event);
        u->hal_event_last = hal_event;

        u->core->mainloop->defer_enable(u->defer_event, 1);

        pa_cond_wait(hal_event->cond, hal_event->mutex);
        pa_mutex_unlock(hal_event->mutex);

        pa_log_info("%s is finished within thread", __get_event_type_string(hal_event_type));

        __free_event(hal_event);
    } else {
        /* called from mainloop */
        pa_log_debug("%s is called within mainloop", __get_event_type_string(hal_event_type));
        __load_n_open_device(u, device_info, params, hal_event_type);
        pa_log_info("%s is finished within mainloop", __get_event_type_string(hal_event_type));
    }

    return AUDIO_RET_OK;
}

static audio_return_t __load_device_callback (void *platform_data, audio_device_info_t *device_info, audio_device_param_info_t *params)
{
    return __load_n_open_device_callback(platform_data, device_info, params, PA_HAL_EVENT_LOAD_DEVICE);
}

static audio_return_t __open_device_callback (void *platform_data, audio_device_info_t *device_info, audio_device_param_info_t *params)
{
    return __load_n_open_device_callback(platform_data, device_info, params, PA_HAL_EVENT_OPEN_DEVICE);
}


static audio_return_t __close_all_devices (struct userdata *u)
{
    pa_source *source_null, *source;
    pa_source_output *so;
    pa_sink *sink_null, *sink;
    pa_sink_input *si;
    uint32_t idx;

    pa_assert(u);

    /* set default sink/src as null */
    source_null = (pa_source *)pa_namereg_get(u->core, "null", PA_NAMEREG_SOURCE);
    pa_namereg_set_default_source(u->core, source_null);
    sink_null = (pa_sink *)pa_namereg_get(u->core, "null", PA_NAMEREG_SINK);
    pa_namereg_set_default_sink(u->core, sink_null);

    /* close input devices */
    PA_IDXSET_FOREACH(so, u->core->source_outputs, idx) {
        if (!so->source)
            continue;
#if 0
        /* Get role (if role is filter, skip it) */
        if (policy_is_filter(so->proplist))
            continue;
#endif
        /* Cork only if source-output was Running */
        if (pa_source_output_get_state(so) == PA_SOURCE_OUTPUT_RUNNING) {
            pa_proplist_sets(so->proplist, PROP_POLICY_CORK, "1");
            pa_source_output_cork(so, true);
            pa_log_info("<Cork> for source-output[%d]:source[%s]", so->index, so->source->name);
        }
        if (source_null)
            pa_source_output_move_to(so, source_null, false);
    }
    PA_IDXSET_FOREACH(source, u->core->sources, idx) {
        pa_source_suspend(source, true, PA_SUSPEND_INTERNAL);
    }

    /* close output devices */
    PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
        if (!si->sink)
            continue;
        /* Get role (if role is filter, skip it) */
        if (policy_is_filter(si))
            continue;
        /* Cork only if sink-input was Running */
        if (pa_sink_input_get_state(si) == PA_SINK_INPUT_RUNNING) {
            pa_proplist_sets(si->proplist, PROP_POLICY_CORK, "1");
            pa_sink_input_cork(si, true);
            pa_log_info("<Cork> for sink-input[%d]:sink[%s]", si->index, si->sink->name);
        }
        if (sink_null)
            pa_sink_input_move_to(si, sink_null, false);
    }
    PA_IDXSET_FOREACH(sink, u->core->sinks, idx) {
        pa_sink_suspend(sink, true, PA_SUSPEND_INTERNAL);
    }

    return AUDIO_RET_OK;
}

static audio_return_t __close_all_devices_callback (void *platform_data)
{
    struct userdata *u = (struct userdata *)platform_data;
    struct pa_hal_event *hal_event;

    pa_assert(u);

    if (u->tid != pthread_self()) {
        /* called from thread */
        pa_log_debug("%s is called within thread", __get_event_type_string(PA_HAL_EVENT_CLOSE_ALL_DEVICES));

        hal_event = pa_xmalloc(sizeof(struct pa_hal_event));
        hal_event->userdata = u;
        hal_event->event_type = PA_HAL_EVENT_CLOSE_ALL_DEVICES;
        hal_event->event_data = NULL;

        hal_event->mutex = pa_mutex_new(true, true);
        hal_event->cond = pa_cond_new();

        pa_mutex_lock(hal_event->mutex);

        PA_LLIST_INSERT_AFTER(struct pa_hal_event, u->hal_event_queue, u->hal_event_last, hal_event);
        u->hal_event_last = hal_event;

        u->core->mainloop->defer_enable(u->defer_event, 1);

        pa_cond_wait(hal_event->cond, hal_event->mutex);
        pa_mutex_unlock(hal_event->mutex);

        pa_log_info("%s is finished within thread", __get_event_type_string(PA_HAL_EVENT_CLOSE_ALL_DEVICES));

        __free_event(hal_event);
    } else {
        /* called from mainloop */
        pa_log_debug("%s is called within mainloop", __get_event_type_string(PA_HAL_EVENT_CLOSE_ALL_DEVICES));
        __close_all_devices(u);
        pa_log_info("%s is finished within mainloop", __get_event_type_string(PA_HAL_EVENT_CLOSE_ALL_DEVICES));
    }

    return AUDIO_RET_OK;
}

static audio_return_t __close_n_unload_device (struct userdata *u, audio_device_info_t *device_info, pa_hal_event_type_t hal_event_type)
{
    audio_info_t audio_info;
    bool is_module_loaded = false;
    pa_source *source_null, *source;
    pa_source_output *so;
    pa_sink *sink_null, *sink;
    pa_sink_input *si;
    uint32_t idx;

    pa_assert(u);
    pa_assert(device_info);
    pa_assert(device_info->direction == AUDIO_DIRECTION_IN || device_info->direction == AUDIO_DIRECTION_OUT);

     if (device_info->direction == AUDIO_DIRECTION_IN) {
        PA_IDXSET_FOREACH(source, u->core->sources, idx) {
            if (!AUDIO_IS_ERROR(__fill_audio_capture_device_info(source->proplist, &audio_info))) {
                if (__compare_device_info(&audio_info.device, device_info)) {
                    is_module_loaded = true;
                    __free_audio_device_info(&audio_info);
                    break;
                }
            }
            __free_audio_device_info(&audio_info);
        }

        if (is_module_loaded) {
            source_null = (pa_source *)pa_namereg_get(u->core, "null", PA_NAMEREG_SOURCE);
            if (pa_namereg_get_default_source(u->core) == source) {
                pa_namereg_set_default_source(u->core, source_null);
            }

            PA_IDXSET_FOREACH(so, u->core->source_outputs, idx) {
                if (__get_real_master_source(so) != source)
                    continue;
                /* Get role (if role is filter, skip it) */
#if 0
                if (policy_is_filter(so->proplist))
                    continue;
#endif
                /* Cork only if source-output was Running */
                if (pa_source_output_get_state(so) == PA_SOURCE_OUTPUT_RUNNING) {
                    pa_proplist_sets(so->proplist, PROP_POLICY_CORK, "1");
                    pa_source_output_cork(so, true);
                    pa_log_info("<Cork> for source-output[%d]:source[%s]", so->index, so->source->name);
                }
                if (source_null)
                    pa_source_output_move_to(so, source_null, false);
            }
            pa_source_suspend(source, true, PA_SUSPEND_INTERNAL);

            if (hal_event_type == PA_HAL_EVENT_UNLOAD_DEVICE) {
                pa_module_unload(u->core, source->module, true);
            }
        }
    } else {
        PA_IDXSET_FOREACH(sink, u->core->sinks, idx) {
            if (!AUDIO_IS_ERROR(__fill_audio_playback_device_info(sink->proplist, &audio_info))) {
                if (__compare_device_info(&audio_info.device, device_info)) {
                    is_module_loaded = true;
                    __free_audio_device_info(&audio_info);
                    break;
                }
            }
            __free_audio_device_info(&audio_info);
        }

        if (is_module_loaded) {
            sink_null = (pa_sink *)pa_namereg_get(u->core, "null", PA_NAMEREG_SINK);
            if (pa_namereg_get_default_sink(u->core) == sink) {
                pa_namereg_set_default_sink(u->core, sink_null);
            }

            PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
                if (__get_real_master_sink(si) != sink)
                    continue;
                /* Get role (if role is filter, skip it) */
                if (policy_is_filter(si))
                    continue;
                /* Cork only if sink-input was Running */
                if (pa_sink_input_get_state(si) == PA_SINK_INPUT_RUNNING) {
                    pa_proplist_sets(si->proplist, PROP_POLICY_CORK, "1");
                    pa_sink_input_cork(si, true);
                    pa_log_info("<Cork> for sink-input[%d]:sink[%s]", si->index, si->sink->name);
                }
                if (sink_null)
                    pa_sink_input_move_to(si, sink_null, false);
            }
            pa_sink_suspend(sink, true, PA_SUSPEND_INTERNAL);

            if (hal_event_type == PA_HAL_EVENT_UNLOAD_DEVICE) {
                pa_module_unload(u->core, sink->module, true);
            }
        }
    }

    return AUDIO_RET_OK;
}

static audio_return_t __close_n_unload_device_callback (void *platform_data, audio_device_info_t *device_info, pa_hal_event_type_t hal_event_type)
{
    struct userdata *u = (struct userdata *)platform_data;
    struct pa_hal_event *hal_event;
    struct pa_hal_device_event_data *event_data;

    pa_assert(u);
    pa_assert(device_info);
    pa_assert(device_info->direction == AUDIO_DIRECTION_IN || device_info->direction == AUDIO_DIRECTION_OUT);
    pa_assert(hal_event_type == PA_HAL_EVENT_CLOSE_DEVICE || hal_event_type == PA_HAL_EVENT_UNLOAD_DEVICE);

    if (u->tid != pthread_self()) {
        /* called from thread */
        pa_log_debug("%s is called within thread", __get_event_type_string(hal_event_type));

        hal_event = pa_xmalloc(sizeof(struct pa_hal_event));
        hal_event->userdata = u;
        hal_event->event_type = hal_event_type;
        event_data = pa_xmalloc(sizeof(struct pa_hal_device_event_data));
        memcpy(&event_data->device_info, device_info, sizeof(audio_device_info_t));
        hal_event->event_data = (void *)event_data;

        hal_event->mutex = pa_mutex_new(true, true);
        hal_event->cond = pa_cond_new();

        pa_mutex_lock(hal_event->mutex);

        PA_LLIST_INSERT_AFTER(struct pa_hal_event, u->hal_event_queue, u->hal_event_last, hal_event);
        u->hal_event_last = hal_event;

        u->core->mainloop->defer_enable(u->defer_event, 1);

        pa_cond_wait(hal_event->cond, hal_event->mutex);
        pa_mutex_unlock(hal_event->mutex);

        pa_log_info("%s is finished within thread", __get_event_type_string(hal_event_type));

        __free_event(hal_event);
    } else {
        /* called from mainloop */
        pa_log_debug("%s is called within mainloop", __get_event_type_string(hal_event_type));
        __close_n_unload_device(u, device_info, hal_event_type);
        pa_log_info("%s is finished within mainloop", __get_event_type_string(hal_event_type));
    }

    return AUDIO_RET_OK;
}

static audio_return_t __close_device_callback (void *platform_data, audio_device_info_t *device_info)
{
    return __close_n_unload_device_callback(platform_data, device_info, PA_HAL_EVENT_CLOSE_DEVICE);
}

static audio_return_t __unload_device_callback (void *platform_data, audio_device_info_t *device_info)
{
    return __close_n_unload_device_callback(platform_data, device_info, PA_HAL_EVENT_UNLOAD_DEVICE);
}

static uint32_t __get_route_flag(struct userdata *u) {
    uint32_t route_flag = 0;

    if (u->session == AUDIO_SESSION_VOICECALL || u->session == AUDIO_SESSION_VIDEOCALL) {
        if (u->subsession == AUDIO_SUBSESSION_RINGTONE) {
            if (__is_mute_policy()) {
                route_flag |= AUDIO_ROUTE_FLAG_MUTE_POLICY;
            }
            else if (!__is_recording()) {
               route_flag |= AUDIO_ROUTE_FLAG_DUAL_OUT;
            }
        } else {
            if (__is_noise_reduction_on())
                route_flag |= AUDIO_ROUTE_FLAG_NOISE_REDUCTION;
            if (__is_extra_volume_on())
                route_flag |= AUDIO_ROUTE_FLAG_EXTRA_VOL;
            if (__is_wideband())
                route_flag |= AUDIO_ROUTE_FLAG_NETWORK_WB;
        }
    } else if (u->session == AUDIO_SESSION_NOTIFICATION) {
        if (__is_mute_policy()) {
            route_flag |= AUDIO_ROUTE_FLAG_MUTE_POLICY;
        }
        else if (!__is_recording()) {
            route_flag |= AUDIO_ROUTE_FLAG_DUAL_OUT;
        }
    } else if(u->session == AUDIO_SESSION_ALARM) {
        if (!__is_recording()) {
            route_flag |= AUDIO_ROUTE_FLAG_DUAL_OUT;
        }
    }
    if (u->session == AUDIO_SESSION_VOICE_RECOGNITION) {
        /* add flag if needed */
    }

    return route_flag;
}
#ifdef BURST_SHOT

#define BURST_SOUND_DEFAULT_TIME_INTERVAL (0.09 * PA_USEC_PER_SEC)
static void __play_audio_sample_timeout_cb(pa_mainloop_api *m, pa_time_event *e, const struct timeval *t, void *userdata)
{
    struct userdata* u = (struct userdata*)userdata;
    pa_usec_t interval = u->audio_sample_userdata.time_interval;
    pa_usec_t now = 0ULL;

    pa_assert(m);
    pa_assert(e);
    pa_assert(u);

    pa_mutex_lock(u->audio_sample_userdata.mutex);

    /* These checks are added to avoid server crashed on unpredictable situation*/
    if ((u->audio_sample_userdata.time_event == NULL) ||
       (u->audio_sample_userdata.i == NULL) ||
       (u->audio_sample_userdata.q == NULL)) {
        pa_log_error("Timer should not have fired with this condition time_event=%p i=%p q=%p",
            u->audio_sample_userdata.time_event, u->audio_sample_userdata.i, u->audio_sample_userdata.q);

        if (u->audio_sample_userdata.time_event != NULL) {
            pa_core_rttime_restart(u->core, e, PA_USEC_INVALID);
            u->core->mainloop->time_free(u->audio_sample_userdata.time_event);
            u->audio_sample_userdata.time_event = NULL;
        }
        if (u->audio_sample_userdata.count > 1) {
            if (u->audio_sample_userdata.i != NULL) {
                pa_sink_input_set_mute(u->audio_sample_userdata.i, true, true);
                pa_sink_input_unlink(u->audio_sample_userdata.i);
            }
            if (u->audio_sample_userdata.q != NULL) {
                pa_memblockq_free(u->audio_sample_userdata.q);
                u->audio_sample_userdata.q = NULL;
            }
        }
        if (u->audio_sample_userdata.i != NULL) {
            pa_sink_input_unref(u->audio_sample_userdata.i);
            u->audio_sample_userdata.i = NULL;
        }
        u->audio_sample_userdata.is_running = false;
    } else if (u->audio_sample_userdata.is_running) {
        // calculate timer boosting
        pa_log_info("- shot count = %d, memq len = %d ", u->audio_sample_userdata.count,
                    pa_memblockq_get_length(u->audio_sample_userdata.q));
        if (u->audio_sample_userdata.factor > 1ULL)
            interval = u->audio_sample_userdata.time_interval / u->audio_sample_userdata.factor;

        if (u->audio_sample_userdata.count == 0) {
            // 5. first post data
            pa_sink_input_put(u->audio_sample_userdata.i);
        } else {
            // 5. post data
            if (pa_memblockq_push(u->audio_sample_userdata.q, &u->audio_sample_userdata.e->memchunk) < 0) {
                pa_log_error("memory push fail cnt(%d), factor(%llu), interval(%llu)",
                    u->audio_sample_userdata.count, u->audio_sample_userdata.factor, u->audio_sample_userdata.time_interval);
                pa_assert(0);
            }
        }
        u->audio_sample_userdata.count++;

        pa_rtclock_now_args(&now);
        pa_core_rttime_restart(u->core, e, now + interval);
        if (u->audio_sample_userdata.factor > 1ULL)
            u->audio_sample_userdata.factor -= 1ULL;
    } else {
        pa_core_rttime_restart(u->core, e, PA_USEC_INVALID);
        u->core->mainloop->time_free(u->audio_sample_userdata.time_event);
        u->audio_sample_userdata.time_event = NULL;

        /* FIXME: How memblockq is freed when count is 1? */
        // fading. but should be emitted first shutter sound totally.
        if (u->audio_sample_userdata.count > 1) {
            pa_sink_input_set_mute(u->audio_sample_userdata.i, true, true);
            pa_sink_input_unlink(u->audio_sample_userdata.i);

            pa_memblockq_free(u->audio_sample_userdata.q);
            u->audio_sample_userdata.q = NULL;
        }
        pa_sink_input_unref(u->audio_sample_userdata.i);
        u->audio_sample_userdata.i = NULL;
        pa_log_info("sample shot clear!!");

        /* Clear Burst vconf */
        vconf_set_int (VCONF_SOUND_BURSTSHOT, 0);
    }
    pa_mutex_unlock(u->audio_sample_userdata.mutex);
}

static audio_return_t policy_play_sample_continuously(struct userdata *u, pa_native_connection *c, const char *name, pa_usec_t interval,
    uint32_t volume_type, uint32_t gain_type, uint32_t volume_level, uint32_t *stream_idx)
{
    audio_return_t audio_ret = AUDIO_RET_OK;
    pa_proplist *p = 0;
    pa_sink *sink = NULL;
    audio_info_t audio_info;
    double volume_linear = 1.0f;
    pa_client *client = pa_native_connection_get_client(c);

    pa_scache_entry *e;
    pa_bool_t pass_volume = true;
    pa_proplist *merged =0;
    pa_sink_input *i = NULL;
    pa_memblockq *q = NULL;
    pa_memchunk silence;
    pa_cvolume r;
    pa_usec_t now = 0ULL;

    if (!u->audio_sample_userdata.mutex)
        u->audio_sample_userdata.mutex = pa_mutex_new(false, false);

    pa_mutex_lock(u->audio_sample_userdata.mutex);

    pa_assert(u->audio_sample_userdata.is_running == false); // allow one instace.

    memset(&audio_info, 0x00, sizeof(audio_info_t));

    p = pa_proplist_new();

    /* Set volume type of stream */
    pa_proplist_setf(p, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE, "%d", volume_type);
    /* Set gain type of stream */
    pa_proplist_setf(p, PA_PROP_MEDIA_TIZEN_GAIN_TYPE, "%d", gain_type);
    /* Set policy */
    pa_proplist_setf(p, PA_PROP_MEDIA_POLICY, "%s", volume_type == AUDIO_VOLUME_TYPE_FIXED ? POLICY_PHONE : POLICY_AUTO);

    pa_proplist_update(p, PA_UPDATE_MERGE, client->proplist);

    sink = pa_namereg_get_default_sink(u->core);

    /* FIXME : Add gain_type parameter to API like volume_type */
    audio_info.stream.gain_type = gain_type;

    if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.get_volume_value(u->audio_mgr.data, &audio_info, volume_type, volume_level, &volume_linear)))) {
        pa_log_warn("get_volume_value returns error:0x%x", audio_ret);
        goto exit;
    }

   /*
    1. load cam-shutter sample
    2. create memchunk using sample.
    3. create sink_input(cork mode)
    4. set timer
    5. post data(sink-input put or push memblockq)
    */

    //  1. load cam-shutter sample
    merged = pa_proplist_new();

    if (!(e = pa_namereg_get(u->core, name, PA_NAMEREG_SAMPLE)))
        goto exit;

    pa_proplist_sets(merged, PA_PROP_MEDIA_NAME, name);
    pa_proplist_sets(merged, PA_PROP_EVENT_ID, name);
    /* Set policy for selecting sink */
    pa_proplist_sets(merged, PA_PROP_MEDIA_POLICY_IGNORE_PRESET_SINK, "yes");

    if (e->lazy && !e->memchunk.memblock) {
        pa_channel_map old_channel_map = e->channel_map;

        if (pa_sound_file_load(u->core->mempool, e->filename, &e->sample_spec, &e->channel_map, &e->memchunk, merged) < 0)
            goto exit;

        pa_subscription_post(u->core, PA_SUBSCRIPTION_EVENT_SAMPLE_CACHE|PA_SUBSCRIPTION_EVENT_CHANGE, e->index);

        if (e->volume_is_set) {
            if (pa_cvolume_valid(&e->volume))
                pa_cvolume_remap(&e->volume, &old_channel_map, &e->channel_map);
            else
                pa_cvolume_reset(&e->volume, e->sample_spec.channels);
        }
    }

    if (!e->memchunk.memblock)
        goto exit;

    if (e->volume_is_set && PA_VOLUME_IS_VALID(pa_sw_volume_from_linear(volume_linear))) {
        pa_cvolume_set(&r, e->sample_spec.channels, pa_sw_volume_from_linear(volume_linear));
        pa_sw_cvolume_multiply(&r, &r, &e->volume);
    } else if (e->volume_is_set)
        r = e->volume;
    else if (PA_VOLUME_IS_VALID(pa_sw_volume_from_linear(volume_linear)))
        pa_cvolume_set(&r, e->sample_spec.channels, pa_sw_volume_from_linear(volume_linear));
    else
        pass_volume = false;

    pa_proplist_update(merged, PA_UPDATE_MERGE, e->proplist);
    pa_proplist_update(p, PA_UPDATE_MERGE, merged);

    if (e->lazy)
        time(&e->last_used_time);

    // 2. create memchunk using sample.
    pa_silence_memchunk_get(&sink->core->silence_cache, sink->core->mempool, &silence, &e->sample_spec, 0);
    q = pa_memblockq_new("pa_play_memchunk() q", 0, e->memchunk.length * 35, 0, &e->sample_spec, 1, 1, 0, &silence);
    pa_memblock_unref(silence.memblock);

    pa_assert_se(pa_memblockq_push(q, &e->memchunk) >= 0);

    // 3. create sink_input(cork mode)
    if (!(i = pa_memblockq_sink_input_new(sink, &e->sample_spec, &e->channel_map, q, pass_volume ? &r : NULL,
        p, PA_SINK_INPUT_NO_CREATE_ON_SUSPEND|PA_SINK_INPUT_KILL_ON_SUSPEND)))
        goto exit;

    // 4. set timer
    u->audio_sample_userdata.e = e;
    u->audio_sample_userdata.i = i;
    u->audio_sample_userdata.q = q;
    u->audio_sample_userdata.time_interval = interval == (pa_usec_t)0 ? BURST_SOUND_DEFAULT_TIME_INTERVAL : interval;
    u->audio_sample_userdata.is_running = true;
    u->audio_sample_userdata.factor = 4ULL; // for memory block boosting
    u->audio_sample_userdata.count = 0;

    pa_rtclock_now_args(&now); // doesn't use arm barrel shiter. SBF
    pa_log_warn("now(%llu), start interval(%llu)", now, interval / u->audio_sample_userdata.factor);
    u->audio_sample_userdata.factor -= 1ULL;
    u->audio_sample_userdata.time_event = pa_core_rttime_new(u->core, now, __play_audio_sample_timeout_cb, u);

exit:
    if (p)
        pa_proplist_free(p);
    if (merged)
        pa_proplist_free(merged);
    if (q && (u->audio_sample_userdata.is_running == false))
        pa_memblockq_free(q);

    pa_mutex_unlock(u->audio_sample_userdata.mutex);

    return audio_ret;
}

static void  policy_stop_sample_continuously(struct userdata *u)
{
    if (u->audio_sample_userdata.time_event) {
        pa_mutex_lock(u->audio_sample_userdata.mutex);
        pa_assert(u->audio_sample_userdata.is_running);
        u->audio_sample_userdata.is_running = false;
        pa_mutex_unlock(u->audio_sample_userdata.mutex);
        pa_log_info("timeout_cb called (%d) times", u->audio_sample_userdata.count);
    }
}

#endif
static audio_return_t policy_play_sample(struct userdata *u, pa_native_connection *c, const char *name, uint32_t volume_type, uint32_t gain_type, uint32_t volume_level, uint32_t *stream_idx)
{
    audio_return_t audio_ret = AUDIO_RET_OK;
    pa_proplist *p;
    pa_sink *sink = NULL;
    audio_info_t audio_info;
    double volume_linear = 1.0f;
    pa_client *client = pa_native_connection_get_client(c);
    pa_bool_t is_boot_sound;
    uint32_t sample_idx = 0;
    char* booting = NULL;
    const char* file_to_add = NULL;
    int sample_ret = 0;

    memset(&audio_info, 0x00, sizeof(audio_info_t));

    p = pa_proplist_new();

    /* Set volume type of stream */
    pa_proplist_setf(p, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE, "%d", volume_type);
    /* Set gain type of stream */
    pa_proplist_setf(p, PA_PROP_MEDIA_TIZEN_GAIN_TYPE, "%d", gain_type);
    /* Set policy */
    pa_proplist_setf(p, PA_PROP_MEDIA_POLICY, "%s", volume_type == AUDIO_VOLUME_TYPE_FIXED ? POLICY_PHONE : POLICY_AUTO);

    pa_proplist_update(p, PA_UPDATE_MERGE, client->proplist);

    /* Set policy for selecting sink */
    pa_proplist_sets(p, PA_PROP_MEDIA_POLICY_IGNORE_PRESET_SINK, "yes");
    sink = pa_namereg_get_default_sink(u->core);

    /* FIXME : Add gain_type parameter to API like volume_type */
    audio_info.stream.gain_type = gain_type;

    if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.get_volume_value(u->audio_mgr.data, &audio_info, volume_type, volume_level, &volume_linear)))) {
        pa_log_warn("get_volume_value returns error:0x%x", audio_ret);
        goto exit;
    }

    pa_log_debug("play_sample volume_type:%d gain_type:%d volume_linear:%f", volume_type, gain_type, volume_linear);

    is_boot_sound = pa_streq(name, BOOTING_SOUND_SAMPLE);
    if (is_boot_sound && pa_namereg_get(u->core, name, PA_NAMEREG_SAMPLE) == NULL) {
        booting = vconf_get_str(VCONF_BOOTING);
        file_to_add = (booting)? booting : (char *)DEFAULT_BOOTING_SOUND_PATH;
        if ((sample_ret = pa_scache_add_file(u->core, name, file_to_add, &sample_idx)) != 0) {
            pa_log_error("failed to add sample [%s][%s]", name, file_to_add);
        } else {
            pa_log_info("success to add sample [%s][%s]", name, file_to_add);
        }
        if (booting)
            free(booting);
    }

    if (pa_scache_play_item(u->core, name, sink, pa_sw_volume_from_linear(volume_linear), p, stream_idx) < 0) {
        pa_log_error("pa_scache_play_item fail");
        audio_ret = AUDIO_ERR_UNDEFINED;
        goto exit;
    }

    if (is_boot_sound && sample_ret == 0) {
        if (pa_scache_remove_item(u->core, name) != 0) {
            pa_log_error("failed to remove sample [%s]", name);
        } else {
            pa_log_info("success to remove sample [%s]", name);
        }
    }

exit:
    pa_proplist_free(p);

    return audio_ret;
}

static audio_return_t policy_reset(struct userdata *u)
{
    audio_return_t audio_ret = AUDIO_RET_OK;

    pa_log_debug("reset");
    __load_dump_config(u);
    if (u->audio_mgr.intf.reset) {
        if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.reset(&u->audio_mgr.data)))) {
            pa_log_error("audio_mgr reset failed");
            return audio_ret;
        }
    }

    return audio_ret;
}

static audio_return_t policy_set_session(struct userdata *u, uint32_t session, uint32_t start) {
    uint32_t prev_session = u->session;
    uint32_t prev_subsession = u->subsession;
    pa_bool_t need_route = false;

    pa_log_info("set_session:%s %s (current:%s,%s)",
            __get_session_str(session), (start) ? "start" : "end", __get_session_str(u->session), __get_subsession_str(u->subsession));

    if (start) {
        u->session = session;

        if ((u->session == AUDIO_SESSION_VOICECALL) || (u->session == AUDIO_SESSION_VIDEOCALL)) {
            u->subsession = AUDIO_SUBSESSION_MEDIA;
            u->call_muted = 0;
        } else if (u->session == AUDIO_SESSION_VOIP) {
            u->subsession = AUDIO_SUBSESSION_VOICE;
        } else if (u->session == AUDIO_SESSION_VOICE_RECOGNITION) {
            u->subsession = AUDIO_SUBSESSION_INIT;
        } else {
            u->subsession = AUDIO_SUBSESSION_NONE;
        }
        if (u->audio_mgr.intf.set_session) {
            u->audio_mgr.intf.set_session(u->audio_mgr.data, session, u->subsession, AUDIO_SESSION_CMD_START);
        }
    } else {
        if (u->audio_mgr.intf.set_session) {
            u->audio_mgr.intf.set_session(u->audio_mgr.data, session, u->subsession, AUDIO_SESSION_CMD_END);
        }
        u->session = AUDIO_SESSION_MEDIA;
        u->subsession = AUDIO_SUBSESSION_NONE;
    }
    if (prev_session != session) {
        if ((session == AUDIO_SESSION_ALARM) || (session == AUDIO_SESSION_NOTIFICATION)) {
            pa_log_info("switch route to dual output due to new session");
            need_route = true;
        } else if ((prev_session == AUDIO_SESSION_ALARM) || (prev_session == AUDIO_SESSION_NOTIFICATION)
            || (((prev_session == AUDIO_SESSION_VOICECALL) || (prev_session == AUDIO_SESSION_VIDEOCALL)) && (prev_subsession == AUDIO_SUBSESSION_RINGTONE))) {
            pa_log_info("switch route from dual output due to previous session");
            need_route = true;
        }
    }


    if (need_route) {
        uint32_t route_flag = __get_route_flag(u);

        if (u->audio_mgr.intf.set_route) {
            u->audio_mgr.intf.set_route(u->audio_mgr.data, u->session, u->subsession, u->active_device_in, u->active_device_out, route_flag);
        }
        u->active_route_flag = route_flag;
    } else {
        /* route should be updated */
        u->active_route_flag = (uint32_t)-1;
    }

    return AUDIO_RET_OK;
}

static audio_return_t policy_set_subsession(struct userdata *u, uint32_t subsession, uint32_t subsession_opt) {
    uint32_t prev_subsession = u->subsession;
    pa_bool_t need_route = false;

    pa_log_info("set_subsession:%s->%s opt:%x->%x (session:%s)",
            __get_subsession_str(u->subsession), __get_subsession_str(subsession), u->subsession_opt, subsession_opt,
            __get_session_str(u->session));

    if (u->subsession == subsession && u->subsession_opt == subsession_opt) {
        pa_log_debug("duplicated request is ignored subsession(%d) opt(0x%x)", subsession, subsession_opt);
        return AUDIO_RET_OK;
    }

    u->subsession = subsession;
#ifndef _TIZEN_PUBLIC_
    if (u->subsession == AUDIO_SUBSESSION_VR_NORMAL || u->subsession == AUDIO_SUBSESSION_VR_DRIVE)
        u->subsession_opt = subsession_opt;
    else
        u->subsession_opt = 0;
#else
    u->subsession_opt = subsession_opt;
#endif

    if (u->audio_mgr.intf.set_session) {
        u->audio_mgr.intf.set_session(u->audio_mgr.data, u->session, u->subsession, AUDIO_SESSION_CMD_SUBSESSION);
    }

    if (prev_subsession!= subsession) {
        if ((u->session == AUDIO_SESSION_VOICECALL) || (u->session == AUDIO_SESSION_VIDEOCALL)) {
            if (subsession == AUDIO_SUBSESSION_RINGTONE) {
                pa_log_info("switch route to dual output due to new subsession");
                need_route = true;
            } else if (prev_subsession == AUDIO_SUBSESSION_RINGTONE) {
                pa_log_info("switch route from dual output due to previous subsession");
                need_route = true;
            }
        }
    }

    if (need_route) {
        uint32_t route_flag = __get_route_flag(u);

        if (u->audio_mgr.intf.set_route) {
            u->audio_mgr.intf.set_route(u->audio_mgr.data, u->session, u->subsession, u->active_device_in, u->active_device_out, route_flag);
        }
        u->active_route_flag = route_flag;
    } else {
        /* route should be updated */
        u->active_route_flag = (uint32_t)-1;
    }
    return AUDIO_RET_OK;

}
static pa_bool_t pa_sink_input_get_mute(pa_sink_input *i) {
    pa_sink_input_assert_ref(i);
    pa_assert_ctl_context();
    pa_assert(PA_SINK_INPUT_IS_LINKED(i->state));

    return i->muted;
}
static audio_return_t policy_set_active_device(struct userdata *u, uint32_t device_in, uint32_t device_out, uint32_t* need_update) {
    pa_sink_input *si = NULL;
    uint32_t idx;
    uint32_t route_flag = 0;

#ifndef TIZEN_MICRO
    *need_update = true;
#endif
    route_flag = __get_route_flag(u);

    pa_log_info("set_active_device session:%s,%s in:%s->%s out:%s->%s flag:%x->%x muteall:%d call_muted:%d",
            __get_session_str(u->session), __get_subsession_str(u->subsession),
            __get_device_in_str(u->active_device_in), __get_device_in_str(device_in),
            __get_device_out_str(u->active_device_out), __get_device_out_str(device_out),
            u->active_route_flag, route_flag, u->muteall, u->call_muted);

    /* Skip duplicated request */
    if ((device_in == AUDIO_DEVICE_IN_NONE || u->active_device_in == device_in) &&
        (device_out == AUDIO_DEVICE_OUT_NONE || u->active_device_out == device_out) &&
        u->active_route_flag == route_flag) {
#ifdef TIZEN_MICRO
        pa_log_debug("duplicated request is ignored device_in(%d) device_out(%d) flag(0x%x)", device_in, device_out, route_flag);
#else
        pa_log_debug("duplicated request is ignored device_in(%d) device_out(%d) flag(0x%x) need_update(%d)", device_in, device_out, route_flag, *need_update);
#endif
        return AUDIO_RET_OK;
    }

#ifdef TIZEN_MICRO
    if (u->audio_mgr.intf.set_route) {
        u->audio_mgr.intf.set_route(u->audio_mgr.data, u->session, u->subsession, device_in, device_out, route_flag);
    }
#else

    /* skip volume changed callback */
    if(u->active_device_out == device_out) {
        *need_update = false;
    }
    if (u->audio_mgr.intf.set_route) {
        audio_return_t audio_ret = AUDIO_RET_OK;
        const char *device_switching_str;
        uint32_t device_switching = 0;
#ifdef PA_SLEEP_DURING_UCM
        uint32_t need_sleep_for_ucm = 0;
#endif

        /* Mute sink inputs which are unmuted */
        PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
            if ((device_switching_str = pa_proplist_gets(si->proplist, "module-policy.device_switching"))) {
                pa_atou(device_switching_str, &device_switching);
                if (device_switching) {
                    if (AUDIO_IS_ERROR((audio_ret = policy_set_mute(u, si->index, (uint32_t)-1, AUDIO_DIRECTION_OUT, 1)))) {
                        pa_log_warn("policy_set_mute(1) for stream[%d] returns error:0x%x", si->index, audio_ret);
                    }
#ifdef PA_SLEEP_DURING_UCM
                    need_sleep_for_ucm = 1;
#endif
                }
            }
        }

#ifdef PA_SLEEP_DURING_UCM
        /* FIXME : sleep for ucm. Will enable if needed */
        if (need_sleep_for_ucm) {
            usleep(150000);
        }
#endif

        u->audio_mgr.intf.set_route(u->audio_mgr.data, u->session, u->subsession, device_in, device_out, route_flag);
        /* Unmute sink inputs which are muted due to device switching */
        PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
            if ((device_switching_str = pa_proplist_gets(si->proplist, "module-policy.device_switching"))) {
                pa_atou(device_switching_str, &device_switching);
                if (device_switching) {
                    pa_proplist_sets(si->proplist, "module-policy.device_switching", "0");
                    if (AUDIO_IS_ERROR((audio_ret = __update_volume(u, si->index, (uint32_t)-1, (uint32_t)-1)))) {
                        pa_log_warn("__update_volume for stream[%d] returns error:0x%x", si->index, audio_ret);
                    }
                    if (AUDIO_IS_ERROR((audio_ret = policy_set_mute(u, si->index, (uint32_t)-1, AUDIO_DIRECTION_OUT, 0)))) {
                        pa_log_warn("policy_set_mute(0) for stream[%d] returns error:0x%x", si->index, audio_ret);
                    }
                }
            }
        }
    }
#endif

    /* sleep for avoiding sound leak during UCM switching
       this is just a workaround, we should synchronize in future */
    if (device_out != AUDIO_DEVICE_OUT_NONE && u->active_device_out != device_out &&
        u->session != AUDIO_SESSION_VOICECALL && u->session != AUDIO_SESSION_VIDEOCALL) {
        /* Mute sink inputs which are unmuted */
        PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
            if (!pa_sink_input_get_mute(si)) {
                pa_proplist_sets(si->proplist, "module-policy.device_switching", "1");
            }
        }
    }

    /* Update active devices */
    if (device_in != AUDIO_DEVICE_IN_NONE)
        u->active_device_in = device_in;
    if (device_out != AUDIO_DEVICE_OUT_NONE)
        u->active_device_out = device_out;
    u->active_route_flag = route_flag;

    if (u->session == AUDIO_SESSION_VOICECALL) {
        if (u->muteall) {
            policy_set_mute(u, (-1), AUDIO_VOLUME_TYPE_CALL, AUDIO_DIRECTION_OUT, 1);
        }
        /* workaround for keeping call mute setting */
        policy_set_mute(u, (-1), AUDIO_VOLUME_TYPE_CALL, AUDIO_DIRECTION_IN, u->call_muted);
    }

    return AUDIO_RET_OK;
}

static audio_return_t policy_get_volume_level_max(struct userdata *u, uint32_t volume_type, uint32_t *volume_level) {
    audio_return_t audio_ret = AUDIO_RET_OK;

    /* Call HAL function if exists */
    if (u->audio_mgr.intf.get_volume_level_max) {
        if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.get_volume_level_max(u->audio_mgr.data, volume_type, volume_level)))) {
            pa_log_error("get_volume_level_max returns error:0x%x", audio_ret);
            return audio_ret;
        }
    }
    pa_log_info("get volume level max type:%d level:%d", volume_type, *volume_level);
    return AUDIO_RET_OK;
}

static audio_return_t __update_volume(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, uint32_t volume_level)
{
    audio_return_t audio_ret = AUDIO_RET_OK;
    pa_sink_input *si = NULL;
    uint32_t idx;
    audio_info_t audio_info;

    pa_log_info("stream[%d], volume_type(%d), volume_level(%d)", stream_idx, volume_type, volume_level);

    /* Update volume as current level if volume_level has -1 */
    if (volume_level == (uint32_t)-1 && stream_idx != (uint32_t)-1) {
        /* Skip updating if stream doesn't have volume type */
        if (policy_get_volume_level(u, stream_idx, &volume_type, &volume_level) == AUDIO_ERR_UNDEFINED) {
            return AUDIO_RET_OK;
        }
    }

    if (u->muteall && (volume_type != AUDIO_VOLUME_TYPE_FIXED)) {
        pa_log_debug("set_mute is called from __update_volume by muteall stream_idx:%d type:%d", stream_idx, volume_type);

        if (policy_set_mute(u, stream_idx, volume_type, AUDIO_DIRECTION_OUT, 1) == AUDIO_RET_USE_HW_CONTROL) {
            return AUDIO_RET_USE_HW_CONTROL;
        }
    }

    /* Call HAL function if exists */
    if (u->audio_mgr.intf.set_volume_level && (stream_idx == PA_INVALID_INDEX)) {
        if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.set_volume_level(u->audio_mgr.data, NULL, volume_type, volume_level)))) {
            pa_log_error("set_volume_level returns error:0x%x", audio_ret);
            return audio_ret;
        }
    }

    PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {

        if (AUDIO_IS_ERROR(__fill_audio_playback_info(si, &audio_info))) {
            /* skip mono sink-input */
            continue;
        }

        /* Update volume of stream if it has requested volume type */
        if ((stream_idx == idx) || ((stream_idx == PA_INVALID_INDEX) && (audio_info.stream.volume_type == volume_type))) {
            double volume_linear = 1.0f;
            pa_cvolume cv;

            /* Call HAL function if exists */
            if (u->audio_mgr.intf.set_volume_level) {
                if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.set_volume_level(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, volume_level)))) {
                    pa_log_error("set_volume_level for sink-input[%d] returns error:0x%x", idx, audio_ret);
                    __free_audio_info(&audio_info);
                    return audio_ret;
                }
            }

            /* Get volume value by type & level */
            if (u->audio_mgr.intf.get_volume_value && (audio_ret != AUDIO_RET_USE_HW_CONTROL)) {
                if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.get_volume_value(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, volume_level, &volume_linear)))) {
                    pa_log_warn("get_volume_value for sink-input[%d] returns error:0x%x", idx, audio_ret);
                    __free_audio_info(&audio_info);
                    return audio_ret;
                }
            }
            pa_cvolume_set(&cv, si->sample_spec.channels, pa_sw_volume_from_linear(volume_linear));

            pa_sink_input_set_volume(si, &cv, true, true);
            if (idx == stream_idx) {
                __free_audio_info(&audio_info);
                break;
            }
        }
        __free_audio_info(&audio_info);
    }

    return audio_ret;
}

static audio_return_t __update_volume_by_value(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, double* value)
{
    audio_return_t audio_ret = AUDIO_RET_OK;
    pa_sink_input *si = NULL;
    uint32_t idx;
    audio_info_t audio_info;

    double volume = *value;
    double gain = 1.0f;

    PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {

        if (AUDIO_IS_ERROR(__fill_audio_playback_info(si, &audio_info))) {
            /* skip mono sink-input */
            continue;
        }

        /* Update volume of stream if it has requested volume type */
        if ((stream_idx == idx) || ((stream_idx == PA_INVALID_INDEX) && (audio_info.stream.volume_type == volume_type))) {
            double volume_linear = 1.0f;
            pa_cvolume cv;

            // 1. get gain first
            if (u->audio_mgr.intf.get_gain_value) {
                if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.get_gain_value(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, &gain)))) {
                    pa_log_warn("get_gain_value for sink-input[%d] volume_type(%d), returns error:0x%x", idx, audio_info.stream.volume_type, audio_ret);
                    __free_audio_info(&audio_info);
                    return audio_ret;
                }
            }

            // 2. mul gain value
            volume *= gain;

            /* 3. adjust hw volume(LPA), Call HAL function if exists */
            if (u->audio_mgr.intf.set_volume_value) {
                if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.set_volume_value(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, &volume)))) {
                    pa_log_error("set_volume_level for sink-input[%d] returns error:0x%x", idx, audio_ret);
                    __free_audio_info(&audio_info);
                    return audio_ret;
                }
            }

            // 4. adjust sw volume.
            if(audio_ret != AUDIO_RET_USE_HW_CONTROL)
                pa_cvolume_set(&cv, si->sample_spec.channels, pa_sw_volume_from_linear(volume));
            else
                pa_cvolume_set(&cv, si->sample_spec.channels, pa_sw_volume_from_linear(volume_linear));

            pa_sink_input_set_volume(si, &cv, true, true);

            if (idx == stream_idx) {
                __free_audio_info(&audio_info);
                break;
            }
        }
        __free_audio_info(&audio_info);
    }

    return audio_ret;
}

#ifdef PRIMARY_VOLUME
static int __set_primary_volume(struct userdata *u, void* key, int volumetype, int is_new)
{
    const int NO_INSTANCE = -1;
    const int CAPURE_ONLY = -2; // check mm_sound.c

    int ret = -1;
    int default_primary_vol = NO_INSTANCE;
    int default_primary_vol_prio = NO_INSTANCE;

    struct pa_primary_volume_type_info* p_volume = NULL;
    struct pa_primary_volume_type_info* n_p_volume = NULL;
    struct pa_primary_volume_type_info* new_volume = NULL;

    // descending order
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
        AUDIO_PRIMARY_VOLUME_TYPE_MAX // for capture handle
    };

    if(is_new) {
        new_volume = pa_xnew0(struct pa_primary_volume_type_info, 1);
        new_volume->key = key;
        new_volume->volumetype = volumetype;
        new_volume->priority = priority[volumetype];

        // no items.
        if(u->primary_volume == NULL) {
            PA_LLIST_PREPEND(struct pa_primary_volume_type_info, u->primary_volume, new_volume);
        } else {
            // already added
            PA_LLIST_FOREACH_SAFE(p_volume, n_p_volume, u->primary_volume) {
                if(p_volume->key == key) {
                    ret = 0;
                    pa_xfree(new_volume);
                    goto exit;
                }
            }

            // add item.
            PA_LLIST_FOREACH_SAFE(p_volume, n_p_volume, u->primary_volume) {
                if(p_volume->priority <= priority[volumetype]) {
                    PA_LLIST_INSERT_AFTER(struct pa_primary_volume_type_info, u->primary_volume, p_volume, new_volume);
                    break;
                } else if(p_volume->priority > priority[volumetype]) {
                    PA_LLIST_PREPEND(struct pa_primary_volume_type_info, u->primary_volume, new_volume);
                    break;
                }
            }
        }
        pa_log_info("add volume data to primary volume list. volumetype(%d), priority(%d)", new_volume->volumetype, new_volume->priority);
    } else { // remove(unlink)
        PA_LLIST_FOREACH_SAFE(p_volume, n_p_volume, u->primary_volume) {
            if(p_volume->key == key) {
                PA_LLIST_REMOVE(struct pa_primary_volume_type_info, u->primary_volume, p_volume);
                pa_log_info("remove volume data from primary volume list. volumetype(%d), priority(%d)", p_volume->volumetype, p_volume->priority);
                pa_xfree(p_volume);
                break;
            }
        }
    }

    if(u->primary_volume) {
        if(u->primary_volume->volumetype == AUDIO_PRIMARY_VOLUME_TYPE_MAX) {
            default_primary_vol = CAPURE_ONLY;
            default_primary_vol_prio = CAPURE_ONLY;
        } else {
            default_primary_vol = u->primary_volume->volumetype;
            default_primary_vol_prio = u->primary_volume->priority;
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


static audio_return_t policy_get_volume_level(struct userdata *u, uint32_t stream_idx, uint32_t *volume_type, uint32_t *volume_level) {
    pa_sink_input *si = NULL;
    const char *si_volume_type_str;

    if (*volume_type == (uint32_t)-1 && stream_idx != (uint32_t)-1) {
        if ((si = pa_idxset_get_by_index(u->core->sink_inputs, stream_idx))) {
            if ((si_volume_type_str = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
                pa_atou(si_volume_type_str, volume_type);
            } else {
                pa_log_debug("stream[%d] doesn't have volume type", stream_idx);
                return AUDIO_ERR_UNDEFINED;
            }
        } else {
            pa_log_warn("stream[%d] doesn't exist", stream_idx);
            return AUDIO_ERR_PARAMETER;
        }
    }

    if (*volume_type >= AUDIO_VOLUME_TYPE_MAX) {
        pa_log_warn("volume_type (%d) invalid", *volume_type);
        return AUDIO_ERR_PARAMETER;
    }
    if (u->audio_mgr.intf.get_volume_level) {
        u->audio_mgr.intf.get_volume_level(u->audio_mgr.data, *volume_type, volume_level);
    }

    pa_log_info("get_volume_level stream_idx:%d type:%d level:%d", stream_idx, *volume_type, *volume_level);
    return AUDIO_RET_OK;
}

static audio_return_t policy_get_volume_value(struct userdata *u, uint32_t stream_idx, uint32_t *volume_type, uint32_t *volume_level, double* volume_linear) {
    audio_return_t audio_ret = AUDIO_RET_OK;
    audio_info_t audio_info;
    pa_sink_input *si = NULL;

    *volume_linear = 1.0f;

    si = pa_idxset_get_by_index(u->core->sink_inputs, stream_idx);
    if (si != NULL) {
        if (AUDIO_IS_ERROR(__fill_audio_playback_info(si, &audio_info))) {
            pa_log_debug("fill info failed. stream_idx[%d]", stream_idx);
            return AUDIO_ERR_UNDEFINED;
        }

        if(u->audio_mgr.intf.get_volume_value) {
            if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.get_volume_value(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, *volume_level, volume_linear)))) {
                pa_log_warn("get_volume_value for stream_idx[%d] returns error:0x%x", stream_idx, audio_ret);
                return audio_ret;
            }
        }
        __free_audio_info(&audio_info);
   }
    return audio_ret;
}

static audio_return_t policy_set_volume_level(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, uint32_t volume_level) {

    pa_log_info("set_volume_level stream_idx:%d type:%d level:%d", stream_idx, volume_type, volume_level);

    /* Store volume level of type */
    if (volume_type != (uint32_t)-1) {
        if (u->audio_mgr.intf.set_volume_level) {
            u->audio_mgr.intf.set_volume_level(u->audio_mgr.data, NULL, volume_type, volume_level);
        }
    }

    return __update_volume(u, stream_idx, volume_type, volume_level);
}

static audio_return_t policy_set_volume_value(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, double* value) {

    pa_log_info("set_volume_value stream_idx:%d type:%d value:%f", stream_idx, volume_type, *value);

    return __update_volume_by_value(u, stream_idx, volume_type, value);
}

static audio_return_t policy_update_volume(struct userdata *u) {
    uint32_t volume_type;
    uint32_t volume_level = 0;

    pa_log_info("update_volume");

    for (volume_type = 0; volume_type < AUDIO_VOLUME_TYPE_MAX; volume_type++) {
        if (u->audio_mgr.intf.get_volume_level) {
            u->audio_mgr.intf.get_volume_level(u->audio_mgr.data, volume_type, &volume_level);
        }
        __update_volume(u, (uint32_t)-1, volume_type, volume_level);
#ifdef WEARABLE_FIX // commit : update call mute status after changing audio pathupdate call mute status after changing audio path
        /* workaround for updating call mute after setting call path */
        if (u->session == AUDIO_SESSION_VOICECALL) {
            uint32_t call_muted = 0;

            if (u->audio_mgr.intf.get_mute) {
                u->audio_mgr.intf.get_mute(u->audio_mgr.data, NULL, AUDIO_VOLUME_TYPE_CALL, AUDIO_DIRECTION_IN, &call_muted);
                if (u->call_muted != (int)call_muted && u->audio_mgr.intf.set_mute) {
                    u->audio_mgr.intf.set_mute(u->audio_mgr.data, NULL, AUDIO_VOLUME_TYPE_CALL, AUDIO_DIRECTION_IN, u->call_muted);
                }
            }
        }
#endif
    }

    return AUDIO_RET_OK;
}

static audio_return_t policy_get_mute(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, uint32_t direction, uint32_t *mute) {
    audio_return_t audio_ret = AUDIO_RET_OK;
    pa_sink_input *si = NULL;
    uint32_t idx;
    audio_info_t audio_info;

    if (u->audio_mgr.intf.get_mute && (stream_idx == PA_INVALID_INDEX)) {
        audio_ret = u->audio_mgr.intf.get_mute(u->audio_mgr.data, NULL, volume_type, direction, mute);
        if (audio_ret == AUDIO_RET_USE_HW_CONTROL) {
            return audio_ret;
        } else {
            pa_log_error("get_mute returns error:0x%x", audio_ret);
            return audio_ret;
        }
    }

    if (direction == AUDIO_DIRECTION_OUT) {
        PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
            if (AUDIO_IS_ERROR(__fill_audio_playback_info(si, &audio_info))) {
                /* skip mono sink-input */
                continue;
            }

            /* Update mute of stream if it has requested stream or volume type */
            if ((stream_idx == idx) || ((stream_idx == PA_INVALID_INDEX) && (audio_info.stream.volume_type == volume_type))) {

                /* Call HAL function if exists */
                if (u->audio_mgr.intf.get_mute) {
                    audio_ret = u->audio_mgr.intf.get_mute(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, direction, mute);
                    if (audio_ret == AUDIO_RET_USE_HW_CONTROL) {
                        return audio_ret;
                    } else if (AUDIO_IS_ERROR(audio_ret)) {
                        pa_log_error("get_mute for sink-input[%d] returns error:0x%x", idx, audio_ret);
                        return audio_ret;
                    }
                }

                *mute = (uint32_t)pa_sink_input_get_mute(si);
                break;
            }
            __free_audio_info(&audio_info);
        }
    }

    pa_log_info("get mute stream_idx:%d type:%d direction:%d mute:%d", stream_idx, volume_type, direction, *mute);
    return audio_ret;
}

static audio_return_t policy_set_mute(struct userdata *u, uint32_t stream_idx, uint32_t volume_type, uint32_t direction, uint32_t mute) {
    audio_return_t audio_ret = AUDIO_RET_OK;
    pa_sink_input *si = NULL;
    uint32_t idx;
    audio_info_t audio_info;
    const char *si_volume_type_str;

    pa_log_info("set_mute stream_idx:%d type:%d direction:%d mute:%d", stream_idx, volume_type, direction, mute);

    if (volume_type == (uint32_t)-1 && stream_idx != (uint32_t)-1) {
        if ((si = pa_idxset_get_by_index(u->core->sink_inputs, stream_idx))) {
            if ((si_volume_type_str = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
                pa_atou(si_volume_type_str, &volume_type);
            } else {
                pa_log_debug("stream[%d] doesn't have volume type", stream_idx);
                return AUDIO_ERR_UNDEFINED;
            }
        } else {
            pa_log_warn("stream[%d] doesn't exist", stream_idx);
            return AUDIO_ERR_PARAMETER;
        }
    }

    /* workaround for keeping call mute setting */
    if ((volume_type == AUDIO_VOLUME_TYPE_CALL) && (direction == AUDIO_DIRECTION_IN)) {
        u->call_muted = mute;
    }

    if (u->muteall && !mute && (direction == AUDIO_DIRECTION_OUT) && (volume_type != AUDIO_VOLUME_TYPE_FIXED)) {
        pa_log_info("set_mute is ignored by muteall");
        return audio_ret;
    }

    /* Call HAL function if exists */
    if (u->audio_mgr.intf.set_mute && (stream_idx == PA_INVALID_INDEX)) {
        audio_ret = u->audio_mgr.intf.set_mute(u->audio_mgr.data, NULL, volume_type, direction, mute);
        if (audio_ret == AUDIO_RET_USE_HW_CONTROL) {
            pa_log_info("set_mute(call) returns error:0x%x mute:%d", audio_ret, mute);
            return audio_ret;
        } else if (AUDIO_IS_ERROR(audio_ret)) {
            pa_log_error("set_mute returns error:0x%x", audio_ret);
            return audio_ret;
        }
    }

    if (direction == AUDIO_DIRECTION_OUT) {
        PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
            if (AUDIO_IS_ERROR(__fill_audio_playback_info(si, &audio_info))) {
                /* skip mono sink-input */
                continue;
            }

            /* Update mute of stream if it has requested stream or volume type */
            if ((stream_idx == idx) || ((stream_idx == PA_INVALID_INDEX) && (audio_info.stream.volume_type == volume_type))) {

                /* Call HAL function if exists */
                if (u->audio_mgr.intf.set_mute) {
                    audio_ret = u->audio_mgr.intf.set_mute(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, direction, mute);
                    if (AUDIO_IS_ERROR(audio_ret)) {
                        pa_log_error("set_mute for sink-input[%d] returns error:0x%x", idx, audio_ret);
                        return audio_ret;
                    }
                }

                pa_sink_input_set_mute(si, (pa_bool_t)mute, true);
                if (idx == stream_idx)
                    break;
            }
            __free_audio_info(&audio_info);
        }
    }

    return audio_ret;
}

static pa_bool_t policy_is_available_high_latency(struct userdata *u)
{
    pa_sink_input *si = NULL;
    uint32_t idx;
    const char *si_policy_str;

    PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
        if ((si_policy_str = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_POLICY))) {
            if (pa_streq(si_policy_str, POLICY_HIGH_LATENCY) && sink_is_highlatency(si->sink)) {
                pa_log_info("high latency is exists");
                return false;
            }
        }
    }

    return true;
}

#define EXT_VERSION 1

static int extension_cb(pa_native_protocol *p, pa_module *m, pa_native_connection *c, uint32_t tag, pa_tagstruct *t) {
  struct userdata *u = NULL;
  uint32_t command;
  pa_tagstruct *reply = NULL;

  pa_sink_input *si = NULL;
  pa_sink *s = NULL;
  uint32_t idx;
  pa_sink* sink_to_move  = NULL;

  pa_assert(p);
  pa_assert(m);
  pa_assert(c);
  pa_assert(t);

  u = m->userdata;

  if (pa_tagstruct_getu32(t, &command) < 0)
    goto fail;

  reply = pa_tagstruct_new(NULL, 0);
  pa_tagstruct_putu32(reply, PA_COMMAND_REPLY);
  pa_tagstruct_putu32(reply, tag);

  switch (command) {
    case SUBCOMMAND_TEST: {
		if (!pa_tagstruct_eof(t))
			goto fail;

		pa_tagstruct_putu32(reply, EXT_VERSION);
		break;
    }

    case SUBCOMMAND_MONO: {

        bool enable;

        if (pa_tagstruct_get_boolean(t, &enable) < 0)
            goto fail;

        pa_log_debug ("[POLICY][%s] new mono value = %d\n", __func__, enable);
        if (enable == u->is_mono) {
			pa_log_debug ("[POLICY][%s] No changes in mono value = %d", __func__, u->is_mono);
			break;
        }

        u->is_mono = enable;

		/* Move current sink-input to proper mono sink */
		PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
			const char *policy = NULL;

			/* Skip this if it is already in the process of being moved
			 * anyway */
			if (!si->sink)
				continue;

			/* It might happen that a stream and a sink are set up at the
			   same time, in which case we want to make sure we don't
			   interfere with that */
			if (!PA_SINK_INPUT_IS_LINKED(pa_sink_input_get_state(si)))
				continue;

			/* Get role (if role is filter, skip it) */
			if (policy_is_filter(si))
				continue;

			/* Check policy, if no policy exists, treat as AUTO */
			if (!(policy = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_POLICY))) {
				pa_log_debug("[POLICY] set policy of sink-input[%d] from [%s] to [auto]", si->index, "null");
				policy  = POLICY_AUTO;
			}
			pa_log_debug("[POLICY] Policy of sink input [%d] = %s", si->index, policy);

			/* Select sink to move and move to it */
			sink_to_move = policy_select_proper_sink (u, policy, si, u->is_mono);
			if (sink_to_move) {
				pa_log_debug("[POLICY][%s] Moving sink-input[%d] from [%s] to [%s]", __func__, si->index, si->sink->name, sink_to_move->name);
				pa_sink_input_move_to(si, sink_to_move, false);
			} else {
				pa_log_debug("[POLICY][%s] Can't move sink-input....", __func__);
			}
		}
        break;
    }

    case SUBCOMMAND_BALANCE: {
		float balance;
		pa_cvolume cvol;
		pa_channel_map map;

		if (pa_tagstruct_get_cvolume(t, &cvol) < 0)
			goto fail;

		pa_channel_map_init_stereo(&map);
		balance = pa_cvolume_get_balance(&cvol, &map);

		pa_log_debug ("[POLICY][%s] new balance value = [%f]\n", __func__, balance);

		if (balance == u->balance) {
			pa_log_debug ("[POLICY][%s] No changes in balance value = [%f]", __func__, u->balance);
			break;
		}

		u->balance = balance;

		/* Apply balance value to each Sinks */
		PA_IDXSET_FOREACH(s, u->core->sinks, idx) {
			pa_cvolume* cvol = pa_sink_get_volume (s, false);
			pa_cvolume_set_balance (cvol, &s->channel_map, u->balance);
			pa_sink_set_volume(s, cvol, true, true);
		}
		break;
	}

   case SUBCOMMAND_PLAY_SAMPLE: {
            const char *name;
            uint32_t volume_type = 0;
            uint32_t gain_type = 0;
            uint32_t volume_level = 0;
            uint32_t stream_idx = PA_INVALID_INDEX;

            if (pa_tagstruct_gets(t, &name) < 0 ||
                pa_tagstruct_getu32(t, &volume_type) < 0 ||
                pa_tagstruct_getu32(t, &gain_type) < 0 ||
                pa_tagstruct_getu32(t, &volume_level) < 0 ||
                !pa_tagstruct_eof(t)) {
                pa_log_error("protocol error");
                goto fail;
            }

            policy_play_sample(u, c, name, volume_type, gain_type, volume_level, &stream_idx);

            pa_tagstruct_putu32(reply, stream_idx);
            break;
        }
#ifdef BURST_SHOT

        case SUBCOMMAND_PLAY_SAMPLE_CONTINUOUSLY: {
            const char *name;
            pa_bool_t start;
            uint32_t volume_type = 0;
            uint32_t gain_type = 0;
            uint32_t volume_level = 0;
            uint32_t stream_idx = PA_INVALID_INDEX;
            pa_usec_t interval;

            if (pa_tagstruct_gets(t, &name) < 0 ||
                pa_tagstruct_get_boolean(t, &start) < 0 ||
                pa_tagstruct_getu32(t, &volume_type) < 0 ||
                pa_tagstruct_getu32(t, &gain_type) < 0 ||
                pa_tagstruct_getu32(t, &volume_level) < 0 ||
                pa_tagstruct_get_usec(t, &interval) < 0 ||
                !pa_tagstruct_eof(t)) {
                pa_log_error("protocol error");
                goto fail;
            }
            /*When play sample continuous is in running state another instance is not allowed*/
            if (start == true) {
                if (u->audio_sample_userdata.is_running == false) {
                    /* Now it is time to prepare burstshot...set burstshot vconf */
                    vconf_set_int (VCONF_SOUND_BURSTSHOT, 1);

                    pa_log_warn("play_sample_continuously start. name(%s), vol_type(%d), gain_type(%d), vol_level(%d), interval(%lu ms)",
                        name, volume_type, gain_type, volume_level, (unsigned long) (interval / PA_USEC_PER_MSEC));
                    policy_play_sample_continuously(u, c, name, interval, volume_type, gain_type, volume_level, &stream_idx);

                    /* Running false after start means, start failed....unset burstshot vconf */
                    if (u->audio_sample_userdata.is_running == false) {
                        vconf_set_int (VCONF_SOUND_BURSTSHOT, 0);
                    }
                } else {
                    pa_log_warn("play_sample_continuously is in running state - do nothing");
                }
            } else if ((start == false) && (u->audio_sample_userdata.is_running == true)) {
                pa_log_warn("play_sample_continuously end.");
                policy_stop_sample_continuously(u);
            } else {
                pa_log_error("play sample continuously unknown command. name(%s), start(%d)", name, start);
            }

            pa_tagstruct_putu32(reply, stream_idx);
            break;
        }

#endif
        case SUBCOMMAND_MUTEALL: {
            const char *si_gain_type_str;
            pa_bool_t enable;
            unsigned i;
            uint32_t gain_type;
#if 0
            pa_cvolume cvol ;
            pa_cvolume* scvol ;
#endif

            if (pa_tagstruct_get_boolean(t, &enable) < 0)
                goto fail;

            pa_log_debug ("new muteall value = %d\n", enable);
            if (enable == u->muteall) {
                pa_log_debug ("No changes in muteall value = %d", u->muteall);
                break;
            }

            u->muteall = enable;

/* Use mute instead of volume for muteall */
#if 1
#ifdef TIZEM_MICRO
            /* Special case. Set mute for call volume type in B3. */
            policy_set_mute(u, (-1), AUDIO_VOLUME_TYPE_CALL, AUDIO_DIRECTION_OUT, u->muteall);
#else
            for (i = 0; i < AUDIO_VOLUME_TYPE_MAX; i++) {
                policy_set_mute(u, (-1), i, AUDIO_DIRECTION_OUT, u->muteall);
            }
#endif
            PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
                /* Skip booting sound for power off mute streams policy. */
                if (u->muteall && (si_gain_type_str = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_TIZEN_GAIN_TYPE))) {
                    pa_atou(si_gain_type_str, &gain_type);
                    if (gain_type == AUDIO_GAIN_TYPE_BOOTING)
                        continue;
                }
                pa_sink_input_set_mute(si, u->muteall, true);
            }
#else
            /* Apply new volume  value to each Sink_input */
            if (u->muteall) {
                PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
                    scvol = pa_sink_input_get_volume (si, &cvol,true);
                    for (i = 0; i < scvol->channels; i++) {
                        scvol->values[i] = 0;
                    }
                    pa_sink_input_set_volume(si,scvol,true,true);
                }
            } else {
                PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
                    if (pa_streq(si->module->name,"module-remap-sink")) {
                        scvol = pa_sink_input_get_volume (si, &cvol,true);
                        for (i = 0; i < scvol->channels; i++) {
                            scvol->values[i] = MAX_VOLUME_FOR_MONO;
                        }
                        pa_sink_input_set_volume(si,scvol,true,true);
                    }
                }
            }
#endif
            break;
        }

        case SUBCOMMAND_SET_USE_CASE: {
            break;
        }

        case SUBCOMMAND_SET_SESSION: {
            uint32_t session = 0;
            uint32_t start = 0;

            pa_tagstruct_getu32(t, &session);
            pa_tagstruct_getu32(t, &start);

            policy_set_session(u, session, start);
            break;
        }

        case SUBCOMMAND_SET_SUBSESSION: {
            uint32_t subsession = 0;
            uint32_t subsession_opt = 0;

            pa_tagstruct_getu32(t, &subsession);
            pa_tagstruct_getu32(t, &subsession_opt);

            policy_set_subsession(u, subsession, subsession_opt);
            break;
        }

        case SUBCOMMAND_SET_ACTIVE_DEVICE: {
            uint32_t device_in = 0;
            uint32_t device_out = 0;
            uint32_t need_update = false;

            pa_tagstruct_getu32(t, &device_in);
            pa_tagstruct_getu32(t, &device_out);

            policy_set_active_device(u, device_in, device_out, &need_update);
            pa_tagstruct_putu32(reply, need_update);
            break;
        }

        case SUBCOMMAND_RESET: {

            policy_reset(u);

            break;
        }

        case SUBCOMMAND_GET_VOLUME_LEVEL_MAX: {
            uint32_t volume_type = 0;
            uint32_t volume_level = 0;

            pa_tagstruct_getu32(t, &volume_type);

            policy_get_volume_level_max(u, volume_type, &volume_level);

            pa_tagstruct_putu32(reply, volume_level);
            break;
        }

        case SUBCOMMAND_GET_VOLUME_LEVEL: {
            uint32_t stream_idx = PA_INVALID_INDEX;
            uint32_t volume_type = 0;
            uint32_t volume_level = 0;

            pa_tagstruct_getu32(t, &stream_idx);
            pa_tagstruct_getu32(t, &volume_type);

            policy_get_volume_level(u, stream_idx, &volume_type, &volume_level);

            pa_tagstruct_putu32(reply, volume_level);
            break;
        }

        case SUBCOMMAND_SET_VOLUME_LEVEL: {
            uint32_t stream_idx = PA_INVALID_INDEX;
            uint32_t volume_type = 0;
            uint32_t volume_level = 0;

            pa_tagstruct_getu32(t, &stream_idx);
            pa_tagstruct_getu32(t, &volume_type);
            pa_tagstruct_getu32(t, &volume_level);

            policy_set_volume_level(u, stream_idx, volume_type, volume_level);
            break;
        }

        case SUBCOMMAND_UPDATE_VOLUME: {
            policy_update_volume(u);
            break;
        }

        case SUBCOMMAND_GET_MUTE: {
            uint32_t stream_idx = PA_INVALID_INDEX;
            uint32_t volume_type = 0;
            uint32_t direction = 0;
            uint32_t mute = 0;

            pa_tagstruct_getu32(t, &stream_idx);
            pa_tagstruct_getu32(t, &volume_type);
            pa_tagstruct_getu32(t, &direction);

            policy_get_mute(u, stream_idx, volume_type, direction, &mute);

            pa_tagstruct_putu32(reply, mute);
            break;
        }

        case SUBCOMMAND_SET_MUTE: {
            uint32_t stream_idx = PA_INVALID_INDEX;
            uint32_t volume_type = 0;
            uint32_t direction = 0;
            uint32_t mute = 0;

            pa_tagstruct_getu32(t, &stream_idx);
            pa_tagstruct_getu32(t, &volume_type);
            pa_tagstruct_getu32(t, &direction);
            pa_tagstruct_getu32(t, &mute);

            policy_set_mute(u, stream_idx, volume_type, direction, mute);
            break;
        }
        case SUBCOMMAND_IS_AVAILABLE_HIGH_LATENCY: {
            pa_bool_t available = false;

            available = policy_is_available_high_latency(u);

            pa_tagstruct_putu32(reply, (uint32_t)available);
            break;
        }
        case SUBCOMMAND_UNLOAD_HDMI: {
            break;
        }

        default:
            goto fail;
  }

  pa_pstream_send_tagstruct(pa_native_connection_get_pstream(c), reply);
  return 0;

  fail:

  if (reply)
	  pa_tagstruct_free(reply);

  return -1;
}
static void __set_sink_input_role_type(pa_proplist *p, int gain_type)
{
    const char* role = NULL;

    if ((role = pa_proplist_gets(p, PA_PROP_MEDIA_ROLE))) {
        /* "solo" has priority over other roles */
        if (pa_streq(role, "solo")) {
            pa_log_info("already set role to [%s]", role);
            return;
        } else {
            if(gain_type == AUDIO_GAIN_TYPE_SHUTTER1 || gain_type == AUDIO_GAIN_TYPE_SHUTTER2)
                role = "camera";
            else
                role = "normal";
        }
    } else {
        role = "normal";
    }
    pa_proplist_sets (p, PA_PROP_MEDIA_ROLE, role);
    pa_log_info("set role [%s]", role);

    return;
}
/*  Called when new sink-input is creating  */
static pa_hook_result_t sink_input_new_hook_callback(pa_core *c, pa_sink_input_new_data *new_data, struct userdata *u)
{
    audio_return_t audio_ret = AUDIO_RET_OK;
    audio_info_t audio_info;
    const char *policy = NULL;
    const char *ignore_preset_sink = NULL;
    const char *master_name = NULL;
    pa_sink *realsink = NULL;
    uint32_t volume_level = 0;
    pa_strbuf *s = NULL;
    const char *rate_str = NULL;
    const char *ch_str = NULL;
    char *s_info = NULL;

    pa_assert(c);
    pa_assert(new_data);
    pa_assert(u);

    if (!new_data->proplist) {
        pa_log_debug(" New stream lacks property data.");
        return PA_HOOK_OK;
    }

    /* If no policy exists, skip */
    if (!(policy = pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_POLICY))) {
        pa_log_debug("Not setting device for stream [%s], because it lacks policy.",
                pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
        return PA_HOOK_OK;
    }

    /* Parse request formats for samplerate & channel infomation */
    if (new_data->req_formats) {
        pa_format_info* req_format = pa_idxset_first(new_data->req_formats, NULL);
        if (req_format && req_format->plist) {
            rate_str = pa_proplist_gets(req_format->plist, PA_PROP_FORMAT_RATE);
            ch_str = pa_proplist_gets(req_format->plist, PA_PROP_FORMAT_CHANNELS);
            pa_log_info("req rate = %s, req ch = %s", rate_str, ch_str);

            if (ch_str)
                new_data->sample_spec.channels = atoi (ch_str);
            if (rate_str)
                new_data->sample_spec.rate = atoi (rate_str);
        }
    } else {
        pa_log_debug("no request formats available");
    }

    /* Check if this input want to be played via the sink selected by module-policy */
    if ((ignore_preset_sink = pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_POLICY_IGNORE_PRESET_SINK))) {
        pa_log_debug("ignore_preset_sink is enabled. module-policy will judge a proper sink for stream [%s]",
                pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
    } else {
        ignore_preset_sink = "no";
    }

    /* If sink-input has already sink, skip */
    if (new_data->sink && (strncmp("yes", ignore_preset_sink, strlen("yes")))) {
        /* sink-input with filter role will be also here because sink is already set */
#ifdef DEBUG_DETAIL
        pa_log_debug(" Not setting device for stream [%s], because already set.",
                pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
#endif
    } else {
	
	/* Set proper sink to sink-input */
    pa_sink* new_sink = policy_select_proper_sink(u, policy, NULL, u->is_mono);
    if(new_sink != new_data->sink)
        pa_sink_input_new_data_set_sink(new_data, new_sink, false);
    }

    s = pa_strbuf_new();
    master_name = pa_proplist_gets(new_data->sink->proplist, PA_PROP_DEVICE_MASTER_DEVICE);
    if (master_name)
        realsink = pa_namereg_get(c, master_name, PA_NAMEREG_SINK);

    if (AUDIO_IS_ERROR((audio_ret = __fill_audio_playback_stream_info(new_data->proplist, &new_data->sample_spec, &audio_info)))) {
        pa_log_debug("__fill_audio_playback_stream_info returns 0x%x", audio_ret);
    } else if (AUDIO_IS_ERROR((audio_ret = __fill_audio_playback_device_info(realsink? realsink->proplist : new_data->sink->proplist, &audio_info)))) {
        pa_log_debug("__fill_audio_playback_device_info returns 0x%x", audio_ret);
    } else {
        double volume_linear = 1.0f;

        // set role type
        __set_sink_input_role_type(new_data->proplist, audio_info.stream.gain_type);

        if (u->audio_mgr.intf.get_volume_level) {
            u->audio_mgr.intf.get_volume_level(u->audio_mgr.data, audio_info.stream.volume_type, &volume_level);
        }

        pa_strbuf_printf(s, "[%s] policy[%s] ch[%d] rate[%d] volume&gain[%d,%d] level[%d]",
                audio_info.stream.name, policy, audio_info.stream.channels, audio_info.stream.samplerate,
                audio_info.stream.volume_type, audio_info.stream.gain_type, volume_level);

        if (audio_info.device.api == AUDIO_DEVICE_API_ALSA) {
            pa_strbuf_printf(s, " device:ALSA[%d,%d]", audio_info.device.alsa.card_idx, audio_info.device.alsa.device_idx);
        } else if (audio_info.device.api == AUDIO_DEVICE_API_BLUEZ) {
            pa_strbuf_printf(s, " device:BLUEZ[%s] nrec[%d]", audio_info.device.bluez.protocol, audio_info.device.bluez.nrec);
        }
        pa_strbuf_printf(s, " sink[%s]", (new_data->sink)? new_data->sink->name : "null");

        /* Call HAL function if exists */
        if (u->audio_mgr.intf.set_volume_level) {
            if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.set_volume_level(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, volume_level)))) {
                pa_log_warn("set_volume_level for new sink-input returns error:0x%x", audio_ret);
                goto exit;
            }
        }

        /* Get volume value by type & level */
        if (u->audio_mgr.intf.get_volume_value && (audio_ret != AUDIO_RET_USE_HW_CONTROL)) {
            if (AUDIO_IS_ERROR((audio_ret = u->audio_mgr.intf.get_volume_value(u->audio_mgr.data, &audio_info, audio_info.stream.volume_type, volume_level, &volume_linear)))) {
                pa_log_warn("get_volume_value for new sink-input returns error:0x%x", audio_ret);
                goto exit;
            }
        }

        pa_cvolume_init(&new_data->volume);
        pa_cvolume_set(&new_data->volume, new_data->sample_spec.channels, pa_sw_volume_from_linear(volume_linear));

        new_data->volume_is_set = true;

        if(u->muteall && audio_info.stream.volume_type != AUDIO_VOLUME_TYPE_FIXED) {
            pa_sink_input_new_data_set_muted(new_data, true); // pa_simpe api use muted stream always. for play_sample_xxx apis
        }
        __free_audio_info(&audio_info);
    }

exit:
    if (s) {
        s_info = pa_strbuf_tostring_free(s);
        pa_log_info("new %s", s_info);
        pa_xfree(s_info);
    }

    return PA_HOOK_OK;
}


static pa_hook_result_t sink_input_unlink_post_hook_callback(pa_core *c, pa_sink_input *i, struct userdata *u)
{
    uint32_t volume_type = 0;
    const char *si_volume_type_str;

    pa_assert(c);
    pa_assert(i);
    pa_assert(u);

#ifdef PRIMARY_VOLUME
    if((si_volume_type_str = pa_proplist_gets(i->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
        pa_atou(si_volume_type_str, &volume_type);
        __set_primary_volume(u, (void*)i, volume_type, false);
    }
#endif

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_put_callback(pa_core *core, pa_sink_input *i, struct userdata *u)
{
    uint32_t volume_type = 0;
    const char *si_volume_type_str;

    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);
    pa_assert(u);

#ifdef PRIMARY_VOLUME
    if ((si_volume_type_str = pa_proplist_gets(i->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE)) &&
        pa_sink_input_get_state(i) != PA_SINK_INPUT_CORKED /* if sink-input is created by pulsesink, sink-input init state is cork.*/) {
        pa_atou(si_volume_type_str, &volume_type);
        __set_primary_volume(u, (void*)i, volume_type, true);
    }
#endif

    return PA_HOOK_OK;
}
/*  Called when new sink is added while sink-input is existing  */
static pa_hook_result_t sink_put_hook_callback(pa_core *c, pa_sink *sink, struct userdata *u)
{
    pa_sink_input *si;
    pa_sink *sink_to_move;
    uint32_t idx;
    char *args = NULL;

    bool is_bt;
    bool is_usb_alsa;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);
    pa_assert(u->on_hotplug);

    /* If connected sink is BLUETOOTH, set as default */
    /* we are checking with device.api property */
    is_bt = policy_is_bluez(sink);
    is_usb_alsa = policy_is_usb_alsa(sink);

	if (is_bt || is_usb_alsa) {
		pa_log_debug("[POLICY][%s] set default sink to sink[%s][%d]", __func__, sink->name, sink->index);
		pa_namereg_set_default_sink (c,sink);
	} else {
		pa_log_debug("[POLICY][%s] this sink [%s][%d] is not a bluez....return", __func__, sink->name, sink->index);
		return PA_HOOK_OK;
	}

	if (is_bt) {
		/* Load mono_bt sink */
		args = pa_sprintf_malloc("sink_name=%s master=%s channels=1", SINK_MONO_BT, sink->name);
		u->module_mono_bt = pa_module_load(u->module->core, "module-remap-sink", args);
		pa_xfree(args);

		/* load combine sink */
		args = pa_sprintf_malloc("sink_name=%s slaves=\"%s,%s\"", SINK_COMBINED, sink->name, SINK_ALSA);
		u->module_combined = pa_module_load(u->module->core, "module-combine", args);
		pa_xfree(args);

		/* load mono_combine sink */
		args = pa_sprintf_malloc("sink_name=%s master=%s channels=1", SINK_MONO_COMBINED, SINK_COMBINED);
		u->module_mono_combined = pa_module_load(u->module->core, "module-remap-sink", args);
		pa_xfree(args);
	}

	/* Iterate each sink inputs to decide whether we should move to new sink */
    PA_IDXSET_FOREACH(si, c->sink_inputs, idx) {
        const char *policy = NULL;

        if (si->sink == sink)
        	continue;

        /* Skip this if it is already in the process of being moved
         * anyway */
        if (!si->sink)
            continue;

        /* It might happen that a stream and a sink are set up at the
           same time, in which case we want to make sure we don't
           interfere with that */
        if (!PA_SINK_INPUT_IS_LINKED(pa_sink_input_get_state(si)))
            continue;

		/* Get role (if role is filter, skip it) */
        if (policy_is_filter(si))
        	continue;

		/* Check policy */
		if (!(policy = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_POLICY))) {
			/* No policy exists, this means auto */
			pa_log_debug("[POLICY][%s] set policy of sink-input[%d] from [%s] to [auto]", __func__, si->index, "null");
			policy = POLICY_AUTO;
		}

		sink_to_move = policy_select_proper_sink (u, policy, si, u->is_mono);
		if (sink_to_move) {
			pa_log_debug("[POLICY][%s] Moving sink-input[%d] from [%s] to [%s]", __func__, si->index, si->sink->name, sink_to_move->name);
			pa_sink_input_move_to(si, sink_to_move, false);
		} else {
			pa_log_debug("[POLICY][%s] Can't move sink-input....",__func__);
		}
    }

	/* Reset sink volume with balance from userdata */
	pa_cvolume* cvol = pa_sink_get_volume(sink, false);
	pa_cvolume_set_balance(cvol, &sink->channel_map, u->balance);
	pa_sink_set_volume(sink, cvol, true, true);

    return PA_HOOK_OK;
}
static void defer_event_cb (pa_mainloop_api *m, pa_defer_event *e, void *userdata)
{
    struct userdata *u = userdata;

    pa_assert(m);
    pa_assert(e);
    pa_assert(u);

    m->defer_enable(u->defer_event, 0);

    /* Dispatch queued events */

    while (u->hal_event_queue) {
        struct pa_hal_event *hal_event = u->hal_event_queue;

        if ((hal_event->event_type == PA_HAL_EVENT_LOAD_DEVICE) || (hal_event->event_type == PA_HAL_EVENT_OPEN_DEVICE)) {
            struct pa_hal_device_event_data *event_data = (struct pa_hal_device_event_data *)hal_event->event_data;

            pa_log_info("dispatch %s event", __get_event_type_string(hal_event->event_type));

            __load_n_open_device(hal_event->userdata, &event_data->device_info, &event_data->params[0], hal_event->event_type);

            pa_log_debug("completed %s event", __get_event_type_string(hal_event->event_type));
        } else if (hal_event->event_type == PA_HAL_EVENT_CLOSE_ALL_DEVICES) {
            pa_log_info("dispatch %s event", __get_event_type_string(PA_HAL_EVENT_CLOSE_ALL_DEVICES));

            __close_all_devices(hal_event->userdata);

            pa_log_debug("completed %s event", __get_event_type_string(PA_HAL_EVENT_CLOSE_ALL_DEVICES));
        } else if ((hal_event->event_type == PA_HAL_EVENT_CLOSE_DEVICE) || (hal_event->event_type == PA_HAL_EVENT_UNLOAD_DEVICE)) {
            struct pa_hal_device_event_data *event_data = (struct pa_hal_device_event_data *)hal_event->event_data;

            pa_log_info("dispatch %s event", __get_event_type_string(hal_event->event_type));

            __close_n_unload_device(hal_event->userdata, &event_data->device_info, hal_event->event_type);

            pa_log_debug("completed %s event", __get_event_type_string(hal_event->event_type));
        }

        if (!hal_event->next)
            hal_event->userdata->hal_event_last = hal_event->prev;

        PA_LLIST_REMOVE(struct pa_hal_event, hal_event->userdata->hal_event_queue, hal_event);

        if (hal_event->cond) {
            pa_mutex_lock(hal_event->mutex);
            pa_cond_signal(hal_event->cond, 0);
            pa_mutex_unlock(hal_event->mutex);
        }
    }
}

static void subscribe_cb(pa_core *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
    struct userdata *u = userdata;
    pa_sink *def;
    pa_sink_input *si;
    uint32_t idx2;
    pa_sink *sink_to_move = NULL;
    pa_sink *sink_cur = NULL;
    pa_source *source_cur = NULL;
    pa_source_state_t source_state;
    int vconf_source_status = 0;
    uint32_t si_index;
    int audio_ret;
    pa_assert(u);

    pa_log_debug("[POLICY][%s] subscribe_cb() t=[0x%x], idx=[%d]", __func__, t, idx);

    /* We only handle server changes */
    if (t == (PA_SUBSCRIPTION_EVENT_SERVER|PA_SUBSCRIPTION_EVENT_CHANGE)) {

    	def = pa_namereg_get_default_sink(c);
		if (def == NULL) {
			pa_log_warn("[POLICY][%s] pa_namereg_get_default_sink() returns null", __func__);
			return;
		}
    	pa_log_debug("[POLICY][%s] trying to move stream to current default sink = [%s]", __func__, def->name);

    	/* Iterate each sink inputs to decide whether we should move to new DEFAULT sink */
    	PA_IDXSET_FOREACH(si, c->sink_inputs, idx2) {
			const char *policy = NULL;

			if (!si->sink)
				continue;

			/* Get role (if role is filter, skip it) */
			if (policy_is_filter(si))
				continue;

            if (pa_streq (def->name, "null")) {
#ifdef TIZEN_MICRO
                /* alarm is not comming via speaker after disconnect bluetooth. */
                if(pa_streq(si->sink->name, "null")) {
                    pa_log_warn("try to move sink-input from null-sink to null-sink(something wrong state). sink-input[%d] will move to proper sink", si->index);
                } else {
#endif
                    pa_log_debug("Moving sink-input[%d] from [%s] to [%s]", si->index, si->sink->name, def->name);
                    pa_sink_input_move_to(si, def, false);
                    continue;
#ifdef TIZEN_MICRO
                }
#endif
            }

			/* Get policy */
			if (!(policy = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_POLICY))) {
				/* No policy exists, this means auto */
				pa_log_debug("[POLICY][%s] set policy of sink-input[%d] from [%s] to [auto]", __func__, si->index, "null");
				policy = POLICY_AUTO;
			}

#ifdef TIZEN_MICRO
            sink_to_move = policy_select_proper_sink (u, policy, si, u->is_mono);
#else
            /** If sink input is an UHQA sink then connect the sink to UHQA sink*/
            if ((si->sample_spec.rate >= UHQA_BASE_SAMPLING_RATE)
                 && (pa_streq(policy, POLICY_HIGH_LATENCY) || pa_streq(policy, POLICY_AUTO))) {
                char tmp_policy[100] = {0};

                sprintf(tmp_policy, "%s-uhqa", policy);
                sink_to_move = policy_select_proper_sink (u, tmp_policy, si, u->is_mono);
            } else {
                sink_to_move = policy_select_proper_sink (u, policy, si, u->is_mono);
            }
#endif
			if (sink_to_move) {
				/* Move sink-input to new DEFAULT sink */
				pa_log_debug("[POLICY][%s] Moving sink-input[%d] from [%s] to [%s]", __func__, si->index, si->sink->name, sink_to_move->name);
				pa_sink_input_move_to(si, sink_to_move, false);
			}
    	}
        pa_log_info("All moved to proper sink finished!!!!");
    } else if (t == (PA_SUBSCRIPTION_EVENT_SINK|PA_SUBSCRIPTION_EVENT_CHANGE)) {
        if ((sink_cur = pa_idxset_get_by_index(c->sinks, idx))) {
            pa_sink_state_t state = pa_sink_get_state(sink_cur);
            pa_log_debug("sink[%s] changed to state[%d]", sink_cur->name, state);

            if (pa_streq (sink_cur->name, SINK_HIGH_LATENCY) && state == PA_SINK_RUNNING) {
                PA_IDXSET_FOREACH(si, c->sink_inputs, si_index) {
                    if (!si->sink)
                        continue;
                    if (pa_streq (si->sink->name, SINK_HIGH_LATENCY)) {
                        if (AUDIO_IS_ERROR((audio_ret = __update_volume(u, si->index, (uint32_t)-1, (uint32_t)-1)))) {
                            pa_log_debug("__update_volume for stream[%d] returns error:0x%x", si->index, audio_ret);
                        }
                    }
                }
            }
        }
    } else if (t == (PA_SUBSCRIPTION_EVENT_SOURCE|PA_SUBSCRIPTION_EVENT_CHANGE)) {
        if ((source_cur = pa_idxset_get_by_index(c->sources, idx))) {
            if (pa_streq (source_cur->name, SOURCE_ALSA)) {
                source_state = pa_source_get_state(source_cur);
                pa_log_debug("source[%s] changed to state[%d]", source_cur->name, source_state);
                if (source_state == PA_SOURCE_RUNNING) {
                    vconf_set_int (VCONFKEY_SOUND_CAPTURE_STATUS, 1);
                } else {
                    vconf_get_int (VCONFKEY_SOUND_CAPTURE_STATUS, &vconf_source_status);
                    if (vconf_source_status)
                        vconf_set_int (VCONFKEY_SOUND_CAPTURE_STATUS, 0);
                }
            }
        }
    }
}

static pa_hook_result_t sink_unlink_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    struct userdata *u = userdata;
    uint32_t idx;
    pa_sink *sink_to_move;
    pa_sink_input	*si;

    const char *si_volume_type_str;
    uint32_t volume_type = AUDIO_VOLUME_TYPE_SYSTEM;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);

     /* There's no point in doing anything if the core is shut down anyway */
    if (c->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    /* if unloading sink is not bt, just return */
	if (!policy_is_bluez (sink)) {
		pa_log_debug("[POLICY][%s] sink[%s][%d] unlinked but not a bluez....return\n", __func__,  sink->name, sink->index);
		return PA_HOOK_OK;
	}

	pa_log_debug ("[POLICY][%s] SINK unlinked ================================ sink [%s][%d], bt_off_idx was [%d]",
	    		__func__, sink->name, sink->index,u->bt_off_idx);

	u->bt_off_idx = sink->index;
	pa_log_debug ("[POLICY][%s] bt_off_idx is set to [%d]", __func__, u->bt_off_idx);

	/* BT sink is unloading, move sink-input to proper sink */
	PA_IDXSET_FOREACH(si, c->sink_inputs, idx) {
        const char *policy = NULL;
		if (!si->sink)
			continue;

		/* Get role (if role is filter, skip it) */
		if (policy_is_filter(si))
			continue;

		/* Find who were using bt sink or bt related sink and move them to proper sink (alsa/mono_alsa) */
		if (pa_streq (si->sink->name, SINK_MONO_BT) ||
			pa_streq (si->sink->name, SINK_MONO_COMBINED) ||
			pa_streq (si->sink->name, SINK_COMBINED) ||
			policy_is_bluez (si->sink)) {

			/* Move sink-input to proper sink : only alsa related sink is available now */
			sink_to_move = policy_get_sink_by_name (c, (u->is_mono)? SINK_MONO_ALSA : SINK_ALSA);
			if (sink_to_move) {
				pa_log_debug("[POLICY][%s] Moving sink-input[%d] from [%s] to [%s]", __func__, si->index, si->sink->name, sink_to_move->name);
				pa_sink_input_move_to(si, sink_to_move, false);
			} else {
				pa_log_warn("[POLICY][%s] No sink to move", __func__);
			}
		}
	}

	pa_log_debug ("[POLICY][%s] unload sink in dependencies", __func__);

    /* Unload mono_combine sink */
    if (u->module_mono_combined) {
        pa_module_unload(u->module->core, u->module_mono_combined, true);
    	u->module_mono_combined = NULL;
    }

	/* Unload combine sink */
    if (u->module_combined) {
        pa_module_unload(u->module->core, u->module_combined, true);
        u->module_combined = NULL;
    }

    /* Unload mono_bt sink */
	if (u->module_mono_bt) {
		pa_module_unload(u->module->core, u->module_mono_bt, true);
		u->module_mono_bt = NULL;
	}

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_unlink_post_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    struct userdata *u = userdata;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);

    pa_log_debug("[POLICY][%s] SINK unlinked POST ================================ sink [%s][%d]", __func__, sink->name, sink->index);

     /* There's no point in doing anything if the core is shut down anyway */
    if (c->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    /* if unloading sink is not bt, just return */
	if (!policy_is_bluez (sink)) {
		pa_log_debug("[POLICY][%s] not a bluez....return\n", __func__);
		return PA_HOOK_OK;
	}

    u->bt_off_idx = -1;
    pa_log_debug ("[POLICY][%s] bt_off_idx is cleared to [%d]", __func__, u->bt_off_idx);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_move_start_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
	audio_return_t audio_ret = AUDIO_RET_OK;
	pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    /* There's no point in doing anything if the core is shut down anyway */
   if (core->state == PA_CORE_SHUTDOWN)
       return PA_HOOK_OK;

    pa_log_debug ("[POLICY][%s]  sink_input_move_start_cb -------------------------------------- sink-input [%d] was sink [%s][%d] : Trying to mute!!!",
    		__func__, i->index, i->sink->name, i->sink->index);
    if (AUDIO_IS_ERROR((audio_ret = policy_set_mute(u, i->index, (uint32_t)-1, AUDIO_DIRECTION_OUT, 1)))) {
        pa_log_warn("policy_set_mute(1) for stream[%d] returns error:0x%x", i->index, audio_ret);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_move_finish_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
    audio_return_t audio_ret = AUDIO_RET_OK;
	pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    /* There's no point in doing anything if the core is shut down anyway */
   if (core->state == PA_CORE_SHUTDOWN)
       return PA_HOOK_OK;

    pa_log_debug("[POLICY][%s] sink_input_move_finish_cb -------------------------------------- sink-input [%d], sink [%s][%d], bt_off_idx [%d] : %s",
    		__func__, i->index, i->sink->name, i->sink->index, u->bt_off_idx,
    		(u->bt_off_idx == -1)? "Trying to un-mute!!!!" : "skip un-mute...");

    /* If sink input move is caused by bt sink unlink, then skip un-mute operation */
    /* If sink input move is caused by bt sink unlink, then skip un-mute operation */
    if (u->bt_off_idx == -1 && !u->muteall) {
        if (AUDIO_IS_ERROR((audio_ret = __update_volume(u, i->index, (uint32_t)-1, (uint32_t)-1)))) {
            pa_log_debug("__update_volume for stream[%d] returns error:0x%x", i->index, audio_ret);
        }
        if (AUDIO_IS_ERROR((audio_ret = policy_set_mute(u, i->index, (uint32_t)-1, AUDIO_DIRECTION_OUT, 0)))) {
            pa_log_debug("policy_set_mute(0) for stream[%d] returns error:0x%x", i->index, audio_ret);
        }
    }
    return PA_HOOK_OK;
}

static pa_source* policy_get_source_by_name (pa_core *c, const char* source_name)
{
	pa_source *s = NULL;
	uint32_t idx;

	if (c == NULL || source_name == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
	}

	PA_IDXSET_FOREACH(s, c->sources, idx) {
		if (pa_streq (s->name, source_name)) {
			pa_log_debug ("[POLICY][%s] return [%p] for [%s]\n",  __func__, s, source_name);
			return s;
		}
	}
	return NULL;
}

static pa_hook_result_t sink_input_state_changed_hook_cb(pa_core *core, pa_sink_input *i, struct userdata *u)
{
    pa_sink* sink_to_move = NULL;
    pa_sink* sink_default = NULL;
    const char * policy = NULL;

    uint32_t volume_type = 0;
    const char *si_volume_type_str = NULL;
    const char *si_policy_str = NULL;
    pa_sink_input_state_t state;

    pa_assert(i);
    pa_assert(u);

    if((si_volume_type_str = pa_proplist_gets(i->proplist, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
        pa_atou(si_volume_type_str, &volume_type);

        state = pa_sink_input_get_state(i);

        switch(state) {
            case PA_SINK_INPUT_CORKED:
                si_policy_str = pa_proplist_gets(i->proplist, PA_PROP_MEDIA_POLICY);

                /* special case. media volume should be set using fake sink-input by fmradio*/
                if(si_policy_str && pa_streq(si_policy_str, "fmradio"))
                    break;

#ifdef PRIMARY_VOLUME
                __set_primary_volume(u, (void*)i, volume_type, false);
#endif
                break;
            case PA_SINK_INPUT_DRAINED:
            case PA_SINK_INPUT_RUNNING:
#ifdef PRIMARY_VOLUME
                __set_primary_volume(u, (void*)i, volume_type, true);
#endif
                break;
            default:
                break;
        }
    }

    if(i->state == PA_SINK_INPUT_RUNNING) {
        policy = pa_proplist_gets (i->proplist, PA_PROP_MEDIA_POLICY);

        pa_log_info("---------------------------------------------");

        /** If sink input is an UHQA sink sink input then connect it to UHQA sink if not connected to UHQA sink*/
        if ( ( i->sample_spec.rate >= UHQA_BASE_SAMPLING_RATE) && (policy != NULL) &&
             ( pa_streq (policy, POLICY_HIGH_LATENCY) || pa_streq (policy, POLICY_AUTO) )) {
            char tmp_policy[100] = {0};

            pa_log_info ("------------------------------------");

            sprintf(tmp_policy, "%s-uhqa", policy);
            sink_to_move = policy_select_proper_sink (u, tmp_policy, i, u->is_mono);

            if (i->sink != sink_to_move) {
                 if (sink_to_move) {
                    pa_log_debug("Moving sink-input[%d] from [%s] to [%s]", i->index, i->sink->name, sink_to_move->name);
                    pa_sink_input_move_to(i, sink_to_move, false);
                }
            }
        }

        /** Get the normal sink and move all sink input from normal sink to UHQA sink if normal sink and UHQA sink are different*/
        sink_default = policy_select_proper_sink (u, policy, i, u->is_mono);
        if ((sink_to_move != NULL) && (sink_default != NULL) && (sink_to_move != sink_default)) {
            pa_sink_input *si = NULL;
            uint32_t idx;

            /** Check any sink input connected to normal sink then move them to UHQA sink*/
            PA_IDXSET_FOREACH (si, sink_default->inputs, idx) {
                pa_log_info ("------------------------------------");
                /* Get role (if role is filter, skip it) */
                if (policy_is_filter (si)) {
                    continue;
                }
                pa_sink_input_move_to (si,  sink_to_move, false);
            }
        }
    }
    return PA_HOOK_OK;
}

static pa_hook_result_t sink_state_changed_hook_cb(pa_core *c, pa_object *o, struct userdata *u) {
    return PA_HOOK_OK;
}
/* Select source for given condition */
static pa_source* policy_select_proper_source (pa_core *c, const char* policy)
{    
	pa_source* source = NULL;
	pa_source* def = NULL;
	pa_source* source_null;

	if (c == NULL || policy == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
	}

	pa_assert (c);
	def = pa_namereg_get_default_source(c);
	if (def == NULL) {
		pa_log_warn ("POLICY][%s] pa_namereg_get_default_source() returns null", __func__);
		return NULL;
	}
    source_null = (pa_source *)pa_namereg_get(c, "null", PA_NAMEREG_SOURCE);
    /* if default source is set as null source, we will use it */
    if (def == source_null)
        return def;

    /* Select source  to */
    if (pa_streq(policy, POLICY_VOIP)) {
        /* NOTE: Check voip source first, if not available, use AEC source  */
        source = policy_get_source_by_name (c, SOURCE_VOIP);
        if (source == NULL) {
            pa_log_info ("VOIP source is not available, try to use AEC source");
            source = policy_get_source_by_name (c, AEC_SOURCE);
            if (source == NULL) {
                pa_log_warn ("AEC source is not available, set to default source");
                source = def;
            }
        }
    } else if (pa_streq(policy, POLICY_MIRRORING)) {
        source = policy_get_source_by_name (c, SOURCE_MIRRORING);
        if (source == NULL) {
            pa_log_info ("MIRRORING source is not available, try to use ALSA MONITOR SOURCE");
            source = policy_get_source_by_name (c, ALSA_MONITOR_SOURCE);
            if (source == NULL) {
                pa_log_warn (" ALSA MONITOR SOURCE source is not available, set to default source");
                source = def;
            }
        }
    } else if (pa_streq(policy, POLICY_LOOPBACK)) {
        source = policy_get_source_by_name (c, ALSA_MONITOR_SOURCE);
        if (source == NULL) {
            pa_log_warn (" ALSA MONITOR SOURCE source is not available, set to default source");
            source = def;
        }
    } else {
        source = def;
    }

	pa_log_debug ("[POLICY][%s] selected source : [%s]\n", __func__, (source)? source->name : "null");
	return source;
}


/*  Called when new source-output is creating  */
static pa_hook_result_t source_output_new_hook_callback(pa_core *c, pa_source_output_new_data *new_data, struct userdata *u) {
	const char *policy = NULL;
	pa_assert(c);
	pa_assert(new_data);
	pa_assert(u);

	if (!new_data->proplist) {
		pa_log_debug("New stream lacks property data.");
		return PA_HOOK_OK;
	}

	if (new_data->source) {
		pa_log_debug("Not setting device for stream %s, because already set.", pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
		return PA_HOOK_OK;
	}

	/* If no policy exists, skip */
	if (!(policy = pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_POLICY))) {
		pa_log_debug("[POLICY][%s] Not setting device for stream [%s], because it lacks policy.",
				__func__, pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
		return PA_HOOK_OK;
	}
	pa_log_debug("[POLICY][%s] Policy for stream [%s] = [%s]",
			__func__, pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)), policy);

	/* Set proper source to source-output */
    pa_source* new_source = policy_select_proper_source(c, policy);
    if(new_source != new_data->source)
    {
        pa_source_output_new_data_set_source(new_data, new_source, false);
    }
	/*new_data->save_source= false;
	new_data->source= policy_select_proper_source (c, policy);*/
	pa_log_debug("[POLICY][%s] set source of source-input to [%s]", __func__, (new_data->source)? new_data->source->name : "null");

	return PA_HOOK_OK;
}

static pa_hook_result_t source_output_put_callback(pa_core *c, pa_source_output *o, struct userdata *u)
{
    pa_core_assert_ref(c);
    pa_source_output_assert_ref(o);
    pa_assert(u);

#ifdef PRIMARY_VOLUME
    __set_primary_volume(u, (void*)o, AUDIO_PRIMARY_VOLUME_TYPE_MAX/*source-output use PRIMARY_MAX*/, true);
#endif
    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_unlink_post_hook_callback(pa_core *c, pa_source_output *o, struct userdata *u)
{
#ifdef PRIMARY_VOLUME
    __set_primary_volume(u, (void*)o, AUDIO_PRIMARY_VOLUME_TYPE_MAX/*source-output use PRIMARY_MAX*/, false);
#endif
    return PA_HOOK_OK;
}

int pa__init(pa_module *m)
{
	pa_modargs *ma = NULL;
	struct userdata *u;
    pa_bool_t on_hotplug = true, on_rescue = true, wideband = false;
    uint32_t frag_size = 0, tsched_size = 0;

	pa_assert(m);

	if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
		pa_log("Failed to parse module arguments");
		goto fail;
	}

	if (pa_modargs_get_value_boolean(ma, "on_hotplug", &on_hotplug) < 0 ||
		pa_modargs_get_value_boolean(ma, "on_rescue", &on_rescue) < 0) {
		pa_log("on_hotplug= and on_rescue= expect boolean arguments");
		goto fail;
	}
	if (pa_modargs_get_value_boolean(ma, "use_wideband_voice", &wideband) < 0 ||
        pa_modargs_get_value_u32(ma, "fragment_size", &frag_size) < 0 ||
        pa_modargs_get_value_u32(ma, "tsched_buffer_size", &tsched_size) < 0) {
        pa_log("Failed to parse module arguments buffer info");
        goto fail;
    }

	m->userdata = u = pa_xnew0(struct userdata, 1);
	u->core = m->core;
	u->module = m;
	u->on_hotplug = on_hotplug;
    u->wideband = wideband;
    u->fragment_size = frag_size;
    u->tsched_buffer_size = tsched_size;

    /* A little bit later than module-stream-restore */
    u->sink_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_STATE_CHANGED], PA_HOOK_NORMAL, (pa_hook_cb_t)sink_state_changed_hook_cb, u);

    u->sink_input_new_hook_slot =
            pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW], PA_HOOK_EARLY+10, (pa_hook_cb_t) sink_input_new_hook_callback, u);
    u->sink_input_unlink_post_slot =
            pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK_POST], PA_HOOK_EARLY+10, (pa_hook_cb_t) sink_input_unlink_post_hook_callback, u);
    u->sink_input_put_slot =
            pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT], PA_HOOK_EARLY+10, (pa_hook_cb_t) sink_input_put_callback, u);
    u->sink_input_state_changed_slot =
             pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_STATE_CHANGED], PA_HOOK_EARLY+10, (pa_hook_cb_t) sink_input_state_changed_hook_cb, u);

    u->source_output_new_hook_slot =
            pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_NEW], PA_HOOK_EARLY+10, (pa_hook_cb_t) source_output_new_hook_callback, u);
    u->source_output_unlink_post_slot =
            pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_UNLINK_POST], PA_HOOK_EARLY+10, (pa_hook_cb_t) source_output_unlink_post_hook_callback, u);
    u->source_output_put_slot =
            pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_PUT], PA_HOOK_EARLY+10, (pa_hook_cb_t) source_output_put_callback, u);

    if (on_hotplug) {
        /* A little bit later than module-stream-restore */
        u->sink_put_hook_slot =
            pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_LATE+10, (pa_hook_cb_t) sink_put_hook_callback, u);
    }

    /* sink unlink comes before sink-input unlink */
    u->sink_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_unlink_hook_callback, u);
    u->sink_unlink_post_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_UNLINK_POST], PA_HOOK_EARLY, (pa_hook_cb_t) sink_unlink_post_hook_callback, u);

    u->sink_input_move_start_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_START], PA_HOOK_LATE, (pa_hook_cb_t) sink_input_move_start_cb, u);
    u->sink_input_move_finish_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_FINISH], PA_HOOK_LATE, (pa_hook_cb_t) sink_input_move_finish_cb, u);

    u->tid = pthread_self();
    u->defer_event = u->core->mainloop->defer_new(u->core->mainloop, defer_event_cb, u);

    u->subscription = pa_subscription_new(u->core, PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE, subscribe_cb, u);

    pa_log_debug("subscription done");


	u->bt_off_idx = -1;	/* initial bt off sink index */

	u->module_mono_bt = NULL;
	u->module_combined = NULL;
	u->module_mono_combined = NULL;

    u->protocol = pa_native_protocol_get(m->core);
    pa_native_protocol_install_ext(u->protocol, m, extension_cb);


#ifdef PRIMARY_VOLUME
    vconf_set_int (VCONFKEY_SOUND_PRIMARY_VOLUME_TYPE, -1);
#endif
    /* Get mono key value for init */
	vconf_get_bool(MONO_KEY, &u->is_mono);
	/* Load library & init audio mgr */
    u->audio_mgr.dl_handle = dlopen(LIB_TIZEN_AUDIO, RTLD_NOW);
    if (u->audio_mgr.dl_handle) {
        u->audio_mgr.intf.init = dlsym(u->audio_mgr.dl_handle, "audio_init");
        u->audio_mgr.intf.deinit = dlsym(u->audio_mgr.dl_handle, "audio_deinit");
        u->audio_mgr.intf.reset = dlsym(u->audio_mgr.dl_handle, "audio_reset");
        u->audio_mgr.intf.set_callback = dlsym(u->audio_mgr.dl_handle, "audio_set_callback");
        u->audio_mgr.intf.get_volume_level_max = dlsym(u->audio_mgr.dl_handle, "audio_get_volume_level_max");
        u->audio_mgr.intf.get_volume_level = dlsym(u->audio_mgr.dl_handle, "audio_get_volume_level");
        u->audio_mgr.intf.get_volume_value = dlsym(u->audio_mgr.dl_handle, "audio_get_volume_value");
        u->audio_mgr.intf.set_volume_level = dlsym(u->audio_mgr.dl_handle, "audio_set_volume_level");
        u->audio_mgr.intf.set_volume_value = dlsym(u->audio_mgr.dl_handle, "audio_set_volume_value");
        u->audio_mgr.intf.get_gain_value = dlsym(u->audio_mgr.dl_handle, "audio_get_gain_value");
        u->audio_mgr.intf.get_mute = dlsym(u->audio_mgr.dl_handle, "audio_get_mute");
        u->audio_mgr.intf.set_mute = dlsym(u->audio_mgr.dl_handle, "audio_set_mute");
        u->audio_mgr.intf.alsa_pcm_open = dlsym(u->audio_mgr.dl_handle, "audio_alsa_pcm_open");
        u->audio_mgr.intf.alsa_pcm_close = dlsym(u->audio_mgr.dl_handle, "audio_alsa_pcm_close");
        u->audio_mgr.intf.set_session = dlsym(u->audio_mgr.dl_handle, "audio_set_session");
        u->audio_mgr.intf.set_route = dlsym(u->audio_mgr.dl_handle, "audio_set_route");
        u->audio_mgr.intf.set_mixer_value_string = dlsym(u->audio_mgr.dl_handle, "audio_set_mixer_value_string");

        if (u->audio_mgr.intf.init) {
            if (u->audio_mgr.intf.init(&u->audio_mgr.data, (void *)u) != AUDIO_RET_OK) {
                pa_log_error("audio_mgr init failed");
            }
        }

        pa_shared_set(u->core, "tizen-audio-data", u->audio_mgr.data);
        pa_shared_set(u->core, "tizen-audio-interface", &u->audio_mgr.intf);

        if (u->audio_mgr.intf.set_callback) {
            audio_cb_interface_t cb_interface;

            cb_interface.load_device = __load_device_callback;
            cb_interface.open_device = __open_device_callback;
            cb_interface.close_all_devices = __close_all_devices_callback;
            cb_interface.close_device = __close_device_callback;
            cb_interface.unload_device = __unload_device_callback;
            u->audio_mgr.intf.set_callback(u->audio_mgr.data, &cb_interface);
        }
        
    } else {
        pa_log_error("open audio_mgr failed :%s", dlerror());
    }
    __load_dump_config(u);
	pa_log_info("policy module is loaded\n");

	if (ma)
		pa_modargs_free(ma);

	return 0;

fail:
	if (ma)
		pa_modargs_free(ma);

	pa__done(m);

	return -1;
}

void pa__done(pa_module *m)
{
    struct userdata* u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink_input_new_hook_slot)
        pa_hook_slot_free(u->sink_input_new_hook_slot);
    if (u->sink_put_hook_slot)
        pa_hook_slot_free(u->sink_put_hook_slot);
    if (u->sink_unlink_slot)
        pa_hook_slot_free(u->sink_unlink_slot);
    if (u->sink_unlink_post_slot)
        pa_hook_slot_free(u->sink_unlink_post_slot);
    if(u->sink_input_state_changed_slot)
         pa_hook_slot_free(u->sink_input_state_changed_slot);
    if (u->sink_input_move_start_slot)
        pa_hook_slot_free(u->sink_input_move_start_slot);
    if (u->sink_input_move_finish_slot)
        pa_hook_slot_free(u->sink_input_move_finish_slot);
    if (u->subscription)
        pa_subscription_free(u->subscription);
    if (u->protocol) {
        pa_native_protocol_remove_ext(u->protocol, m);
        pa_native_protocol_unref(u->protocol);
    }
    if (u->source_output_new_hook_slot)
        pa_hook_slot_free(u->source_output_new_hook_slot);
		
	/* Deinit audio mgr & unload library */
    if (u->audio_mgr.intf.deinit) {
        if (u->audio_mgr.intf.deinit(&u->audio_mgr.data) != AUDIO_RET_OK) {
            pa_log_error("audio_mgr deinit failed");
        }
    }
    if (u->audio_mgr.dl_handle) {
        dlclose(u->audio_mgr.dl_handle);
    }

    pa_xfree(u);


	pa_log_info("policy module is unloaded\n");
}
