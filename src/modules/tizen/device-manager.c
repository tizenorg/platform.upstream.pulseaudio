
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <json.h>
#include <libudev.h>
#include <pulse/proplist.h>
#include <pulse/util.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/strbuf.h>
#include <pulsecore/modargs.h>
#include <pulsecore/namereg.h>

#include <vconf.h>
#include <vconf-keys.h>
#ifdef HAVE_DBUS
#include <pulsecore/dbus-shared.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/protocol-dbus.h>
#endif

#include "communicator.h"
#include "device-manager.h"

#define DEVICE_MAP_FILE                    "/etc/pulse/device-map.json"
#define DEVICE_PROFILE_MAX                  2
#define DEVICE_STR_MAX                      30
#define DEVICE_DIRECTION_MAX                3
#define DEVICE_PARAM_STRING_MAX             50
#define DEVICE_AVAIL_COND_NUM_MAX           2
#define DEVICE_AVAIL_COND_STR_MAX           6
#define DEVICE_FILE_PER_TYPE_MAX            4
#define DEVICE_FILE_STRING_MAX              4

#define DEVICE_TYPE_OBJECT                  "device-types"
#define DEVICE_FILE_OBJECT                  "device-files"
#define DEVICE_TYPE_PROP_DEVICE_TYPE        "device-type"
#define DEVICE_TYPE_PROP_BUILTIN            "builtin"
#define DEVICE_TYPE_PROP_DIRECTION          "direction"
#define DEVICE_TYPE_PROP_AVAIL_CONDITION    "avail-conditioin"
#define DEVICE_TYPE_PROP_PLAYBACK_DEVICES   "playback-devices"
#define DEVICE_TYPE_PROP_CAPTURE_DEVICES    "capture-devices"
#define DEVICE_TYPE_PROP_DEVICE_STRING      "device-string"
#define DEVICE_TYPE_PROP_DEFAULT_PARAMS     "default-params"
#define DEVICE_TYPE_PROP_ROLE               "role"

#define DEVICE_TYPE_STR_MAX                 20

#define DEVICE_DIRECTION_STR_NONE           "none"
#define DEVICE_DIRECTION_STR_OUT            "out"
#define DEVICE_DIRECTION_STR_IN             "in"
#define DEVICE_DIRECTION_STR_BOTH           "both"

#define DEVICE_AVAIL_CONDITION_STR_PULSE    "pulse"
#define DEVICE_AVAIL_CONDITION_STR_DBUS     "dbus"

/* Properties of sink/sources */
#define DEVICE_API_BLUEZ                    "bluez"
#define DEVICE_API_ALSA                     "alsa"
#define DEVICE_API_NULL                     "null"
#define DEVICE_BUS_USB                      "usb"
#define DEVICE_CLASS_SOUND                  "sound"
#define DEVICE_CLASS_MONITOR                "monitor"

/* Dbus defines */
#define DBUS_INTERFACE_DEVICE_MANAGER       "org.pulseaudio.DeviceManager"
#define DBUS_OBJECT_DEVICE_MANAGER          "/org/pulseaudio/DeviceManager"

#define DBUS_INTERFACE_DEVICED_SYSNOTI      "org.tizen.system.deviced.SysNoti"
#define DBUS_OBJECT_DEVICED_SYSNOTI         "/Org/Tizen/System/DeviceD/SysNoti"

#define DBUS_INTERFACE_SOUND_SERVER         "org.tizen.SoundServer1"
#define DBUS_OBJECT_SOUND_SERVER            "/org/tizen/SoundServer1"

#define DBUS_SERVICE_BLUEZ                  "org.bluez"
#define DBUS_INTERFACE_BLUEZ_HEADSET        "org.bluez.Headset"
#define DBUS_INTERFACE_BLUEZ_DEVICE         "org.bluez.Device1"
#define DBUS_OBJECT_BLUEZ                   "/org/bluez"

#define DBUS_INTERFACE_MIRRORING_SERVER     "org.tizen.scmirroring.server"
#define DBUS_OBJECT_MIRRORING_SERVER        "/org/tizen/scmirroring/server"

#define DBUS_SERVICE_HFP_AGENT "org.bluez.ag_agent"
#define DBUS_OBJECT_HFP_AGENT "/org/bluez/hfp_agent"
#define DBUS_INTERFACE_HFP_AGENT "Org.Hfp.App.Interface"

#define DEVICE_MANAGER_INTROSPECT_XML                                                       \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                                               \
    "<node>\n"                                                                              \
    " <interface name=\"" DBUS_INTERFACE_DEVICE_MANAGER "\">\n"                             \
    "  <method name=\"GetConnectedDeviceList\">\n"                                          \
    "   <arg name=\"mask_flags\" direction=\"in\" type=\"i\"/>\n"                           \
    "   <arg name=\"ConnectedDeviceList\" direction=\"out\" type=\"a(iiiis)\"/>\n"          \
    "  </method>\n"                                                                         \
    "  <method name='GetBTA2DPStatus'>"                                                     \
    "    <arg type='b' name='is_bt_on' direction='out'/>"                                   \
    "    <arg type='s' name='bt_name' direction='out'/>"                                    \
    "  </method>"                                                                           \
    "  <method name=\"LoadSink\">\n"                                                        \
    "   <arg name=\"device_type\" direction=\"in\" type=\"s\"/>\n"                          \
    "   <arg name=\"device_profile\" direction=\"in\" type=\"s\"/>\n"                       \
    "   <arg name=\"role\" direction=\"in\" type=\"s\"/>\n"                                 \
    "  </method>\n"                                                                         \
    "  <method name=\"TestStatusChange\">\n"                                                \
    "   <arg name=\"device_type\" direction=\"in\" type=\"s\"/>\n"                          \
    "   <arg name=\"device_profile\" direction=\"in\" type=\"s\"/>\n"                       \
    "   <arg name=\"status\" direction=\"in\" type=\"i\"/>\n"                               \
    "  </method>\n"                                                                         \
    "  <property name=\"PropertyTest1\" type=\"i\" access=\"readwrite\"/>\n"                \
    "  <property name=\"PropertyTest2\" type=\"s\" access=\"read\"/>\n"                     \
    "  <signal name=\"DeviceConnected\">\n"                                                 \
    "   <arg name=\"arg1\" type=\"i\"/>\n"                                                  \
    "  </signal>\n"                                                                         \
    "  <signal name=\"DeviceInfoChanged\">\n"                                               \
    "   <arg name=\"arg1\" type=\"s\"/>\n"                                                  \
    "  </signal>\n"                                                                         \
    " </interface>\n"                                                                       \
    " <interface name=\"" DBUS_INTERFACE_INTROSPECTABLE "\">\n"                             \
    "  <method name=\"Introspect\">\n"                                                      \
    "   <arg name=\"data\" type=\"s\" direction=\"out\"/>\n"                                \
    "  </method>\n"                                                                         \
    " </interface>\n"                                                                       \
    " <interface name=\"" DBUS_INTERFACE_PROPERTIES "\">\n"                                 \
    "  <method name=\"Get\">\n"                                                             \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"                       \
    "   <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"                        \
    "   <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"                               \
    "  </method>\n"                                                                         \
    "  <method name=\"Set\">\n"                                                             \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"                       \
    "   <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"                        \
    "   <arg name=\"value\" type=\"v\" direction=\"in\"/>\n"                                \
    "  </method>\n"                                                                         \
    "  <method name=\"GetAll\">\n"                                                          \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"                       \
    "   <arg name=\"props\" type=\"a{sv}\" direction=\"out\"/>\n"                           \
    "  </method>\n"                                                                         \
    " </interface>\n"                                                                       \
    "</node>\n"


#define FILTER_DEVICED_SYSNOTI                             \
    "type='signal',"                                       \
    " interface='" DBUS_INTERFACE_DEVICED_SYSNOTI "'"

#define FILTER_SOUND_SERVER                                \
    "type='signal',"                                       \
    " interface='" DBUS_INTERFACE_SOUND_SERVER "'"

#define FILTER_MIRRORING                                   \
    "type='signal',"                                       \
    " interface='" DBUS_INTERFACE_MIRRORING_SERVER "', member='miracast_wfd_source_status_changed'"

#define FILTER_BLUEZ                                       \
    "type='signal',"                                       \
    " interface='" DBUS_INTERFACE_BLUEZ_HEADSET "', member='PropertyChanged'"

static const char* const valid_alsa_device_modargs[] = {
    "name",
    "sink_name",
    "sink_properties",
    "source_name",
    "source_properties",
    "namereg_fail",
    "device",
    "device_id",
    "format",
    "rate",
    "alternate_rate",
    "channels",
    "channel_map",
    "fragments",
    "fragment_size",
    "mmap",
    "tsched",
    "tsched_buffer_size",
    "tsched_buffer_watermark",
    "ignore_dB",
    "control",
    "rewind_safeguard",
    "deferred_volume",
    "deferred_volume_safety_margin",
    "deferred_volume_extra_delay",
    "fixed_latency_range",
    "need_audio_pm",
    "start_threshold",
    NULL
};



/* A macro to ease iteration through all entries */
#define PA_HASHMAP_FOREACH_KEY(e, h, state, key) \
    for ((state) = NULL, (e) = pa_hashmap_iterate((h), &(state),(const void**)&(key)); (e); (e) = pa_hashmap_iterate((h), &(state), (const void**)&(key)))

#define PA_DEVICE(pulse_device, pdt) \
    pdt == PA_DEVICE_TYPE_SINK ? ((pa_sink *) pulse_device) : ((pa_source *) pulse_device)

#define PA_DEVICES(core, pdt) \
    pdt == PA_DEVICE_TYPE_SINK ? (((pa_core *) core)->sinks) : (((pa_core *) core)->sources)

#define MAKE_SINK(s) ((pa_sink*) (s))
#define MAKE_SOURCE(s) ((pa_source*) (s))


#define BT_CVSD_CODEC_ID 1 // narrow-band
#define BT_MSBC_CODEC_ID 2 // wide-band
/*
    Enums for represent values which is defined on other module.
    This is needed to identify values which are sent by dbus or vconf.
*/
typedef enum external_value_earjack_type {
    EARJACK_DISCONNECTED = 0,
    EARJACK_TYPE_SPK_ONLY = 1,
    EARJACK_TYPE_SPK_WITH_MIC = 3,
} external_value_earjack_t;

typedef enum external_value_bt_sco_type {
    BT_SCO_DISCONNECTED = 0,
    BT_SCO_CONNECTED = 1,
} external_value_bt_sco_t;

typedef enum external_value_forwarding_type {
    FORWARDING_DISCONNECTED = 0,
    FORWARDING_CONNECTED = 1,
} external_value_mirroring_t;

typedef enum external_value_hdmi_type {
    HDMI_AUDIO_DISCONNECTED = -1,
    HDMI_AUDIO_NOT_AVAILABLE = 0,
    HDMI_AUDIO_AVAILABLE = 1,
} external_value_hdmi_t;


/*
    Enums for represent device detected status (through dbus)
    When some device is detected, one of these values should be saved in device_status hashmap.
    device_detected_type_t is needed to distinguish detected device-types ( ex. earjack which can be out or both way)
    So If you just want to know whether detected or not, can device_detected_t as mask.
*/
typedef enum device_detected {
    DEVICE_NOT_DETECTED = 0x00,
    DEVICE_DETECTED = 0x01,
} device_detected_t;

typedef enum device_detected_type {
    DEVICE_DETECTED_BT_SCO = DEVICE_DETECTED,
    DEVICE_DETECTED_FORWARDING = DEVICE_DETECTED,
    DEVICE_DETECTED_HDMI = DEVICE_DETECTED,
    DEVICE_DETECTED_AUDIO_JACK_BOTH_DIREC = DEVICE_DETECTED | 0x2,
    DEVICE_DETECTED_AUDIO_JACK_OUT_DIREC = DEVICE_DETECTED | 0x4,
} device_detected_type_t;

typedef enum dm_device_class_type {
    DM_DEVICE_CLASS_NONE,
    DM_DEVICE_CLASS_ALSA,
    DM_DEVICE_CLASS_TIZEN,
    DM_DEVICE_CLASS_BT,
    DM_DEVICE_CLASS_NULL,
    DM_DEVICE_CLASS_MAX,
} dm_device_class_t;


typedef enum {
    DEVICE_IO_DIRECTION_IN_FLAG      = 0x0001,  /**< Flag for input devices */
    DEVICE_IO_DIRECTION_OUT_FLAG     = 0x0002,  /**< Flag for output devices */
    DEVICE_IO_DIRECTION_BOTH_FLAG    = 0x0004,  /**< Flag for input/output devices (both directions are available) */
    DEVICE_TYPE_INTERNAL_FLAG        = 0x0010,  /**< Flag for built-in devices */
    DEVICE_TYPE_EXTERNAL_FLAG        = 0x0020,  /**< Flag for external devices */
    DEVICE_STATE_DEACTIVATED_FLAG    = 0x1000,  /**< Flag for deactivated devices */
    DEVICE_STATE_ACTIVATED_FLAG      = 0x2000,  /**< Flag for activated devices */
    DEVICE_ALL_FLAG                  = 0xFFFF,  /**< Flag for all devices */
} device_flag_t;

typedef enum {
    DEVICE_IO_DIRECTION_FLAGS        = 0x000F,  /**< Flag for io direction */
    DEVICE_TYPE_FLAGS                = 0x00F0,  /**< Flag for device type */
    DEVICE_STATE_FLAGS               = 0xF000,  /**< Flag for device state */
} device_flags_type_t;


/************* structures for represent device items can be connected/disconnected */
/*
    Before beginning, There are two structure(dm_device, dm_device_profile)
    for represent device by following reasons.
    When bt-a2dp and bt-sco are on physically same device ,it is of course same device.
    So those physically same device item are represended by dm_device,
    and each profile is represented by dm_device_profile.
*/


/*
    Structure to represent physicall device.
    This is profile known data-structure, which means it can have multiple profiles ( ex. bt-a2dp and sco)
*/
struct dm_device {
    uint32_t id;
    char *type;
    char *name;
    const char *identifier;

    /* Indicate currently activated profile */
    uint32_t active_profile;
    /* include profile_items(dm_device_profile) , currently has only one item except bt case*/
    pa_idxset *profiles;

    pa_device_manager *dm;
};

/*
    Structure to represent each device profile (subtype).
    Even if both-way device(earjack, sco..) , one device_profile.
*/
typedef struct dm_device_profile {
    char *profile;
    dm_device_direction_t direction;
    dm_device_state_t state;

    /* Can get proper sink/source in hashmaps with key(=device_role) */
    pa_hashmap *playback_devices;
    pa_hashmap *capture_devices;

    /* device belongs to */
    dm_device *device_item;
} dm_device_profile;

/*
    Structure to save parsed information about device-file.
*/
struct device_file_map {
    /* { key:device_string -> value:device_file_prop } */
    pa_idxset *playback;
    pa_idxset *capture;
};

struct pa_device_manager {
    pa_core *core;
    pa_hook_slot *sink_put_hook_slot, *sink_unlink_hook_slot;
    pa_hook_slot *source_put_hook_slot, *source_unlink_hook_slot;
    pa_communicator *comm;

    /*
       Idxset for save parsed information about device-type.
       { device_type_info }
    */
    pa_idxset *type_infos;
    /* For save Parsed information about device-file */
    struct device_file_map *file_map;

    /* device list */
    pa_idxset *device_list;
    /*
       Hashmap for save statuses got through dbus.
       { key:device_type -> value:(audio_detected_type_t or device_detected_t) }
    */
    pa_idxset *device_status;
    pa_dbus_connection *dbus_conn;
};

/***************** structures for static information get from json *********/

/*
    Structure for informations related to some device-file(ex. 0:0)
*/
struct device_file_info {
    /*
        String for identify target device.
        ex) alsa:0,0  or null ..
    */
    const char *device_string;
    /*
        For save roles which are supported on device file, and parameters.
        { key:device_role -> value:parameters for load sink/source }
        ex) "normal"->"rate=44100 tsched=0", "uhqa"->"rate=192000 mmap=1"
    */
    pa_hashmap *roles;
    /*
        For save device-types related to device file.
        { key:device_type-> value:pulse_device_prop }
    */
    pa_hashmap *device_types;
};

/* structure for represent device-types(ex. builtin-speaker) properties*/
struct device_type_info {
    const char *type;
    const char *profile;
    pa_bool_t builtin;
    /*
        Possible directions of this device.
        ex) speaker is always out, but earjack can be both or out.
    */
    dm_device_direction_t direction[DEVICE_DIRECTION_MAX];
    /*
        Conditions for make device available.
        ex) Speaker be available, only if proper pcm-device exists.
        but audio-jack is available, if pcm-device exists and got detected status.
    */
    char avail_condition[DEVICE_AVAIL_COND_NUM_MAX][DEVICE_AVAIL_COND_STR_MAX];
    int num;
    /*
        For save supported roles and related device-file.
        { key:role -> value:device_string ]
    */
    pa_hashmap *playback_devices;
    pa_hashmap *capture_devices;
};

struct device_status_info {
    const char *type;
    const char *profile;
    /* Identify devices among same device-types (for multi-device), currently not works*/
    const char *identifier;
    device_detected_t detected;
    device_detected_type_t detected_type;
};

struct pulse_device_prop {
    /* roles related to (device_type + device_file)*/
    pa_idxset *roles;
    /* For save that this devie_type is activated or not on sink/source */
    int status;
};
/******************************************************************************/

int device_id_max_g = 1;

#ifdef HAVE_DBUS

