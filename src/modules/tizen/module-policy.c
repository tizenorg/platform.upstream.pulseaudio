#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <strings.h>
#include <vconf.h> // for mono
#include <iniparser.h>
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
#ifdef HAVE_DBUS
#include <pulsecore/dbus-shared.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/protocol-dbus.h>
#endif

#include "module-policy-symdef.h"
#include "communicator.h"
#include "hal-manager.h"
#include "stream-manager.h"
#include "stream-manager-volume.h"
//#define DEVICE_MANAGER
#ifdef DEVICE_MANAGER
#include "device-manager.h"
#endif

#define VCONFKEY_SOUND_HDMI_SUPPORT "memory/private/sound/hdmisupport"

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

struct userdata;

#ifdef HAVE_DBUS

/*** Defines for module policy dbus interface ***/
#define OBJECT_PATH "/org/pulseaudio/policy1"
#define INTERFACE_POLICY "org.PulseAudio.Ext.Policy1"
#define POLICY_INTROSPECT_XML                                               \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                               \
    "<node>"                                                                \
    " <interface name=\"" INTERFACE_POLICY "\">"                            \
    "  <method name=\"MethodTest1\">"                                       \
    "   <arg name=\"arg1\" direction=\"in\" type=\"s\"/>"                   \
    "   <arg name=\"arg2\" direction=\"out\" type=\"u\"/>"                  \
    "  </method>"                                                           \
    "  <method name=\"MethodTest2\">"                                       \
    "   <arg name=\"arg1\" direction=\"in\" type=\"i\"/>"                   \
    "   <arg name=\"arg2\" direction=\"in\" type=\"i\"/>"                   \
    "   <arg name=\"arg3\" direction=\"out\" type=\"i\"/>"                  \
    "  </method>"                                                           \
    "  <property name=\"PropertyTest1\" type=\"i\" access=\"readwrite\"/>"  \
    "  <property name=\"PropertyTest2\" type=\"s\" access=\"read\"/>"       \
    "  <signal name=\"PropertyTest1Changed\">"                              \
    "   <arg name=\"arg1\" type=\"i\"/>"                                    \
    "  </signal>"                                                           \
    "  <signal name=\"SignalTest2\">"                                       \
    "   <arg name=\"arg1\" type=\"s\"/>"                                    \
    "  </signal>"                                                           \
    " </interface>"                                                         \
    " <interface name=\"" DBUS_INTERFACE_INTROSPECTABLE "\">\n"             \
    "  <method name=\"Introspect\">\n"                                      \
    "   <arg name=\"data\" type=\"s\" direction=\"out\"/>\n"                \
    "  </method>\n"                                                         \
    " </interface>\n"                                                       \
    " <interface name=\"" DBUS_INTERFACE_PROPERTIES "\">\n"                 \
    "  <method name=\"Get\">\n"                                             \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"       \
    "   <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"        \
    "   <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"               \
    "  </method>\n"                                                         \
    "  <method name=\"Set\">\n"                                             \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"       \
    "   <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"        \
    "   <arg name=\"value\" type=\"v\" direction=\"in\"/>\n"                \
    "  </method>\n"                                                         \
    "  <method name=\"GetAll\">\n"                                          \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"       \
    "   <arg name=\"props\" type=\"a{sv}\" direction=\"out\"/>\n"           \
    "  </method>\n"                                                         \
    " </interface>\n"                                                       \
    "</node>"