/*** Defines for method handle ***/
/* method handlers */
static void handle_get_connected_device_list(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_bt_a2dp_status(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_load_sink(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_test_device_status_change(DBusConnection *conn, DBusMessage *msg, void *userdata);

static void notify_device_connection_changed(dm_device *device_item, pa_bool_t connected, pa_device_manager *dm);
static void notify_device_info_changed(dm_device *device_item, dm_device_changed_info_t changed_type, pa_device_manager *dm);

static int method_call_bt_sco(DBusConnection *conn, pa_bool_t onoff);
static int method_call_bt_sco_get_property(DBusConnection *conn, pa_bool_t *is_wide_band, pa_bool_t *nrec);
static int method_call_bt_get_name(DBusConnection *conn, const char *device_path, char **name);

enum method_handler_index {
    METHOD_HANDLER_GET_CONNECTED_DEVICE_LIST,
    METHOD_HANDLER_GET_BT_A2DP_STATUS,
    METHOD_HANDLER_LOAD_SINK,
    METHOD_HANDLER_STATUS_TEST,
    METHOD_HANDLER_MAX
};

static pa_dbus_method_handler method_handlers[METHOD_HANDLER_MAX] = {
    [METHOD_HANDLER_GET_CONNECTED_DEVICE_LIST] = {
        .method_name = "GetConnectedDeviceList",
        .receive_cb = handle_get_connected_device_list },
    [METHOD_HANDLER_GET_BT_A2DP_STATUS] = {
        .method_name = "GetBTA2DPStatus",
        .receive_cb = handle_get_bt_a2dp_status },
    [METHOD_HANDLER_LOAD_SINK] = {
        .method_name = "LoadSink",
        .receive_cb = handle_load_sink},
    [METHOD_HANDLER_STATUS_TEST] = {
        .method_name = "TestStatusChange",
        .receive_cb = handle_test_device_status_change},
};

/*** Defines for signal send ***/

enum signal_index {
    SIGNAL_DEVICE_CONNECTED,
    SIGNAL_DEVICE_INFO_CHANGED,
    SIGNAL_MAX
};

#endif

static pa_bool_t device_type_is_valid(const char *device_type) {
    if (!device_type)
        return FALSE;
    else if (pa_streq(device_type, DEVICE_TYPE_SPEAKER))
        return TRUE;
    else if (pa_streq(device_type, DEVICE_TYPE_RECEIVER))
        return TRUE;
    else if (pa_streq(device_type, DEVICE_TYPE_MIC))
        return TRUE;
    else if (pa_streq(device_type, DEVICE_TYPE_AUDIO_JACK))
        return TRUE;
    else if (pa_streq(device_type, DEVICE_TYPE_BT))
        return TRUE;
    else if (pa_streq(device_type, DEVICE_TYPE_HDMI))
        return TRUE;
    else if (pa_streq(device_type, DEVICE_TYPE_FORWARDING))
        return TRUE;
    else if (pa_streq(device_type, DEVICE_TYPE_USB_AUDIO))
        return TRUE;
    else
        return FALSE;
}

static const char* device_direction_to_string(dm_device_direction_t direction) {
    if (direction <= DM_DEVICE_DIRECTION_NONE || direction > DM_DEVICE_DIRECTION_BOTH) {
        return NULL;
    }

    if (direction == DM_DEVICE_DIRECTION_NONE)
        return DEVICE_DIRECTION_STR_NONE;
    else if (direction == DM_DEVICE_DIRECTION_OUT)
        return DEVICE_DIRECTION_STR_OUT;
    else if (direction == DM_DEVICE_DIRECTION_IN)
        return DEVICE_DIRECTION_STR_IN;
    else if (direction == DM_DEVICE_DIRECTION_BOTH)
        return DEVICE_DIRECTION_STR_BOTH;
    else
        return NULL;
}

static pa_bool_t device_role_is_valid(const char *device_role) {
    if (!device_role)
        return FALSE;
    else if (pa_streq(device_role, DEVICE_ROLE_NORMAL))
        return TRUE;
    else if (pa_streq(device_role, DEVICE_ROLE_VOIP))
        return TRUE;
    else if (pa_streq(device_role, DEVICE_ROLE_LOW_LATENCY))
        return TRUE;
    else if (pa_streq(device_role, DEVICE_ROLE_HIGH_LATENCY))
        return TRUE;
    else if (pa_streq(device_role, DEVICE_ROLE_UHQA))
        return TRUE;
    else
        return FALSE;
}

static dm_device_direction_t device_direction_to_int(const char *device_direction) {
    if (!device_direction) {
        return -1;
    }

    if (pa_streq(device_direction, DEVICE_DIRECTION_STR_NONE)) {
        return DM_DEVICE_DIRECTION_NONE;
    } else if (pa_streq(device_direction, DEVICE_DIRECTION_STR_OUT)) {
        return DM_DEVICE_DIRECTION_OUT;
    } else if (pa_streq(device_direction, DEVICE_DIRECTION_STR_IN)) {
        return DM_DEVICE_DIRECTION_IN;
    } else if (pa_streq(device_direction, DEVICE_DIRECTION_STR_BOTH)) {
        return DM_DEVICE_DIRECTION_BOTH;
    } else {
        return -1;
    }
}

static void type_info_free_func(struct device_type_info *type_info) {
    if (!type_info)
        return;

    if (type_info->playback_devices)
        pa_hashmap_free(type_info->playback_devices);
    if (type_info->capture_devices)
        pa_hashmap_free(type_info->capture_devices);

}

static void file_info_free_func(struct device_file_info *file_info) {
    if (!file_info)
        return;

    if (file_info->roles)
        pa_hashmap_free(file_info->roles);
}

static void profile_item_free_func(dm_device_profile *profile_item) {
    if (!profile_item)
        return;

    if (profile_item->profile) {
        pa_xfree(profile_item->profile);
    }

    if (profile_item->playback_devices) {
        pa_hashmap_free(profile_item->playback_devices);
    }
    if (profile_item->capture_devices) {
        pa_hashmap_free(profile_item->capture_devices);
    }

    profile_item->device_item = NULL;

    pa_xfree(profile_item);
}

static void device_item_free_func(dm_device *device_item) {
    if (!device_item)
        return;

    if (device_item->type)
        pa_xfree(device_item->type);
    if (device_item->name)
        pa_xfree(device_item->name);
    if (device_item->profiles)
        pa_idxset_free(device_item->profiles, (pa_free_cb_t)profile_item_free_func);

    pa_xfree(device_item);
}

static pa_proplist* pulse_device_get_proplist(void *pulse_device, pa_device_type_t pdt) {
    if (pdt == PA_DEVICE_TYPE_SINK)
        return MAKE_SINK(pulse_device)->proplist;
    else
        return MAKE_SOURCE(pulse_device)->proplist;
}

static pa_core* pulse_device_get_core(void *pulse_device, pa_device_type_t pdt) {
    if (pdt == PA_DEVICE_TYPE_SINK)
        return MAKE_SINK(pulse_device)->core;
    else
        return MAKE_SOURCE(pulse_device)->core;
}

static char* pulse_device_get_name(void *pulse_device, pa_device_type_t pdt) {
    if (pdt == PA_DEVICE_TYPE_SINK)
        return MAKE_SINK(pulse_device)->name;
    else
        return MAKE_SOURCE(pulse_device)->name;
}

static pa_idxset* pulse_core_get_device_list(pa_core *core, pa_device_type_t pdt) {
    if (pdt == PA_DEVICE_TYPE_SINK)
        return core->sinks;
    else
        return core->sources;
}

static pa_bool_t pulse_device_is_alsa(pa_proplist *prop) {
    const char *api_name = NULL;

    if (!prop) {
        return FALSE;
    }

    if ((api_name = pa_proplist_gets(prop, PA_PROP_DEVICE_API))) {
        if (pa_streq (api_name, DEVICE_API_ALSA)) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }
}


static pa_bool_t pulse_device_is_bluez(pa_proplist *prop) {
    const char *api_name = NULL;

    if (!prop) {
        return FALSE;
    }

    if ((api_name = pa_proplist_gets(prop, PA_PROP_DEVICE_API))) {
        if (pa_streq (api_name, DEVICE_API_BLUEZ)) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }
}

static pa_bool_t pulse_device_is_tizenaudio(void *pulse_device, pa_device_type_t pdt) {
    pa_sink *sink;

    if (!pulse_device) {
        return FALSE;
    }

    if (pdt == PA_DEVICE_TYPE_SOURCE) {
        return FALSE;
    }

    sink = (pa_sink *) pulse_device;
    return pa_streq(sink->module->name, "module-tizenaudio-sink");
}

static pa_bool_t pulse_device_is_usb(pa_proplist *prop) {
    const char *bus_name = NULL;

    if ((bus_name = pa_proplist_gets(prop, PA_PROP_DEVICE_BUS))) {
        if (pa_streq (bus_name, DEVICE_BUS_USB)) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        pa_log_debug("This device doesn't have property '%s'", PA_PROP_DEVICE_BUS);
        return FALSE;
    }
}

static pa_bool_t pulse_device_is_null(void *pulse_device, pa_device_type_t pdt) {
    pa_sink *sink;
    pa_source *source;

    if (!pulse_device)
        return FALSE;

    if (pdt == PA_DEVICE_TYPE_SINK) {
        sink = (pa_sink *) pulse_device;
        return pa_streq(sink->module->name, "module-null-sink");
    } else {
        source = (pa_source *) pulse_device;

        return pa_streq(source->module->name, "module-null-source");
    }
}

static const char* device_class_to_string(dm_device_class_t device_class) {
    if (device_class == DM_DEVICE_CLASS_ALSA) {
        return "alsa";
    } else if (device_class == DM_DEVICE_CLASS_TIZEN) {
        return "tizen";
    } else if (device_class == DM_DEVICE_CLASS_BT) {
        return "bt";
    } else if (device_class == DM_DEVICE_CLASS_NULL) {
        return "null";
    } else if (device_class == DM_DEVICE_CLASS_NONE) {
        return "none";
    } else {
        return NULL;
    }
}

static dm_device_class_t device_string_get_class(const char *device_string) {
    if (!device_string) {
        return DM_DEVICE_CLASS_NONE;
    }

    if (device_string == strstr(device_string, "alsa")) {
        return DM_DEVICE_CLASS_ALSA;
    } else if (device_string == strstr(device_string, "null")) {
        return DM_DEVICE_CLASS_NULL;
    } else if (device_string == strstr(device_string, "tizen")) {
        return DM_DEVICE_CLASS_TIZEN;
    } else {
        return DM_DEVICE_CLASS_NONE;
    }
}

static const char* device_string_get_value(const char *device_string) {
    int len;
    const char *end_p, *value_p;

    if (!device_string) {
        return NULL;
    }

    len = strlen(device_string);
    end_p = device_string + len -1;

    if (!(value_p = strchr(device_string, ':'))) {
        return NULL;
    }
    if (value_p < end_p) {
        return value_p + 1;
    } else {
        return NULL;
    }
}

static dm_device_class_t pulse_device_get_class(void *pulse_device, pa_device_type_t pdt) {
    pa_sink *sink = NULL;
    pa_source *source = NULL;

    if (!pulse_device) {
        pa_log_error("pulse_device null");
        return DM_DEVICE_CLASS_NONE;
    }

    if (pdt == PA_DEVICE_TYPE_SINK)
        sink = (pa_sink *) pulse_device;
    else
        source = (pa_source *) pulse_device;

    if (pulse_device_is_null(pulse_device, pdt)) {
        return DM_DEVICE_CLASS_NULL;
    } else if (pulse_device_is_alsa(pdt == PA_DEVICE_TYPE_SINK ? sink->proplist : source->proplist)) {
        return DM_DEVICE_CLASS_ALSA;
    } else if (pulse_device_is_tizenaudio(pulse_device, pdt)) {
        return DM_DEVICE_CLASS_TIZEN;
    } else if (pulse_device_is_bluez(pdt == PA_DEVICE_TYPE_SINK ? sink->proplist : source->proplist)) {
        return DM_DEVICE_CLASS_BT;
    } else {
        return DM_DEVICE_CLASS_NONE;
    }
}

static const char* device_class_get_module_name(dm_device_class_t device_class, pa_device_type_t pdt) {
    if (device_class == DM_DEVICE_CLASS_NONE) {
        return NULL;
    } else if (device_class == DM_DEVICE_CLASS_ALSA) {
        return pdt == PA_DEVICE_TYPE_SINK ? "module-alsa-sink" : "module-alsa-source";
    } else if (device_class == DM_DEVICE_CLASS_TIZEN) {
        return pdt == PA_DEVICE_TYPE_SINK ? "module-tizenaudio-sink" : NULL;
    } else if (device_class == DM_DEVICE_CLASS_BT) {
        return pdt == PA_DEVICE_TYPE_SINK ? "module-bluez5-device" : NULL;
    } else if (device_class == DM_DEVICE_CLASS_NULL) {
        return pdt == PA_DEVICE_TYPE_SINK ? "module-null-sink" : "module-null-source";
    } else {
        return NULL;
    }
}

static int compare_device_profile(const char *device_profile1, const char *device_profile2) {
    if (!device_profile1 && !device_profile2) {
        return 0;
    } else if (!device_profile1 || !device_profile2) {
        return 1;
    } else if (pa_streq(device_profile1, device_profile2)) {
        return 0;
    } else {
        return 1;
    }
}

static int compare_device_type(const char *device_type1, const char *device_profile1, const char *device_type2, const char *device_profile2) {
    if (!device_type1 || !device_type2) {
        return -1;
    }
    if (pa_streq(device_type1, device_type2)) {
        return compare_device_profile(device_profile1, device_profile2);
    }
    return 1;
}

static struct device_type_info* _device_manager_get_type_info(pa_idxset *type_infos, const char *device_type, const char *device_profile) {
    struct device_type_info *type_info;
    uint32_t type_idx;

    PA_IDXSET_FOREACH(type_info, type_infos, type_idx) {
        if (!compare_device_type(type_info->type, type_info->profile, device_type, device_profile)) {
            return type_info;
        }
    }

    return NULL;
}

static struct device_status_info* _device_manager_get_status_info(pa_idxset *status_infos, const char *device_type, const char *device_profile, const char *identifier) {
    struct device_status_info *status_info;
    uint32_t status_idx;

    PA_IDXSET_FOREACH(status_info, status_infos, status_idx) {
        if (!compare_device_type(status_info->type, status_info->profile, device_type, device_profile)) {
            if (!status_info->identifier && !identifier) {
                return status_info;
            } else if (!status_info->identifier || !identifier) {
                continue;
            } else if (pa_streq(status_info->identifier, identifier)) {
                return status_info;
            } else {
                continue;
            }
        }
    }

    return NULL;
}

static struct device_file_info* _device_manager_get_file_info(pa_idxset *file_infos, const char *device_string) {
    struct device_file_info *file_info;
    uint32_t file_idx;
    if (!file_infos)
        return NULL;

    PA_IDXSET_FOREACH(file_info, file_infos, file_idx) {
        if (file_info->device_string) {
            if (pa_streq(file_info->device_string, device_string)) {
                return file_info;
            }
        }
    }

    return NULL;
}

static dm_device* _device_manager_get_device(pa_idxset *device_list, const char *device_type) {
    dm_device *device_item;
    uint32_t device_idx;

    if (!device_list || !device_type)
        return NULL;

    PA_IDXSET_FOREACH(device_item, device_list, device_idx) {
        if (pa_streq(device_item->type, device_type)) {
            return device_item;
        }
    }

    return NULL;
}

static dm_device* _device_manager_get_device_with_id(pa_idxset *device_list, uint32_t id) {
    dm_device *device_item;
    uint32_t idx;

    pa_assert(device_list);

    PA_IDXSET_FOREACH(device_item, device_list, idx) {
        if (device_item->id == id) {
            return device_item;
        }
    }
    return NULL;
}

static void dump_playback_device_list(pa_hashmap *playback_devices) {
    pa_sink *sink = NULL;
    void *state = NULL;
    const char *role;

    if (!playback_devices) {
        return ;
    }

    pa_log_debug("    playback device list");
    if (pa_hashmap_size(playback_devices) == 0) {
        pa_log_debug("        empty");
        return;
    }
    PA_HASHMAP_FOREACH_KEY(sink, playback_devices, state, role) {
        pa_log_debug("        %-13s -> %s", role, sink->name);
    }
}

static void dump_capture_device_list(pa_hashmap *capture_devices) {
    pa_source *source= NULL;
    void *state = NULL;
    const char *role;

    if (!capture_devices) {
        return ;
    }

    pa_log_debug("    capture device list");
    if (pa_hashmap_size(capture_devices) == 0) {
        pa_log_debug("        empty");
        return;
    }
    PA_HASHMAP_FOREACH_KEY(source, capture_devices, state, role) {
        pa_log_debug("        %-13s -> %s", role, source->name);
    }
}

static void dump_device_profile_info(dm_device_profile *profile_item) {
    if (!profile_item)
        return;

    pa_log_debug("    profile   : %s", profile_item->profile);
    pa_log_debug("    direction : %s", device_direction_to_string(profile_item->direction));
    pa_log_debug("    activated : %s", profile_item->state == DM_DEVICE_STATE_ACTIVATED ? "activated" : "not activated");
    dump_playback_device_list(profile_item->playback_devices);
    dump_capture_device_list(profile_item->capture_devices);
}

static void dump_device_info(dm_device *device_item) {
    dm_device_profile *profile_item = NULL;
    uint32_t device_idx = 0;

    if (!device_item)
        return;
    if (!device_item->profiles) {
        pa_log_warn("empty device item");
        return;
    }

    pa_log_debug("  id             : %u", device_item->id);
    pa_log_debug("  type           : %s", device_item->type);
    pa_log_debug("  name           : %s", device_item->name);
    pa_log_debug("  active-profile : %u", device_item->active_profile);
    PA_IDXSET_FOREACH(profile_item, device_item->profiles, device_idx) {
        pa_log_debug("  (Profile #%u)", device_idx);
        dump_device_profile_info(profile_item);
    }
}

static void dump_device_list(pa_device_manager *dm) {
    dm_device *device_item = NULL;
    uint32_t device_idx = 0;

    if (!dm || !dm->device_list) {
        return;
    }

    pa_log_debug("====== Device List Dump ======");
    PA_IDXSET_FOREACH(device_item, dm->device_list, device_idx) {
        pa_log_debug("[ Device #%u ]", device_item->id);
        dump_device_info(device_item);
    }
    pa_log_debug("===================================");
}
static pa_bool_t pulse_device_class_is_sound(pa_proplist *prop) {
    const char *device_class = NULL;

    if ((device_class = pa_proplist_gets(prop, PA_PROP_DEVICE_CLASS))) {
        if (device_class && pa_streq (device_class, DEVICE_CLASS_SOUND)) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }
}

static pa_bool_t pulse_device_class_is_monitor(pa_proplist *prop) {
    const char *device_class = NULL;

    if (!prop) {
        return FALSE;
    }

    if ((device_class = pa_proplist_gets(prop, PA_PROP_DEVICE_CLASS))) {
        if (device_class && pa_streq (device_class, DEVICE_CLASS_MONITOR)) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        return FALSE;
    }
}
static void* pulse_device_get_opposite_sibling_device(void *pulse_device, pa_device_type_t pdt) {
    const char *sysfs_path, *sysfs_path_tmp;
    uint32_t device_idx;
    void *pulse_device_tmp;
    pa_core *core;

    pa_assert(pulse_device);

    if (!(sysfs_path = pa_proplist_gets(pulse_device_get_proplist(pulse_device, pdt), "sysfs.path"))) {
        pa_log_warn("No sysfs.path for '%s'", pulse_device_get_name(pulse_device, pdt));
        return NULL;
    }

    core = pulse_device_get_core(pulse_device, pdt);

    PA_IDXSET_FOREACH(pulse_device_tmp, pulse_core_get_device_list(core, !pdt), device_idx) {
        if (!pulse_device_class_is_sound(pulse_device_get_proplist(pulse_device_tmp, !pdt)))
            continue;
        sysfs_path_tmp = pa_proplist_gets(pulse_device_get_proplist(pulse_device_tmp, !pdt), "sysfs.path");
        if (sysfs_path_tmp && pa_streq(sysfs_path_tmp, sysfs_path)) {
            return pulse_device_tmp;
        }
    }

    return NULL;
}

static int pulse_device_get_alsa_device_string(pa_proplist *prop, char **device_string) {
    const char *device_string_prop = NULL;
    char *device_string_tmp;

    if (!prop || !device_string) {
        pa_log_error("Invalid Parameter");
        return -1;
    }

    if (!(device_string_prop = pa_proplist_gets(prop, "device.string"))) {
        pa_log_error("failed to get property 'device.string'");
        return -1;
    }
    if (!(device_string_tmp = strchr(device_string_prop, ':'))) {
        pa_log_error("failed to parse device string");
        return -1;
    }

    if (((device_string_tmp + 1) == '\0')) {
        pa_log_error("no device string value");
        return -1;
    }

    *device_string = device_string_tmp + 1;

    return 0;
}

static const char* build_params_to_load_device(const char *device_string, const char *params, dm_device_class_t device_class) {
    pa_strbuf *args_buf;
    static char args[DEVICE_PARAM_STRING_MAX] = {0,};

    if (!device_string) {
        pa_log_error("device string null");
        return NULL;
    }

    if (device_class == DM_DEVICE_CLASS_NULL) {
        return params;
    } else if (device_class == DM_DEVICE_CLASS_ALSA) {
        const char *alsa_device_name;
        if (!(alsa_device_name = device_string_get_value(device_string))) {
            pa_log_error("Invalid device string for alsa-device, '%s'", device_string);
            return NULL;
        }
        args_buf = pa_strbuf_new();
        pa_strbuf_printf(args_buf, "device=hw:%s ", alsa_device_name);
        if (params) {
            pa_strbuf_printf(args_buf, "%s", params);
        }
        strncpy(args, pa_strbuf_tostring_free(args_buf), DEVICE_PARAM_STRING_MAX);
    } else {
        return params;
    }

    return (const char*) args;
}

static const char* pulse_device_get_device_string_removed_argument(void *pulse_device, pa_device_type_t pdt) {
    static char removed_param[DEVICE_PARAM_STRING_MAX] = {0,};
    char *device_string_p = NULL;
    char *next_p = NULL;
    const char *params_p, *params;
    char *end_p = NULL;
    int len = 0, prev_len = 0;
    pa_sink *sink;
    pa_source *source;

    if (pdt == PA_DEVICE_TYPE_SINK)
        sink = (pa_sink *) pulse_device;
    else
        source = (pa_source *) pulse_device;

    params = pdt == PA_DEVICE_TYPE_SINK ? sink->module->argument : source->module->argument;
    params_p = params;

    if (!params) {
        return NULL;
    }
    if (!(device_string_p = strstr(params, "device="))) {
        return params;
    }

    next_p = device_string_p;
    while (!isblank(*next_p)) {
        next_p++;
    }
    while (isblank(*next_p)) {
        next_p++;
    }

    strncpy(removed_param, next_p, DEVICE_PARAM_STRING_MAX);

    if (device_string_p > params_p) {
        prev_len = device_string_p - params_p;
        len = strlen(removed_param);
        end_p = removed_param + len;
        *end_p = ' ';
        end_p++;
        strncpy(end_p, params_p, prev_len);
    }

    return removed_param;
}


static int compare_device_params(const char *params1, const char *params2) {
    const char *key = NULL;
    const char *value1, *value2;
    pa_modargs *modargs1, *modargs2;
    void *state = NULL;
    int ret = 0;

    if (!params1 && !params2)
        return 0;
    if (!params1 || !params2)
        return -1;

    modargs1 = pa_modargs_new(params1, valid_alsa_device_modargs);
    modargs2 = pa_modargs_new(params2, valid_alsa_device_modargs);

    if (!modargs1 || !modargs2) {
        ret = 1;
        goto end;
    }

    for (state = NULL, key = pa_modargs_iterate(modargs1, &state); key; key = pa_modargs_iterate(modargs1, &state)) {
        value1 = pa_modargs_get_value(modargs1, key, NULL);
        value2 = pa_modargs_get_value(modargs2, key, NULL);
        if (!value1 || !value2 || !pa_streq(value1, value2)) {
            ret = 1;
            goto end;
        }
    }

    for (state = NULL, key = pa_modargs_iterate(modargs2, &state); key; key = pa_modargs_iterate(modargs2, &state)) {
        value1 = pa_modargs_get_value(modargs1, key, NULL);
        value2 = pa_modargs_get_value(modargs2, key, NULL);
        if (!value1 || !value2 || !pa_streq(value1, value2)) {
            ret = 1;
            goto end;
        }
    }

end:

    if (modargs1)
        pa_modargs_free(modargs1);
    if (modargs2)
        pa_modargs_free(modargs2);


    return ret;
}

static int compare_device_params_with_module_args(void *pulse_device, pa_device_type_t pdt, const char *params) {
    const char *removed_module_args;
    const char *module_args;
    pa_sink *sink;
    pa_source *source;

    if (pdt == PA_DEVICE_TYPE_SINK) {
        sink = (pa_sink *) pulse_device;
        module_args = sink->module->argument;
    } else {
        source = (pa_source *) pulse_device;
        module_args = source->module->argument;
    }

    if (!params && !module_args)
        return 0;
    if (!params || !module_args)
        return -1;

    removed_module_args = pulse_device_get_device_string_removed_argument(pulse_device, pdt);
    return compare_device_params(params, removed_module_args);
}

static const char* pulse_device_get_device_string(void *pulse_device, pa_device_type_t pdt) {
    dm_device_class_t device_class;
    static char device_string[DEVICE_STR_MAX] = {0,};
    char *device_string_val = NULL;
    pa_sink *sink;
    pa_source *source;

    if (!pulse_device) {
        pa_log_error("pulse_device null");
        return NULL;
    }

    device_class = pulse_device_get_class(pulse_device, pdt);

    if (pdt == PA_DEVICE_TYPE_SINK)
        sink = (pa_sink *) pulse_device;
    else
        source = (pa_source *) pulse_device;

    if (device_class == DM_DEVICE_CLASS_ALSA) {
        if (pulse_device_get_alsa_device_string(pdt == PA_DEVICE_TYPE_SINK ? sink->proplist : source->proplist, &device_string_val) < 0)
            return NULL;
        snprintf(device_string, DEVICE_STR_MAX, "alsa:%s", device_string_val);
        return device_string;
    } else if (device_class == DM_DEVICE_CLASS_NULL) {
        return "null";
    } else if (device_class == DM_DEVICE_CLASS_TIZEN) {
        return "tizen";
    } else if (device_class == DM_DEVICE_CLASS_BT) {
        return "bt";
    } else {
        return device_string;
    }
}

/*  pulse_device is sink or source */
static pa_bool_t pulse_device_same_device_string(void *pulse_device, pa_device_type_t pdt, const char *device_string) {
    const char *pulse_device_string;

    if (!pulse_device || !device_string) {
        return FALSE;
    }

    if (!(pulse_device_string = pulse_device_get_device_string(pulse_device, pdt))) {
        return FALSE;
    }

    return pa_streq(pulse_device_string, device_string);
}

static dm_device* _device_item_set_active_profile(dm_device *device_item, const char *device_profile) {
    dm_device_profile *profile_item = NULL;
    uint32_t idx, active_profile_idx = PA_INVALID_INDEX, prev_active_profile = PA_INVALID_INDEX;

    if (!device_item || !device_item->profiles ) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    prev_active_profile = device_item->active_profile;
    PA_IDXSET_FOREACH(profile_item,  device_item->profiles, idx) {
        if (!compare_device_profile(profile_item->profile, device_profile)) {
            active_profile_idx = idx;
        }
    }

    if (active_profile_idx != PA_INVALID_INDEX) {
        device_item->active_profile = active_profile_idx;
    } else {
        return NULL;
    }

    if (prev_active_profile != device_item->active_profile) {
        pa_log_debug("%s's active profile : %u", device_item->name, device_item->active_profile);
        notify_device_info_changed(device_item, DM_DEVICE_CHANGED_INFO_SUBTYPE, device_item->dm);
    }

    return device_item;
}

static int get_profile_priority(const char *device_profile) {
    if (!device_profile) {
        return 0;
    } else if (pa_streq(device_profile,  DEVICE_PROFILE_BT_SCO)) {
        return 1;
    } else if (pa_streq(device_profile,  DEVICE_PROFILE_BT_A2DP)) {
        return 2;
    } else {
        return -1;
    }
}

static int compare_profile_priority(const char *device_profile1,  const char *device_profile2) {
    int priority1, priority2;

    priority1 = get_profile_priority(device_profile1);
    priority2 = get_profile_priority(device_profile2);

    if (priority1 > priority2) {
        return 1;
    } else if (priority1 == priority2) {
        return 0;
    } else {
        return -1;
    }
}

static dm_device* _device_item_set_active_profile_auto(dm_device *device_item) {
    dm_device_profile *profile_item = NULL, *prev_profile_item = NULL;
    uint32_t idx, prev_active_profile;
    unsigned int device_size;

    if (!device_item || !device_item->profiles ) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    prev_active_profile = device_item->active_profile;

    device_size = pa_idxset_size(device_item->profiles);
    if (device_size == 1) {
        pa_idxset_first(device_item->profiles,  &idx);
        device_item->active_profile = idx;
    } else if (device_size == 0) {
        device_item->active_profile = PA_INVALID_INDEX;
        return device_item;
    } else {
        PA_IDXSET_FOREACH(profile_item, device_item->profiles, idx) {
            if (prev_profile_item) {
                if (compare_profile_priority(profile_item->profile, prev_profile_item->profile) > 0) {
                    device_item->active_profile = idx;
                }
            }
            prev_profile_item = profile_item;
        }
    }

    if (prev_active_profile != device_item->active_profile) {
        pa_log_debug("%s's active profile : %u", device_item->name, device_item->active_profile);
        notify_device_info_changed(device_item, DM_DEVICE_CHANGED_INFO_SUBTYPE, device_item->dm);
    }

    return device_item;
}

static dm_device* _device_item_add_profile(dm_device *device_item, dm_device_profile *profile_item, uint32_t *idx, pa_device_manager *dm) {
    uint32_t profile_idx;

    pa_assert(device_item);
    pa_assert(device_item->profiles);
    pa_assert(profile_item);
    pa_assert(dm);

    pa_idxset_put(device_item->profiles, profile_item, &profile_idx);
    _device_item_set_active_profile_auto(device_item);
    profile_item->device_item = device_item;

    return device_item;
}


static int _device_list_add_device(pa_idxset *device_list, dm_device *device_item, pa_device_manager *dm) {
    pa_assert(device_list);
    pa_assert(device_item);

    if (pa_idxset_put(device_list, device_item, NULL) < 0)
        return -1;

    pa_log_debug("Notify Device connected");

    return 0;
}


static int _device_list_remove_device(pa_idxset *device_list, dm_device *device_item, pa_device_manager *dm) {
    pa_assert(device_list);
    pa_assert(device_item);

    if (!pa_idxset_remove_by_data(device_list, device_item, NULL))
        return -1;

    return 0;
}



static dm_device* create_device_item(const char *device_type, const char *name, dm_device_profile *profile_item, pa_device_manager *dm) {
    dm_device *device_item = NULL;

    pa_assert(device_type);
    pa_assert(profile_item);

    pa_log_debug("Create device item for %s", device_type);

    device_item = (dm_device *)pa_xmalloc(sizeof(dm_device));
    device_item->id = device_id_max_g++;
    device_item->type = strdup(device_type);
    device_item->active_profile = PA_INVALID_INDEX;
    device_item->profiles = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    device_item->dm = dm;

    if (name) {
        device_item->name = strdup(name);
    } else {
        device_item->name = strdup(device_type);
    }

    _device_item_add_profile(device_item, profile_item, NULL, dm);
    _device_list_add_device(dm->device_list, device_item, dm);
    notify_device_connection_changed(device_item, TRUE, dm);

    return device_item;
}

static void destroy_device_item(dm_device *device_item, pa_device_manager *dm) {
    if (!device_item) {
        return;
    }

    pa_log_debug("Destroy device item which of type is %s", device_item->type);

    _device_list_remove_device(dm->device_list, device_item, dm);
    notify_device_connection_changed(device_item, FALSE, dm);

    device_item_free_func(device_item);
}

static unsigned _device_item_get_size(dm_device *device_item) {
    unsigned int profile_num;
    pa_assert(device_item);

    profile_num = pa_idxset_size(device_item->profiles);
    return profile_num;
}

static dm_device* _device_item_remove_profile(dm_device *device_item, dm_device_profile *profile_item, pa_device_manager *dm) {
    unsigned int profile_num;

    pa_assert(device_item);
    pa_assert(device_item->profiles);
    pa_assert(profile_item);
    pa_assert(dm);

    profile_num = pa_idxset_size(device_item->profiles);

    if (profile_num == 0) {
        pa_log_error("Already Empty device_item");
        return NULL;
    }

    pa_idxset_remove_by_data(device_item->profiles, profile_item, NULL);
    _device_item_set_active_profile_auto(device_item);

    return device_item;
}

static dm_device_profile* create_device_profile(const char *device_profile, dm_device_direction_t direction, pa_hashmap *playback, pa_hashmap *capture) {
    dm_device_profile *profile_item = NULL;

    pa_assert(direction >= DM_DEVICE_DIRECTION_IN && direction <= DM_DEVICE_DIRECTION_BOTH);

    pa_log_debug("Create device profile for %s, direction:%s", device_profile, device_direction_to_string(direction));

    if (!(profile_item = (dm_device_profile *)pa_xmalloc(sizeof(dm_device_profile)))) {
        pa_log_error("Cannot alloc for device item");
        return NULL;
    }
    profile_item->profile = device_profile ? strdup(device_profile) : NULL;
    profile_item->direction = direction;
    profile_item->state = DM_DEVICE_STATE_DEACTIVATED;
    profile_item->playback_devices = playback;
    profile_item->capture_devices = capture;

    return profile_item;
}

static dm_device* destroy_device_profile(dm_device_profile *profile_item, pa_device_manager *dm) {
    dm_device *device_item;

    pa_assert(profile_item);
    pa_assert(profile_item->device_item);

    device_item = profile_item->device_item;

    pa_log_debug("Destroy device profile item which of profile is %s", profile_item->profile);

    if (_device_item_get_size(device_item) == 1) {
        destroy_device_item(device_item, dm);
        return NULL;
    } else {
        _device_item_remove_profile(device_item, profile_item, dm);
        profile_item_free_func(profile_item);
        return device_item;
    }
}

static void _device_profile_update_direction(dm_device_profile *profile_item) {
    int prev_direction;
    pa_bool_t playback_exist = FALSE, capture_exist = FALSE;

    if (!profile_item)
        return;

    prev_direction = profile_item->direction;

    if (profile_item->playback_devices) {
        if (pa_hashmap_size(profile_item->playback_devices) > 0) {
            playback_exist = TRUE;
        }
    }
    if (profile_item->capture_devices) {
        if (pa_hashmap_size(profile_item->capture_devices) > 0) {
            capture_exist = TRUE;
        }
    }

    if (playback_exist && capture_exist) {
        profile_item->direction = DM_DEVICE_DIRECTION_BOTH;
    } else if (playback_exist) {
        profile_item->direction = DM_DEVICE_DIRECTION_OUT;
    } else if (capture_exist) {
        profile_item->direction = DM_DEVICE_DIRECTION_IN;
    } else {
        profile_item->direction = DM_DEVICE_DIRECTION_NONE;
    }

    pa_log_debug("direction updated '%s'->'%s'", device_direction_to_string(prev_direction), device_direction_to_string(profile_item->direction));
}

static dm_device_profile* _device_profile_add_pulse_device(dm_device_profile *profile_item, const char *role, void *pulse_device, pa_device_type_t pdt) {
    if (!profile_item || !pulse_device || !device_role_is_valid(role)) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    if (pdt == PA_DEVICE_TYPE_SINK) {
        if (!(profile_item->playback_devices))
            profile_item->playback_devices = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        if(pa_hashmap_put(profile_item->playback_devices, (void *)role, pulse_device) < 0)
            return NULL;
    } else {
        if (!(profile_item->capture_devices))
            profile_item->capture_devices = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        if(pa_hashmap_put(profile_item->capture_devices, (void *)role, pulse_device) < 0)
            return NULL;
    }

    return profile_item;
}

static dm_device_profile* _device_profile_add_sink(dm_device_profile *profile_item, const char *role, pa_sink *sink) {
    if (!profile_item || !sink || !device_role_is_valid(role)) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    if (!(profile_item->playback_devices))
        profile_item->playback_devices = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    if(pa_hashmap_put(profile_item->playback_devices, (void *)role, sink) < 0)
        return NULL;

    return profile_item;
}

static dm_device_profile* _device_profile_remove_sink(dm_device_profile *profile_item, const char *role) {
    if (!profile_item || !device_role_is_valid(role)) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }
    pa_hashmap_remove(profile_item->playback_devices, role);
    return profile_item;
}

static dm_device_profile* _device_profile_add_source(dm_device_profile *profile_item, const char *role, pa_source *source) {
    if (!profile_item || !source || !device_role_is_valid(role)) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    if (!(profile_item->capture_devices))
        profile_item->capture_devices = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    if(pa_hashmap_put(profile_item->capture_devices, (void *)role, source) < 0)
        return NULL;

    return profile_item;
}


static dm_device_profile* _device_profile_remove_source(dm_device_profile *profile_item, const char *role) {
    if (!profile_item || !(profile_item->capture_devices) || !device_role_is_valid(role)) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }
    pa_hashmap_remove(profile_item->capture_devices, role);
    return profile_item;
}

void _device_profile_set_state(dm_device_profile *profile_item, dm_device_state_t state) {
    pa_assert(profile_item);

    if (profile_item->state != state) {
        profile_item->state = state;
        notify_device_info_changed(profile_item->device_item, DM_DEVICE_CHANGED_INFO_STATE, profile_item->device_item->dm);
    }
}


static int device_type_get_direction(pa_device_manager *dm, const char *device_type, const char *device_profile, const char *identifier) {
    struct device_type_info *type_info = NULL;
    struct device_status_info *status_info;
    dm_device_direction_t direction = 0, d_num = 0, d_idx = 0, correct_d_idx = 0;

    if (!dm || !device_type) {
        pa_log_error("Invalid Parameter");
        return -1;
    }


    if (!(type_info = _device_manager_get_type_info(dm->type_infos, device_type, device_profile))) {
        pa_log_error("No type map for %s", device_type);
        return -1;
    }

    for (d_idx = 0; d_idx < DEVICE_DIRECTION_MAX; d_idx++) {
        if (type_info->direction[d_idx] != DM_DEVICE_DIRECTION_NONE) {
            correct_d_idx = d_idx;
            d_num++;
        }
    }

    if (d_num == 1) {
        direction = type_info->direction[correct_d_idx];
    } else {
        /* Actually, only 'audio-jack' should come here */
        if (pa_streq(device_type, DEVICE_TYPE_AUDIO_JACK)) {
            status_info = _device_manager_get_status_info(dm->device_status, type_info->type, type_info->profile, identifier);
            if (status_info->detected_type == DEVICE_DETECTED_AUDIO_JACK_BOTH_DIREC) {
                direction = DM_DEVICE_DIRECTION_BOTH;
            } else if (status_info->detected_type == DEVICE_DETECTED_AUDIO_JACK_OUT_DIREC) {
                direction = DM_DEVICE_DIRECTION_OUT;
            } else {
                pa_log_debug("Cannot get audio jack device direction");
                return -1;
            }
        } else {
            pa_log_error("Weird case, '%s' is not expected to have multiple direction", device_type);
            return -1;
        }
    }

    return direction;
}

static int pulse_device_get_device_type(void *pulse_device, pa_device_type_t pdt, dm_device_class_t device_class, const char **device_type, const char **device_profile, const char **device_name) {
    pa_proplist *prop;
    pa_sink *sink;
    pa_source *source;

    pa_assert(pulse_device);
    pa_assert(device_type);
    pa_assert(device_profile);

    if (pdt == PA_DEVICE_TYPE_SINK) {
        sink = (pa_sink *) pulse_device;
        prop = sink->proplist;
    } else {
        source = (pa_source*) pulse_device;
        prop = source->proplist;
    }

    if (device_class == DM_DEVICE_CLASS_ALSA) {
        if (pulse_device_is_usb(prop)) {
            *device_type = DEVICE_TYPE_USB_AUDIO;
            *device_profile = NULL;
            *device_name = pa_proplist_gets(prop, PA_PROP_DEVICE_SERIAL);
        } else {
            pa_log_warn("This is alsa device, but not usb. really unknown device");
            return -1;
        }
    } else if (device_class == DM_DEVICE_CLASS_BT) {
        *device_type = DEVICE_TYPE_BT;
        *device_profile = DEVICE_PROFILE_BT_A2DP;
        *device_name = pa_proplist_gets(prop, "bluez.alias");
    } else {
        pa_log_warn("Invalid device type, neither alsa nor bluez");
        return -1;
    }

    return 0;
}

static dm_device_profile* _device_item_get_profile(dm_device *device_item, const char *profile) {
    dm_device_profile *profile_item;
    uint32_t profile_idx;

    if (!device_item || !device_item->profiles)
        return NULL;

    PA_IDXSET_FOREACH(profile_item, device_item->profiles, profile_idx) {
        if (!compare_device_profile(profile_item->profile, profile)) {
            return profile_item;
        }
    }
    return NULL;
}

static dm_device_profile* _device_item_get_active_profile(dm_device *device_item) {
    dm_device_profile *profile_item;

    if (!device_item || !device_item->profiles)
        return NULL;

    if ((profile_item = pa_idxset_get_by_index(device_item->profiles, device_item->active_profile)))
        return profile_item;


    return NULL;
}

static pa_sink* _device_manager_set_default_sink(pa_device_manager *dm,  const char *device_type,  const char *device_profile,  const char *role) {
    dm_device *device_item;
    dm_device_profile *profile_item;
    pa_sink *sink;

    if (!device_type || !role) {
        pa_log_warn("Argument for set_default_sink invalid");
        return NULL;
    }

    if (!(device_item = _device_manager_get_device(dm->device_list, device_type))) {
        pa_log_warn("cannot get device item for %s", device_type);
        return NULL;
    }
    if (!(profile_item = _device_item_get_profile(device_item, device_profile))) {
        pa_log_warn("cannot get profile item for %s", device_profile);
        return NULL;
    }

    if (!(sink = pa_hashmap_get(profile_item->playback_devices, role))) {
        pa_log_warn("cannot get sink for %s", role);
        return NULL;
    }

    sink = pa_namereg_set_default_sink(dm->core, sink);
    return sink;
}

static pa_source* _device_manager_set_default_source(pa_device_manager *dm,  const char *device_type,  const char *device_profile,  const char *role) {
    dm_device *device_item;
    dm_device_profile *profile_item;
    pa_source *source;

    if (!device_type || !role) {
        pa_log_warn("Argument for set_default_source invalid");
        return NULL;
    }

    if (!(device_item = _device_manager_get_device(dm->device_list, device_type))) {
        pa_log_warn("cannot get device item for %s", device_type);
        return NULL;
    }
    if (!(profile_item = _device_item_get_profile(device_item, device_profile))) {
        pa_log_warn("cannot get profile item for %s", device_profile);
        return NULL;
    }

    if (!(source= pa_hashmap_get(profile_item->capture_devices, role))) {
        pa_log_warn("cannot get source for %s", role);
        return NULL;
    }

    source = pa_namereg_set_default_source(dm->core, source);
    return source;
}

static dm_device_profile* handle_not_predefined_device_profile(void *pulse_device, pa_device_type_t pdt, const char *device_profile) {
    dm_device_profile *profile_item = NULL;
    dm_device_direction_t direc;

    pa_log_debug("Create device profile item %s", device_profile);
    if (pdt == PA_DEVICE_TYPE_SINK)
        direc = DM_DEVICE_DIRECTION_OUT;
    else
        direc = DM_DEVICE_DIRECTION_IN;

    if(!(profile_item = create_device_profile(device_profile, direc, NULL, NULL))) {
        pa_log_error("create_device_profile failed");
        goto failed;
    }
    if (pdt == PA_DEVICE_TYPE_SINK) {
        if (!(_device_profile_add_sink(profile_item, DEVICE_ROLE_NORMAL, pulse_device))) {
            pa_log_error("failed to add sink");
            goto failed;
        }
    } else {
        if (!(_device_profile_add_source(profile_item, DEVICE_ROLE_NORMAL, pulse_device))) {
            pa_log_error("failed to add source");
            goto failed;
        }
    }

    return profile_item;
failed :
    if (profile_item)
        pa_xfree(profile_item);
    return NULL;
}


static dm_device* handle_not_predefined_device(pa_device_manager *dm, void *pulse_device, pa_device_type_t pdt, dm_device_class_t device_class) {
    dm_device_profile *profile_item = NULL;
    const char *device_type, *device_profile, *device_name;
    dm_device *device_item = NULL;
    pa_source *sibling_source, *source;
    pa_sink *sibling_sink, *sink;

    pa_assert(dm);
    pa_assert(pulse_device);

    pa_log_debug("handle_not_predefined_device");

    if (pulse_device_get_device_type(pulse_device, pdt, device_class, &device_type, &device_profile, &device_name) < 0) {
        pa_log_warn("Cannot get device type of this device");
        return NULL;
    }

    /*
       Find opposite direction sink/sources on same device.
       If Found, add sink or source to same device_item.
    */
    if (pa_streq(device_type, DEVICE_TYPE_USB_AUDIO)) {
        if (pdt == PA_DEVICE_TYPE_SINK) {
            sink = (pa_sink *) pulse_device;
            if ((sibling_source = pulse_device_get_opposite_sibling_device(sink, PA_DEVICE_TYPE_SINK))) {
                if (sibling_source->device_item) {
                    device_item = (dm_device *) sibling_source->device_item;
                    profile_item = _device_item_get_profile(device_item, NULL);
                    if (!(_device_profile_add_sink(profile_item, DEVICE_ROLE_NORMAL, sink))) {
                        pa_log_error("failed to add sink beside sibling source");
                        goto failed;
                    }
                    _device_profile_update_direction(profile_item);
                    notify_device_info_changed(device_item, DM_DEVICE_CHANGED_INFO_IO_DIRECTION, dm);
                    goto end;
                }
            }
        } else {
            source = (pa_source*) pulse_device;
            if ((sibling_sink = pulse_device_get_opposite_sibling_device(source, PA_DEVICE_TYPE_SOURCE))) {
                if (sibling_sink->device_item) {
                    device_item = (dm_device *) sibling_sink->device_item;
                    profile_item = _device_item_get_profile(device_item, NULL);
                    if (!(_device_profile_add_source(profile_item, DEVICE_ROLE_NORMAL, source))) {
                        pa_log_error("failed to add source beside sibling sink");
                        goto failed;
                    }
                    _device_profile_update_direction(profile_item);
                    notify_device_info_changed(device_item, DM_DEVICE_CHANGED_INFO_IO_DIRECTION, dm);
                    goto end;
                }
            }
        }
    }

    if(!(profile_item = handle_not_predefined_device_profile(pulse_device, pdt, device_profile))) {
        pa_log_error("failed to handle unknown device profile");
        goto failed;
    }
    _device_profile_update_direction(profile_item);

    if (device_class == DM_DEVICE_CLASS_BT) {
        if((device_item = _device_manager_get_device(dm->device_list, DEVICE_TYPE_BT))) {
            pa_log_debug("found bt device");
            _device_item_add_profile(device_item, profile_item, NULL, dm);
            goto end;
        }
    }

    if (!(device_item = create_device_item(device_type, device_name, profile_item, dm))) {
        pa_log_error("failed to create device item for not predefined device");
        goto failed;
    }

end:

    if (pdt == PA_DEVICE_TYPE_SINK) {
        sink = (pa_sink *) pulse_device;
        sink->device_item = device_item;
    } else {
        source = (pa_source *) pulse_device;
        source->device_item = device_item;
    }

    return device_item;

failed:
    pa_log_error("Failed to handle external device");
    if (profile_item)
        pa_xfree(profile_item);
    if (pdt == PA_DEVICE_TYPE_SINK) {
        sink = (pa_sink *) pulse_device;
        sink->device_item = device_item;
    } else {
        source = (pa_source *) pulse_device;
        source->device_item = device_item;
    }


    return NULL;
}

static pa_bool_t pulse_device_loaded_with_param(pa_core *core, pa_device_type_t pdt, const char *device_string, const char *params) {
    pa_sink *sink;
    pa_source *source;
    uint32_t device_idx;

    pa_assert(core);
    pa_assert(device_string);

    if (pdt == PA_DEVICE_TYPE_SINK) {
        PA_IDXSET_FOREACH(sink, core->sinks, device_idx) {
            if (pulse_device_class_is_monitor(sink->proplist))
                continue;
            if (pa_streq(device_string, pulse_device_get_device_string(sink, pdt))) {
                if (!compare_device_params_with_module_args(sink, pdt, params)) {
                    return TRUE;
                }
            }
        }
    } else {
        PA_IDXSET_FOREACH(source, core->sources, device_idx) {
            if (pulse_device_class_is_monitor(source->proplist))
                continue;
            if (pa_streq(device_string, pulse_device_get_device_string(source, pdt))) {
                if (!compare_device_params_with_module_args(source, pdt, params)) {
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

static int device_type_get_pulse_devices(struct device_type_info *type_info, pa_hashmap **playback, pa_hashmap **capture, pa_device_manager *dm) {
    struct device_file_info *file_info;
    const char *device_string, *params, *role;
    uint32_t device_idx;
    pa_sink *sink;
    pa_source *source;
    void *state;

    pa_assert(type_info);
    pa_assert(playback);
    pa_assert(capture);
    pa_assert(dm);

    if (type_info->playback_devices) {
        *playback = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        PA_HASHMAP_FOREACH_KEY(device_string, type_info->playback_devices, state, role) {
            if (!(file_info = _device_manager_get_file_info(dm->file_map->playback, device_string))) {
                pa_log_error("No playback file map for '%s'", device_string);
                goto failed;
            }

            if (!(params = pa_hashmap_get(file_info->roles, role))) {
                pa_log_error("No params for '%s:%s'", device_string, role);
                goto failed;
            }

            PA_IDXSET_FOREACH(sink, dm->core->sinks, device_idx) {
                if (pulse_device_class_is_monitor(sink->proplist))
                    continue;
                if (pulse_device_same_device_string(sink, PA_DEVICE_TYPE_SINK, device_string)) {
                    if (!compare_device_params_with_module_args(sink, PA_DEVICE_TYPE_SINK, params)) {
                        pa_hashmap_put(*playback, (void *)role, sink);
                        pa_log_debug("role:%s <- sink:%s", role, sink->name);
                        break;
                    }
                }
            }
        }
    }


    if (type_info->capture_devices) {
        *capture = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        PA_HASHMAP_FOREACH_KEY(device_string, type_info->capture_devices, state, role) {
            if (!(file_info = _device_manager_get_file_info(dm->file_map->capture, device_string))) {
                pa_log_error("No capture file map for '%s'", device_string);
                goto failed;
            }
            if (!(params = pa_hashmap_get(file_info->roles, role))) {
                pa_log_error("No params for '%s:%s'", device_string, role);
                goto failed;
            }
            PA_IDXSET_FOREACH(source, dm->core->sources, device_idx) {
                if (pulse_device_class_is_monitor(source->proplist))
                    continue;
                if (pulse_device_same_device_string(source, PA_DEVICE_TYPE_SOURCE, device_string)) {
                    if (!compare_device_params_with_module_args(source, PA_DEVICE_TYPE_SOURCE, params)) {
                        pa_hashmap_put(*capture, (void *)role, source);
                        pa_log_debug("role:%s <- source:%s", role, source->name);
                        break;
                    }
                }
            }
        }
    }

    return 0;

failed:
    if (!(*playback))
        pa_hashmap_free(*playback);
    if (!(*capture))
        pa_hashmap_free(*capture);
    *playback = NULL;
    *capture = NULL;

    return -1;
}

static const char* device_type_info_get_device_string(struct device_type_info *type_info, const char *role, pa_device_type_t pdt) {
    pa_assert(type_info);
    pa_assert(role);

    if (pdt == PA_DEVICE_TYPE_SINK) {
        if (type_info->playback_devices)
            return pa_hashmap_get(type_info->playback_devices, role);
        else
            return NULL;
    } else {
        if (type_info->capture_devices)
            return pa_hashmap_get(type_info->capture_devices, role);
        else
            return NULL;
    }

}

static const char* device_file_info_get_role_with_params(struct device_file_info *file_info, const char *params) {
    char *params_tmp, *role;
    void *state;

    PA_HASHMAP_FOREACH_KEY(params_tmp, file_info->roles, state, role) {
        if (!compare_device_params(params_tmp, params)) {
            return role;
        }
    }
    return NULL;
}

static dm_device* handle_device_type_available(struct device_type_info *type_info, const char *name, pa_device_manager *dm) {
    dm_device_profile *profile_item = NULL;
    dm_device *device_item = NULL;
    pa_bool_t made_newly = FALSE;
    dm_device_direction_t direction;
    pa_hashmap *playback = NULL, *capture = NULL;

    pa_assert(dm);
    pa_assert(dm->type_infos);

    pa_log_debug("handle_device_type_available, type:%s, profile:%s, name:%s", type_info->type, type_info->profile, name);



    /* Directions of some types are not statically defined, ex) earjack */
    if ((direction = device_type_get_direction(dm, type_info->type, type_info->profile, NULL)) < 0) {
        pa_log_error("Failed to get direction of %s.%s", type_info->type, type_info->profile);
        return NULL;
    }
    pa_log_debug("Direction of %s.%s is %s", type_info->type, type_info->profile, device_direction_to_string(direction));

    /* Get Sink/Sources for device_type, profile */
    if (device_type_get_pulse_devices(type_info, &playback, &capture, dm) < 0) {
        pa_log_error("Failed to get sink/sources related to %s.%s", type_info->type, type_info->profile);
        return NULL;
    }

    /* Check whether Sink/Sources for direction of type are loaded */
    if ((((direction & DM_DEVICE_DIRECTION_IN) && !capture) || ((direction & DM_DEVICE_DIRECTION_OUT) && !playback))) {
        pa_log_debug("Sink/Sources for %s.%s are not fully loaded yet", type_info->type, type_info->profile);
        goto failed;
    }

    profile_item = create_device_profile(type_info->profile,  direction,  playback,  capture);

    if (!(device_item = _device_manager_get_device(dm->device_list, type_info->type))) {
        pa_log_debug("No device item for %s, Create", type_info->type);
        device_item = create_device_item(type_info->type, name, profile_item, dm);
        made_newly = TRUE;
    } else {
        _device_item_add_profile(device_item, profile_item, NULL, dm);
    }

    return device_item;

failed :
    if (playback)
        pa_hashmap_free(playback);
    if (capture)
        pa_hashmap_free(capture);
    if (device_item && made_newly)
        pa_xfree(device_item);
    if (profile_item)
        pa_xfree(profile_item);
    return NULL;
}

/* FIXME to get identifier of physical device */
static const char* pulse_device_get_identifier(void *pulse_device, pa_device_type_t pdt, dm_device_class_t device_class) {
/*
    const char *sysfs_path;

    if (device_class == TIZEN_AUDIO_DEVICE_CLASS_ALSA) {
        if (!(sysfs_path = pa_proplist_gets(sink->proplist, "sysfs.path"))) {
            pa_log_warn("No sysfs.path for sink '%s'", sink->name);
            return NULL;
        } else {
            return sysfs_path;
        }
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_BT) {
    }
    */
    return NULL;
}

static void handle_predefined_device_loaded(void *pulse_device, pa_device_type_t pdt, dm_device_class_t device_class, const char *device_string, const char *role,  pa_device_manager *dm) {
    const char *identifier, *device_string_tmp;
    struct device_type_info *type_info;
    struct device_status_info *status_info;
    uint32_t type_idx;
    dm_device *device_item;
    dm_device_profile *profile_item;

    pa_assert(pulse_device);
    pa_assert(dm);
    pa_assert(dm->file_map);
    pa_assert(device_string);
    pa_assert(role);

    pa_log_debug("Predefined device loaded, Type:%s, Class:%d, device_string:%s, role:%s", pdt == PA_DEVICE_TYPE_SINK ? "sink" : "source", device_class, device_string, role);

    identifier = pulse_device_get_identifier(pulse_device, pdt, device_class);
    PA_IDXSET_FOREACH(type_info, dm->type_infos, type_idx) {
        /* foreach matching types (which has device_string-role) */
        if ((device_string_tmp = device_type_info_get_device_string(type_info, role, pdt)) && pa_streq(device_string_tmp, device_string)) {
            /*
               Check device_item is already exists.
               If already exists, add loaded sink or source to that.
            */
            if((device_item = _device_manager_get_device(dm->device_list, type_info->type))) {
                if((profile_item = _device_item_get_profile(device_item, type_info->profile))) {
                    pa_log_debug("device_item for %s.%s already exists", type_info->type, type_info->profile);
                    if (!_device_profile_add_pulse_device(profile_item,  role,  pulse_device,  pdt))
                        pa_log_error("add pulse device to profile_item failed");
                    continue;
                }
            }

            /* Get status_info for device_type, profile*/
            if (!(status_info = _device_manager_get_status_info(dm->device_status, type_info->type, type_info->profile, identifier))) {
                pa_log_error("%s.%s.%s doesn't have status_info", type_info->type, type_info->profile, identifier);
                continue;
            }
            /* Only if device_type is on detected state*/
            if (status_info->detected == DEVICE_DETECTED) {
                pa_log_debug("%s.%s type is detected status", type_info->type, type_info->profile);

                handle_device_type_available(type_info, NULL, dm);
            } else {
                pa_log_debug("  This type is not detected status");
            }
        }
    }
}

static pa_bool_t _device_type_direction_available(struct device_type_info *type_info, dm_device_direction_t direction) {
    int direc_idx;

    for (direc_idx = 0; direc_idx < DEVICE_DIRECTION_MAX; direc_idx++) {
        if (type_info->direction[direc_idx] == direction) {
            return TRUE;
        }
    }

    return FALSE;
}

static void handle_sink_unloaded(pa_sink *sink, pa_device_manager *dm) {
    dm_device_profile *profile_item= NULL;
    struct device_type_info *type_info;
    dm_device *device_item;
    uint32_t device_idx = 0, profile_idx;
    pa_sink *sink_iter = NULL;
    void *state = NULL;
    const char *role;

    if (!sink || !dm) {
        pa_log_error("Invalid Paramter");
        return;
    }
    pa_assert(sink);
    pa_assert(dm);
    pa_assert(dm->device_list);

    pa_log_debug("Sink unloaded, Let's remove associated device_profiles with this sink");

    PA_IDXSET_FOREACH(device_item, dm->device_list, device_idx) {
        PA_IDXSET_FOREACH(profile_item, device_item->profiles, profile_idx) {
            if (profile_item->playback_devices) {
                PA_HASHMAP_FOREACH_KEY(sink_iter, profile_item->playback_devices, state, role) {
                    if (sink_iter == sink) {
                        pa_log_debug("device '%s' have this sink", device_item->name);
                        _device_profile_remove_sink(profile_item, role);
                    }
                }
                if (!pa_hashmap_size(profile_item->playback_devices)) {
                    pa_hashmap_free(profile_item->playback_devices);
                    profile_item->playback_devices = NULL;

                    if (profile_item->direction == DM_DEVICE_DIRECTION_BOTH) {
                        type_info = _device_manager_get_type_info(dm->type_infos, profile_item->device_item->type, profile_item->profile);
                        if (_device_type_direction_available(type_info, DM_DEVICE_DIRECTION_IN)) {
                            profile_item->direction = DM_DEVICE_DIRECTION_IN;
                        } else {
                            if (!destroy_device_profile(profile_item, dm))
                                break;
                        }
                    } else {
                        if (!destroy_device_profile(profile_item, dm))
                            break;
                    }
                } else {
                    _device_profile_update_direction(profile_item);
                }
            }
        }
    }
}

static void handle_source_unloaded(pa_source *source, pa_device_manager *dm) {
    dm_device_profile *profile_item= NULL;
    struct device_type_info *type_info;
    dm_device *device_item;
    uint32_t device_idx = 0, profile_idx;
    pa_source *source_iter = NULL;
    void *state = NULL;
    const char *role;

    if (!source|| !dm) {
        pa_log_error("Invalid Paramter");
        return;
    }
    pa_assert(source);
    pa_assert(dm);
    pa_assert(dm->device_list);

    pa_log_debug("Source unloaded, Let's remove associated device_profiles with this source");

    PA_IDXSET_FOREACH(device_item, dm->device_list, device_idx) {
        PA_IDXSET_FOREACH(profile_item, device_item->profiles, profile_idx) {
            if (profile_item->capture_devices) {
                PA_HASHMAP_FOREACH_KEY(source_iter, profile_item->capture_devices, state, role) {
                    if (source_iter == source) {
                        pa_log_debug("device '%s' have this source", device_item->name);
                        _device_profile_remove_source(profile_item, role);
                    }
                }

                if (!pa_hashmap_size(profile_item->capture_devices)) {
                    pa_hashmap_free(profile_item->capture_devices);
                    profile_item->capture_devices= NULL;

                    if (profile_item->direction == DM_DEVICE_DIRECTION_BOTH) {
                        type_info = _device_manager_get_type_info(dm->type_infos, profile_item->device_item->type, profile_item->profile);
                        if (_device_type_direction_available(type_info, DM_DEVICE_DIRECTION_OUT)) {
                            profile_item->direction = DM_DEVICE_DIRECTION_OUT;
                        } else {
                            if (!destroy_device_profile(profile_item, dm))
                                break;
                        }
                    } else {
                        if (!destroy_device_profile(profile_item, dm))
                            break;
                    }

                } else {
                    _device_profile_update_direction(profile_item);
                }
            }
        }
    }
}

static pa_hook_result_t sink_put_hook_callback(pa_core *c, pa_sink *sink, pa_device_manager *dm) {
    const char *device_string = NULL, *role = NULL, *device_string_removed_params = NULL;
    struct device_file_info *file_info = NULL;
    dm_device_class_t device_class;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(sink->proplist);
    pa_assert(dm);

    if (pulse_device_class_is_monitor(sink->proplist)) {
        pa_log_debug("This device's class is monitor. Skip this");
        return PA_HOOK_OK;
    }

    pa_log_debug("========== Sink Put Hook Callback '%s'(%d) ==========", sink->name, sink->index);

    device_class = pulse_device_get_class(sink, PA_DEVICE_TYPE_SINK);
    pa_log_debug("Device Class '%s'", device_class_to_string(device_class));

    if (!(device_string = pulse_device_get_device_string(sink, PA_DEVICE_TYPE_SINK))) {
        return PA_HOOK_OK;
    } else {
        pa_log_debug("Device String '%s'", device_string);
    }

    if (device_class == DM_DEVICE_CLASS_BT) {
        handle_not_predefined_device(dm, sink, PA_DEVICE_TYPE_SINK, device_class);
    } else if ((file_info = _device_manager_get_file_info(dm->file_map->playback, device_string))) {
        /* module's argument includes device-string(ex. device=hw:0,0 ),
           but key params for device_types hashmap is not. */
        if (!(device_string_removed_params = pulse_device_get_device_string_removed_argument(sink, PA_DEVICE_TYPE_SINK))) {
            pa_log_debug("argument null");
            return PA_HOOK_OK;
        }
        if(!(role = device_file_info_get_role_with_params(file_info, device_string_removed_params))) {
            pa_log_error("No role for %s", file_info->device_string);
            return PA_HOOK_OK;
        }

        handle_predefined_device_loaded(sink, PA_DEVICE_TYPE_SINK, device_class, device_string, role, dm);
    } else {
        pa_log_debug("Not-predefined device");
        handle_not_predefined_device(dm, sink, PA_DEVICE_TYPE_SINK, device_class);
    }

    dump_device_list(dm);
    return PA_HOOK_OK;
}

static pa_hook_result_t sink_unlink_hook_callback(pa_core *c, pa_sink *sink, pa_device_manager *dm) {
    pa_assert(c);
    pa_assert(sink);
    pa_assert(sink->proplist);
    pa_assert(dm);

    if (pulse_device_class_is_monitor(sink->proplist)) {
        pa_log_debug("This device's class is monitor. Skip this");
        return PA_HOOK_OK;
    }

    pa_log_debug("=========== Sink unlink Hook Callback '%s'(%d) ==========", sink->name, sink->index);
    handle_sink_unloaded(sink, dm);
    dump_device_list(dm);
    return PA_HOOK_OK;
}


static pa_hook_result_t source_put_hook_callback(pa_core *c, pa_source *source, pa_device_manager *dm) {
    const char *device_string = NULL, *role = NULL, *device_string_removed_params = NULL;
    struct device_file_info *file_info = NULL;
    dm_device_class_t device_class;

    pa_assert(c);
    pa_assert(source);
    pa_assert(source->proplist);
    pa_assert(dm);

    if (pulse_device_class_is_monitor(source->proplist)) {
        pa_log_debug("This device's class is monitor. Skip this");
        return PA_HOOK_OK;
    }

    pa_log_debug("========== Source Put Hook Callback '%s'(%d) ==========", source->name, source->index);


    device_class = pulse_device_get_class(source, PA_DEVICE_TYPE_SOURCE);
    pa_log_debug("Device Class '%s'", device_class_to_string(device_class));

    if (!(device_string = pulse_device_get_device_string(source, PA_DEVICE_TYPE_SOURCE))) {
        return PA_HOOK_OK;
    } else {
        pa_log_debug("Device String '%s'", device_string);
    }

    if (device_class == DM_DEVICE_CLASS_BT) {
        handle_not_predefined_device(dm, source, PA_DEVICE_TYPE_SOURCE, device_class);
    } else if ((file_info = _device_manager_get_file_info(dm->file_map->capture, device_string))) {
        /* module's argument includes device-string(ex. device=hw:0,0 ),
           but key params for device_types hashmap is not. */
        if (!(device_string_removed_params = pulse_device_get_device_string_removed_argument(source, PA_DEVICE_TYPE_SOURCE))) {
            pa_log_debug("argument null");
            return PA_HOOK_OK;
        }
        if(!(role = device_file_info_get_role_with_params(file_info, device_string_removed_params))) {
            pa_log_error("No role for %s", file_info->device_string);
            return PA_HOOK_OK;
        }

        handle_predefined_device_loaded(source, PA_DEVICE_TYPE_SOURCE, device_class, device_string, role, dm);
    } else {
        pa_log_debug("Not-predefined device");
        handle_not_predefined_device(dm, source, PA_DEVICE_TYPE_SOURCE, device_class);
    }

    dump_device_list(dm);
    return PA_HOOK_OK;
}

static pa_hook_result_t source_unlink_hook_callback(pa_core *c, pa_source *source, pa_device_manager *dm) {
    pa_assert(c);
    pa_assert(source);
    pa_assert(source->proplist);
    pa_assert(dm);

    if (pulse_device_class_is_monitor(source->proplist)) {
        pa_log_debug("This device's class is monitor. Skip this");
        return PA_HOOK_OK;
    }

    pa_log_debug("========== Source unlink Hook Callback '%s'(%d) ==========", source->name, source->index);
    handle_source_unloaded(source, dm);
    dump_device_list(dm);

    return PA_HOOK_OK;
}

/*
    Build params for load sink or source, and load it.
*/

static void* load_device(pa_core *c, pa_device_type_t pdt, const char *device_string, const char *device_params) {
    const char *args = NULL;
    const char *module_name;
    pa_module *module;
    pa_sink *sink;
    pa_source *source;
    uint32_t device_idx;
    dm_device_class_t device_class;

    pa_assert(c);
    pa_assert(device_string);
    pa_assert(device_params);

    pa_log_debug("-------- load_%s_device : '%s' '%s' -------", pdt == PA_DEVICE_TYPE_SINK ? "playback" : "capture", device_string, device_params);

    device_class = device_string_get_class(device_string);
    if (device_class <= DM_DEVICE_CLASS_NONE || device_class >= DM_DEVICE_CLASS_MAX) {
        pa_log_warn("Invalid device_string '%s'", device_string);
        return NULL;
    }

    if (!(module_name = device_class_get_module_name(device_class, pdt))) {
        pa_log_error("Get proper module name to load failed");
        return NULL;
    }
    if (!(args = build_params_to_load_device(device_string, device_params, device_class))) {
        pa_log_error("Get proper module name to load failed");
        return NULL;
    }
    if (!(module = pa_module_load(c, module_name, args))) {
        pa_log_error("Load module with name '%s' argu '%s' failed", module_name, args);
        return NULL;
    }


    if (pdt == PA_DEVICE_TYPE_SINK) {
        PA_IDXSET_FOREACH(sink, c->sinks, device_idx) {
            if (sink->module == module) {
                return sink;
            }
        }
    } else {
        PA_IDXSET_FOREACH(source, c->sources, device_idx) {
            if (source->module == module) {
                return source;
            }
        }
    }

    return NULL;
}

/*
     Load sink/sources with information written in device-file map,
    If there is several roles in same device-file, then first load with 'normal' params
    and other roles with same params just reference it. if there is a role which is on same device
    but have different params, then do not load it. (ex.uhqa)
    This does not make device_item , just load sink or source.
*/
static int load_builtin_devices(pa_device_manager *dm) {
    void *role_state = NULL;
    struct device_file_info *file_info = NULL;
    const char *params, *role;
    uint32_t file_idx;

    pa_assert(dm);

    pa_log_debug("\n==================== Load Builtin Devices ====================");

    if (dm->file_map->playback) {
        PA_IDXSET_FOREACH(file_info, dm->file_map->playback, file_idx) {
            pa_log_debug("---------------- load sink for '%s' ------------------", file_info->device_string);

            /* if normal device exists , load first */
            if ((params = pa_hashmap_get(file_info->roles, DEVICE_ROLE_NORMAL))) {
                if (!load_device(dm->core, PA_DEVICE_TYPE_SINK, file_info->device_string, params))
                    pa_log_error("load normal playback device failed");
            }

            PA_HASHMAP_FOREACH_KEY(params, file_info->roles, role_state, role) {
                if (pa_streq(role, DEVICE_ROLE_NORMAL))
                    continue;
                pa_log_debug("load sink for role %s", role);
                if (!pulse_device_loaded_with_param(dm->core, PA_DEVICE_TYPE_SINK, file_info->device_string, params)) {
                    if (!load_device(dm->core, PA_DEVICE_TYPE_SINK, file_info->device_string, params))
                        pa_log_error("load playback device failed");
                }
            }
        }
    }



    if (dm->file_map->capture) {
        PA_IDXSET_FOREACH(file_info, dm->file_map->capture, file_idx) {
            pa_log_debug("---------------- load source for '%s' ------------------", file_info->device_string);

            /* if normal device exists , load first */
            if ((params = pa_hashmap_get(file_info->roles, DEVICE_ROLE_NORMAL))) {
                if (!load_device(dm->core, PA_DEVICE_TYPE_SOURCE, file_info->device_string, params)) pa_log_error("load normal capture device failed");
            }

            PA_HASHMAP_FOREACH_KEY(params, file_info->roles, role_state, role) {
                if (pa_streq(role, DEVICE_ROLE_NORMAL)) continue;
                pa_log_debug("load source for role %s", role);
                if (!pulse_device_loaded_with_param(dm->core, PA_DEVICE_TYPE_SOURCE, file_info->device_string, params)) {
                    if (!load_device(dm->core, PA_DEVICE_TYPE_SOURCE, file_info->device_string, params)) {
                        pa_log_error("load capture device failed");
                    }
                }
            }
        }
    }

    return 0;
}


/***************** Parse json file *******************/
static pa_hashmap* parse_device_role_object(json_object *device_role_o) {
    pa_hashmap *roles = NULL;
    const char *params, *device_role;
    struct json_object_iterator it, it_end;
    json_object *params_o;

    pa_assert(device_role_o);
    pa_assert(json_object_is_type(device_role_o, json_type_object));

    roles = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    if (!roles) {
        pa_log_debug("hashmap new failed");
        goto failed;
    }

    it = json_object_iter_begin(device_role_o);
    it_end = json_object_iter_end(device_role_o);

    while (!json_object_iter_equal(&it, &it_end)) {
        device_role = json_object_iter_peek_name(&it);
        params_o = json_object_iter_peek_value(&it);

        if (!(params = json_object_get_string(params_o))) {
            pa_log_debug("There is no device params for role '%s'", device_role);
        }
        pa_log_debug("[DEBUG_PARSE] role '%s' - params '%s'", device_role, params);
        if (device_role_is_valid(device_role)) {
            if (pa_hashmap_put(roles, (void *)device_role, (void *)params)) {
                pa_log_error("put new role to hashmap faild");
                goto failed;
            }
        } else {
            pa_log_error("Invalid device role '%s'", device_role);
        }

        json_object_iter_next(&it);
    }

    if (pa_hashmap_size(roles) == 0) {
        pa_log_warn("There is no role for device.. free hashmap");
        pa_hashmap_free(roles);
        roles = NULL;
    }

    return roles;

failed:
    if (roles)
        pa_hashmap_free(roles);

    return NULL;
}

static struct device_file_info* parse_device_file_object(json_object *device_file_o, const char **device_string_key) {
    pa_hashmap *roles = NULL;
    json_object *device_file_prop_o = NULL;
    const char *device_string = NULL;
    struct device_file_info *file_info = NULL;

    pa_assert(device_file_o);
    pa_assert(device_string_key);
    pa_assert(json_object_is_type(device_file_o, json_type_object));

    if ((device_file_prop_o = json_object_object_get(device_file_o, "device-string")) && json_object_is_type(device_file_prop_o, json_type_string)) {
        if ((device_string = json_object_get_string(device_file_prop_o))) {
            pa_log_debug("[DEBUG_PARSE] ---------------- Device File '%s' ----------------", device_string);
        } else {
            pa_log_error("Get device-string failed");
            return NULL;
        }
    } else {
        pa_log_error("Get device-string object failed");
        return NULL;
    }

    if ((device_file_prop_o = json_object_object_get(device_file_o, DEVICE_TYPE_PROP_ROLE))) {
        if (!(roles = parse_device_role_object(device_file_prop_o))) {
            pa_log_error("Parse device role for '%s' failed", device_string);
            goto failed;
        }
    } else {
        pa_log_error("Get device role object failed");
    }

    file_info = pa_xmalloc0(sizeof(struct device_file_info));
    file_info->device_string = device_string;
    file_info->device_types = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    file_info->roles = roles;

//    *device_string_key = device_string;

    return file_info;

failed :
    if (roles)
        pa_xfree(roles);

    return NULL;
}

static pa_idxset* parse_device_file_array_object(json_object *device_file_array_o) {
    int device_file_num, device_file_idx;
    struct device_file_info *file_info = NULL;
    json_object *device_file_o = NULL;
    pa_idxset *device_files = NULL;
    const char *device_string = NULL;

    pa_assert(device_file_array_o);
    pa_assert(json_object_is_type(device_file_array_o, json_type_array));

    device_files = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    device_file_num = json_object_array_length(device_file_array_o);
    for (device_file_idx = 0; device_file_idx < device_file_num; device_file_idx++) {
        if ((device_file_o = json_object_array_get_idx(device_file_array_o, device_file_idx)) && json_object_is_type(device_file_o, json_type_object)) {
            if ((file_info = parse_device_file_object(device_file_o, &device_string))) {
                pa_idxset_put(device_files, file_info, NULL);
            } else {
                pa_log_error("parse device file object failed");
                goto failed;
            }
        } else {
            pa_log_error("Get device file object failed");
            goto failed;
        }
    }

    if (pa_idxset_size(device_files) == 0) {
        pa_idxset_free(device_files, NULL);
        device_files = NULL;
    }
    return device_files;
failed:
    if (device_files)
        pa_xfree(device_files);
    return NULL;
}

static struct device_file_map *parse_device_file_map() {
    struct device_file_map *file_map = NULL;
    json_object *o, *device_files_o;
    json_object *playback_devices_o = NULL, *capture_devices_o = NULL;

    pa_log_debug("\n[DEBUG_PARSE] ==================== Parse device files ====================");

    o = json_object_from_file(DEVICE_MAP_FILE);

    if (is_error(o)) {
        pa_log_error("Read device-map file failed");
        return NULL;
    }

    file_map = pa_xmalloc0(sizeof(struct device_file_map));

    if ((device_files_o = json_object_object_get(o, DEVICE_FILE_OBJECT)) && json_object_is_type(device_files_o, json_type_object)) {
        if ((playback_devices_o = json_object_object_get(device_files_o, "playback-devices"))) {
            pa_log_debug("[DEBUG_PARSE] ----------------- Playback Device Files ------------------");
            file_map->playback = parse_device_file_array_object(playback_devices_o);
        }
        if ((capture_devices_o = json_object_object_get(device_files_o, "capture-devices"))) {
            pa_log_debug("[DEBUG_PARSE] ----------------- Capture Device Files ------------------");
            file_map->capture = parse_device_file_array_object(capture_devices_o);
        }
    }
    else {
        pa_log_error("Get device files object failed");
        return NULL;
    }

    return file_map;
}


static pa_hashmap* parse_device_role_map(json_object *device_role_map_o) {
    pa_hashmap *roles = NULL;
    const char *device_string, *device_role;
    struct json_object_iterator it, it_end;
    json_object *device_string_o;

    pa_assert(device_role_map_o);
    pa_assert(json_object_is_type(device_role_map_o, json_type_object));

    roles = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    it = json_object_iter_begin(device_role_map_o);
    it_end = json_object_iter_end(device_role_map_o);

    while (!json_object_iter_equal(&it, &it_end)) {
        device_role = json_object_iter_peek_name(&it);
        device_string_o = json_object_iter_peek_value(&it);

        if (!(device_string = json_object_get_string(device_string_o))) {
            pa_log_debug("There is no device string for role '%s'", device_role);
        }
        pa_log_debug("[DEBUG_PARSE] role '%s' - device_string '%s'", device_role, device_string);
        if (device_role_is_valid(device_role)) {
            if (pa_hashmap_put(roles, (void *)device_role, (void *)device_string)) {
                pa_log_error("put new role to hashmap faild");
                goto failed;
            }
        } else {
            pa_log_error("Invalid device role '%s'", device_role);
            goto failed;
        }

        json_object_iter_next(&it);
    }

    return roles;

failed :
    if (roles)
        pa_xfree(roles);

    return NULL;
}



static pa_idxset* parse_device_type_infos() {
    json_object *o, *device_array_o = NULL;
    int device_type_num = 0;
    int device_type_idx = 0;
    json_bool builtin;
    struct device_type_info *type_info = NULL;
    //pa_hashmap *type_infos = NULL;
    pa_idxset *type_infos = NULL;

    o = json_object_from_file(DEVICE_MAP_FILE);
    if (is_error(o)) {
        pa_log_error("Read device-map file failed");
        return NULL;
    }

    pa_log_debug("\n[DEBUG_PARSE] ==================== Parse device types ====================");
    type_infos = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    if ((device_array_o = json_object_object_get(o, DEVICE_TYPE_OBJECT)) && json_object_is_type(device_array_o, json_type_array)) {
        device_type_num = json_object_array_length(device_array_o);
        for (device_type_idx = 0; device_type_idx < device_type_num ; device_type_idx++) {
            json_object *device_o;

            if ((device_o = json_object_array_get_idx(device_array_o, device_type_idx)) && json_object_is_type(device_o, json_type_object)) {
                json_object *device_prop_o;
                json_object *array_item_o;
                int array_len, array_idx;
                const char *device_type = NULL, *device_profile = NULL;
                type_info = pa_xmalloc0(sizeof(struct device_type_info));

                if ((device_prop_o = json_object_object_get(device_o, "device-type")) && json_object_is_type(device_prop_o, json_type_string)) {
                    device_type = json_object_get_string(device_prop_o);
                    pa_log_debug("[DEBUG_PARSE] ---------------- Parse device '%s' ----------------", device_type);
                    type_info->type = device_type;
                } else {
                    pa_log_error("Get device type failed");
                    goto failed;
                }
                if ((device_prop_o = json_object_object_get(device_o, "profile")) && json_object_is_type(device_prop_o, json_type_string)) {
                    device_profile = json_object_get_string(device_prop_o);
                    pa_log_debug("[DEBUG_PARSE] Profile: %s", device_profile);
                    type_info->profile= device_profile;
                } else {
                    pa_log_debug("no device-profile");
                }


                if ((device_prop_o = json_object_object_get(device_o, DEVICE_TYPE_PROP_BUILTIN)) && json_object_is_type(device_prop_o, json_type_boolean)) {
                    builtin = json_object_get_boolean(device_prop_o);
                    pa_log_debug("[DEBUG_PARSE] builtin: %d", builtin);
                    type_info->builtin = builtin;
                } else {
                    pa_log_error("Get device prop '%s' failed", DEVICE_TYPE_PROP_BUILTIN);
                }

                if ((device_prop_o = json_object_object_get(device_o, DEVICE_TYPE_PROP_DIRECTION)) && json_object_is_type(device_prop_o, json_type_array)) {
                    const char *direction;
                    array_len = json_object_array_length(device_prop_o);
                    if ((array_len = json_object_array_length(device_prop_o)) > DEVICE_DIRECTION_MAX) {
                        pa_log_error("Invalid case, The number of direction is too big (%d)", array_len);
                        goto failed;
                    }
                    for (array_idx = 0; array_idx < array_len; array_idx++) {
                        if ((array_item_o = json_object_array_get_idx(device_prop_o, array_idx)) && json_object_is_type(array_item_o, json_type_string)) {
                            direction = json_object_get_string(array_item_o);
                            pa_log_debug("[DEBUG_PARSE] direction : %s", direction);
                            type_info->direction[array_idx] = device_direction_to_int(direction);
                        }
                    }
                } else {
                    pa_log_error("Get device prop '%s' failed", DEVICE_TYPE_PROP_DIRECTION);
                }

                if ((device_prop_o = json_object_object_get(device_o, "avail-condition")) && json_object_is_type(device_prop_o, json_type_array)) {
                    const char *avail_cond;
                    if ((array_len = json_object_array_length(device_prop_o)) > DEVICE_AVAIL_COND_NUM_MAX) {
                        pa_log_error("Invalid case, The number of avail-condition is too big (%d)", array_len);
                        goto failed;
                    }
                    for (array_idx = 0; array_idx < array_len; array_idx++) {
                        if ((array_item_o = json_object_array_get_idx(device_prop_o, array_idx)) && json_object_is_type(array_item_o, json_type_string)) {
                            avail_cond = json_object_get_string(array_item_o);
                            pa_log_debug("[DEBUG_PARSE] avail-condition : %s", avail_cond);
                            strncpy(type_info->avail_condition[array_idx], avail_cond, DEVICE_AVAIL_COND_STR_MAX);
                        }
                    }
                } else {
                    pa_log_error("Get device prop 'avail-condition' failed");
                }

                if ((device_prop_o = json_object_object_get(device_o, "playback-devices")) && json_object_is_type(device_prop_o, json_type_object)) {
                    pa_log_debug("[DEBUG_PARSE] ------ playback devices ------");
                    type_info->playback_devices = parse_device_role_map(device_prop_o);
                }

                if ((device_prop_o = json_object_object_get(device_o, "capture-devices")) && json_object_is_type(device_prop_o, json_type_object)) {
                    pa_log_debug("[DEBUG_PARSE] ------ capture devices ------");
                    type_info->capture_devices = parse_device_role_map(device_prop_o);
                }
                pa_idxset_put(type_infos, type_info, NULL);

            }
            else {
                pa_log_debug("Get device type object failed");
            }
        }
    }
    else {
        pa_log_debug("Get device type array object failed");
    }
    return type_infos;

failed :
    if (type_infos)
        pa_xfree(type_infos);

    return NULL;
}

/*
    Handle device connection detected through dbus.
    First, update device-status hashmap.
    And if correnspondent sink/sources for device_type exist, should make device_item and notify it.
    Use [device_type->roles] mappings in sink/source for find proper sink/source.
*/
static int handle_device_connected(pa_device_manager *dm, const char *device_type, const char *device_profile, const char *name, const char *identifier, int detected_type) {
    struct device_status_info *status_info;
    struct device_type_info *type_info;
    dm_device *device_item;
    dm_device_profile *profile_item;

    pa_assert(dm);
    pa_assert(dm->device_status);

    pa_log_debug("Device %s connected, detected_type : %d", device_type, detected_type);
    if (!(status_info = _device_manager_get_status_info(dm->device_status, device_type, device_profile, identifier))) {
        pa_log_error("No device_status_info for %s.%s", device_type, device_profile);
        return -1;
    }
    status_info->detected = DEVICE_DETECTED;
    status_info->detected_type = detected_type;

    if (!(type_info = _device_manager_get_type_info(dm->type_infos, device_type, device_profile))) {
        pa_log_error("Failed to get type_info for %s.%s", device_type, device_profile);
    }

    if((device_item = _device_manager_get_device(dm->device_list, type_info->type))) {
        if((profile_item = _device_item_get_profile(device_item, type_info->profile))) {
            pa_log_debug("device_item for %s.%s already exists", type_info->type, type_info->profile);
            return 0;
        }
    }

    handle_device_type_available(type_info, name, dm);

    return 0;
}

/*
    Handle device disconnection detected through dbus.
    First, update device-status hashmap.
    And if there is device_item which has the device_type, remove it.
*/
static int handle_device_disconnected(pa_device_manager *dm, const char *device_type, const char *device_profile, const char *identifier) {
    dm_device_profile *profile_item;
    dm_device *device_item;
    struct device_status_info *status_info;
    uint32_t device_idx = 0;

    pa_assert(dm);
    pa_assert(dm->device_status);

    pa_log_debug("Device %s disconnected", device_type);
    if (!(status_info = _device_manager_get_status_info(dm->device_status, device_type, device_profile, identifier))) {
        pa_log_error("No device_status_info for %s.%s", device_type, device_profile);
        return -1;
    }
    status_info->detected = DEVICE_NOT_DETECTED;

    PA_IDXSET_FOREACH(device_item, dm->device_list, device_idx) {
        if (pa_streq(device_item->type, device_type)) {
            if((profile_item = _device_item_get_profile(device_item, device_profile))) {
                destroy_device_profile(profile_item, dm);
            } else {
                pa_log_debug("no matching profile");
            }
        }
    }

    return 0;
}


/*
   look detected status which is external value, make conversion to internal consistent value, and handle it
   device_type, device_profile : which type of device is detected
   identifier : identifier among same device types for support multi-device
*/
static int handle_device_status_changed(pa_device_manager *dm, const char *device_type, const char *device_profile, const char *name, const char *identifier, int detected_status) {
    pa_assert(dm);
    pa_assert(device_type_is_valid(device_type));

    pa_log_debug("Device Status Changed, type : '%s', profile : '%s', identifier : '%s', detected_status : %d", device_type, device_profile, identifier, detected_status);
    if (pa_streq(device_type, DEVICE_TYPE_AUDIO_JACK)) {
        if (detected_status == EARJACK_DISCONNECTED) {
            handle_device_disconnected(dm, device_type, device_profile, identifier);
        } else if (detected_status == EARJACK_TYPE_SPK_ONLY) {
            handle_device_connected(dm, device_type, device_profile, name, identifier, DEVICE_DETECTED_AUDIO_JACK_OUT_DIREC);
        } else if (detected_status == EARJACK_TYPE_SPK_WITH_MIC) {
            handle_device_connected(dm, device_type, device_profile, name, identifier, DEVICE_DETECTED_AUDIO_JACK_BOTH_DIREC);
        } else {
            pa_log_warn("Got invalid audio-jack detected value");
            return -1;
        }
    } else if (pa_streq(device_type, DEVICE_TYPE_BT) && device_profile && pa_streq(device_profile, DEVICE_PROFILE_BT_SCO)) {
        if (detected_status == BT_SCO_DISCONNECTED) {
            handle_device_disconnected(dm, device_type, device_profile, identifier);
        } else if (detected_status == BT_SCO_CONNECTED) {
            handle_device_connected(dm, device_type, device_profile, name, identifier, DEVICE_DETECTED_BT_SCO);
        } else {
            pa_log_warn("Got invalid bt-sco detected value");
            return -1;
        }
    } else if (pa_streq(device_type, DEVICE_TYPE_HDMI)) {
        if (detected_status == HDMI_AUDIO_DISCONNECTED) {
            handle_device_disconnected(dm, device_type, device_profile, identifier);
        } else if (detected_status >= HDMI_AUDIO_AVAILABLE) {
            handle_device_connected(dm, device_type, device_profile, name, identifier, DEVICE_DETECTED_HDMI);
        } else if (detected_status == HDMI_AUDIO_NOT_AVAILABLE) {
            pa_log_debug("HDMI audio not available");
            return -1;
        } else {
            pa_log_warn("Got invalid hdmi detected value");
            return -1;
        }
    } else if (pa_streq(device_type, DEVICE_TYPE_FORWARDING)) {
        if (detected_status == FORWARDING_DISCONNECTED) {
            handle_device_disconnected(dm, device_type, device_profile, identifier);
        } else if (detected_status == FORWARDING_CONNECTED) {
            handle_device_connected(dm, device_type, device_profile, name, identifier, DEVICE_DETECTED_FORWARDING);
        } else {
            pa_log_warn("Got invalid mirroring detected value");
            return -1;
        }
    } else {
        pa_log_debug("unknown device type");
    }
    return 0;
}

/*
    Initialize device-status idxset.
    This is for device-status detected through dbus.
    So, if device_type is not detected through dbus, let's initialize them to detected. (ex. spk, rcv,...)
    If not, initialize to not detected.
*/
static pa_idxset* device_type_status_init(pa_idxset *type_infos) {
    int avail_cond_idx = 0, avail_cond_num = 0, correct_avail_cond = 0;
    struct device_type_info *type_info;
    struct device_status_info *status_info;
    pa_idxset *device_status;
    uint32_t type_idx;

    pa_assert(type_infos);

    pa_log_debug("\n==================== Init Device Status ====================");

    device_status = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    PA_IDXSET_FOREACH(type_info, type_infos, type_idx) {
        status_info = (struct device_status_info *) pa_xmalloc0(sizeof(struct device_status_info));
        status_info->type = type_info->type;
        status_info->profile = type_info->profile;
        if (!compare_device_type(status_info->type, status_info->profile, DEVICE_TYPE_AUDIO_JACK, NULL)) {
            int earjack_status = 0;
            if (vconf_get_int(VCONFKEY_SYSMAN_EARJACK, &earjack_status) < 0) {
                status_info->detected = DEVICE_NOT_DETECTED;
                pa_log_error("Get earjack status failed");
            } else if (earjack_status == EARJACK_DISCONNECTED) {
                status_info->detected = DEVICE_NOT_DETECTED;
            } else if (earjack_status == EARJACK_TYPE_SPK_ONLY) {
                status_info->detected = DEVICE_DETECTED;
                status_info->detected_type = DEVICE_DETECTED_AUDIO_JACK_OUT_DIREC;
            } else if (earjack_status == EARJACK_TYPE_SPK_WITH_MIC) {
                status_info->detected = DEVICE_DETECTED;
                status_info->detected_type = DEVICE_DETECTED_AUDIO_JACK_BOTH_DIREC;
            } else {
                status_info->detected = DEVICE_NOT_DETECTED;
                pa_log_warn("Unknown earjack status : %d", earjack_status);
            }
        } else if (!compare_device_type(status_info->type, status_info->profile, DEVICE_TYPE_BT, DEVICE_PROFILE_BT_SCO)) {
        } else if (!compare_device_type(status_info->type, status_info->profile, DEVICE_TYPE_FORWARDING, NULL)) {
            int miracast_wfd_status = 0;
            if (vconf_get_bool(VCONFKEY_MIRACAST_WFD_SOURCE_STATUS, &miracast_wfd_status) < 0) {
                status_info->detected = DEVICE_NOT_DETECTED;
                pa_log_error("Get mirroring status failed");
            } else if (miracast_wfd_status == FORWARDING_DISCONNECTED) {
                status_info->detected = DEVICE_NOT_DETECTED;
            } else if (miracast_wfd_status == FORWARDING_CONNECTED) {
                status_info->detected = DEVICE_DETECTED;
                status_info->detected_type = DEVICE_DETECTED_FORWARDING;
            } else {
                status_info->detected = DEVICE_NOT_DETECTED;
                pa_log_warn("Unknown mirroring status : %d", miracast_wfd_status);
            }
        } else {
            for (avail_cond_idx = 0, avail_cond_num = 0; avail_cond_idx < DEVICE_AVAIL_COND_NUM_MAX; avail_cond_idx++) {
                if (pa_streq(type_info->avail_condition[avail_cond_idx], "")) {
                    avail_cond_num++;
                }
            }
            if (avail_cond_num == 1 && pa_streq(type_info->avail_condition[correct_avail_cond], DEVICE_AVAIL_CONDITION_STR_PULSE)) {
                /* device types which don't need to be detected from other-side, let's just set 'detected'*/
                status_info->detected = DEVICE_DETECTED;
            } else {
                status_info->detected = DEVICE_NOT_DETECTED;
            }
        }

        pa_log_debug("Set %-17s %s detected", type_info->type, (status_info->detected == DEVICE_DETECTED) ? "" : "not");
        pa_idxset_put(device_status, status_info, NULL);
    }
    return device_status;
}

static int device_list_init(pa_device_manager *dm) {
    pa_assert(dm);

    dm->device_list = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    return 0;
}

#ifdef HAVE_DBUS

static DBusHandlerResult dbus_filter_device_detect_handler(DBusConnection *c, DBusMessage *s, void *userdata) {
    DBusError error;
    int status = 0;
    pa_device_manager *dm = (pa_device_manager *) userdata;

    pa_assert(userdata);

    if (dbus_message_get_type(s) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_info("Dbus device detect handler received msg");

    pa_log_debug("path       : %s", dbus_message_get_path(s));
    pa_log_debug("interface  : %s", dbus_message_get_interface(s));
    pa_log_debug("member     : %s", dbus_message_get_member(s));
    pa_log_debug("siganature : %s", dbus_message_get_signature(s));

    dbus_error_init(&error);

    if (dbus_message_is_signal(s, DBUS_INTERFACE_DEVICED_SYSNOTI, "ChangedEarjack")) {
        if (!dbus_message_get_args(s, NULL, DBUS_TYPE_INT32, &status, DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            handle_device_status_changed(dm, DEVICE_TYPE_AUDIO_JACK, NULL, NULL, NULL, status);
        }
    } else if (dbus_message_is_signal(s, DBUS_INTERFACE_DEVICED_SYSNOTI, "ChangedHDMIAudio")) {
        if (!dbus_message_get_args(s, NULL, DBUS_TYPE_INT32, &status, DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            handle_device_status_changed(dm, DEVICE_TYPE_HDMI, NULL, NULL, NULL, status);
        }
    } else if (dbus_message_is_signal(s, DBUS_INTERFACE_MIRRORING_SERVER, "miracast_wfd_source_status_changed")) {
        if (!dbus_message_get_args(s, NULL, DBUS_TYPE_INT32, &status, DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            handle_device_status_changed(dm, DEVICE_TYPE_FORWARDING, NULL, NULL, NULL, status);
        }
    } else if (dbus_message_is_signal(s, DBUS_INTERFACE_BLUEZ_HEADSET, "PropertyChanged")) {
        DBusMessageIter msg_iter, variant_iter;
        char *property_name;

        pa_log_debug("Got %s PropertyChanged signal", DBUS_INTERFACE_BLUEZ_HEADSET);
        dbus_message_iter_init(s, &msg_iter);
        if (dbus_message_iter_get_arg_type(&msg_iter) != DBUS_TYPE_STRING) {
            pa_log_error("Property name not string");
            goto fail;
        }
        dbus_message_iter_get_basic(&msg_iter, &property_name);
        pa_log_debug("property name : %s", property_name);

        if (!dbus_message_iter_next(&msg_iter)) {
            pa_log_debug("Property value missing");
            goto fail;
        }

        if (dbus_message_iter_get_arg_type(&msg_iter) != DBUS_TYPE_VARIANT) {
            pa_log_debug("Property value not a variant.");
            goto fail;
        }

        dbus_message_iter_recurse(&msg_iter, &variant_iter);

        if (DBUS_TYPE_BOOLEAN == dbus_message_iter_get_arg_type(&variant_iter)) {
            dbus_bool_t value;
            char *name;
            dbus_message_iter_get_basic(&variant_iter, &value);
            if (pa_streq(property_name, "Playing")) {
                dm_device *device_item;
                pa_log_debug("SCO Playing : %d", value);
                if ((device_item = _device_manager_get_device(dm->device_list, DEVICE_TYPE_BT))) {
                    if (value)
                        _device_item_set_active_profile(device_item, DEVICE_PROFILE_BT_SCO);
                    else
                        _device_item_set_active_profile_auto(device_item);
                }
            } else if (pa_streq(property_name, "Connected")) {
                pa_log_debug("HFP Connection : %d", value);
                if (value) {
                    method_call_bt_get_name(c, dbus_message_get_path(s), &name);
                    status = BT_SCO_CONNECTED;
                } else {
                    status = BT_SCO_DISCONNECTED;
                }
                handle_device_status_changed(dm, DEVICE_TYPE_BT, DEVICE_PROFILE_BT_SCO, name, NULL, status);
            }
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

static int watch_signals(pa_device_manager *dm) {
    DBusError error;

    pa_assert(dm);
    pa_assert(dm->dbus_conn);

    dbus_error_init(&error);

    pa_log_debug("Watch Dbus signals");

    if (!dbus_connection_add_filter(pa_dbus_connection_get(dm->dbus_conn), dbus_filter_device_detect_handler, dm, NULL)) {
        pa_log_error("Unable to add D-Bus filter : %s: %s", error.name, error.message);
        goto fail;
    }

    if (pa_dbus_add_matches(pa_dbus_connection_get(dm->dbus_conn), &error, FILTER_DEVICED_SYSNOTI, FILTER_SOUND_SERVER, FILTER_BLUEZ, FILTER_MIRRORING, NULL) < 0) {
        pa_log_error("Unable to subscribe to signals: %s: %s", error.name, error.message);
        goto fail;
    }
    return 0;

fail:
    dbus_error_free(&error);
    return -1;
}

static void unwatch_signals(pa_device_manager *dm) {
    pa_log_debug("Unwatch Dbus signals");

    pa_assert(dm);
    pa_assert(dm->dbus_conn);

    pa_dbus_remove_matches(pa_dbus_connection_get(dm->dbus_conn), FILTER_DEVICED_SYSNOTI, FILTER_SOUND_SERVER, FILTER_BLUEZ, FILTER_MIRRORING, NULL);
    dbus_connection_remove_filter(pa_dbus_connection_get(dm->dbus_conn), dbus_filter_device_detect_handler, dm);
}


static void send_device_connected_signal(dm_device *device_item, pa_bool_t connected, pa_device_manager *dm) {
    DBusMessage *signal_msg;
    DBusMessageIter msg_iter, device_iter;
    dm_device_profile *profile_item;
    dbus_bool_t _connected = connected;
    dbus_int32_t device_id;

    pa_assert(device_item);
    pa_assert(device_item->profiles);
    pa_assert(dm);

    pa_log_debug("Send following device %s signal", connected ? "Connected" : "Disconnected");
    dump_device_info(device_item);

    pa_assert_se(signal_msg = dbus_message_new_signal(DBUS_OBJECT_DEVICE_MANAGER, DBUS_INTERFACE_DEVICE_MANAGER, "DeviceConnected"));
    dbus_message_iter_init_append(signal_msg, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_STRUCT, NULL, &device_iter));
    if (!(profile_item = _device_item_get_active_profile(device_item))) {
        pa_log_error("active profile null");
        return;
    }

    device_id = (dbus_int32_t) device_item->id;
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device_id);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &device_item->type);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &profile_item->direction);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &profile_item->state);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &device_item->name);
    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &device_iter));
    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_BOOLEAN, &_connected);


    pa_assert_se(dbus_connection_send(pa_dbus_connection_get(dm->dbus_conn), signal_msg, NULL));
    dbus_message_unref(signal_msg);
}

static void send_device_info_changed_signal(dm_device *device_item, int changed_type, pa_device_manager *dm) {
    DBusMessage *signal_msg;
    DBusMessageIter msg_iter, device_iter;
    dm_device_profile *profile_item;
    dbus_int32_t device_id;

    pa_assert(device_item);
    pa_assert(device_item->profiles);
    pa_assert(dm);

    pa_log_debug("Send folling device info changed signal");
    dump_device_info(device_item);

    pa_assert_se(signal_msg = dbus_message_new_signal(DBUS_OBJECT_DEVICE_MANAGER, DBUS_INTERFACE_DEVICE_MANAGER, "DeviceInfoChanged"));
    dbus_message_iter_init_append(signal_msg, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_STRUCT, NULL, &device_iter));
    if (!(profile_item = _device_item_get_active_profile(device_item))) {
        pa_log_error("active profile null");
        return;
    }
    device_id = (dbus_int32_t) device_item->id;
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device_id);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &device_item->type);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &profile_item->direction);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &profile_item->state);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &device_item->name);
    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &device_iter));
    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_INT32, &changed_type);


    pa_assert_se(dbus_connection_send(pa_dbus_connection_get(dm->dbus_conn), signal_msg, NULL));
    dbus_message_unref(signal_msg);
}

static void notify_device_connection_changed(dm_device *device_item, pa_bool_t connected, pa_device_manager *dm) {
    pa_device_manager_hook_data_for_conn_changed hook_data;

    send_device_connected_signal(device_item, connected, dm);
    hook_data.is_connected = connected;
    hook_data.device = device_item;
    pa_hook_fire(pa_communicator_hook(dm->comm, PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED), &hook_data);
}

static void notify_device_info_changed(dm_device *device_item, dm_device_changed_info_t changed_type, pa_device_manager *dm) {
    pa_device_manager_hook_data_for_info_changed hook_data;

    send_device_info_changed_signal(device_item, changed_type, dm);

    hook_data.changed_info = changed_type;
    hook_data.device = device_item;
    pa_hook_fire(pa_communicator_hook(dm->comm, PA_COMMUNICATOR_HOOK_DEVICE_INFORMATION_CHANGED), &hook_data);
}

static pa_bool_t device_item_match_for_mask(dm_device *device_item, int device_flags, pa_device_manager *dm) {
    dm_device_profile *profile_item = NULL;
    pa_bool_t match = FALSE;
    int need_to_check_for_io_direction = device_flags & DEVICE_IO_DIRECTION_FLAGS;
    int need_to_check_for_state = device_flags & DEVICE_STATE_FLAGS;
    int need_to_check_for_type = device_flags & DEVICE_TYPE_FLAGS;

    pa_assert(device_item);

    if (device_flags == DEVICE_ALL_FLAG)
        return TRUE;

    profile_item = _device_item_get_active_profile(device_item);
    if (need_to_check_for_io_direction) {
        if ((profile_item->direction == DM_DEVICE_DIRECTION_IN) && (device_flags & DEVICE_IO_DIRECTION_IN_FLAG)) match = TRUE;
        else if ((profile_item->direction == DM_DEVICE_DIRECTION_OUT) && (device_flags & DEVICE_IO_DIRECTION_OUT_FLAG)) match = TRUE;
        else if ((profile_item->direction == DM_DEVICE_DIRECTION_BOTH) && (device_flags & DEVICE_IO_DIRECTION_BOTH_FLAG)) match = TRUE;
        if (match) {
            if (!need_to_check_for_state && !need_to_check_for_type) return TRUE;
        } else {
            return FALSE;
        }
    }
    if (need_to_check_for_state) {
        match = FALSE;
        if ((profile_item->state == DM_DEVICE_STATE_DEACTIVATED) && (device_flags & DEVICE_STATE_DEACTIVATED_FLAG))
            match = TRUE;
        else if ((profile_item->state == DM_DEVICE_STATE_ACTIVATED) && (device_flags & DEVICE_STATE_ACTIVATED_FLAG))
            match = TRUE;
        if (match) {
            if (!need_to_check_for_type)
                return TRUE;
        } else {
            return FALSE;
        }
    }
    if (need_to_check_for_type) {
        struct device_type_info *type_info;
        if (!(type_info = _device_manager_get_type_info(dm->type_infos,  device_item->type,  profile_item->profile))) {
            pa_log_error("No type_info for %s.%s", device_item->type, profile_item->profile);
            return FALSE;
        }
        if (type_info->builtin && (device_flags & DEVICE_TYPE_INTERNAL_FLAG))
            return TRUE;
        else if (!type_info->builtin && (device_flags & DEVICE_TYPE_EXTERNAL_FLAG))
            return TRUE;
    }

    return FALSE;
}