static DBusHandlerResult handle_get_property(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_get_all_property(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_set_property(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_policy_methods(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult method_call_handler(DBusConnection *c, DBusMessage *m, void *userdata);
static void endpoint_init(struct userdata *u);
static void endpoint_done(struct userdata* u);

/*** Called when module-policy load/unload ***/
static void dbus_init(struct userdata* u);
static void dbus_deinit(struct userdata* u);

/*** Defines for Property handle ***/
/* property handlers */
static void handle_get_property_test1(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_property_test1(DBusConnection *conn, DBusMessage *msg, DBusMessageIter *iter, void *userdata);
static void handle_get_property_test2(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum property_index {
    PROPERTY_TEST1,
    PROPERTY_TEST2,
    PROPERTY_MAX
};

static pa_dbus_property_handler property_handlers[PROPERTY_MAX] = {
    [PROPERTY_TEST1] = { .property_name = "PropertyTest1", .type = "i",
                                 .get_cb = handle_get_property_test1,
                                 .set_cb = handle_set_property_test1 },
    [PROPERTY_TEST2] = { .property_name = "PropertyTest2", .type = "s",
                                 .get_cb = handle_get_property_test2,
                                 .set_cb = NULL },
};


/*** Defines for method handle ***/
/* method handlers */
static void handle_method_test1(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_method_test2(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_all(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum method_handler_index {
    METHOD_HANDLER_TEST1,
    METHOD_HANDLER_TEST2,
    METHOD_HANDLER_MAX
};

static pa_dbus_arg_info method1_args[] = { { "arg1",              "s",     "in" },
                                             { "arg2",             "u",     "out" } };

static pa_dbus_arg_info method2_args[] = { { "arg1",              "i",     "in" },
                                             { "arg2",             "i",     "in" },
                                             { "arg3",             "i",     "out" } };

static const char* method_arg_signatures[] = { "s", "ii" };

static pa_dbus_method_handler method_handlers[METHOD_HANDLER_MAX] = {
    [METHOD_HANDLER_TEST1] = {
        .method_name = "MethodTest1",
        .arguments = method1_args,
        .n_arguments = sizeof(method1_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_method_test1 },
    [METHOD_HANDLER_TEST2] = {
        .method_name = "MethodTest2",
        .arguments = method2_args,
        .n_arguments = sizeof(method2_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_method_test2 }
};

/*** Defines for signal send ***/
static int watch_signals(struct userdata* u);
static void unwatch_signals(struct userdata* u);
static void send_prop1_changed_signal(struct userdata* u);

enum signal_index {
    SIGNAL_PROP_CHANGED,
    SIGNAL_TEST2,
    SIGNAL_MAX
};

static pa_dbus_arg_info signal_test1_args[] = { { "arg1", "i", NULL } };
static pa_dbus_arg_info signal_test2_args[] = { { "arg1", "s", NULL } };

static pa_dbus_signal_info signals[SIGNAL_MAX] = {
    [SIGNAL_PROP_CHANGED] = { .name = "PropertyTest1Changed", .arguments = signal_test1_args, .n_arguments = 1 },
    [SIGNAL_TEST2] = { .name = "SignalTest2", .arguments = signal_test2_args, .n_arguments = 1 },
};

/*** For handle module-policy dbus interface ***/
static pa_dbus_interface_info policy_interface_info = {
    .name = INTERFACE_POLICY,
    .method_handlers = method_handlers,
    .n_method_handlers = METHOD_HANDLER_MAX,
    .property_handlers = property_handlers,
    .n_property_handlers = PROPERTY_MAX,
    .get_all_properties_cb = handle_get_all,
    .signals = signals,
    .n_signals = SIGNAL_MAX
};

/*** Defines for get signal ***/
#define SOUND_SERVER_INTERFACE_NAME "org.tizen.soundserver.service"
#define AUDIO_CLIENT_INTERFACE_NAME "org.tizen.audioclient.service"

#define SOUND_SERVER_FILTER              \
    "type='signal',"                    \
    " interface='" SOUND_SERVER_INTERFACE_NAME "'"
#define AUDIO_CLIENT_FILTER              \
    "type='signal',"                    \
    " interface='" AUDIO_CLIENT_INTERFACE_NAME "'"

#endif

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
#define MONO_KEY			VCONFKEY_SETAPPL_ACCESSIBILITY_MONO_AUDIO

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

struct pa_hal_device_event_data {
    audio_device_info_t device_info;
    audio_device_param_info_t params[AUDIO_DEVICE_PARAM_MAX];
};

struct userdata {
    pa_core *core;
    pa_module *module;

    pa_hook_slot *sink_input_new_hook_slot,*sink_put_hook_slot;

    pa_hook_slot *sink_unlink_slot;

    pa_hook_slot *sink_input_unlink_post_slot, *sink_unlink_post_slot;
    pa_hook_slot *sink_input_move_start_slot,*sink_input_move_finish_slot;
    pa_hook_slot *source_output_new_hook_slot;

    pa_hook_slot *sink_state_changed_slot;
    pa_hook_slot *sink_input_state_changed_slot;

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

    pa_hal_manager *hal_manager;

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
#ifdef HAVE_DBUS
    pa_dbus_connection *dbus_conn;
    int32_t test_property1;
#endif

    struct {
        pa_communicator *comm;
        pa_hook_slot *comm_hook_select_proper_sink_or_source_slot;
        pa_hook_slot *comm_hook_change_route_slot;
        pa_hook_slot *comm_hook_update_route_options_slot;
    } communicator;

    pa_stream_manager *stream_manager;
#ifdef DEVICE_MANAGER
    pa_device_manager *device_manager;
#endif
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
    SUBCOMMAND_MONO,
    SUBCOMMAND_BALANCE,
    SUBCOMMAND_MUTEALL,
    SUBCOMMAND_SET_USE_CASE,
    SUBCOMMAND_SET_SESSION,
    SUBCOMMAND_SET_SUBSESSION,
    SUBCOMMAND_SET_ACTIVE_DEVICE,
    SUBCOMMAND_RESET,
    SUBCOMMAND_GET_VOLUME_LEVEL,
    SUBCOMMAND_SET_VOLUME_LEVEL,
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

static audio_return_t policy_volume_reset(struct userdata *u);
static audio_return_t policy_set_session(struct userdata *u, uint32_t session, uint32_t start);
static audio_return_t policy_set_active_device(struct userdata *u, uint32_t device_in, uint32_t device_out, uint32_t* need_update);
static pa_bool_t policy_is_filter (pa_sink_input* si);

static int __convert_volume_type_to_string(uint32_t volume_type, const char **volume_type_str) {
    int ret = 0;
    switch (volume_type) {
    case AUDIO_VOLUME_TYPE_SYSTEM:
        *volume_type_str = "system";
        break;
    case AUDIO_VOLUME_TYPE_NOTIFICATION:
        *volume_type_str = "notification";
        break;
    case AUDIO_VOLUME_TYPE_ALARM:
        *volume_type_str = "alarm";
        break;
    case AUDIO_VOLUME_TYPE_RINGTONE:
        *volume_type_str = "ringtone";
        break;
    case AUDIO_VOLUME_TYPE_MEDIA:
        *volume_type_str = "media";
        break;
    case AUDIO_VOLUME_TYPE_CALL:
        *volume_type_str = "call";
        break;
    case AUDIO_VOLUME_TYPE_VOIP:
        *volume_type_str = "voip";
        break;
    case AUDIO_VOLUME_TYPE_VOICE:
        *volume_type_str = "voice";
        break;
    case AUDIO_VOLUME_TYPE_FIXED:
        *volume_type_str = "fixed";
        break;
    default:
        ret = -1;
    }
    pa_log_debug("volume_type[%d] => [%s], ret[%d]", volume_type, *volume_type_str, ret);
    return ret;
}

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
static pa_bool_t policy_is_filter (pa_sink_input* si)
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

/* It will be removed soon. This operation will be moved to stream-manager using dbus */
/* This is for the volume tunning app. for product. It needs to reset volume table of stream-manager itself as well as HAL's */
static audio_return_t policy_volume_reset(struct userdata *u)
{
    audio_return_t audio_ret = AUDIO_RET_OK;

    pa_log_debug("reset");
    __load_dump_config(u);

    audio_ret = pa_hal_manager_reset_volume(u->hal_manager);

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
        /* it will be removed soon */
        if (u->hal_manager->intf.set_session) {
            u->hal_manager->intf.set_session(u->hal_manager->data, session, u->subsession, AUDIO_SESSION_CMD_START);
        }
    } else {
        /* it will be removed soon */
        if (u->hal_manager->intf.set_session) {
            u->hal_manager->intf.set_session(u->hal_manager->data, session, u->subsession, AUDIO_SESSION_CMD_END);
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
        /* it will be removed soon */
        uint32_t route_flag = __get_route_flag(u);
        if (u->hal_manager->intf.set_route) {
            u->hal_manager->intf.set_route(u->hal_manager->data, u->session, u->subsession, u->active_device_in, u->active_device_out, route_flag);
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
    /* it will be removed soon */
    if (u->hal_manager->intf.set_session) {
        u->hal_manager->intf.set_session(u->hal_manager->data, u->session, u->subsession, AUDIO_SESSION_CMD_SUBSESSION);
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
        /* it will be removed soon */
        if (u->hal_manager->intf.set_route) {
            u->hal_manager->intf.set_route(u->hal_manager->data, u->session, u->subsession, u->active_device_in, u->active_device_out, route_flag);
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
    /* it will be removed soon */
    if (u->hal_manager->intf.set_route) {
        u->hal_manager->intf.set_route(u->hal_manager->data, u->session, u->subsession, device_in, device_out, route_flag);
    }
#else

    /* skip volume changed callback */
    if(u->active_device_out == device_out) {
        *need_update = false;
    }
    /* it will be removed soon */
    if (u->hal_manager->intf.set_route) {
        int32_t audio_ret = 0;
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
                    audio_ret = pa_stream_manager_volume_set_mute_by_idx(u->stream_manager, STREAM_SINK_INPUT, si->index, 1);
                    if (audio_ret) {
                        pa_log_warn("pa_stream_manager_volume_set_mute_by_idx(1) for stream[%d] returns error:0x%x", si->index, audio_ret);
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
        /* it will be removed soon */
        u->hal_manager->intf.set_route(u->hal_manager->data, u->session, u->subsession, device_in, device_out, route_flag);
        /* Unmute sink inputs which are muted due to device switching */
        PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
            if ((device_switching_str = pa_proplist_gets(si->proplist, "module-policy.device_switching"))) {
                pa_atou(device_switching_str, &device_switching);
                if (device_switching) {
                    pa_proplist_sets(si->proplist, "module-policy.device_switching", "0");
//                    if (AUDIO_IS_ERROR((audio_ret = __update_volume(u, si->index, (uint32_t)-1, (uint32_t)-1)))) {
//                        pa_log_warn("__update_volume for stream[%d] returns error:0x%x", si->index, audio_ret);
//                    }
                    audio_ret = pa_stream_manager_volume_set_mute_by_idx(u->stream_manager, STREAM_SINK_INPUT, si->index, 0);
                    if (audio_ret) {
                        pa_log_warn("pa_stream_manager_volume_set_mute_by_idx(0) for stream[%d] returns error:0x%x", si->index, audio_ret);
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
            pa_stream_manager_volume_set_mute(u->stream_manager, STREAM_SINK_INPUT, "call", 1);
        }
        /* workaround for keeping call mute setting */
        //policy_set_mute(u, (-1), AUDIO_VOLUME_TYPE_CALL, AUDIO_DIRECTION_IN, u->call_muted);
        pa_stream_manager_volume_set_mute(u->stream_manager, STREAM_SOURCE_OUTPUT, "call", 1); /* FOR IN? */
    }

    return AUDIO_RET_OK;
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
        /* it will be removed soon */
        case SUBCOMMAND_GET_VOLUME_LEVEL: {
            uint32_t stream_idx = PA_INVALID_INDEX;
            uint32_t volume_type = 0;
            uint32_t volume_level = 0;
            const char *volume_str = NULL;

            pa_tagstruct_getu32(t, &stream_idx);
            pa_tagstruct_getu32(t, &volume_type);

            __convert_volume_type_to_string(volume_type, &volume_str);
            pa_stream_manager_volume_get_level(u->stream_manager, STREAM_SINK_INPUT, volume_str, &volume_level);

            pa_tagstruct_putu32(reply, volume_level);
            break;
        }
        /* it will be removed soon */
        case SUBCOMMAND_SET_VOLUME_LEVEL: {
            uint32_t stream_idx = PA_INVALID_INDEX;
            uint32_t volume_type = 0;
            uint32_t volume_level = 0;
            const char *volume_str = NULL;

            pa_tagstruct_getu32(t, &stream_idx);
            pa_tagstruct_getu32(t, &volume_type);
            pa_tagstruct_getu32(t, &volume_level);

            __convert_volume_type_to_string(volume_type, &volume_str);
            pa_stream_manager_volume_set_level(u->stream_manager, STREAM_SINK_INPUT, volume_str, volume_level);
            break;
        }
        /* it will be removed soon */
        case SUBCOMMAND_GET_MUTE: {
            uint32_t stream_idx = PA_INVALID_INDEX;
            uint32_t volume_type = 0;
            uint32_t direction = 0;
            uint32_t mute = 0;
            const char *volume_str = NULL;

            pa_tagstruct_getu32(t, &stream_idx);
            pa_tagstruct_getu32(t, &volume_type);
            pa_tagstruct_getu32(t, &direction);

            __convert_volume_type_to_string(volume_type, &volume_str);
            pa_stream_manager_volume_get_mute(u->stream_manager, STREAM_SINK_INPUT, volume_str, &mute);

            pa_tagstruct_putu32(reply, mute);
            break;
        }
        /* it will be removed soon */
        case SUBCOMMAND_SET_MUTE: {
            uint32_t stream_idx = PA_INVALID_INDEX;
            uint32_t volume_type = 0;
            uint32_t direction = 0;
            uint32_t mute = 0;
            const char *volume_str = NULL;

            pa_tagstruct_getu32(t, &stream_idx);
            pa_tagstruct_getu32(t, &volume_type);
            pa_tagstruct_getu32(t, &direction);
            pa_tagstruct_getu32(t, &mute);

            __convert_volume_type_to_string(volume_type, &volume_str);
            if (stream_idx == -1)
                pa_stream_manager_volume_set_mute(u->stream_manager, STREAM_SINK_INPUT, volume_str, mute);
            else
                pa_stream_manager_volume_set_mute_by_idx(u->stream_manager, STREAM_SINK_INPUT, stream_idx, mute);
            break;
        }
        case SUBCOMMAND_IS_AVAILABLE_HIGH_LATENCY: {
            pa_bool_t available = FALSE;

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
//                        if (AUDIO_IS_ERROR((audio_ret = __update_volume(u, si->index, (uint32_t)-1, (uint32_t)-1)))) {
//                            pa_log_debug("__update_volume for stream[%d] returns error:0x%x", si->index, audio_ret);
//                        }
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

/* Set the proper sink(source) according to the data of the parameter.  */
/* - ROUTE_TYPE_AUTO(_ALL)                                              */
/*     1. Find the proper sink/source comparing between avail_devices   */
/*       and current connected devices.                                 */
/*     2. If not found, set it to null sink/source.                     */
/* - ROUTE_TYPE_MANUAL                                                  */
/*     1. Find the proper sink/source comparing between avail_devices   */
/*        and manual_devices that have been set by user.                */
/*     2. If not found, set it to null sink/source.                     */
static pa_hook_result_t select_proper_sink_or_source_hook_cb(pa_core *c, pa_stream_manager_hook_data_for_select *data, struct userdata *u) {
#ifdef DEVICE_MANAGER
    uint32_t idx = 0;
    uint32_t m_idx = 0;
    uint32_t conn_idx = 0;
    uint32_t *device_id = NULL;
    const char *device_type = NULL;
    const char *dm_device_type = NULL;
    const char *dm_device_subtype = NULL;
    dm_device *device = NULL;
    dm_device_direction_t device_direction = DM_DEVICE_DIRECTION_NONE;
    pa_idxset *conn_devices = NULL;

    if ((data->route_type == STREAM_ROUTE_TYPE_AUTO || data->route_type == STREAM_ROUTE_TYPE_AUTO_ALL) && data->idx_avail_devices) {
        /* Get current connected devices */
        conn_devices = pa_device_manager_get_device_list(u->device_manager);
        PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
            pa_log_debug("-- [AUTO] avail_device[%u] for this role[%s]: type(%s)", idx, data->stream_role, device_type);
            PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
                dm_device_type = pa_device_manager_get_device_type(device);
                dm_device_subtype = pa_device_manager_get_device_subtype(device);
                device_direction = pa_device_manager_get_device_direction(device);
                pa_log_debug("-- [AUTO] conn_devices, type[%s], subtype[%s], direction[%p]", dm_device_type, dm_device_subtype, device_direction);
                if (pa_streq(device_type, dm_device_type) &&
                    (((data->stream_type==STREAM_SINK_INPUT) && (device_direction & DM_DEVICE_DIRECTION_OUT)) ||
                    ((data->stream_type==STREAM_SOURCE_OUTPUT) && (device_direction & DM_DEVICE_DIRECTION_IN)))) {
                    pa_log_debug("-- [AUTO] found a matched device: type[%s], direction[%p]", device_type, device_direction);
                    if (data->stream_type == STREAM_SINK_INPUT)
                        *(data->proper_sink) = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                    else
                        *(data->proper_source) = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL);
                    goto SUCCESS;
                    }
                }
        }
        /* need to add logic for auto-all. (use combine-sink, move sink-input/source-output) */

    } else if (data->route_type == STREAM_ROUTE_TYPE_MANUAL && data->idx_manual_devices && data->idx_avail_devices) {
        PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
            pa_log_debug("-- [MANUAL] avail_device[%u] for this role[%s]: type(%s)", idx, data->stream_role, device_type);
            PA_IDXSET_FOREACH(device_id, data->idx_manual_devices, m_idx) {
                device = pa_device_manager_get_device_by_id(u->device_manager, *device_id);
                if (device) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    dm_device_subtype = pa_device_manager_get_device_subtype(device);
                    device_direction = pa_device_manager_get_device_direction(device);
                    pa_log_debug("-- [MANUAL] manual_devices, type[%s], subtype[%s], direction[%p], device id[%u]",
                            dm_device_type, dm_device_subtype, device_direction, *device_id);
                    if (pa_streq(device_type, dm_device_type) &&
                        (((data->stream_type==STREAM_SINK_INPUT) && (device_direction & DM_DEVICE_DIRECTION_OUT)) ||
                        ((data->stream_type==STREAM_SOURCE_OUTPUT) && (device_direction & DM_DEVICE_DIRECTION_IN)))) {
                        pa_log_debug("-- [MANUAL] found a matched device: type[%s], direction[0x%x]", device_type, device_direction);
                        if (data->stream_type == STREAM_SINK_INPUT)
                            *(data->proper_sink) = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                        else
                            *(data->proper_source) = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL);
                        goto SUCCESS;
                        }
                }
            }
        }
    }

    if ((data->stream_type==STREAM_SINK_INPUT)?!(*(data->proper_sink)):!(*(data->proper_source))) {
        pa_log_warn("could not find a proper sink/source, set it to null sink/source");
        if (data->stream_type == STREAM_SINK_INPUT)
            *(data->proper_sink) = (pa_sink*)pa_namereg_get(u->core, "null", PA_NAMEREG_SINK);
        else
            *(data->proper_source) = (pa_source*)pa_namereg_get(u->core, "null", PA_NAMEREG_SOURCE);
    }
SUCCESS:
#endif
    return PA_HOOK_OK;
}

/* Change the route setting according to the data from argument.              */
/* This function is called only when it needs to change routing path via HAL. */
/* - role is "reset"                                                          */
/*     1. It will be received when it is needed to terminate playback         */
/*       or capture routing path.                                             */
/*     2. Update the state of the device to be deactivated.                   */
/*     3. Call HAL API to reset routing.                                      */
/* - ROUTE_TYPE_AUTO                                                          */
/*     1. Find the proper sink/source comparing between avail_devices         */
/*       and current connected devices.                                       */
/*      : Need to check the priority of the device list by order of receipt.  */
/*     2. Update the state of devices.                                        */
/*     3. Call HAL API to apply the routing setting                           */
/* - ROUTE_TYPE_AUTO_ALL                                                      */
/*     1. Find the proper sink/source comparing between avail_devices         */
/*       and current connected devices.                                       */
/*      : Might use combine-sink according to the conditions.                 */
/*     2. Update the state of devices.                                        */
/*     3. Call HAL API to apply the routing setting                           */
/* - ROUTE_TYPE_MANUAL                                                        */
/*     1. Find the proper sink/source comparing between avail_devices         */
/*        and manual_devices that have been set by user.                      */
/*     2. Update the state of devices.                                        */
/*     3. Call HAL API to apply the routing setting                           */
static pa_hook_result_t route_change_hook_cb(pa_core *c, pa_stream_manager_hook_data_for_route *data, struct userdata *u) {
#ifdef DEVICE_MANAGER
    int32_t i = 0;
    uint32_t idx = 0;
    uint32_t m_idx = 0;
    uint32_t *stream_idx = NULL;
    hal_route_info route_info = {NULL, NULL, 0};
    uint32_t conn_idx = 0;
    uint32_t *device_id = NULL;
    uint32_t device_idx = 0;
    const char *device_type = NULL;
    dm_device *device = NULL;
    dm_device *_device = NULL;
    const char *dm_device_type = NULL;
    const char *dm_device_subtype = NULL;
    dm_device_state_t device_state = DM_DEVICE_STATE_DEACTIVATED;
    dm_device_direction_t device_direction = DM_DEVICE_DIRECTION_NONE;
    void *s = NULL;
    pa_sink *sink = NULL;
    pa_source *source = NULL;
    pa_idxset *conn_devices = NULL;

    pa_log_info("route_change_hook_cb is called. (%p), stream_type(%d), stream_role(%s), route_type(%d)",
            data, data->stream_type, data->stream_role, data->route_type);
    route_info.role = data->stream_role;

    /* Streams */
    if (data->idx_streams) {
        PA_IDXSET_FOREACH(stream_idx, data->idx_streams, idx) {
            pa_log_debug("-- stream[%u]: idx(%u)", idx, *stream_idx);
        }
    }

    /* Devices */
    if (pa_streq(data->stream_role, "reset")) {
        /* Get current connected devices */
        conn_devices = pa_device_manager_get_device_list(u->device_manager);
        /* Set device state to deactivate */
        PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
            dm_device_type = pa_device_manager_get_device_type(device);
            device_state = pa_device_manager_get_device_state(device);
            device_direction = pa_device_manager_get_device_direction(device);
            if (device_state == DM_DEVICE_STATE_ACTIVATED &&
                (((data->stream_type==STREAM_SINK_INPUT) && (device_direction & DM_DEVICE_DIRECTION_OUT)) ||
                ((data->stream_type==STREAM_SOURCE_OUTPUT) && (device_direction & DM_DEVICE_DIRECTION_IN)))) {
                pa_log_debug("-- [RESET] found a matched device and set state to DE-ACTIVATED: type[%s], direction[%p]", dm_device_type, device_direction);
                /* set device state to deactivated */
                pa_device_manager_set_device_state(device, DM_DEVICE_STATE_DEACTIVATED);
              }
        }
        route_info.num_of_devices = 1;
        route_info.device_infos = pa_xmalloc0(sizeof(hal_device_info)*route_info.num_of_devices);
        route_info.device_infos[0].direction = (data->stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN;

    } else if ((data->route_type == STREAM_ROUTE_TYPE_AUTO || data->route_type == STREAM_ROUTE_TYPE_AUTO_ALL) && data->idx_avail_devices) {
        /* Get current connected devices */
        conn_devices = pa_device_manager_get_device_list(u->device_manager);
        PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
            pa_log_debug("-- [AUTO] avail_device[%u] for this role[%s]: type[%s]", idx, route_info.role, device_type);
            PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
                dm_device_type = pa_device_manager_get_device_type(device);
                dm_device_subtype = pa_device_manager_get_device_subtype(device);
                device_direction = pa_device_manager_get_device_direction(device);
                device_idx = pa_device_manager_get_device_id(device);
                pa_log_debug("-- [AUTO] conn_devices, type[%s], subtype[%s], direction[%p], id[%u]",
                        dm_device_type, dm_device_subtype, device_direction, device_idx);
                if (pa_streq(device_type, dm_device_type) &&
                    (((data->stream_type==STREAM_SINK_INPUT) && (device_direction & DM_DEVICE_DIRECTION_OUT)) ||
                    ((data->stream_type==STREAM_SOURCE_OUTPUT) && (device_direction & DM_DEVICE_DIRECTION_IN)))) {
                    if (dm_device_subtype && pa_streq(DEVICE_PROFILE_BT_SCO, dm_device_subtype))
                        continue;
                    route_info.num_of_devices++;
                    route_info.device_infos = pa_xrealloc(route_info.device_infos, sizeof(hal_device_info)*route_info.num_of_devices);
                    route_info.device_infos[route_info.num_of_devices-1].type = dm_device_type;
                    route_info.device_infos[route_info.num_of_devices-1].direction = (data->stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN;
                    route_info.device_infos[route_info.num_of_devices-1].id = device_idx;
                    pa_log_debug("-- [AUTO] found a matched device and set state to ACTIVATED: type[%s], direction[%p], id[%u]",
                        route_info.device_infos[route_info.num_of_devices-1].type, device_direction, device_idx);
                    /* Set device state to activated */
                    pa_device_manager_set_device_state(device, DM_DEVICE_STATE_ACTIVATED);
                    break;
                    }
                }
            if (data->route_type == STREAM_ROUTE_TYPE_AUTO && route_info.num_of_devices) {
                /* Set other device's state to deactivated */
                PA_IDXSET_FOREACH(_device, conn_devices, conn_idx) {
                    if (device == _device)
                        continue;
                    pa_device_manager_set_device_state(device, DM_DEVICE_STATE_DEACTIVATED);
                }

                /* Move sink-inputs/source-outputs if needed */
                if (!data->origins_from_new_data) {
                    if (data->stream_type == STREAM_SINK_INPUT)
                        sink = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                    else if (data->stream_type == STREAM_SOURCE_OUTPUT)
                        source = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL);
                    if (data->idx_streams) {
                        PA_IDXSET_FOREACH (s, data->idx_streams, idx) {
                            if (sink && sink != (data->origins_from_new_data?((pa_sink_input_new_data*)s)->sink:((pa_sink_input*)s)->sink))
                                pa_sink_input_move_to(s, sink, FALSE);
                            else if (source && source != (data->origins_from_new_data?((pa_source_output_new_data*)s)->source:((pa_source_output*)s)->source))
                                pa_source_output_move_to(s, source, FALSE);
                        }
                    }
                }
                break;
            }
        }
        if (data->route_type == STREAM_ROUTE_TYPE_AUTO_ALL && route_info.num_of_devices) {
            /* Set other device's state to deactivated */
            PA_IDXSET_FOREACH(_device, conn_devices, conn_idx) {
                pa_bool_t need_to_deactive = TRUE;
                device_idx = pa_device_manager_get_device_id(_device);
                for (i = 0; i < route_info.num_of_devices; i++) {
                    if (device_idx == route_info.device_infos[i].id) {
                        need_to_deactive = FALSE;
                        break;
                    }
                }
                if (need_to_deactive)
                    pa_device_manager_set_device_state(_device, DM_DEVICE_STATE_DEACTIVATED);
            }
        }

    } else if (data->route_type == STREAM_ROUTE_TYPE_MANUAL && data->idx_manual_devices && data->idx_avail_devices) {
        PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
            pa_log_debug("-- [MANUAL] avail_device[%u] for this role[%s]: type(%s)", idx, data->stream_role, device_type);
            PA_IDXSET_FOREACH(device_id, data->idx_manual_devices, m_idx) {
                device = pa_device_manager_get_device_by_id(u->device_manager, *device_id);
                if (device) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    dm_device_subtype = pa_device_manager_get_device_subtype(device);
                    device_direction = pa_device_manager_get_device_direction(device);
                    pa_log_debug("-- [MANUAL] manual_devices, type[%s], subtype[%s], direction[%p]", dm_device_type, dm_device_subtype, device_direction);
                    if (pa_streq(device_type, dm_device_type) &&
                        (((data->stream_type==STREAM_SINK_INPUT) && (device_direction & DM_DEVICE_DIRECTION_OUT)) ||
                        ((data->stream_type==STREAM_SOURCE_OUTPUT) && (device_direction & DM_DEVICE_DIRECTION_IN)))) {
                        pa_log_debug("-- [MANUAL] found a matched device: type[%s], direction[%p]", device_type, device_direction);
                        route_info.num_of_devices++;
                        route_info.device_infos = pa_xrealloc(route_info.device_infos, sizeof(hal_device_info)*route_info.num_of_devices);
                        route_info.device_infos[route_info.num_of_devices-1].type = dm_device_type;
                        route_info.device_infos[route_info.num_of_devices-1].direction = (data->stream_type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN;
                        pa_log_debug("-- [MANUAL] found a matched device and set state to ACTIVATED: type[%s], direction[%p]",
                            route_info.device_infos[route_info.num_of_devices-1].type, device_direction);
                        /* Set device state to activated */
                        pa_device_manager_set_device_state(device, DM_DEVICE_STATE_ACTIVATED);
                        }
                }
            }
        }

        /* Move sink-inputs/source-outputs if needed */
        if (!data->origins_from_new_data) {
            if (data->stream_type == STREAM_SINK_INPUT)
                sink = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
            else if (data->stream_type == STREAM_SOURCE_OUTPUT)
                source = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL);
            if (data->idx_streams) {
                PA_IDXSET_FOREACH (s, data->idx_streams, idx) {
                    if (sink && sink != (data->origins_from_new_data?((pa_sink_input_new_data*)s)->sink:((pa_sink_input*)s)->sink))
                        pa_sink_input_move_to(s, sink, FALSE);
                    else if (source && source != (data->origins_from_new_data?((pa_source_output_new_data*)s)->source:((pa_source_output*)s)->source))
                        pa_source_output_move_to(s, source, FALSE);
                }
            }
        }
    }

    if (route_info.device_infos) {
        /* Send information to HAL to set routing */
        if(pa_hal_manager_do_route (u->hal_manager, &route_info))
            pa_log_error("Failed to pa_hal_manager_do_route()");
        pa_xfree(route_info.device_infos);
    }
#endif
    return PA_HOOK_OK;
}

/* Forward routing options to HAL */
static pa_hook_result_t route_options_update_hook_cb(pa_core *c, pa_stream_manager_hook_data_for_options *data, struct userdata *u) {
    void *state = NULL;
    const char *option_name = NULL;
    int i = 0;
    hal_route_option route_option;

    pa_log("route_options_update_hook_cb is called. (%p), stream_role(%s), route_options(%p), num_of_options(%d)",
            data, data->stream_role, data->route_options, pa_idxset_size(data->route_options));
    route_option.role = data->stream_role;
    route_option.num_of_options = pa_idxset_size(data->route_options);
    if (route_option.num_of_options)
        route_option.options = pa_xmalloc0(sizeof(char*)*route_option.num_of_options);

    while (data->route_options && (option_name = pa_idxset_iterate(data->route_options, &state, NULL))) {
        pa_log("-- option : %s", option_name);
        route_option.options[i++] = option_name;
    }

    /* Send information to HAL to update routing option */
    if(pa_hal_manager_update_route_option (u->hal_manager, &route_option))
        pa_log_error("Failed to pa_hal_manager_update_route_option()");
    pa_xfree(route_option.options);

    return PA_HOOK_OK;
}

#ifdef HAVE_DBUS
static void _do_something1(char* arg1, int arg2, void *data)
{
    pa_assert(data);
    pa_assert(arg1);

    pa_log_debug("Do Something 1 , arg1 (%s) arg2 (%d)", arg1, arg2);
}

static void _do_something2(char* arg1, void *data)
{
    pa_assert(data);
    pa_assert(arg1);

    pa_log_debug("Do Something 2 , arg1 (%s) ", arg1);
}

static void handle_get_property_test1(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    struct userdata *u = userdata;
    dbus_int32_t value_i = 0;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    value_i = u->test_property1;

    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_INT32, &value_i);
}

static void handle_set_property_test1(DBusConnection *conn, DBusMessage *msg, DBusMessageIter *iter, void *userdata)
{
    struct userdata *u = userdata;
    dbus_int32_t value_i = 0;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    dbus_message_iter_get_basic(iter, &value_i);

    u->test_property1 = value_i;
    pa_dbus_send_empty_reply(conn, msg);

    /* send signal to notify change of property1*/
    send_prop1_changed_signal(u);
}

/* test property handler : return module name */
static void handle_get_property_test2(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    struct userdata *u = userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    if (!u->module->name) {
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_ARGS, "property(module name) null");
        return;
    }

    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_STRING, &u->module->name);
}

static void handle_get_all(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    struct userdata *u = userdata;
    dbus_int32_t value_i = 0;

    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;
    DBusMessageIter dict_iter;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    value_i = u->test_property1;

    dbus_message_iter_init_append(reply, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter));

    pa_dbus_append_basic_variant_dict_entry(&dict_iter, property_handlers[PROPERTY_TEST1].property_name, DBUS_TYPE_INT32, &value_i);
    pa_dbus_append_basic_variant_dict_entry(&dict_iter, property_handlers[PROPERTY_TEST2].property_name, DBUS_TYPE_STRING, &u->module->name);
    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &dict_iter));
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

/* test method : return length of argument string */
static void handle_method_test1(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    const char* arg1_s = NULL;
    dbus_uint32_t value_u = 0;
    pa_assert(conn);
    pa_assert(msg);

    pa_assert_se(dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &arg1_s, DBUS_TYPE_INVALID));
    value_u = strlen(arg1_s);
    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_UINT32, &value_u);
}

static void handle_method_test2(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    dbus_int32_t value1, value2, result;
    pa_assert(conn);
    pa_assert(msg);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_INT32, &value1,
                                       DBUS_TYPE_INT32, &value2,
                                       DBUS_TYPE_INVALID));

    result = value1 * value2;

    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_INT32, &result);
}

static DBusMessage* _generate_basic_property_change_signal_msg(int property_index, int property_type, void *data) {
    DBusMessage *signal_msg;
    DBusMessageIter signal_iter, dict_iter;
    const char *interface = INTERFACE_POLICY;

    /* org.freedesktop.DBus.Properties.PropertiesChanged (
           STRING interface_name,
           DICT<STRING,VARIANT> changed_properties,
           ARRAY<STRING> invalidated_properties); */

    pa_assert_se(signal_msg = dbus_message_new_signal(OBJECT_PATH, DBUS_INTERFACE_PROPERTIES, SIGNAL_PROP_CHANGED));
    dbus_message_iter_init_append(signal_msg, &signal_iter);

    /* STRING interface_name */
    dbus_message_iter_append_basic(&signal_iter, DBUS_TYPE_STRING, &interface);

    /* DICT<STRING,VARIANT> changed_properties */
    pa_assert_se(dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter));
    pa_dbus_append_basic_variant_dict_entry(&dict_iter, property_handlers[property_index].property_name,
                                            property_type, data);
    dbus_message_iter_close_container(&signal_iter, &dict_iter);

    /* ARRAY<STRING> invalidated_properties (empty) */
    dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY, "s", &dict_iter);
    dbus_message_iter_close_container(&signal_iter, &dict_iter);

    return signal_msg;
}