static int method_call_bt_sco(DBusConnection *conn, pa_bool_t onoff) {
    DBusMessage *msg, *reply;
    DBusError err;
    const char *method;

    method = onoff ? "Play" : "Stop";
    if (!(msg = dbus_message_new_method_call(DBUS_SERVICE_HFP_AGENT, DBUS_OBJECT_HFP_AGENT, DBUS_INTERFACE_HFP_AGENT, method))) {
        pa_log_error("dbus method call failed");
        return -1;
    }

    dbus_error_init(&err);
    if (!(reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err))) {
        pa_log_error("Failed to method call %s.%s, %s", DBUS_INTERFACE_HFP_AGENT, method, err.message);
        dbus_error_free(&err);
        return -1;
    }

    dbus_message_unref(reply);
    return 0;
}

static int method_call_bt_sco_get_property(DBusConnection *conn, pa_bool_t *is_wide_band, pa_bool_t *nrec) {
    DBusMessage *msg, *reply;
    DBusMessageIter reply_iter, reply_iter_entry;
    DBusError err;
    unsigned int codec;
    const char *property;

    pa_assert(conn);

    if (!is_wide_band && !nrec) {
        return -1;
    }

    if (!(msg = dbus_message_new_method_call(DBUS_SERVICE_HFP_AGENT, DBUS_OBJECT_HFP_AGENT, DBUS_INTERFACE_HFP_AGENT, "GetProperties"))) {
        pa_log_error("dbus method call failed");
        return -1;
    }

    dbus_error_init(&err);
    if (!(reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err))) {
        pa_log_error("Failed to method call %s.%s, %s", DBUS_INTERFACE_HFP_AGENT, "GetProperties", err.message);
        dbus_error_free(&err);
        return -1;
    }

    dbus_message_iter_init(reply,  &reply_iter);

    if (dbus_message_iter_get_arg_type(&reply_iter) != DBUS_TYPE_ARRAY) {
        pa_log_error("Cannot get reply argument");
        return -1;
    }

    dbus_message_iter_recurse(&reply_iter,  &reply_iter_entry);

    while (dbus_message_iter_get_arg_type(&reply_iter_entry) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter dict_entry, dict_entry_val;
        dbus_message_iter_recurse(&reply_iter_entry, &dict_entry);
        dbus_message_iter_get_basic(&dict_entry, &property);
        pa_log_debug("String received = %s", property);
        if (property) {
            if (pa_streq("codec", property) && is_wide_band) {
                dbus_message_iter_next(&dict_entry);
                dbus_message_iter_recurse(&dict_entry, &dict_entry_val);
                if (dbus_message_iter_get_arg_type(&dict_entry_val) != DBUS_TYPE_UINT32)
                    continue;
                dbus_message_iter_get_basic(&dict_entry_val, &codec);
                pa_log_debug("Codec = [%d]", codec);
                *is_wide_band= codec == BT_MSBC_CODEC_ID ? TRUE : FALSE;
            } else if (pa_streq("nrec", property) && nrec) {
                dbus_message_iter_next(&dict_entry);
                dbus_message_iter_recurse(&dict_entry, &dict_entry_val);
                if (dbus_message_iter_get_arg_type(&dict_entry_val) != DBUS_TYPE_BOOLEAN)
                    continue;
                dbus_message_iter_get_basic(&dict_entry_val, nrec);
                pa_log_debug("nrec= [%d]", *nrec);
            }
        }
        dbus_message_iter_next(&reply_iter_entry);
    }


    dbus_message_unref(reply);
    return 0;
}


static int method_call_bt_get_name(DBusConnection *conn, const char *device_path, char **name) {
    const char *intf = DBUS_INTERFACE_BLUEZ_DEVICE, *prop = "Alias";
    DBusMessage *msg, *reply;
    DBusMessageIter reply_iter, variant_iter;
    DBusError err;

    pa_assert(conn);
    pa_assert(device_path);
    pa_assert(name);

    if (!(msg = dbus_message_new_method_call(DBUS_SERVICE_BLUEZ, device_path, "org.freedesktop.DBus.Properties", "Get"))) {
        pa_log_error("dbus method call failed");
        return -1;
    }

    dbus_message_append_args(msg,
                DBUS_TYPE_STRING, &intf,
                DBUS_TYPE_STRING, &prop,
                DBUS_TYPE_INVALID);

    dbus_error_init(&err);
    if (!(reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err))) {
        pa_log_error("Failed to method call %s.%s, %s", DBUS_INTERFACE_BLUEZ_DEVICE, "Get", err.message);
        dbus_error_free(&err);
        return -1;
    }

    dbus_message_iter_init(reply,  &reply_iter);

    if (dbus_message_iter_get_arg_type(&reply_iter) != DBUS_TYPE_VARIANT) {
        pa_log_error("Cannot get reply argument");
        return -1;
    }

    dbus_message_iter_recurse(&reply_iter,  &variant_iter);

    if (dbus_message_iter_get_arg_type(&variant_iter) == DBUS_TYPE_STRING) {
        dbus_message_iter_get_basic(&variant_iter, name);
    }

    dbus_message_unref(reply);
    return 0;
}