static void send_prop1_changed_signal(struct userdata* u) {
    DBusMessage *signal_msg = _generate_basic_property_change_signal_msg(PROPERTY_TEST1, DBUS_TYPE_INT32, &u->test_property1);

#ifdef USE_DBUS_PROTOCOL
    pa_dbus_protocol_send_signal(u->dbus_protocol, signal_msg);
#else
    dbus_connection_send(pa_dbus_connection_get(u->dbus_conn), signal_msg, NULL);
#endif

    dbus_message_unref(signal_msg);
}

static DBusHandlerResult dbus_filter_audio_handler(DBusConnection *c, DBusMessage *s, void *userdata)
{
    DBusError error;
    char* arg_s = NULL;
    int arg_i = 0;

    pa_assert(userdata);

    if(dbus_message_get_type(s)!=DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_info("Audio handler received msg");
    dbus_error_init(&error);

    if (dbus_message_is_signal(s, SOUND_SERVER_INTERFACE_NAME, "TestSignalFromSS1")) {
        if (!dbus_message_get_args(s, NULL,
            DBUS_TYPE_STRING, &arg_s,
            DBUS_TYPE_INT32, &arg_i ,
            DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            _do_something1(arg_s, arg_i, userdata);
        }
    } else if (dbus_message_is_signal(s, SOUND_SERVER_INTERFACE_NAME, "TestSignalFromSS2")) {
        if (!dbus_message_get_args(s, NULL,
            DBUS_TYPE_STRING, &arg_s,
            DBUS_TYPE_INVALID)) {
            goto fail;
        } else{
            _do_something2(arg_s, userdata);
        }
    } else if (dbus_message_is_signal(s, AUDIO_CLIENT_INTERFACE_NAME, "TestSignalFromClient1")) {
        if (!dbus_message_get_args(s, NULL,
            DBUS_TYPE_STRING, &arg_s,
            DBUS_TYPE_INVALID)) {
            goto fail;
        } else{
            _do_something2(arg_s, userdata);
        }
    } else {
        pa_log_info("Unknown message, not handle it");
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    pa_log_debug("Dbus Message handled");

    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_HANDLED;

fail:
    pa_log_error("Fail to handle dbus signal");
    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_get_property(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    int prop_idx = 0;
    const char *interface_name, *property_name;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    if (pa_streq(dbus_message_get_signature(msg), "ss")) {
        pa_assert_se(dbus_message_get_args(msg, NULL,
                                           DBUS_TYPE_STRING, &interface_name,
                                           DBUS_TYPE_STRING, &property_name,
                                           DBUS_TYPE_INVALID));
        if (pa_streq(interface_name, INTERFACE_POLICY)) {
            for (prop_idx = 0; prop_idx < PROPERTY_MAX; prop_idx++) {
                if (pa_streq(property_name, property_handlers[prop_idx].property_name)) {
                    property_handlers[prop_idx].get_cb(conn, msg, userdata);
                    return DBUS_HANDLER_RESULT_HANDLED;
                }
            }
        }
        else{
            pa_log_warn("Not our interface, not handle it");
        }
    } else{
        pa_log_warn("Wrong Signature");
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected (ss)");
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_get_all_property(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    const char *interface_name;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    if (pa_streq(dbus_message_get_signature(msg), "s")) {
        pa_assert_se(dbus_message_get_args(msg, NULL,
                                           DBUS_TYPE_STRING, &interface_name,
                                           DBUS_TYPE_INVALID));
        if (pa_streq(interface_name, INTERFACE_POLICY)) {
            handle_get_all(conn, msg, userdata);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
        else{
            pa_log_warn("Not our interface, not handle it");
        }
    } else{
        pa_log_warn("Wrong Signature");
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected (ss)");
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_set_property(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    int prop_idx = 0;
    const char *interface_name, *property_name, *property_sig;
    DBusMessageIter msg_iter;
    DBusMessageIter variant_iter;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    if (pa_streq(dbus_message_get_signature(msg), "ssv")) {
        pa_assert_se(dbus_message_iter_init(msg, &msg_iter));
        dbus_message_iter_get_basic(&msg_iter, &interface_name);
        pa_assert_se(dbus_message_iter_next(&msg_iter));
        dbus_message_iter_get_basic(&msg_iter, &property_name);
        pa_assert_se(dbus_message_iter_next(&msg_iter));

        dbus_message_iter_recurse(&msg_iter, &variant_iter);

        property_sig = dbus_message_iter_get_signature(&variant_iter);

        if (pa_streq(interface_name, INTERFACE_POLICY)) {
            for (prop_idx = 0; prop_idx < PROPERTY_MAX; prop_idx++) {
                if (pa_streq(property_name, property_handlers[prop_idx].property_name)) {
                    if (pa_streq(property_handlers[prop_idx].type,property_sig)) {
                        property_handlers[prop_idx].set_cb(conn, msg, &variant_iter, userdata);
                        return DBUS_HANDLER_RESULT_HANDLED;
                    }
                    else{
                        pa_log_warn("Wrong Property Signature");
                        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected (ssv)");
                    }
                    break;
                }
            }
        }
        else{
            pa_log_warn("Not our interface, not handle it");
        }
    } else{
        pa_log_warn("Wrong Signature");
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected (ssv)");
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_policy_methods(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    int method_idx = 0;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    for (method_idx = 0; method_idx < METHOD_HANDLER_MAX; method_idx++) {
        if (dbus_message_is_method_call(msg, INTERFACE_POLICY, method_handlers[method_idx].method_name )) {
            if (pa_streq(dbus_message_get_signature(msg), method_arg_signatures[method_idx])) {
                method_handlers[method_idx].receive_cb(conn, msg, userdata);
                return DBUS_HANDLER_RESULT_HANDLED;
            }
            else{
                pa_log_warn("Wrong Argument Signature");
                pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected %s", method_arg_signatures[method_idx]);
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    const char *xml = POLICY_INTROSPECT_XML;
    DBusMessage *r = NULL;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    pa_assert_se(r = dbus_message_new_method_return(msg));
    pa_assert_se(dbus_message_append_args(r, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID));

    if (r) {
        pa_assert_se(dbus_connection_send((conn), r, NULL));
        dbus_message_unref(r);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult method_call_handler(DBusConnection *c, DBusMessage *m, void *userdata)
{
    struct userdata *u = userdata;
    const char *path, *interface, *member;

    pa_assert(c);
    pa_assert(m);
    pa_assert(u);

    path = dbus_message_get_path(m);
    interface = dbus_message_get_interface(m);
    member = dbus_message_get_member(m);

    pa_log_debug("dbus: path=%s, interface=%s, member=%s", path, interface, member);

    if (!pa_streq(path, OBJECT_PATH))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        return handle_introspect(c, m, u);
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "Get")){
        return handle_get_property(c, m, u);
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "Set")){
        return  handle_set_property(c, m, u);
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "GetAll")){
        return handle_get_all_property(c, m, u);
    } else{
        return handle_policy_methods(c, m, u);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void endpoint_init(struct userdata *u)
{
    static const DBusObjectPathVTable vtable_endpoint = {
        .message_function = method_call_handler,
    };

    pa_log_debug("Dbus endpoint init");

    if (u && u->dbus_conn) {
        if(!dbus_connection_register_object_path(pa_dbus_connection_get(u->dbus_conn), OBJECT_PATH, &vtable_endpoint, u))
            pa_log_error("Failed to register object path");
    } else{
        pa_log_error("Cannot get dbus connection to register object path");
    }
}

static void endpoint_done(struct userdata* u)
{
    pa_log_debug("Dbus endpoint done");
    if (u && u->dbus_conn) {
        if(!dbus_connection_unregister_object_path(pa_dbus_connection_get(u->dbus_conn), OBJECT_PATH))
            pa_log_error("Failed to unregister object path");
    } else{
        pa_log_error("Cannot get dbus connection to unregister object path");
    }
}

static int watch_signals(struct userdata* u)
{
    DBusError error;

    dbus_error_init(&error);

    pa_log_debug("Watch Dbus signals");

    if (u && u->dbus_conn) {

        if (!dbus_connection_add_filter(pa_dbus_connection_get(u->dbus_conn), dbus_filter_audio_handler, u, NULL)) {
            pa_log_error("Unable to add D-Bus filter : %s: %s", error.name, error.message);
            goto fail;
        }

        if (pa_dbus_add_matches(pa_dbus_connection_get(u->dbus_conn), &error, SOUND_SERVER_FILTER, AUDIO_CLIENT_FILTER, NULL) < 0) {
            pa_log_error("Unable to subscribe to signals: %s: %s", error.name, error.message);
            goto fail;
        }
        return 0;
    }

fail:
    dbus_error_free(&error);
    return -1;
}

static void unwatch_signals(struct userdata* u)
{
    pa_log_debug("Unwatch Dbus signals");

    if (u && u->dbus_conn) {
        pa_dbus_remove_matches(pa_dbus_connection_get(u->dbus_conn), SOUND_SERVER_FILTER, AUDIO_CLIENT_FILTER, NULL);
        dbus_connection_remove_filter(pa_dbus_connection_get(u->dbus_conn), dbus_filter_audio_handler, u);
    }
}



static void dbus_init(struct userdata* u)
{
    DBusError error;
    pa_dbus_connection *connection = NULL;

    pa_log_debug("Dbus init");
    dbus_error_init(&error);

    if (!(connection = pa_dbus_bus_get(u->core, DBUS_BUS_SYSTEM, &error)) || dbus_error_is_set(&error)) {
        if (connection) {
            pa_dbus_connection_unref(connection);
        }
        pa_log_error("Unable to contact D-Bus system bus: %s: %s", error.name, error.message);
        goto fail;
    } else{
        pa_log_debug("Got dbus connection");
    }

    u->dbus_conn = connection;

    if( watch_signals(u) < 0 )
        pa_log_error("dbus watch signals failed");
    else
        pa_log_debug("dbus ready to get signals");

    endpoint_init(u);

fail:
    dbus_error_free(&error);

}

static void dbus_deinit(struct userdata* u)
{
    pa_log_debug("Dbus deinit");
    if (u) {

        endpoint_done(u);
        unwatch_signals(u);

        if (u->dbus_conn){
            pa_dbus_connection_unref(u->dbus_conn);
            u->dbus_conn = NULL;
        }
    }
}
#endif

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
#ifdef HAVE_DBUS
    u->dbus_conn = NULL;
    u->test_property1 = 123;
#endif

    /* A little bit later than module-stream-restore */
    u->sink_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_STATE_CHANGED], PA_HOOK_NORMAL, (pa_hook_cb_t)sink_state_changed_hook_cb, u);

    u->subscription = pa_subscription_new(u->core, PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE, subscribe_cb, u);

    pa_log_debug("subscription done");

    u->bt_off_idx = -1;    /* initial bt off sink index */
    u->module_combined = NULL;
    u->module_mono_combined = NULL;

    u->protocol = pa_native_protocol_get(m->core);
    pa_native_protocol_install_ext(u->protocol, m, extension_cb);

    u->hal_manager = pa_hal_manager_get(u->core, (void *)u);

    u->communicator.comm = pa_communicator_get(u->core);
    if (u->communicator.comm) {
        u->communicator.comm_hook_select_proper_sink_or_source_slot = pa_hook_connect(pa_communicator_hook(u->communicator.comm,PA_COMMUNICATOR_HOOK_SELECT_INIT_SINK_OR_SOURCE),
                    PA_HOOK_EARLY, (pa_hook_cb_t) select_proper_sink_or_source_hook_cb, u);
        u->communicator.comm_hook_change_route_slot = pa_hook_connect(pa_communicator_hook(u->communicator.comm,PA_COMMUNICATOR_HOOK_CHANGE_ROUTE),
                    PA_HOOK_EARLY, (pa_hook_cb_t) route_change_hook_cb, u);
        u->communicator.comm_hook_update_route_options_slot = pa_hook_connect(pa_communicator_hook(u->communicator.comm,PA_COMMUNICATOR_HOOK_UPDATE_ROUTE_OPTIONS),
                    PA_HOOK_EARLY, (pa_hook_cb_t) route_options_update_hook_cb, u);
    }
    u->stream_manager = pa_stream_manager_init(u->core);

#ifdef DEVICE_MANAGER
    u->device_manager = pa_device_manager_init(u->core);
#endif

    __load_dump_config(u);

#ifdef HAVE_DBUS
    dbus_init(u);
#endif

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
#ifdef HAVE_DBUS
    dbus_deinit(u);
#endif
#ifdef DEVICE_MANAGER
    if (u->device_manager)
        pa_device_manager_done(u->device_manager);
#endif

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

    if (u->stream_manager)
        pa_stream_manager_done(u->stream_manager);

    if (u->communicator.comm) {
        if (u->communicator.comm_hook_change_route_slot)
            pa_hook_slot_free(u->communicator.comm_hook_change_route_slot);
        if (u->communicator.comm_hook_change_route_slot)
            pa_hook_slot_free(u->communicator.comm_hook_update_route_options_slot);
        pa_communicator_unref(u->communicator.comm);
    }

    if (u->hal_manager)
        pa_hal_manager_unref(u->hal_manager);

    pa_xfree(u);


    pa_log_info("policy module is unloaded\n");
}