static void handle_get_connected_device_list(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    pa_device_manager *dm;
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter, array_iter, device_iter;
    dm_device *device_item;
    dm_device_profile *profile_item;
    uint32_t device_idx;
    dbus_int32_t device_id;
    int mask_flags;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    pa_log_debug("Get connected device list");

    dm = (pa_device_manager*) userdata;

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_INT32, &mask_flags,
                                       DBUS_TYPE_INVALID));

    dbus_message_iter_init_append(reply, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_ARRAY, "(isiis)", &array_iter));

    PA_IDXSET_FOREACH(device_item, dm->device_list, device_idx) {
        if(!(profile_item = pa_idxset_get_by_index(device_item->profiles, device_item->active_profile))) {
            pa_log_error("no active profile");
            continue;
        }
        if (device_item_match_for_mask(device_item,  mask_flags, dm)) {
            device_id = (dbus_int32_t)device_item->id;
            pa_assert_se(dbus_message_iter_open_container(&array_iter, DBUS_TYPE_STRUCT, NULL, &device_iter));
            dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device_id);
            dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &profile_item->device_item->type);
            dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &profile_item->direction);
            dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &profile_item->state);
            dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &device_item->name);
            pa_assert_se(dbus_message_iter_close_container(&array_iter, &device_iter));
        }
    }

    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &array_iter));

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}


static void handle_get_bt_a2dp_status(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    pa_device_manager *dm;
    DBusMessage *reply = NULL;
    dm_device *device_item;
    dm_device_profile *profile_item;
    dbus_bool_t is_bt_on = FALSE;
    char *bt_name = "none";

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    pa_log_debug("Get bt a2dp list");

    dm = (pa_device_manager*) userdata;

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (!(device_item = _device_manager_get_device(dm->device_list, DEVICE_TYPE_BT))) {
        if (!(profile_item = _device_item_get_profile(device_item, DEVICE_PROFILE_BT_A2DP))) {
            is_bt_on = TRUE;
            bt_name = device_item->name;
        }
    }

    pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &is_bt_on,
                                                 DBUS_TYPE_STRING, &bt_name,
                                                 DBUS_TYPE_INVALID));

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}



static void handle_load_sink(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    pa_device_manager *dm;
    char *device_type, *device_profile, *role;
    DBusMessage *reply = NULL;

    pa_assert_se((reply = dbus_message_new_method_return(msg)));
    dm = (pa_device_manager *) userdata;
    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &device_type,
                                       DBUS_TYPE_STRING, &device_profile,
                                       DBUS_TYPE_STRING, &role,
                                       DBUS_TYPE_INVALID));

    if (pa_streq(device_profile, "none"))
        device_profile = NULL;
    pa_device_manager_load_sink(device_type, device_profile, role, dm);
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static void handle_test_device_status_change(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    pa_device_manager *dm = (pa_device_manager *)userdata;
    char *device_type, *device_profile;
    dbus_int32_t status;
    DBusMessage *reply = NULL;
    DBusError error;

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    dbus_error_init(&error);
    if(!dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &device_type,
                                       DBUS_TYPE_STRING, &device_profile,
                                       DBUS_TYPE_INT32, &status,
                                       DBUS_TYPE_INVALID)) {
        pa_log_error("failed to get dbus args : %s", error.message);
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_ARGS, "%s", error.message);
        dbus_error_free(&error);
    }

    pa_log_debug("handle_test_device_status_change, type:%s, profile:%s, status:%d", device_type, device_profile, status);
    if (pa_streq(device_profile, "none"))
        device_profile = NULL;

    handle_device_status_changed(dm, device_type, device_profile, NULL,  NULL, status);
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *xml = DEVICE_MANAGER_INTROSPECT_XML;
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

static DBusHandlerResult handle_device_manager_methods(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    int method_idx = 0;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    for (method_idx = 0; method_idx < METHOD_HANDLER_MAX; method_idx++) {
        if (dbus_message_is_method_call(msg, DBUS_INTERFACE_DEVICE_MANAGER, method_handlers[method_idx].method_name )) {
            method_handlers[method_idx].receive_cb(conn, msg, userdata);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult method_call_handler(DBusConnection *c, DBusMessage *m, void *userdata) {
    struct userdata *u = userdata;
    const char *path, *interface, *member;

    pa_assert(c);
    pa_assert(m);
    pa_assert(u);

    path = dbus_message_get_path(m);
    interface = dbus_message_get_interface(m);
    member = dbus_message_get_member(m);

    pa_log_debug("DeviceManager Method Call Handler : path=%s, interface=%s, member=%s", path, interface, member);

    if (!pa_streq(path, DBUS_OBJECT_DEVICE_MANAGER))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        return handle_introspect(c, m, u);
        /*
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "Get")) {
        return handle_get_property(c, m, u);
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "Set")) {
        return  handle_set_property(c, m, u);
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "GetAll")) {
        return handle_get_all_property(c, m, u);
        */
    } else {
        return handle_device_manager_methods(c, m, u);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void endpoint_init(pa_device_manager *dm) {
    static const DBusObjectPathVTable vtable_endpoint = {
        .message_function = method_call_handler,
    };

    pa_log_debug("Device manager dbus endpoint init");

    if (dm && dm->dbus_conn) {
        if (!dbus_connection_register_object_path(pa_dbus_connection_get(dm->dbus_conn), DBUS_OBJECT_DEVICE_MANAGER, &vtable_endpoint, dm))
            pa_log_error("Failed to register object path");
    } else {
        pa_log_error("Cannot get dbus connection to register object path");
    }
}

static void endpoint_done(pa_device_manager *dm) {
    pa_log_debug("Device manager dbus endpoint done");
    if (dm && dm->dbus_conn) {
        if (!dbus_connection_unregister_object_path(pa_dbus_connection_get(dm->dbus_conn), DBUS_OBJECT_DEVICE_MANAGER))
            pa_log_error("Failed to unregister object path");
    } else {
        pa_log_error("Cannot get dbus connection to unregister object path");
    }
}

static void dbus_init(pa_device_manager *dm) {
    DBusError error;
    pa_dbus_connection *connection = NULL;

    pa_assert(dm);
    pa_log_debug("Dbus init");
    dbus_error_init(&error);

    if (!(connection = pa_dbus_bus_get(dm->core, DBUS_BUS_SYSTEM, &error)) || dbus_error_is_set(&error)) {
        if (connection) {
            pa_dbus_connection_unref(connection);
        }
        pa_log_error("Unable to contact D-Bus system bus: %s: %s", error.name, error.message);
        goto fail;
    } else {
        pa_log_debug("Got dbus connection");
    }

    dm->dbus_conn = connection;

    if (watch_signals(dm) < 0)
        pa_log_error("dbus watch signals failed");
    else
        pa_log_debug("dbus ready to get signals");

    endpoint_init(dm);

fail:
    dbus_error_free(&error);
}

static void dbus_deinit(pa_device_manager *dm) {
    pa_assert(dm);

    pa_log_debug("Dbus deinit");

    endpoint_done(dm);
    unwatch_signals(dm);

    if (dm->dbus_conn) {
        pa_dbus_connection_unref(dm->dbus_conn);
        dm->dbus_conn = NULL;
    }
}
#endif

pa_idxset* pa_device_manager_get_device_list(pa_device_manager *dm) {
    pa_assert(dm);
    pa_assert(dm->device_list);

    return dm->device_list;
}

dm_device* pa_device_manager_get_device(pa_device_manager *dm, const char *device_type) {
    pa_assert(dm);

    return _device_manager_get_device(dm->device_list, device_type);
}

dm_device* pa_device_manager_get_device_by_id(pa_device_manager *dm, uint32_t id) {
    pa_assert(dm);

    return _device_manager_get_device_with_id(dm->device_list, id);
}

pa_sink* pa_device_manager_get_sink(dm_device *device_item, const char *role) {
    dm_device_profile *profile_item;

    pa_assert(device_item);
    pa_assert(profile_item = _device_item_get_active_profile(device_item));

    if (!profile_item->playback_devices) {
        pa_log_warn("No playback device in %s", device_item->name);
        return NULL;
    }

    return pa_hashmap_get(profile_item->playback_devices, role);
}

pa_source* pa_device_manager_get_source(dm_device *device_item, const char *role) {
    dm_device_profile *profile_item;

    pa_assert(device_item);
    pa_assert(profile_item = _device_item_get_active_profile(device_item));

    if (!profile_item->capture_devices) {
        pa_log_warn("No capture device in %s", device_item->name);
        return NULL;
    }

    return pa_hashmap_get(profile_item->capture_devices, role);
}

void pa_device_manager_set_device_state(dm_device *device_item, dm_device_state_t state) {
    dm_device_profile *profile_item;

    pa_assert(device_item);
    pa_assert(profile_item = _device_item_get_active_profile(device_item));

    pa_log_debug("pa_device_manager_set_device_state : %s.%s -> %d", device_item->type, profile_item->profile, state);
    _device_profile_set_state(profile_item,  state);
}

dm_device_state_t pa_device_manager_get_device_state(dm_device *device_item) {
    dm_device_profile *profile_item;

    pa_assert(device_item);
    pa_assert(profile_item = _device_item_get_active_profile(device_item));

    return profile_item->state;
}

uint32_t pa_device_manager_get_device_id(dm_device *device_item) {
    pa_assert(device_item);

    return device_item->id;
}

const char* pa_device_manager_get_device_type(dm_device *device_item) {
    pa_assert(device_item);

    return device_item->type;
}

const char* pa_device_manager_get_device_subtype(dm_device *device_item) {
    dm_device_profile *profile_item;

    pa_assert(device_item);
    pa_assert(profile_item = _device_item_get_active_profile(device_item));

    return profile_item->profile;
}

dm_device_direction_t pa_device_manager_get_device_direction(dm_device *device_item) {
    dm_device_profile *profile_item;

    pa_assert(device_item);
    pa_assert(profile_item = _device_item_get_active_profile(device_item));

    return profile_item->direction;
}

int pa_device_manager_bt_sco_open(pa_device_manager *dm) {
    struct device_status_info *status_info;

    pa_assert(dm);
    pa_assert(dm->dbus_conn);

    if (!(status_info = _device_manager_get_status_info(dm->device_status, DEVICE_TYPE_BT, DEVICE_PROFILE_BT_SCO, NULL))) {
        pa_log_error("No status info for bt-sco");
        return -1;
    }
    if (!status_info->detected) {
        pa_log_error("bt-sco not detected");
        return -1;
    }

    pa_log_debug("bt sco open start");
    if (method_call_bt_sco(pa_dbus_connection_get(dm->dbus_conn), TRUE) < 0) {
        pa_log_error("Failed to bt sco on");
        return -1;
    }

    pa_log_debug("bt sco open end");

    return 0;
}

int pa_device_manager_bt_sco_close(pa_device_manager *dm) {
    struct device_status_info *status_info;

    pa_assert(dm);
    pa_assert(dm->dbus_conn);

    if (!(status_info = _device_manager_get_status_info(dm->device_status, DEVICE_TYPE_BT, DEVICE_PROFILE_BT_SCO, NULL))) {
        pa_log_error("No status info for bt-sco");
        return -1;
    }
    if (!status_info->detected) {
        pa_log_error("bt-sco not detected");
        return -1;
    }

    pa_log_debug("bt sco close start");
    if (method_call_bt_sco(pa_dbus_connection_get(dm->dbus_conn), FALSE) < 0) {
        pa_log_error("Failed to bt sco close");
        return -1;
    }
    pa_log_debug("bt sco close end");

    return 0;
}

int pa_device_manager_bt_sco_get_property(pa_device_manager *dm, pa_bool_t *is_wide_band, pa_bool_t *nrec) {
    pa_assert(dm);
    pa_assert(dm->dbus_conn);

    pa_log_debug("bt sco get property start");

    if (method_call_bt_sco_get_property(pa_dbus_connection_get(dm->dbus_conn), is_wide_band, nrec) < 0) {
        pa_log_error("Failed to get bt property");
        return -1;
    }

    pa_log_debug("bt sco get property end");

    return 0;
}

int pa_device_manager_load_sink(const char *device_type, const char *device_profile, const char *role, pa_device_manager *dm) {
    const char *device_string, *params;
    struct device_type_info *type_info;
    struct device_file_info *file_info;
    dm_device_profile *profile_item;
    dm_device *device_item;
    pa_sink *sink;
    uint32_t device_idx;

    pa_assert(dm);
    pa_assert(dm->device_list);

    pa_log_debug("load sink for '%s,%s'", device_type, role);
    PA_IDXSET_FOREACH(device_item, dm->device_list, device_idx) {
        if (pa_streq(device_type, device_item->type)) {
            if ((profile_item = _device_item_get_profile(device_item, device_profile))) {
                if (pa_hashmap_get(profile_item->playback_devices, role)) {
                    pa_log_warn("Proper sink for '%s:%s' already loaded", device_type, role);
                    return -1;
                }
            }
        }
    }

    if (!(type_info = _device_manager_get_type_info(dm->type_infos, device_type, device_profile))) {
        pa_log_error("No type map for %s", device_type);
        return -1;
    }

    if (!(device_string = pa_hashmap_get(type_info->playback_devices, role))) {
        pa_log_error("No device-string for '%s:%s'", device_type, role);
        goto failed;
    }

    if (!(file_info = _device_manager_get_file_info(dm->file_map->playback, device_string))) {
        pa_log_error("No playback file-map for '%s'", device_string);
        goto failed;
    }

    if (!(params = pa_hashmap_get(file_info->roles, role))) {
        pa_log_error("No params for '%s,%s'", device_string, role);
        goto failed;
    }

    if ((sink = load_device(dm->core, PA_DEVICE_TYPE_SINK, device_string, params))) {
        pa_log_debug("loaded sink '%s' for '%s,%s' success", sink->name, device_type, role);
    } else {
        pa_log_warn("Cannot load playback device with '%s,%s'", device_string, params);
        goto failed;
    }

    return 0;

failed:
    return -1;
}

int pa_device_manager_load_source(const char *device_type, const char *device_profile, const char *role, pa_device_manager *dm) {
    const char *device_string, *params;
    struct device_type_info *type_info;
    struct device_file_info *file_info;
    dm_device_profile *profile_item;
    dm_device *device_item;
    pa_source *source;
    uint32_t device_idx;

    pa_assert(dm);

    pa_log_debug("load source for '%s,%s'", device_type, role);

    PA_IDXSET_FOREACH(device_item, dm->device_list, device_idx) {
        if (pa_streq(device_type, device_item->type)) {
            if ((profile_item = _device_item_get_profile(device_item, device_profile))) {
                if (pa_hashmap_get(profile_item->capture_devices, role)) {
                    pa_log_warn("Proper source for '%s:%s' already loaded", device_type, role);
                    return -1;
                }
            }
        }
    }


    if (!(type_info = _device_manager_get_type_info(dm->type_infos, device_type, device_profile))) {
        pa_log_error("No type map for %s", device_type);
        return -1;
    }

    if (!(device_string = pa_hashmap_get(type_info->capture_devices, role))) {
        pa_log_error("No device-string for '%s:%s'", device_type, role);
        goto failed;
    }

    if (!(file_info = _device_manager_get_file_info(dm->file_map->capture, device_string))) {
        pa_log_error("No capture file-map for '%s'", device_string);
        goto failed;
    }

    if (!(params = pa_hashmap_get(file_info->roles, role))) {
        pa_log_error("No params for '%s,%s'", device_string, role);
        goto failed;
    }

    if ((source = load_device(dm->core, PA_DEVICE_TYPE_SOURCE, device_string, params))) {
        pa_log_debug("loaded source '%s' for '%s,%s' success", source->name, device_type, role);
    } else {
        pa_log_warn("Cannot load capture device with '%s,%s'", device_string, params);
        goto failed;
    }

    return 0;

failed:
    return -1;
}

pa_device_manager* pa_device_manager_init(pa_core *c) {
    pa_device_manager *dm;

    pa_log_debug("pa_device_manager_init start");

    dm = pa_xnew0(pa_device_manager, 1);
    dm->core = c;

    dbus_init(dm);

    dm->sink_put_hook_slot = pa_hook_connect(&dm->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_LATE+10, (pa_hook_cb_t) sink_put_hook_callback, dm);
    dm->sink_unlink_hook_slot = pa_hook_connect(&dm->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_unlink_hook_callback, dm);
    dm->source_put_hook_slot = pa_hook_connect(&dm->core->hooks[PA_CORE_HOOK_SOURCE_PUT], PA_HOOK_LATE+10, (pa_hook_cb_t) source_put_hook_callback, dm);
    dm->source_unlink_hook_slot = pa_hook_connect(&dm->core->hooks[PA_CORE_HOOK_SOURCE_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) source_unlink_hook_callback, dm);

    dm->comm = pa_communicator_get(dm->core);
    if (!(dm->type_infos = parse_device_type_infos())) {
        pa_log_error("Parse device-type-map failed");
        return NULL;
    }

    if (!(dm->file_map = parse_device_file_map())) {
        pa_log_error("Parse device-file-map failed");
        return NULL;
    }

    if (device_list_init(dm) < 0) {
        pa_log_error("Init device list failed");
        return NULL;
    }

    if (!(dm->device_status = device_type_status_init(dm->type_infos))) {
        pa_log_error("Init device status failed");
        return NULL;
    }

    if (load_builtin_devices(dm) != 0) {
        pa_log_error("Load Builtin Devices faled");
        return NULL;
    }

    /* Just for convenience when test*/
    if (!_device_manager_set_default_sink(dm,  DEVICE_TYPE_SPEAKER,  NULL, "normal")) {
        pa_log_warn("Set default sink with speaker(normal) failed");
    }
    if (!_device_manager_set_default_source(dm,  DEVICE_TYPE_MIC,  NULL, "normal")) {
        pa_log_warn("Set default source with mic(normal) failed");
    }

    pa_log_debug("pa_device_manager_init end");

    return dm;
}

void pa_device_manager_done(pa_device_manager *dm) {

    if (!dm)
        return;

    pa_log_debug("pa_device_manager_done start");

    if (dm->sink_put_hook_slot)
        pa_hook_slot_free(dm->sink_put_hook_slot);
    if (dm->sink_unlink_hook_slot)
        pa_hook_slot_free(dm->sink_unlink_hook_slot);
    if (dm->source_put_hook_slot)
        pa_hook_slot_free(dm->source_put_hook_slot);
    if (dm->source_unlink_hook_slot)
        pa_hook_slot_free(dm->source_unlink_hook_slot);

    if (dm->comm)
        pa_communicator_unref(dm->comm);

    if (dm->type_infos)
        pa_idxset_free(dm->type_infos, (pa_free_cb_t)type_info_free_func);
    if (dm->file_map) {
        if (dm->file_map->playback)
            pa_idxset_free(dm->file_map->playback, (pa_free_cb_t)file_info_free_func);
        if (dm->file_map->capture)
            pa_idxset_free(dm->file_map->capture, (pa_free_cb_t)file_info_free_func);
        pa_xfree(dm->file_map);
    }
    if (dm->device_list)
        pa_idxset_free(dm->device_list, (pa_free_cb_t)device_item_free_func);
    if (dm->device_status)
        pa_idxset_free(dm->device_status, NULL);

    dbus_deinit(dm);

    pa_log_debug("pa_device_manager_done end");
}
