
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

#ifdef HAVE_DBUS
#include <pulsecore/dbus-shared.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/protocol-dbus.h>
#endif

#include "communicator.h"
#include "device-manager.h"

#define DEVICE_MAP_FILE                    "/etc/pulse/device-map.json"
#define DEVICE_PROFILE_MAX                  2
#define DEVICE_STRING_MAX                   10
#define DEVICE_DIRECTION_MAX                3
#define DEVICE_PARAM_STRING_MAX             50
#define DEVICE_AVAIL_CONDITION_MAX          2
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

#define DEVICE_TYPE_STR_SPEAKER             "builtin-speaker"
#define DEVICE_TYPE_STR_RECEIVER            "builtin-receiver"
#define DEVICE_TYPE_STR_MIC                 "builtin-mic"
#define DEVICE_TYPE_STR_AUDIO_JACK          "audio-jack"
#define DEVICE_TYPE_STR_BT_A2DP             "bt-a2dp"
#define DEVICE_TYPE_STR_BT_SCO              "bt-sco"
#define DEVICE_TYPE_STR_HDMI                "hdmi"
#define DEVICE_TYPE_STR_MIRRORING           "mirroring"
#define DEVICE_TYPE_STR_USB_AUDIO           "usb-audio"

#define DEVICE_TYPE_STR_BT                  "bt"

#define DEVICE_DIRECTION_STR_NONE           "none"
#define DEVICE_DIRECTION_STR_OUT            "out"
#define DEVICE_DIRECTION_STR_IN             "in"
#define DEVICE_DIRECTION_STR_BOTH           "both"

#define DEVICE_AVAIL_CONDITION_STR_PULSE    "pulse"
#define DEVICE_AVAIL_CONDITION_STR_DBUS     "dbus"

#define DEVICE_ROLE_STR_NORMAL              "normal"
#define DEVICE_ROLE_STR_VOIP                "voip"
#define DEVICE_ROLE_STR_LOW_LATENCY         "low-latency"
#define DEVICE_ROLE_STR_HIGH_LATENCY        "high-latency"
#define DEVICE_ROLE_STR_UHQA                "uhqa"

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

#define DBUS_INTERFACE_DEVICED              "org.tizen.system.deviced.SysNoti"
#define DBUS_OBJECT_DEVICED                 "/Org/Tizen/System/DeviceD/SysNoti"

#define DBUS_INTERFACE_SOUND_SERVER         "org.tizen.SoundServer1"
#define DBUS_OBJECT_SOUND_SERVER            "/org/tizen/SoundServer1"

#define DEVICE_MANAGER_INTROSPECT_XML                                                       \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                                               \
    "<node>\n"                                                                              \
    " <interface name=\"" DBUS_INTERFACE_DEVICE_MANAGER "\">\n"                             \
    "  <method name=\"GetConnectedDeviceList\">\n"                                          \
    "   <arg name=\"ConnectedDeviceList\" direction=\"in\" type=\"a(iiiis)\"/>\n"           \
    "  </method>\n"                                                                         \
    "  <method name=\"LoadSink\">\n"                                                        \
    "   <arg name=\"device_type\" direction=\"in\" type=\"i\"/>\n"                          \
    "   <arg name=\"role\" direction=\"in\" type=\"i\"/>\n"                                 \
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


#define FILTER_DEVICED                  \
    "type='signal',"                    \
    " interface='" DBUS_INTERFACE_DEVICED "'"

#define FILTER_SOUND_SERVER             \
    "type='signal',"                    \
    " interface='" DBUS_INTERFACE_SOUND_SERVER "'"



/* A macro to ease iteration through all entries */
#define PA_HASHMAP_FOREACH_KEY(e, h, state, key) \
    for ((state) = NULL, (e) = pa_hashmap_iterate((h), &(state),(const void**)&(key)); (e); (e) = pa_hashmap_iterate((h), &(state), (const void**)&(key)))


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

typedef enum external_value_mirroring_type {
    MIRRORING_DISCONNECTED = 0,
    MIRRORING_CONNECTED = 1,
} external_value_mirroring_t;


/*
    Enums for represent device detected status (through dbus)
    When some device is detected, one of these values should be saved in device_status hashmap.
    audio_device_detected_type_t is needed to distinguish detected device-types ( ex. earjack which can be out or both way)
    So If you just want to know whether detected or not, can audio_device_detected_mask_t as mask.
*/
typedef enum audio_device_detected_mask_type {
    AUDIO_DEVICE_NOT_DETECTED = 0x00,
    AUDIO_DEVICE_DETECTED = 0x01,
} audio_device_detected_mask_t;

typedef enum audio_device_detected_type {
    AUDIO_DEVICE_DETECTED_BT_SCO = AUDIO_DEVICE_DETECTED,
    AUDIO_DEVICE_DETECTED_MIRRORING = AUDIO_DEVICE_DETECTED,
    AUDIO_DEVICE_DETECTED_AUDIO_JACK_BOTH_DIREC = AUDIO_DEVICE_DETECTED | 0x2,
    AUDIO_DEVICE_DETECTED_AUDIO_JACK_OUT_DIREC = AUDIO_DEVICE_DETECTED | 0x4,
} audio_device_detected_type_t;

typedef enum tizen_audio_device_class_type {
    TIZEN_AUDIO_DEVICE_CLASS_NONE,
    TIZEN_AUDIO_DEVICE_CLASS_ALSA,
    TIZEN_AUDIO_DEVICE_CLASS_BT,
    TIZEN_AUDIO_DEVICE_CLASS_NULL,
    TIZEN_AUDIO_DEVICE_CLASS_MAX,
} tizen_audio_device_class_t;


typedef enum audio_user_device_type {
    AUDIO_USER_DEVICE_NONE,
    AUDIO_USER_DEVICE_SPEAKER,
    AUDIO_USER_DEVICE_RECEIVER,
    AUDIO_USER_DEVICE_MIC,
    AUDIO_USER_DEVICE_AUDIO_JACK,
    AUDIO_USER_DEVICE_BT,
    AUDIO_USER_DEVICE_HDMI,
    AUDIO_USER_DEVICE_MIRRORING,
    AUDIO_USER_DEVICE_USB_AUDIO,
    AUDIO_USER_DEVICE_MAX
} audio_user_device_t;

typedef enum audio_device_avail_condition_type {
    AUDIO_DEVICE_AVAIL_CONDITION_NONE = 0x0,
    AUDIO_DEVICE_AVAIL_CONDITION_PULSE = 0x1,
    AUDIO_DEVICE_AVAIL_CONDITION_DBUS = 0x2,
} audio_device_avail_condition_t;



/************* structures for represent device items can be connected/disconnected */
/*
    Before beginning, There are two structure(device_item, user_device_item)                                          .
    for represent device by following reasons.
    When bt-a2dp and bt-sco are on physically same device ,it is of course same device in user-side.
    But in audio framework, those should be treated as indivisual devices for routing or some other jobs.
    So user-side representation of device is user_device_item,
    and audio-framework-side representation of device is device_item.
*/


/*
    Structure to represent user-side device. (for give information to users through sound-manager)
    This is profile known data-structure, which means it can have multiple profiles ( ex. bt-a2dp and sco)
*/
typedef struct user_device_item {
    uint32_t id;
    audio_user_device_t type;

    /* Indicate currently activated profile */
    uint32_t active_profile;
    pa_idxset *profile;
} user_device_item;

/*
    Structure to represent device. (for use internally)
    Even if both-way device(earjack, sco..) , one device_item
*/
struct device_item {
    audio_device_t type;
    char *name;
    audio_device_direction_t direction;
    audio_device_status_t state;

    /* Can get proper sink/source in hashmaps with key(=device_role) */
    pa_hashmap *playback_devices;
    pa_hashmap *capture_devices;

    /* User device belongs to */
    user_device_item *device_u;
};

/*
    Structure to save parsed information about device-file.
*/
struct device_file_map {
    /* { key:device_string -> value:device_file_prop } */
    pa_hashmap *playback;
    pa_hashmap *capture;
};

struct pa_device_manager {
    pa_core *core;
    pa_hook_slot *sink_put_hook_slot, *sink_unlink_hook_slot;
    pa_hook_slot *source_put_hook_slot, *source_unlink_hook_slot;
    pa_communicator *comm;

    /*
       Hashmap for save parsed information about device-type.
       { key:audio_device_t -> value:device_type_prop }
    */
    pa_hashmap *type_map;
    /* For save Parsed information about device-file */
    struct device_file_map *file_map;

    /* device list for internal use */
    pa_idxset *device_list;
    /* device list for user ( sound_manager )*/
    pa_idxset *user_device_list;
    /*
       Hashmap for save statuses got through dbus.
       { key:device_type -> value:(audio_detected_type_t or audio_device_detected_mask_t) }
    */
    pa_hashmap *device_status;
    pa_dbus_connection *dbus_conn;
};

/***************** structures for static information get from json *********/

/*
    Structure for informations related to some device-file(ex. 0:0)
*/
struct device_file_prop {
    /*
        For save roles which are supported on device file, and parameters.
        { key:audio_device_role_t -> value:parameters for load sink/source }
        ex) "normal"->"rate=44100 tsched=0", "uhqa"->"rate=192000 mmap=1"
    */
    pa_hashmap *roles;
    /*
        For save device-types related to device file.
        { key:audio_device_t -> value:pulse_device_prop }
    */
    pa_hashmap *device_types;
};

/* structure for represent device-types(ex. builtin-speaker) properties*/
struct device_type_prop {
    pa_bool_t builtin;
    /*
        Possible directions that this device can be.
        ex) speaker is always out, but earjack can be both or out.
    */
    audio_device_direction_t direction[DEVICE_DIRECTION_MAX];
    /*
        Conditions for make device available.
        ex) Speaker be available, only if proper pcm-device exists.
        but audio-jack is available, if pcm-device exists and got detected status.
    */
    int avail_condition[DEVICE_AVAIL_CONDITION_MAX];
    int num;
    /*
        For save supported roles and related device-file.
        { key:role -> value:device_string ]
    */
    pa_hashmap *playback_devices;
    pa_hashmap *capture_devices;
};

struct pulse_device_prop {
    /* roles related to (device_type + device_file)*/
    pa_idxset *roles;
    /* For save that this devie_type is activated or not on sink/source */
    int status;
};
/******************************************************************************/

#ifdef HAVE_DBUS

/*** Defines for method handle ***/
/* method handlers */
static void handle_get_connected_device_list(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_load_sink(DBusConnection *conn, DBusMessage *msg, void *userdata);

static void send_device_connected_signal(user_device_item *device_u, pa_bool_t connected, pa_device_manager *dm);
static void send_device_info_changed_signal(user_device_item *device_u, int changed_type, pa_device_manager *dm);

enum method_handler_index {
    METHOD_HANDLER_GET_CONNECTED_DEVICE_LIST,
    METHOD_HANDLER_LOAD_SINK,
    METHOD_HANDLER_MAX
};

static pa_dbus_method_handler method_handlers[METHOD_HANDLER_MAX] = {
    [METHOD_HANDLER_GET_CONNECTED_DEVICE_LIST] = {
        .method_name = "GetConnectedDeviceList",
        .receive_cb = handle_get_connected_device_list },
    [METHOD_HANDLER_LOAD_SINK] = {
        .method_name = "LoadSink",
        .receive_cb = handle_load_sink},
};

/*** Defines for signal send ***/

enum signal_index {
    SIGNAL_DEVICE_CONNECTED,
    SIGNAL_DEVICE_INFO_CHANGED,
    SIGNAL_MAX
};

#endif

static const char* user_device_type_to_string(audio_user_device_t user_device_type) {
    if (user_device_type <= AUDIO_USER_DEVICE_NONE || user_device_type >= AUDIO_USER_DEVICE_MAX) {
        return NULL;
    }

    switch (user_device_type) {
        case AUDIO_USER_DEVICE_SPEAKER:
            return DEVICE_TYPE_STR_SPEAKER;
        case AUDIO_USER_DEVICE_RECEIVER:
            return DEVICE_TYPE_STR_RECEIVER;
        case AUDIO_USER_DEVICE_MIC:
            return DEVICE_TYPE_STR_MIC;
        case AUDIO_USER_DEVICE_AUDIO_JACK:
            return DEVICE_TYPE_STR_AUDIO_JACK;
        case AUDIO_USER_DEVICE_BT:
            return DEVICE_TYPE_STR_BT;
        case AUDIO_USER_DEVICE_HDMI:
            return DEVICE_TYPE_STR_HDMI;
        case AUDIO_USER_DEVICE_MIRRORING:
            return DEVICE_TYPE_STR_MIRRORING;
        case AUDIO_USER_DEVICE_USB_AUDIO:
            return DEVICE_TYPE_STR_USB_AUDIO;
        default:
            return NULL;
    }
}

static const char* device_type_to_string(audio_device_t device_type) {
    if (device_type <= AUDIO_DEVICE_NONE || device_type >= AUDIO_DEVICE_MAX) {
        return NULL;
    }

    switch (device_type) {
        case AUDIO_DEVICE_SPEAKER:
            return DEVICE_TYPE_STR_SPEAKER;
        case AUDIO_DEVICE_RECEIVER:
            return DEVICE_TYPE_STR_RECEIVER;
        case AUDIO_DEVICE_MIC:
            return DEVICE_TYPE_STR_MIC;
        case AUDIO_DEVICE_AUDIO_JACK:
            return DEVICE_TYPE_STR_AUDIO_JACK;
        case AUDIO_DEVICE_BT_A2DP:
            return DEVICE_TYPE_STR_BT_A2DP;
        case AUDIO_DEVICE_BT_SCO:
            return DEVICE_TYPE_STR_BT_SCO;
        case AUDIO_DEVICE_HDMI:
            return DEVICE_TYPE_STR_HDMI;
        case AUDIO_DEVICE_MIRRORING:
            return DEVICE_TYPE_STR_MIRRORING;
        case AUDIO_DEVICE_USB_AUDIO:
            return DEVICE_TYPE_STR_USB_AUDIO;
        default:
            return NULL;
    }
}

static audio_device_t device_type_to_int(const char *device_str) {
    if (!device_str) {
        return -1;
    }

    if (!strcmp(device_str, DEVICE_TYPE_STR_SPEAKER)) {
        return AUDIO_DEVICE_SPEAKER;
    } else if (!strcmp(device_str, DEVICE_TYPE_STR_RECEIVER)) {
        return AUDIO_DEVICE_RECEIVER;
    } else if (!strcmp(device_str, DEVICE_TYPE_STR_MIC)) {
        return AUDIO_DEVICE_MIC;
    } else if (!strcmp(device_str, DEVICE_TYPE_STR_AUDIO_JACK)) {
        return AUDIO_DEVICE_AUDIO_JACK;
    } else if (!strcmp(device_str, DEVICE_TYPE_STR_BT_A2DP)) {
        return AUDIO_DEVICE_BT_A2DP;
    } else if (!strcmp(device_str, DEVICE_TYPE_STR_BT_SCO)) {
        return AUDIO_DEVICE_BT_SCO;
    } else if (!strcmp(device_str, DEVICE_TYPE_STR_HDMI)) {
        return AUDIO_DEVICE_HDMI;
    } else if (!strcmp(device_str, DEVICE_TYPE_STR_MIRRORING)) {
        return AUDIO_DEVICE_MIRRORING;
    } else if (!strcmp(device_str, DEVICE_TYPE_STR_USB_AUDIO)) {
        return AUDIO_DEVICE_USB_AUDIO;
    } else {
        return -1;
    }
}

static audio_user_device_t device_type_to_user_device_type(audio_device_t device_type) {
    switch (device_type) {
        case AUDIO_DEVICE_SPEAKER:
            return AUDIO_USER_DEVICE_SPEAKER;
        case AUDIO_DEVICE_RECEIVER:
            return AUDIO_USER_DEVICE_RECEIVER;
        case AUDIO_DEVICE_MIC:
            return AUDIO_USER_DEVICE_MIC;
        case AUDIO_DEVICE_AUDIO_JACK:
            return AUDIO_USER_DEVICE_AUDIO_JACK;
        case AUDIO_DEVICE_BT_A2DP:
            return AUDIO_USER_DEVICE_BT;
        case AUDIO_DEVICE_BT_SCO:
            return AUDIO_USER_DEVICE_BT;
        case AUDIO_DEVICE_HDMI:
            return AUDIO_USER_DEVICE_HDMI;
        case AUDIO_DEVICE_MIRRORING:
            return AUDIO_USER_DEVICE_MIRRORING;
        case AUDIO_DEVICE_USB_AUDIO:
            return AUDIO_USER_DEVICE_USB_AUDIO;
        default:
            return AUDIO_USER_DEVICE_NONE;
    }
}

static const char* device_direction_to_string(audio_device_direction_t direction) {
    if (direction <= AUDIO_DEVICE_DIRECTION_NONE || direction > AUDIO_DEVICE_DIRECTION_BOTH) {
        return NULL;
    }

    switch (direction) {
        case AUDIO_DEVICE_DIRECTION_NONE:
            return DEVICE_DIRECTION_STR_NONE;
        case AUDIO_DEVICE_DIRECTION_OUT:
            return DEVICE_DIRECTION_STR_OUT;
        case AUDIO_DEVICE_DIRECTION_IN:
            return DEVICE_DIRECTION_STR_IN;
        case AUDIO_DEVICE_DIRECTION_BOTH:
            return DEVICE_DIRECTION_STR_BOTH;
        default:
            return NULL;
    }
}


static audio_device_direction_t device_direction_to_int(const char *device_direction) {
    if (!device_direction) {
        return -1;
    }

    if (!strcmp(device_direction, DEVICE_DIRECTION_STR_NONE)) {
        return AUDIO_DEVICE_DIRECTION_NONE;
    } else if (!strcmp(device_direction, DEVICE_DIRECTION_STR_OUT)) {
        return AUDIO_DEVICE_DIRECTION_OUT;
    } else if (!strcmp(device_direction, DEVICE_DIRECTION_STR_IN)) {
        return AUDIO_DEVICE_DIRECTION_IN;
    } else if (!strcmp(device_direction, DEVICE_DIRECTION_STR_BOTH)) {
        return AUDIO_DEVICE_DIRECTION_BOTH;
    } else {
        return -1;
    }
}

static int device_avail_cond_to_int(const char *avail_cond) {
    if (!avail_cond) {
        return -1;
    }

    if (!strcmp(avail_cond, DEVICE_AVAIL_CONDITION_STR_PULSE)) {
        return AUDIO_DEVICE_AVAIL_CONDITION_PULSE;
    } else if (!strcmp(avail_cond, DEVICE_AVAIL_CONDITION_STR_DBUS)) {
        return AUDIO_DEVICE_AVAIL_CONDITION_DBUS;
    } else {
        return -1;
    }
}
static const char* device_role_to_string(audio_device_role_t device_role) {
    if (device_role < AUDIO_DEVICE_ROLE_NORMAL || device_role >= AUDIO_DEVICE_ROLE_MAX) {
        return NULL;
    }

    switch (device_role) {
        case AUDIO_DEVICE_ROLE_NORMAL:
            return DEVICE_ROLE_STR_NORMAL;
        case AUDIO_DEVICE_ROLE_VOIP:
            return DEVICE_ROLE_STR_VOIP;
        case AUDIO_DEVICE_ROLE_LOW_LATENCY:
            return DEVICE_ROLE_STR_LOW_LATENCY;
        case AUDIO_DEVICE_ROLE_HIGH_LATENCY:
            return DEVICE_ROLE_STR_HIGH_LATENCY;
        case AUDIO_DEVICE_ROLE_UHQA:
            return DEVICE_ROLE_STR_UHQA;
        default:
            return NULL;
    }
}

static audio_device_role_t device_role_to_int(const char *device_role) {
    if (!device_role) {
        return -1;
    }

    if (!strcmp(device_role, DEVICE_ROLE_STR_NORMAL)) {
        return AUDIO_DEVICE_ROLE_NORMAL;
    } else if (!strcmp(device_role, DEVICE_ROLE_STR_VOIP)) {
        return AUDIO_DEVICE_ROLE_VOIP;
    } else if (!strcmp(device_role, DEVICE_ROLE_STR_LOW_LATENCY)) {
        return AUDIO_DEVICE_ROLE_LOW_LATENCY;
    } else if (!strcmp(device_role, DEVICE_ROLE_STR_HIGH_LATENCY)) {
        return AUDIO_DEVICE_ROLE_HIGH_LATENCY;
    } else if (!strcmp(device_role, DEVICE_ROLE_STR_UHQA)) {
        return AUDIO_DEVICE_ROLE_UHQA;
    } else {
        return -1;
    }
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


static pa_bool_t sink_is_null(pa_sink *sink) {
    if (!sink)
        return FALSE;
    return !strcmp(sink->module->name, "module-null-sink");
}

static pa_bool_t source_is_null(pa_source *source) {
    if (!source)
        return FALSE;
    return !strcmp(source->module->name, "module-null-source");
}

static const char* device_class_to_string(tizen_audio_device_class_t device_class) {
    if (device_class == TIZEN_AUDIO_DEVICE_CLASS_ALSA) {
        return "alsa";
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_BT) {
        return "bt";
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NULL) {
        return "null";
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NONE) {
        return "none";
    } else {
        return NULL;
    }
}

static tizen_audio_device_class_t device_string_get_class(const char *device_string) {
    if (!device_string) {
        return TIZEN_AUDIO_DEVICE_CLASS_NONE;
    }

    if (device_string == strstr(device_string, "alsa")) {
        return TIZEN_AUDIO_DEVICE_CLASS_ALSA;
    } else if (device_string == strstr(device_string, "null")) {
        return TIZEN_AUDIO_DEVICE_CLASS_NULL;
    } else {
        return TIZEN_AUDIO_DEVICE_CLASS_NONE;
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

static tizen_audio_device_class_t sink_get_class(pa_sink *sink) {
    if (sink_is_null(sink)) {
        return TIZEN_AUDIO_DEVICE_CLASS_NULL;
    } else if (pulse_device_is_alsa(sink->proplist)) {
        return TIZEN_AUDIO_DEVICE_CLASS_ALSA;
    } else if (pulse_device_is_bluez(sink->proplist)) {
        return TIZEN_AUDIO_DEVICE_CLASS_BT;
    } else {
        return TIZEN_AUDIO_DEVICE_CLASS_NONE;
    }
}

static tizen_audio_device_class_t source_get_class(pa_source *source) {
    if (source_is_null(source)) {
        return TIZEN_AUDIO_DEVICE_CLASS_NULL;
    } else if (pulse_device_is_alsa(source->proplist)) {
        return TIZEN_AUDIO_DEVICE_CLASS_ALSA;
    } else if (pulse_device_is_bluez(source->proplist)) {
        return TIZEN_AUDIO_DEVICE_CLASS_BT;
    } else {
        return TIZEN_AUDIO_DEVICE_CLASS_NONE;
    }
}

static const char* device_class_get_module_name(tizen_audio_device_class_t device_class, pa_bool_t is_playback) {
    if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NONE) {
        return NULL;
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_ALSA) {
        return is_playback ? "module-alsa-sink" : "module-alsa-source";
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_BT) {
        return is_playback ? "module-bluez5-device" : NULL;
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NULL) {
        return is_playback ? "module-null-sink" : "module-null-source";
    } else {
        return NULL;
    }
}

static void dump_playback_device_list(pa_hashmap *playback_devices) {
    pa_sink *sink = NULL;
    void *state = NULL;
    audio_device_role_t role = AUDIO_DEVICE_ROLE_NONE;

    if (!playback_devices) {
        return ;
    }

    pa_log_debug("    playback device list");
    PA_HASHMAP_FOREACH_KEY(sink, playback_devices, state, role) {
        pa_log_debug("        %-13s -> %s", device_role_to_string(role), sink->name);
    }
}

static void dump_capture_device_list(pa_hashmap *capture_devices) {
    pa_source *source= NULL;
    void *state = NULL;
    audio_device_role_t role = AUDIO_DEVICE_ROLE_NONE;

    if (!capture_devices) {
        return ;
    }

    pa_log_debug("    capture device list");
    PA_HASHMAP_FOREACH_KEY(source, capture_devices, state, role) {
        pa_log_debug("        %-13s -> %s", device_role_to_string(role), source->name);
    }
}

static void dump_device_info(device_item *device) {
    if (!device)
        return;

    pa_log_debug("    type      : %s", device_type_to_string(device->type));
    pa_log_debug("    direction : %s", device_direction_to_string(device->direction));
    pa_log_debug("    activated : %s", device->state == AUDIO_DEVICE_STATE_ACTIVATED ? "activated" : "not activated");
    dump_playback_device_list(device->playback_devices);
    dump_capture_device_list(device->capture_devices);
}

static void dump_device_list(pa_device_manager *dm) {
    device_item *device = NULL;
    uint32_t device_idx = 0;

    if (!dm || !dm->device_list) {
        return;
    }

    pa_log_debug("====== Device List Dump ======");
    PA_IDXSET_FOREACH(device, dm->device_list, device_idx) {
        pa_log_debug("    [ Device #%u ]", device_idx);
        dump_device_info(device);
    }
    pa_log_debug("==============================");
}

static void dump_user_device_info(user_device_item *device_u) {
    device_item *device = NULL;
    uint32_t device_idx = 0;

    if (!device_u)
        return;
    if (!device_u->profile) {
        pa_log_warn("empty user device");
        return;
    }

    pa_log_debug("  id             : %u", device_u->id);
    pa_log_debug("  type           : %s", user_device_type_to_string(device_u->type));
    pa_log_debug("  active-profile : %u", device_u->active_profile);
    PA_IDXSET_FOREACH(device, device_u->profile, device_idx) {
        pa_log_debug("  (Profile #%u)", device_idx);
        dump_device_info(device);
    }
}

static void dump_user_device_list(pa_device_manager *dm) {
    user_device_item *device_u = NULL;
    uint32_t device_idx = 0;

    if (!dm || !dm->user_device_list) {
        return;
    }

    pa_log_debug("====== User Device List Dump ======");
    PA_IDXSET_FOREACH(device_u, dm->user_device_list, device_idx) {
        pa_log_debug("[ User Device #%d ]", device_idx);
        dump_user_device_info(device_u);
    }
    pa_log_debug("===================================");
}

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

int device_id_max_g;

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

static pa_source* get_sibling_source(pa_sink *sink) {
    const char *sink_sysfs_path, *source_sysfs_path;
    pa_source *source;
    uint32_t source_idx;

    pa_assert(sink);
    pa_assert(sink->proplist);

    if (!(sink_sysfs_path = pa_proplist_gets(sink->proplist, "sysfs.path"))) {
        pa_log_warn("No sysfs.path for sink '%s'", sink->name);
        return NULL;
    }

    PA_IDXSET_FOREACH(source, sink->core->sources, source_idx) {
        if (!pulse_device_class_is_sound(source->proplist))
            continue;
        source_sysfs_path = pa_proplist_gets(source->proplist, "sysfs.path");
        if (source_sysfs_path && !strcmp(source_sysfs_path, sink_sysfs_path)) {
            return source;
        }
    }

    return NULL;
}

static pa_sink* get_sibling_sink(pa_source *source) {
    const char *sink_sysfs_path, *source_sysfs_path;
    pa_sink *sink;
    uint32_t sink_idx;

    pa_assert(source);
    pa_assert(source->proplist);

    if (!(source_sysfs_path = pa_proplist_gets(source->proplist, "sysfs.path"))) {
        pa_log_warn("No sysfs.path for source '%s'", source->name);
        return NULL;
    }

    PA_IDXSET_FOREACH(sink, source->core->sources, sink_idx) {
        if (!pulse_device_class_is_sound(sink->proplist))
            continue;
        sink_sysfs_path = pa_proplist_gets(sink->proplist, "sysfs.path");
        if (sink_sysfs_path && !strcmp(source_sysfs_path, sink_sysfs_path)) {
            return sink;
        }
    }

    return NULL;
}


static int pulse_device_get_alsa_card_device_num(pa_proplist *prop, int *card, int *device) {
    int alsa_card_i = 0, alsa_device_i = 0;
    const char *alsa_card = NULL, *alsa_device = NULL;

    if (!prop || !card || !device) {
        pa_log_error("Invalid Parameter");
        return -1;
    }

    alsa_card = pa_proplist_gets(prop, "alsa.card");
    alsa_device = pa_proplist_gets(prop, "alsa.device");

    if (alsa_card && alsa_device) {
        pa_atoi(alsa_card, &alsa_card_i);
        pa_atoi(alsa_device, &alsa_device_i);
        *card = alsa_card_i;
        *device = alsa_device_i;
    } else {
        return -1;
    }
    return 0;
}

static const char* build_params_to_load_device(const char *device_string, const char *params, tizen_audio_device_class_t device_class) {
    pa_strbuf *args_buf;
    static char args[DEVICE_PARAM_STRING_MAX] = {0,};

    if (!device_string) {
        pa_log_error("device string null");
        return NULL;
    }

    if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NULL) {
        return params;
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_ALSA) {
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


static const char* device_params_get_alsa_device_removed_string(const char *params) {
    static char removed_param[DEVICE_PARAM_STRING_MAX] = {0,};
    char *device_string_p = NULL;
    char *next_p = NULL;
    const char *params_p = params;
    char *end_p = NULL;
    int len = 0, prev_len = 0;

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


/* TODO : parse parameters and compare each parameter*/
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
        if (!value1 || !value2 || strcmp(value1, value2)) {
            ret = 1;
            goto end;
        }
    }

    for (state = NULL, key = pa_modargs_iterate(modargs2, &state); key; key = pa_modargs_iterate(modargs2, &state)) {
        value1 = pa_modargs_get_value(modargs1, key, NULL);
        value2 = pa_modargs_get_value(modargs2, key, NULL);
        if (!value1 || !value2 || strcmp(value1, value2)) {
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

static int compare_device_params_with_module_args(const char *params, const char *module_args) {
    const char *removed_module_args;

    if (!params && !module_args)
        return 0;
    if (!params || !module_args)
        return -1;

    removed_module_args = device_params_get_alsa_device_removed_string(module_args);
    return compare_device_params(params, removed_module_args);
}

static const char* sink_get_device_string(pa_sink *sink) {
    int card = 0, device = 0;
    tizen_audio_device_class_t device_class;
    static char device_string[DEVICE_STRING_MAX] = {0,};

    device_class = sink_get_class(sink);

    if (device_class == TIZEN_AUDIO_DEVICE_CLASS_ALSA) {
        if (pulse_device_get_alsa_card_device_num(sink->proplist, &card, &device) < 0)
            return NULL;
        snprintf(device_string, DEVICE_STRING_MAX, "alsa:%d,%d", card, device);
        return device_string;
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NULL) {
        return "null";
    } else {
        return device_string;
    }
}

static const char* source_get_device_string(pa_source *source) {
    int card = 0, device = 0;
    tizen_audio_device_class_t device_class;
    static char device_string[DEVICE_STRING_MAX] = {0,};

    device_class = source_get_class(source);

    if (device_class == TIZEN_AUDIO_DEVICE_CLASS_ALSA) {
        if (pulse_device_get_alsa_card_device_num(source->proplist, &card, &device) < 0)
            return NULL;
        snprintf(device_string, DEVICE_STRING_MAX, "alsa:%d,%d", card, device);
        return device_string;
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NULL) {
        return "null";
    } else {
        return device_string;
    }
}

static pa_bool_t sink_same_device_string(pa_sink *sink, const char *device_string) {
    const char *sink_device_string;

    if (!sink || !device_string) {
        return FALSE;
    }

    if (!(sink_device_string = sink_get_device_string(sink))) {
        return FALSE;
    }

    return !strcmp(sink_device_string, device_string);
}

static pa_bool_t source_same_device_string(pa_source *source, const char *device_string) {
    const char *source_device_string;

    if (!source || !device_string) {
        return FALSE;
    }

    if (!(source_device_string = source_get_device_string(source))) {
        return FALSE;
    }

    return !strcmp(source_device_string, device_string);
}

static user_device_item* build_user_device_item(audio_user_device_t device_type_u) {
    user_device_item *device_u = NULL;

    pa_log_debug("Build User device item for %s", user_device_type_to_string(device_type_u));

    device_u = (user_device_item *)pa_xmalloc(sizeof(user_device_item));
    device_u->id = device_id_max_g++;
    device_u->type = device_type_u;
    device_u->active_profile = -1;
    device_u->profile = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    return device_u;
}

static void destroy_user_device_item(user_device_item *device_u) {
    if (!device_u) {
        return;
    }

    pa_log_debug("Destroy User device item which of type is %s", user_device_type_to_string(device_u->type));

    if (device_u->profile) {
        pa_idxset_free(device_u->profile, NULL);
    }

    pa_xfree(device_u);
}

static user_device_item* user_device_item_add_profile(user_device_item *device_u, device_item *device, uint32_t *idx) {
    if (!device_u || !device_u->profile || !device) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    pa_idxset_put(device_u->profile, device, idx);
    return device_u;
}


static user_device_item* user_device_item_remove_profile(user_device_item *device_u, device_item *device) {
    if (!device_u || !device_u->profile || !device) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    pa_idxset_remove_by_data(device_u->profile, device, NULL);
    return device_u;
}

static user_device_item* user_device_item_set_active_profile(user_device_item *device_u, uint32_t idx) {
    if (!device_u || !device_u->profile ) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    device_u->active_profile = idx;
    return device_u;
}

static device_item* build_device_item(audio_device_t device_type, const char *name, audio_device_direction_t direction) {
    device_item *device = NULL;

    if (device_type <= AUDIO_DEVICE_NONE || device_type >= AUDIO_DEVICE_MAX) {
        pa_log_error("Wrong device type");
        return NULL;
    }
    if (direction < AUDIO_DEVICE_DIRECTION_IN || direction > AUDIO_DEVICE_DIRECTION_BOTH) {
        pa_log_error("Wrong device direction");
        return NULL;
    }

    pa_log_debug("Build device item for type:%s, name:%s, direction:%s", device_type_to_string(device_type), name, device_direction_to_string(direction));

    if (!(device = (device_item *)pa_xmalloc(sizeof(device_item)))) {
        pa_log_error("Cannot alloc for device item");
        return NULL;
    }
    device->type = device_type;
    device->direction = direction;
    device->state = AUDIO_DEVICE_STATE_DEACTIVATED;
    device->playback_devices = NULL;
    device->capture_devices = NULL;

    if (name) {
        device->name = strdup(name);
    } else {
        device->name = strdup(device_type_to_string(device_type));
    }

    if (direction == AUDIO_DEVICE_DIRECTION_OUT) {
        device->playback_devices = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    } else if (direction == AUDIO_DEVICE_DIRECTION_IN) {
        device->capture_devices = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    } else {
        device->playback_devices = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
        device->capture_devices = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    }

    return device;
}

static void destroy_device_item(device_item *device) {
    if (!device) {
        return;
    }

    pa_log_debug("Destroy device item which of type is %s", device_type_to_string(device->type));

    pa_xfree(device->name);

    if (device->playback_devices) {
        pa_hashmap_free(device->playback_devices);
    }
    if (device->capture_devices) {
        pa_hashmap_free(device->capture_devices);
    }
    pa_xfree(device);
}

static user_device_item* device_item_get_correspondent_user_deivce_item(pa_idxset *user_device_list, device_item *device) {
    user_device_item *device_u;
    uint32_t user_device_idx;
    audio_user_device_t device_type_u;

    pa_assert(user_device_list);
    pa_assert(device);

    /* TODO : Consider multi-device */
    device_type_u = device_type_to_user_device_type(device->type);
    PA_IDXSET_FOREACH(device_u, user_device_list, user_device_idx) {
        if (device_u->type == device_type_u) {
            if (device_type_u != AUDIO_USER_DEVICE_BT) {
                pa_log_warn("Weird case, This should happen only on bt device, but this is %s", user_device_type_to_string(device_type_u));
                return NULL;
            }
            return device_u;
        }
    }

    return NULL;
}

static void device_item_update_direction(device_item *device) {
    int prev_direction;
    pa_bool_t playback_exist = FALSE, capture_exist = FALSE;

    if (!device)
        return;

    prev_direction = device->direction;

    if (device->playback_devices) {
        if (pa_hashmap_size(device->playback_devices) > 0) {
            playback_exist = TRUE;
        }
    }
    if (device->capture_devices) {
        if (pa_hashmap_size(device->capture_devices) > 0) {
            capture_exist = TRUE;
        }
    }

    if (playback_exist && capture_exist) {
        device->direction = AUDIO_DEVICE_DIRECTION_BOTH;
    } else if (playback_exist) {
        device->direction = AUDIO_DEVICE_DIRECTION_OUT;
    } else if (capture_exist) {
        device->direction = AUDIO_DEVICE_DIRECTION_IN;
    } else {
        device->direction = AUDIO_DEVICE_DIRECTION_NONE;
    }

    pa_log_debug("%s, direction updated '%s'->'%s'", device->name, device_direction_to_string(prev_direction), device_direction_to_string(device->direction));
}

static device_item* device_item_add_sink(device_item *device, audio_device_role_t role, pa_sink *sink) {
    if (!device ||(role < AUDIO_DEVICE_ROLE_NORMAL || role >= AUDIO_DEVICE_ROLE_MAX) || !sink) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    if (!(device->playback_devices))
        device->playback_devices = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    pa_hashmap_put(device->playback_devices, PA_INT_TO_PTR(role), sink);

    return device;
}

static device_item* device_item_remove_sink(device_item *device, audio_device_role_t role) {
    if (!device || !(device->playback_devices) ||(role < AUDIO_DEVICE_ROLE_NORMAL || role >= AUDIO_DEVICE_ROLE_MAX)) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }
    pa_hashmap_remove(device->playback_devices, PA_INT_TO_PTR(role));
    return device;
}

static device_item* device_item_add_source(device_item *device, audio_device_role_t role, pa_source *source) {
    if (!device || (role < AUDIO_DEVICE_ROLE_NORMAL || role >= AUDIO_DEVICE_ROLE_MAX) || !source) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }

    if (!(device->capture_devices))
        device->capture_devices = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    pa_hashmap_put(device->capture_devices, PA_INT_TO_PTR(role), source);
    return device;
}


static device_item* device_item_remove_source(device_item *device, audio_device_role_t role) {
    if (!device || !(device->capture_devices) ||(role < AUDIO_DEVICE_ROLE_NORMAL || role >= AUDIO_DEVICE_ROLE_MAX)) {
        pa_log_error("Invalid Parameter");
        return NULL;
    }
    pa_hashmap_remove(device->capture_devices, PA_INT_TO_PTR(role));
    return device;
}

static int user_device_list_add_device(pa_idxset *user_device_list, user_device_item *device_u, pa_device_manager *dm) {
    pa_assert(user_device_list);
    pa_assert(device_u);

    if (pa_idxset_put(user_device_list, device_u, NULL) < 0)
        return -1;

    /* only if new user device */
    send_device_connected_signal(device_u, TRUE, dm);
    return 0;
}


static int user_device_list_remove_device(pa_idxset *user_device_list, user_device_item *device_u, pa_device_manager *dm) {
    pa_assert(user_device_list);
    pa_assert(device_u);

    if (!pa_idxset_remove_by_data(user_device_list, device_u, NULL))
        return -1;

    send_device_connected_signal(device_u, FALSE, dm);
    return 0;
}


static int device_list_add_device(pa_idxset *device_list, device_item *device, pa_device_manager *dm) {
    user_device_item *device_u;
    uint32_t active_profile;
    pa_device_manager_hook_data_for_conn_changed conn_changed_info;

    pa_assert(device_list);
    pa_assert(device);

    if (pa_idxset_put(device_list, device, NULL) < 0)
        return -1;

    if (!(device_u = device_item_get_correspondent_user_deivce_item(dm->user_device_list, device))) {
        device_u = build_user_device_item(device_type_to_user_device_type(device->type));
        user_device_item_add_profile(device_u, device, &active_profile);
        user_device_item_set_active_profile(device_u, active_profile);
    } else {
        user_device_item_add_profile(device_u, device, NULL);
    }
    device->device_u = device_u;
    user_device_list_add_device(dm->user_device_list, device_u, dm);

    pa_log_debug("Notify Device connected");

    conn_changed_info.is_connected = TRUE;
    conn_changed_info.device = device;
    pa_hook_fire(pa_communicator_hook(dm->comm, PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED), &conn_changed_info);

    return 0;
}

static int device_list_remove_device(pa_idxset *device_list, device_item *device, pa_device_manager *dm) {
    user_device_item *device_u;
    uint32_t active_profile = 0, prev_active_profile;
    pa_device_manager_hook_data_for_conn_changed conn_changed_info;

    pa_assert(device_list);
    pa_assert(device);

    if (!pa_idxset_remove_by_data(device_list, device, NULL))
        return -1;

    device_u = device->device_u;
    prev_active_profile = device_u->active_profile;
    if (pa_idxset_size(device_u->profile) == 1) {
        user_device_list_remove_device(dm->user_device_list, device_u, dm);
        destroy_user_device_item(device_u);
    } else {
        user_device_item_remove_profile(device_u, device);
        device = pa_idxset_first(device_u->profile, &active_profile);
        if (prev_active_profile != active_profile) {
            user_device_item_set_active_profile(device_u, active_profile);
            if (device->direction != device->direction) {
                send_device_info_changed_signal(device_u, AUDIO_DEVICE_CHANGED_INFO_IO_DIRECTION, dm);
            }
        }
    }

    pa_log_debug("Notify Device disconnected");

    conn_changed_info.is_connected = FALSE;
    conn_changed_info.device = device;
    pa_hook_fire(pa_communicator_hook(dm->comm, PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED), &conn_changed_info);

    destroy_device_item(device);

    return 0;
}

static int device_type_get_direction(pa_device_manager *dm, audio_device_t device_type) {
    pa_hashmap *device_type_map = NULL;
    struct device_type_prop *type_item = NULL;
    audio_device_direction_t direction = 0, d_num = 0, d_idx = 0, correct_d_idx = 0;
    int device_status;

    if (!dm) {
        pa_log_error("Invalid Parameter");
        return -1;
    }


    device_type_map = dm->type_map;
    type_item = pa_hashmap_get(device_type_map, PA_INT_TO_PTR(device_type));

    for (d_idx = 0; d_idx < DEVICE_DIRECTION_MAX; d_idx++) {
        if (type_item->direction[d_idx] != AUDIO_DEVICE_DIRECTION_NONE) {
            correct_d_idx = d_idx;
            d_num++;
        }
    }

    if (d_num == 1) {
        direction = type_item->direction[correct_d_idx];
    } else {
        /* Actually, only 'audio-jack' should come here */
        if (device_type == AUDIO_DEVICE_AUDIO_JACK) {
            device_status = (int) pa_hashmap_get(dm->device_status, PA_INT_TO_PTR(device_type));
            if (device_status == AUDIO_DEVICE_DETECTED_AUDIO_JACK_BOTH_DIREC) {
                direction = AUDIO_DEVICE_DIRECTION_BOTH;
            } else if (device_status == AUDIO_DEVICE_DETECTED_AUDIO_JACK_OUT_DIREC) {
                direction = AUDIO_DEVICE_DIRECTION_OUT;
            } else {
                pa_log_debug("Cannot get audio jack device direction");
                return -1;
            }
        } else {
            pa_log_error("Weird case, '%s' is not expected to have multiple direction", device_type_to_string(device_type));
            return -1;
        }
    }

    pa_log_debug("Direction of '%s' = '%s'", device_type_to_string(device_type), device_direction_to_string(direction));
    return direction;
}

/*
static void device_item_remove(pa_idxset *device_list, device_item *device)
{
    uint32_t device_idx = 0;

    pa_idxset_remove_by_data(device_list, device, &device_idx);
    destroy_device_item(device);
}
*/
static device_item* handle_unknown_sink(pa_device_manager *dm, pa_sink *sink) {
    device_item *device = NULL;
    audio_device_t device_type;
    const char *device_name;
    pa_source *sibling_source;

    if (!sink) {
        pa_log_error("Invalide Parameter");
        return NULL;
    }

    pa_log_debug("handle_unknown_sink");

    if (pulse_device_is_alsa(sink->proplist)) {
        if (pulse_device_is_usb(sink->proplist)) {
            device_type = AUDIO_DEVICE_USB_AUDIO;
            device_name = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_SERIAL);
        } else {
            pa_log_warn("This sink is alsa device, but not usb. really unknown sink");
            return NULL;
        }
    } else if (pulse_device_is_bluez(sink->proplist)) {
        device_type = AUDIO_DEVICE_BT_A2DP;
        device_name = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_DESCRIPTION);
    } else {
        pa_log_warn("Invalid sink type, neither alsa nor bluez");
        goto failed;
    }

    if (device_type == AUDIO_DEVICE_USB_AUDIO) {
        if ((sibling_source = get_sibling_source(sink))) {
            if (sibling_source->device_item) {
                device = (device_item*) sibling_source->device_item;
                if (!(device_item_add_sink(device, AUDIO_DEVICE_ROLE_NORMAL, sink))) {
                    pa_log_error("failed to add sink beside sibling source");
                    goto failed;
                }
                return device;
            }
        }
    }

    pa_log_debug("Create device item %s", device_type_to_string(device_type));
    if (!(device= build_device_item(device_type, device_name, AUDIO_DEVICE_DIRECTION_OUT))) {
        pa_log_error("Build device item failed");
        goto failed;
    }

    if (!(device_item_add_sink(device, AUDIO_DEVICE_ROLE_NORMAL, sink))) {
        pa_log_error("failed to add sink");
        goto failed;
    }

    device_item_update_direction(device);

    sink->device_item = device;
    if (device_list_add_device(dm->device_list, device, dm) < 0 ) {
        pa_log_error("failed to put device into device_list");
        goto failed;
    }

    return device;

failed:
    pa_log_error("Failed to handle external sink");
    if (device)
        pa_xfree(device);
    sink->device_item = NULL;

    return NULL;
}


static device_item* handle_unknown_source(pa_device_manager *dm, pa_source *source) {
    device_item *device= NULL;
    audio_device_t device_type = 0;
    const char *device_name;
    pa_sink *sibling_sink;

    if (!source) {
        pa_log_error("Invalide Parameter");
        return NULL;
    }

    pa_log_debug("handle_unknown_source");

    if (pulse_device_is_alsa(source->proplist)) {
        if (pulse_device_is_usb(source->proplist)) {
            device_type = AUDIO_DEVICE_USB_AUDIO;
            device_name = pa_proplist_gets(source->proplist, PA_PROP_DEVICE_SERIAL);
        } else {
            pa_log_warn("This source is alsa device, but not usb. really unknown source");
            return NULL;
        }
    } else if (pulse_device_is_bluez(source->proplist)) {
        device_type = AUDIO_DEVICE_BT_A2DP;
        device_name = pa_proplist_gets(source->proplist, PA_PROP_DEVICE_DESCRIPTION);
    } else {
        pa_log_warn("Invalid source type, neither alsa nor bluez");
        goto failed;
    }

    if (device_type == AUDIO_DEVICE_USB_AUDIO) {
        if ((sibling_sink = get_sibling_sink(source))) {
            if (sibling_sink->device_item) {
                device = (device_item*) sibling_sink->device_item;
                if (!(device_item_add_source(device, AUDIO_DEVICE_ROLE_NORMAL, source))) {
                    pa_log_error("failed to add source beside sibling sink");
                    goto failed;
                }
                return device;
            }
        }
    }

    pa_log_debug("Create device item %s", device_type_to_string(device_type));
    if (!(device= build_device_item(device_type, device_name, AUDIO_DEVICE_DIRECTION_IN))) {
        pa_log_error("Build device item failed");
        goto failed;
    }

    if (!(device_item_add_source(device, AUDIO_DEVICE_ROLE_NORMAL, source))) {
        pa_log_error("failed to add sink");
        goto failed;
    }

    device_item_update_direction(device);

    source->device_item = device;
    if (device_list_add_device(dm->device_list, device, dm) < 0) {
        pa_log_error("failed to put device into device_list");
        goto failed;
    }

    return device;

failed:
    pa_log_error("Failed to handle external source");
    if (device)
        pa_xfree(device);
    source->device_item = NULL;

    return NULL;
}

static pa_bool_t device_playback_file_loaded(pa_core *core, const char *device_string) {
    pa_sink *sink;
    uint32_t sink_idx;

    pa_assert(core);
    pa_assert(device_string);

    PA_IDXSET_FOREACH(sink, core->sinks, sink_idx) {
        if (!strcmp(device_string, sink_get_device_string(sink))) {
            return TRUE;
        }
    }
    return FALSE;
}

static pa_bool_t device_capture_file_loaded(pa_core *core, const char *device_string) {
    pa_source *source;
    uint32_t source_idx;

    pa_assert(core);
    pa_assert(device_string);

    PA_IDXSET_FOREACH(source, core->sources, source_idx) {
        if (!strcmp(device_string, source_get_device_string(source))) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Check for pcm devices associated with device-type are loaded */
static pa_bool_t device_type_ready(audio_device_t device_type, audio_device_direction_t direction, pa_device_manager *dm) {
    struct device_type_prop *type_item = NULL;
    char *device_string = NULL;
    void *state = NULL;
    audio_device_role_t role = AUDIO_DEVICE_ROLE_NONE;

    pa_assert(dm);
    pa_assert(dm->type_map);

    if (!(type_item = pa_hashmap_get(dm->type_map, PA_INT_TO_PTR(device_type)))) {
        pa_log_error("No type-map for '%s'", device_type_to_string(device_type));
        return FALSE;
    }

    if (direction == AUDIO_DEVICE_DIRECTION_OUT || direction == AUDIO_DEVICE_DIRECTION_BOTH) {
        if (!(type_item->playback_devices)) {
            pa_log_warn("Not ready, no playback_devices");
            return FALSE;
        }
        PA_HASHMAP_FOREACH_KEY(device_string, type_item->playback_devices, state, role) {
            if (!device_playback_file_loaded(dm->core, device_string)) {
                pa_log_warn("Not ready, No sink for '%s:%s' ", device_string, device_role_to_string(role));
                return FALSE;
            }
        }
    }
    if (direction == AUDIO_DEVICE_DIRECTION_IN || direction == AUDIO_DEVICE_DIRECTION_BOTH) {
        if (!(type_item->capture_devices)) {
            pa_log_warn("Not ready, no capture_devices");
            return FALSE;
        }
        PA_HASHMAP_FOREACH_KEY(device_string, type_item->capture_devices, state, role) {
            if (!device_capture_file_loaded(dm->core, device_string)) {
                pa_log_warn("Not ready, No source for '%s:%s' ", device_string, device_role_to_string(role));
                return FALSE;
            }
        }
    }

    return TRUE;
}

static device_item* device_list_get_device(pa_idxset *device_list, audio_device_t device_type) {
    device_item *device= NULL;
    uint32_t device_idx = 0;

    PA_IDXSET_FOREACH(device, device_list, device_idx) {
        if (device->type == device_type) {
            return device;
        }
    }
    return NULL;
}

static device_item* handle_device_type_loaded(audio_device_t device_type, audio_device_direction_t direction, pa_device_manager *dm) {
    struct device_type_prop *type_item;
    struct device_file_prop *file_item;
    device_item *device = NULL;
    audio_device_role_t role = AUDIO_DEVICE_ROLE_NONE;
    uint32_t device_idx;
    const char *device_string, *params;
    pa_sink *sink;
    pa_source *source;
    void *state;

    pa_assert(dm);
    pa_assert(dm->device_list);
    pa_assert(dm->type_map);

    /* TODO : multi device */
    pa_log_debug("handle_device_type_loaded '%s'", device_type_to_string(device_type));
    if (!(device = device_list_get_device(dm->device_list, device_type))) {
        if (!(device = build_device_item(device_type, NULL, direction))) {
            pa_log_error("build new device item failed");
            return NULL;
        }
    }

    type_item = pa_hashmap_get(dm->type_map, PA_INT_TO_PTR(device_type));

    if (direction == AUDIO_DEVICE_DIRECTION_OUT || direction == AUDIO_DEVICE_DIRECTION_BOTH) {
        PA_HASHMAP_FOREACH_KEY(device_string, type_item->playback_devices, state, role) {
            if (!(file_item = pa_hashmap_get(dm->file_map->playback, device_string))) {
                pa_log_error("No playback file map for '%s'", device_string);
                return NULL;
            }
            if (!(params = pa_hashmap_get(file_item->roles, PA_INT_TO_PTR(role)))) {
                pa_log_error("No params for '%s:%s'", device_string, device_role_to_string(role));
                return NULL;
            }
            PA_IDXSET_FOREACH(sink, dm->core->sinks, device_idx) {
                if (pulse_device_class_is_monitor(sink->proplist))
                    continue;
                if (sink_same_device_string(sink, device_string)) {
                    if (!compare_device_params_with_module_args(params, sink->module->argument)) {
                        device_item_add_sink(device, role, sink);
                        pa_log_debug("role:%s <- sink:%s", device_role_to_string(role), sink->name);
                        break;
                    }
                }
            }
        }
    }
    if (direction == AUDIO_DEVICE_DIRECTION_IN || direction == AUDIO_DEVICE_DIRECTION_BOTH) {
        PA_HASHMAP_FOREACH_KEY(device_string, type_item->capture_devices, state, role) {
            if (!(file_item = pa_hashmap_get(dm->file_map->capture, device_string))) {
                pa_log_error("No capture file map for '%s'", device_string);
                return NULL;
            }
            if (!(params = pa_hashmap_get(file_item->roles, PA_INT_TO_PTR(role)))) {
                pa_log_error("No params for '%s:%s'", device_string, device_role_to_string(role));
                return NULL;
            }
            PA_IDXSET_FOREACH(source, dm->core->sources, device_idx) {
                if (pulse_device_class_is_monitor(source->proplist))
                    continue;
                if (source_same_device_string(source, device_string)) {
                    if (!compare_device_params_with_module_args(params, source->module->argument)) {
                        device_item_add_source(device, role, source);
                        pa_log_debug("role:%s <- source:%s", device_role_to_string(role), source->name);
                        break;
                    }
                }
            }
        }
    }

    device_item_update_direction(device);
    device_list_add_device(dm->device_list, device, dm);

    return device;
}

static pa_bool_t device_status_is_detected(pa_hashmap *device_statuses, audio_device_t device_type) {
    int device_status;

    pa_assert(device_statuses);

    device_status = (int) pa_hashmap_get(device_statuses, PA_INT_TO_PTR(device_type));
    if (device_status & AUDIO_DEVICE_DETECTED) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static void handle_predefined_sink_loaded(pa_sink *sink, struct device_file_prop *file_item,  pa_device_manager *dm) {
    const char *device_string_removed_params = NULL;
    struct pulse_device_prop *pulse_device_prop = NULL;
    audio_device_t device_type;
    audio_device_direction_t direction;
    void *state = NULL;
    pa_hashmap *params_related_type = NULL;

    pa_assert(sink);
    pa_assert(sink->proplist);
    pa_assert(dm);
    pa_assert(dm->file_map);
    pa_assert(file_item);

    pa_log_debug("Predefined sink loaded, Check which device_type is associated with this sink");
    /* module's argument includes device-string(ex. device=hw:0,0 ),
       but key params for device_types hashmap is not. */
    if (!(device_string_removed_params = device_params_get_alsa_device_removed_string(sink->module->argument))) {
        pa_log_debug("argument null sink");
        return;
    }
    if ((params_related_type = pa_hashmap_get(file_item->device_types, device_string_removed_params))) {
        sink->device_types = params_related_type;
        PA_HASHMAP_FOREACH_KEY(pulse_device_prop, params_related_type, state, device_type) {
            pa_log_debug("Related type : %s", device_type_to_string(device_type));
            if (device_status_is_detected(dm->device_status, device_type)) {
                pa_log_debug("  This type is detected status");
                direction = device_type_get_direction(dm, device_type);
                if (device_type_ready(device_type, direction, dm)) {
                    pa_log_debug("  This type is ready");
                    handle_device_type_loaded(device_type, direction, dm);
                } else {
                    pa_log_debug("  This type is not ready");
                }
            } else {
                pa_log_debug("  This type is not detected status");
            }
        }
    } else {
        pa_log_warn("There is no matching type with this params '%s'", sink->module->argument);
    }
}

static void handle_predefined_source_loaded(pa_source *source, struct device_file_prop *file_item,  pa_device_manager *dm) {
    const char *device_string_removed_params = NULL;
    struct pulse_device_prop *pulse_device_prop = NULL;
    audio_device_t device_type;
    audio_device_direction_t direction;
    void *state = NULL;
    pa_hashmap *params_related_type = NULL;

    pa_assert(source);
    pa_assert(source->proplist);
    pa_assert(dm);
    pa_assert(dm->file_map);
    pa_assert(file_item);

    pa_log_debug("Predefined source loaded, Check which device_type is associated with this source");
    /* module's argument includes device-string(ex. device=hw:0,0 ),
       but key params for device_types hashmap is not. */
    device_string_removed_params = device_params_get_alsa_device_removed_string(source->module->argument);
    if ((params_related_type = pa_hashmap_get(file_item->device_types, device_string_removed_params))) {
        source->device_types = params_related_type;
        PA_HASHMAP_FOREACH_KEY(pulse_device_prop, params_related_type, state, device_type) {
            pa_log_debug("Related type : %s", device_type_to_string(device_type));
            if (device_status_is_detected(dm->device_status, device_type)) {
                pa_log_debug("  This type is detected status");
                direction = device_type_get_direction(dm, device_type);
                if (device_type_ready(device_type, direction, dm)) {
                    pa_log_debug("  This type is ready");
                    handle_device_type_loaded(device_type, direction, dm);
                } else {
                    pa_log_debug("  This type is not ready");
                }
            } else {
                pa_log_debug("  This type is not detected status");
            }
        }
    } else {
        pa_log_warn("There is no matching type with this params '%s'", source->module->argument);
    }
}

static void handle_sink_unloaded(pa_sink *sink, pa_device_manager *dm) {
    device_item *device= NULL;
    uint32_t device_idx = 0;
    pa_sink *sink_iter = NULL;
    void *state = NULL;
    audio_device_role_t role = AUDIO_DEVICE_ROLE_NONE;

    if (!sink || !dm) {
        pa_log_error("Invalid Paramter");
        return;
    }
    pa_assert(sink);
    pa_assert(dm);
    pa_assert(dm->device_list);

    pa_log_debug("Sink unloaded, Let's remove associated device_items with this sink");

    PA_IDXSET_FOREACH(device, dm->device_list, device_idx) {
        if (device->playback_devices) {
            PA_HASHMAP_FOREACH_KEY(sink_iter, device->playback_devices, state, role) {
                if (sink_iter == sink) {
                    pa_log_debug("device '%s' have this sink", device->name);
                    device_item_remove_sink(device, role);
                }
            }
            if (!pa_hashmap_size(device->playback_devices)) {
                pa_hashmap_free(device->playback_devices);
                device->playback_devices = NULL;
                if (!(device->capture_devices)) {
                    device_list_remove_device(dm->device_list, device, dm);
                }
            } else {
                device_item_update_direction(device);
            }
        }
    }
}

static void handle_source_unloaded(pa_source *source, pa_device_manager *dm) {
    device_item *device= NULL;
    uint32_t device_idx = 0;
    pa_source *source_iter = NULL;
    void *state = NULL;
    audio_device_role_t role = AUDIO_DEVICE_ROLE_NONE;

    if (!source|| !dm) {
        pa_log_error("Invalid Paramter");
        return;
    }
    pa_assert(source);
    pa_assert(dm);
    pa_assert(dm->device_list);

    pa_log_debug("Source unloaded, Let's remove associated device_items with this source");

    PA_IDXSET_FOREACH(device, dm->device_list, device_idx) {
        if (device->capture_devices) {
            PA_HASHMAP_FOREACH_KEY(source_iter, device->capture_devices, state, role) {
                if (source_iter == source) {
                    pa_log_debug("device '%s' have this source", device->name);
                    device_item_remove_source(device, role);
                }
            }
            if (!pa_hashmap_size(device->capture_devices)) {
                pa_hashmap_free(device->capture_devices);
                device->capture_devices= NULL;
                if (!(device->playback_devices)) {
                    device_list_remove_device(dm->device_list, device, dm);
                    //device_item_remove(dm->device_list, device);
                }
            } else {
                device_item_update_direction(device);
            }
        }
    }
}

static pa_hook_result_t sink_put_hook_callback(pa_core *c, pa_sink *sink, pa_device_manager *dm) {
    const char *device_string = NULL;
    struct device_file_prop *file_item = NULL;
    tizen_audio_device_class_t device_class;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(sink->proplist);
    pa_assert(dm);

    if (pulse_device_class_is_monitor(sink->proplist)) {
        pa_log_debug("This device's class is monitor. Skip this");
        return PA_HOOK_OK;
    }

    pa_log_debug("========== Sink Put Hook Callback '%s'(%d) ==========", sink->name, sink->index);

    device_class = sink_get_class(sink);
    pa_log_debug("Device Class '%s'", device_class_to_string(device_class));

    if (!(device_string = sink_get_device_string(sink))) {
        return PA_HOOK_OK;
    }

    if (device_class == TIZEN_AUDIO_DEVICE_CLASS_ALSA) {
        if ((file_item = pa_hashmap_get(dm->file_map->playback, device_string))) {
            handle_predefined_sink_loaded(sink, file_item, dm);
        } else {
            pa_log_debug("Not-predefined device");
            handle_unknown_sink(dm, sink);
        }
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_BT) {
        handle_unknown_sink(dm, sink);
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NULL) {
        if ((file_item = pa_hashmap_get(dm->file_map->playback, device_string))) {
            handle_predefined_sink_loaded(sink, file_item, dm);
        }
    } else {
        pa_log_debug("Unknown device class, Skip this");
    }

    dump_device_list(dm);
    dump_user_device_list(dm);
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
    dump_user_device_list(dm);
    return PA_HOOK_OK;
}


static pa_hook_result_t source_put_hook_callback(pa_core *c, pa_source *source, pa_device_manager *dm) {
    const char *device_string = NULL;
    struct device_file_prop *file_item = NULL;
    tizen_audio_device_class_t device_class;

    pa_assert(c);
    pa_assert(source);
    pa_assert(source->proplist);
    pa_assert(dm);

    if (pulse_device_class_is_monitor(source->proplist)) {
        pa_log_debug("This device's class is monitor. Skip this");
        return PA_HOOK_OK;
    }

    pa_log_debug("========== Source Put Hook Callback '%s'(%d) ==========", source->name, source->index);


    device_class = source_get_class(source);
    pa_log_debug("Device Class '%s'", device_class_to_string(device_class));

    if (!(device_string = source_get_device_string(source))) {
        return PA_HOOK_OK;
    }

    if (device_class == TIZEN_AUDIO_DEVICE_CLASS_ALSA) {
        if ((file_item = pa_hashmap_get(dm->file_map->capture, device_string))) {
            handle_predefined_source_loaded(source, file_item, dm);
        } else {
            pa_log_debug("Not-predefined device");
            handle_unknown_source(dm, source);
        }
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_BT) {
        handle_unknown_source(dm, source);
    } else if (device_class == TIZEN_AUDIO_DEVICE_CLASS_NULL) {
        if ((file_item = pa_hashmap_get(dm->file_map->capture, device_string))) {
            handle_predefined_source_loaded(source, file_item, dm);
        }
    } else {
        pa_log_debug("Unknown device class, Skip this");
    }

    dump_device_list(dm);
    dump_user_device_list(dm);
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
    dump_user_device_list(dm);

    return PA_HOOK_OK;
}

/*
    Build params for load sink, and load it.
*/
static pa_sink* load_playback_device(pa_core *c, const char *device_string, const char *device_params) {
    const char *args = NULL;
    const char *module_name;
    pa_module *module = NULL;
    pa_sink *s = NULL, *loaded_sink = NULL;
    uint32_t sink_idx;
    tizen_audio_device_class_t device_class;

    pa_assert(c);
    pa_assert(device_string);
    pa_assert(device_params);

    pa_log_debug("-------- load_playback_device : '%s' '%s' -------", device_string, device_params);

    device_class = device_string_get_class(device_string);
    if (device_class <= TIZEN_AUDIO_DEVICE_CLASS_NONE || device_class >= TIZEN_AUDIO_DEVICE_CLASS_MAX) {
        pa_log_warn("Invalid device_string '%s'", device_string);
        return NULL;
    }

    if (!(module_name = device_class_get_module_name(device_class, TRUE))) {
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

    PA_IDXSET_FOREACH(s, c->sinks, sink_idx) {
        if (s->module == module) {
            loaded_sink = s;
        }
    }

    return loaded_sink;
}

/*
    Build params for load source, and load it.
*/
static pa_source* load_capture_device(pa_core *c, const char *device_string, const char *device_params) {
    const char *args = NULL;
    pa_module *module = NULL;
    const char *module_name;
    pa_source *s = NULL, *loaded_source = NULL;
    uint32_t source_idx;
    tizen_audio_device_class_t device_class;

    pa_assert(c);
    pa_assert(device_string);
    pa_assert(device_params);

    pa_log_debug("-------- load_capture_device : '%s' '%s' -------", device_string, device_params);

    device_class = device_string_get_class(device_string);
    if (device_class <= TIZEN_AUDIO_DEVICE_CLASS_NONE || device_class >= TIZEN_AUDIO_DEVICE_CLASS_MAX) {
        pa_log_warn("Invalid device_string '%s'", device_string);
        return NULL;
    }

    if (!(module_name = device_class_get_module_name(device_class, FALSE))) {
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

    PA_IDXSET_FOREACH(s, c->sources, source_idx) {
        if (s->module == module) {
            loaded_source = s;
        }
    }
    return loaded_source;
}

/*
     Load sink/sources with information written in device-file map,
    If there is several roles in same device-file, then first load with 'normal' params
    and other roles with same params just reference it. if there is a role which is on same device
    but have different params, then do not load it. (ex.uhqa)
    This does not make device_item , just load sink or source.
*/
static int load_builtin_devices(pa_device_manager *dm) {
    void *state = NULL, *role_state = NULL;
    struct device_file_prop *file_item = NULL;
    const char *device_string = NULL, *params = NULL;
    audio_device_role_t role = AUDIO_DEVICE_ROLE_NONE;

    pa_assert(dm);

    pa_log_debug("\n==================== Load Builtin Devices ====================");

    PA_HASHMAP_FOREACH_KEY(file_item, dm->file_map->playback, state, device_string) {
        pa_log_debug("---------------- load sink for '%s' ------------------", device_string);

        /* if normal device exists , load first */
        if ((params = pa_hashmap_get(file_item->roles, PA_INT_TO_PTR(AUDIO_DEVICE_ROLE_NORMAL)))) {
            if (!load_playback_device(dm->core, device_string, params))
                pa_log_error("load normal playback device failed");
        }

        PA_HASHMAP_FOREACH_KEY(params, file_item->roles, role_state, role) {
            if (role == AUDIO_DEVICE_ROLE_NORMAL)
                continue;
            pa_log_debug("load sink for role %s", device_role_to_string(role));
            if (!device_playback_file_loaded(dm->core, device_string)) {
                if (!load_playback_device(dm->core, device_string, params))
                    pa_log_error("load playback device failed");
            }
        }
    }


    PA_HASHMAP_FOREACH_KEY(file_item, dm->file_map->capture, state, device_string) {
        pa_log_debug("---------------- load source for '%s' ------------------", device_string);

        /* if normal device exists , load first */
        if ((params = pa_hashmap_get(file_item->roles, PA_INT_TO_PTR(AUDIO_DEVICE_ROLE_NORMAL)))) {
            if (!load_capture_device(dm->core, device_string, params))
                pa_log_error("load normal capture device failed");
        }

        PA_HASHMAP_FOREACH_KEY (params, file_item->roles, role_state, role) {
            if (role == AUDIO_DEVICE_ROLE_NORMAL)
                continue;
            pa_log_debug("load source for role %s", device_role_to_string(role));
            if (!device_capture_file_loaded(dm->core, device_string)) {
                if (!load_capture_device(dm->core, device_string, params)) {
                    pa_log_error("load capture device failed");
                }
            }
        }
    }

    return 0;
}


/***************** Parse json file *******************/
static pa_hashmap* parse_device_role_object(json_object *device_role_o) {
    pa_hashmap *roles = NULL;
    audio_device_role_t device_role = AUDIO_DEVICE_ROLE_NONE;
    const char *params, *device_role_s;
    struct json_object_iterator it, it_end;
    json_object *params_o;

    pa_assert(device_role_o);
    pa_assert(json_object_is_type(device_role_o, json_type_object));

    roles = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    if (!roles) {
        pa_log_debug("hashmap new failed");
        goto failed;
    }

    it = json_object_iter_begin(device_role_o);
    it_end = json_object_iter_end(device_role_o);

    while (!json_object_iter_equal(&it, &it_end)) {
        device_role_s = json_object_iter_peek_name(&it);
        params_o = json_object_iter_peek_value(&it);

        if (!(params = json_object_get_string(params_o))) {
            pa_log_debug("There is no device params for role '%s'", device_role_s);
        }
        pa_log_debug("[DEBUG_PARSE] role '%s' - params '%s'", device_role_s, params);
        if ((device_role = device_role_to_int(device_role_s)) >= 0) {
            if (pa_hashmap_put(roles, PA_INT_TO_PTR(device_role), (void*) params)) {
                pa_log_error("put new role to hashmap faild");
                goto failed;
            }
        }
        else
            pa_log_error("Invalid device role '%s'", device_role_s);

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

static struct device_file_prop* parse_device_file_object(json_object *device_file_o, const char **device_string_key) {
    pa_hashmap *roles = NULL;
    json_object *device_file_prop_o = NULL;
    const char *device_string = NULL;
    struct device_file_prop *dd = NULL;

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

    if ((device_file_prop_o = json_object_object_get(device_file_o, "role"))) {
        if (!(roles = parse_device_role_object(device_file_prop_o))) {
            pa_log_error("Parse device role for '%s' failed", device_string);
            goto failed;
        }
    } else {
        pa_log_error("Get device role object failed");
    }

    dd = pa_xmalloc0(sizeof(struct device_file_prop));
    dd->device_types = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    dd->roles = roles;

    *device_string_key = device_string;

    return dd;

failed :
    if (roles)
        pa_xfree(roles);

    return NULL;
}

static pa_hashmap* parse_device_file_array_object(json_object *device_file_array_o) {
    int device_file_num, device_file_idx;
    struct device_file_prop *parsed_device_file = NULL;
    json_object *device_file_o = NULL;
    pa_hashmap *device_files = NULL;
    const char *device_string = NULL;

    pa_assert(device_file_array_o);
    pa_assert(json_object_is_type(device_file_array_o, json_type_array));

    device_files = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    device_file_num = json_object_array_length(device_file_array_o);
    for (device_file_idx = 0; device_file_idx < device_file_num; device_file_idx++) {
        if ((device_file_o = json_object_array_get_idx(device_file_array_o, device_file_idx)) && json_object_is_type(device_file_o, json_type_object)) {
            if ((parsed_device_file = parse_device_file_object(device_file_o, &device_string)) && device_string) {
                pa_hashmap_put(device_files, strdup(device_string), parsed_device_file);
            } else {
                pa_log_error("parse capture-devices failed");
                goto failed;
            }
        } else {
            pa_log_error("Get device file object failed");
            goto failed;
        }
    }

    if (pa_hashmap_size(device_files) == 0) {
        pa_hashmap_free(device_files);
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

    file_map = pa_xmalloc(sizeof(struct device_file_map));

    if ((device_files_o = json_object_object_get(o, DEVICE_FILE_OBJECT)) && json_object_is_type(device_files_o, json_type_object)) {
        pa_log_debug("[DEBUG_PARSE] ----------------- Playback Device Files ------------------");
        playback_devices_o = json_object_object_get(device_files_o, "playback-devices");
        file_map->playback = parse_device_file_array_object(playback_devices_o);
        pa_log_debug("[DEBUG_PARSE] ----------------- Capture Device Files ------------------");
        capture_devices_o = json_object_object_get(device_files_o, "capture-devices");
        file_map->capture = parse_device_file_array_object(capture_devices_o);
    }
    else {
        pa_log_error("Get device files object failed");
        return NULL;
    }

    return file_map;
}


static pa_hashmap* parse_device_role_map(json_object *device_role_map_o) {
    pa_hashmap *roles = NULL;
    const char *device_string, *device_role_s;
    audio_device_role_t device_role = AUDIO_DEVICE_ROLE_NONE;
    struct json_object_iterator it, it_end;
    json_object *device_string_o;

    pa_assert(device_role_map_o);
    pa_assert(json_object_is_type(device_role_map_o, json_type_object));

    roles = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    it = json_object_iter_begin(device_role_map_o);
    it_end = json_object_iter_end(device_role_map_o);

    while (!json_object_iter_equal(&it, &it_end)) {
        device_role_s = json_object_iter_peek_name(&it);
        device_string_o = json_object_iter_peek_value(&it);

        if (!(device_string = json_object_get_string(device_string_o))) {
            pa_log_debug("There is no device string for role '%s'", device_role_s);
        }
        pa_log_debug("[DEBUG_PARSE] role '%s' - device_string '%s'", device_role_s, device_string);
        if ((device_role = device_role_to_int(device_role_s)) >= 0) {
            if (pa_hashmap_put(roles, PA_INT_TO_PTR(device_role), (void*) device_string)) {
                pa_log_error("put new role to hashmap faild");
                goto failed;
            }
        }
        else {
            pa_log_error("Invalid device role '%s'", device_role_s);
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



static pa_hashmap* parse_device_type_map() {
    json_object *o, *device_array_o = NULL;
    int device_type_num = 0;
    int device_type_idx = 0;
    json_bool builtin;
    struct device_type_prop *type_item = NULL;
    pa_hashmap *type_map = NULL;

    o = json_object_from_file(DEVICE_MAP_FILE);
    if (is_error(o)) {
        pa_log_error("Read device-map file failed");
        return NULL;
    }

    pa_log_debug("\n[DEBUG_PARSE] ==================== Parse device types ====================");
    type_map = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    if ((device_array_o = json_object_object_get(o, DEVICE_TYPE_OBJECT)) && json_object_is_type(device_array_o, json_type_array)) {
        device_type_num = json_object_array_length(device_array_o);
        for (device_type_idx = 0; device_type_idx < device_type_num ; device_type_idx++) {
            json_object *device_o;

            if ((device_o = json_object_array_get_idx(device_array_o, device_type_idx)) && json_object_is_type(device_o, json_type_object)) {
                json_object *device_prop_o;
                json_object *array_item_o;
                int array_len, array_idx;
                const char *device_type = NULL;
                audio_device_t device_type_i = 0;
                type_item = pa_xmalloc0(sizeof(struct device_type_prop));

                if ((device_prop_o = json_object_object_get(device_o, "device-type")) && json_object_is_type(device_prop_o, json_type_string)) {
                    device_type = json_object_get_string(device_prop_o);
                    device_type_i = device_type_to_int(device_type);
                    pa_log_debug("[DEBUG_PARSE] ---------------- Parse device '%s' ----------------", device_type);
                } else {
                    pa_log_error("Get device type failed");
                    goto failed;
                }

                if ((device_prop_o = json_object_object_get(device_o, DEVICE_TYPE_PROP_BUILTIN)) && json_object_is_type(device_prop_o, json_type_boolean)) {
                    builtin = json_object_get_boolean(device_prop_o);
                    pa_log_debug("[DEBUG_PARSE] builtin: %d", builtin);
                    type_item->builtin = builtin;
                } else {
                    pa_log_error("Get device prop '%s' failed", DEVICE_TYPE_PROP_BUILTIN);
                }

                if ((device_prop_o = json_object_object_get(device_o, DEVICE_TYPE_PROP_DIRECTION)) && json_object_is_type(device_prop_o, json_type_array)) {
                    const char *direction;
                    array_len = json_object_array_length(device_prop_o);
                    for (array_idx = 0; array_idx < array_len; array_idx++) {
                        if ((array_item_o = json_object_array_get_idx(device_prop_o, array_idx)) && json_object_is_type(array_item_o, json_type_string)) {
                            direction = json_object_get_string(array_item_o);
                            pa_log_debug("[DEBUG_PARSE] direction : %s", direction);
                            type_item->direction[array_idx] = device_direction_to_int(direction);
                        }
                    }
                } else {
                    pa_log_error("Get device prop '%s' failed", DEVICE_TYPE_PROP_DIRECTION);
                }

                if ((device_prop_o = json_object_object_get(device_o, "avail-condition")) && json_object_is_type(device_prop_o, json_type_array)) {
                    const char *avail_cond;
                    array_len = json_object_array_length(device_prop_o);
                    for (array_idx = 0; array_idx < array_len; array_idx++) {
                        if ((array_item_o = json_object_array_get_idx(device_prop_o, array_idx)) && json_object_is_type(array_item_o, json_type_string)) {
                            avail_cond = json_object_get_string(array_item_o);
                            pa_log_debug("[DEBUG_PARSE] avail-condition : %s", avail_cond);
                            type_item->avail_condition[array_idx] = device_avail_cond_to_int(avail_cond);
                        }
                    }
                } else {
                    pa_log_error("Get device prop 'avail-condition' failed");
                }

                if ((device_prop_o = json_object_object_get(device_o, "playback-devices")) && json_object_is_type(device_prop_o, json_type_object)) {
                    pa_log_debug("[DEBUG_PARSE] ------ playback devices ------");
                    type_item->playback_devices = parse_device_role_map(device_prop_o);
                }

                if ((device_prop_o = json_object_object_get(device_o, "capture-devices")) && json_object_is_type(device_prop_o, json_type_object)) {
                    pa_log_debug("[DEBUG_PARSE] ------ capture devices ------");
                    type_item->capture_devices = parse_device_role_map(device_prop_o);
                }
                pa_hashmap_put(type_map, PA_INT_TO_PTR(device_type_i), type_item);

            }
            else {
                pa_log_debug("Get device type object failed");
            }
        }
    }
    else {
        pa_log_debug("Get device type array object failed");
    }
    return type_map;

failed :
    if (type_map)
        pa_xfree(type_map);

    return NULL;
}

/*
    Handle device connection detected through dbus.
    First, update device-status hashmap.
    And if correnspondent sink/sources for device_type exist, should make device_item and notify it.
    Use [device_type->roles] mappings in sink/source for find proper sink/source.
*/
static int handle_device_connected(pa_device_manager *dm, audio_device_t device_type, int detected_type) {
    int device_direction = 0;

    pa_assert(dm);
    pa_assert(dm->device_status);

    pa_log_debug("Device %s connected", device_type_to_string(device_type));
    pa_hashmap_remove(dm->device_status, PA_INT_TO_PTR(device_type));
    pa_hashmap_put(dm->device_status, PA_INT_TO_PTR(device_type), PA_INT_TO_PTR(detected_type));

    device_direction = device_type_get_direction(dm, device_type);
    if (device_type_ready(device_type, device_direction, dm)) {
        handle_device_type_loaded(device_type, device_direction, dm);
    }

    return 0;
}

/*
    Handle device disconnection detected through dbus.
    First, update device-status hashmap.
    And if there is device_item which has the device_type, remove it.
*/
static int handle_device_disconnected(pa_device_manager *dm, audio_device_t device_type) {
    device_item *device;
    uint32_t device_idx = 0;

    pa_assert(dm);
    pa_assert(dm->device_status);

    pa_log_debug("Device %s disconnected", device_type_to_string(device_type));
    pa_hashmap_remove(dm->device_status, PA_INT_TO_PTR(device_type));
    pa_hashmap_put(dm->device_status, PA_INT_TO_PTR(device_type), PA_INT_TO_PTR(AUDIO_DEVICE_NOT_DETECTED));

    PA_IDXSET_FOREACH(device, dm->device_list, device_idx) {
        if (device->type == device_type) {
            device_list_remove_device(dm->device_list, device, dm);
//            device_item_remove(dm->device_list, device);
        }
    }

    return 0;
}


/*
   look detected status which is external value,
   make conversion to internal consistent value,
   and handle it
*/
static int handle_device_status_changed(pa_device_manager *dm, audio_device_t device_type, int detected_status) {
    pa_assert(dm);

    if (device_type == AUDIO_DEVICE_AUDIO_JACK) {
        if (detected_status == EARJACK_DISCONNECTED) {
            handle_device_disconnected(dm, device_type);
        } else if (detected_status == EARJACK_TYPE_SPK_ONLY) {
            handle_device_connected(dm, device_type, AUDIO_DEVICE_DETECTED_AUDIO_JACK_OUT_DIREC);
        } else if (detected_status == EARJACK_TYPE_SPK_WITH_MIC) {
            handle_device_connected(dm, device_type, AUDIO_DEVICE_DETECTED_AUDIO_JACK_BOTH_DIREC);
        } else {
            pa_log_warn("Got invalid audio-jack detected value");
            return -1;
        }
    } else if (device_type == AUDIO_DEVICE_BT_SCO) {
        if (detected_status == BT_SCO_DISCONNECTED) {
            handle_device_disconnected(dm, device_type);
        } else if (detected_status == BT_SCO_CONNECTED) {
            handle_device_connected(dm, device_type, AUDIO_DEVICE_DETECTED_BT_SCO);
        } else {
            pa_log_warn("Got invalid bt-sco detected value");
            return -1;
        }
    } else if (device_type == AUDIO_DEVICE_MIRRORING) {
        if (detected_status == MIRRORING_DISCONNECTED) {
            handle_device_disconnected(dm, device_type);
        } else if (detected_status == MIRRORING_CONNECTED) {
            handle_device_connected(dm, device_type, AUDIO_DEVICE_DETECTED_MIRRORING);
        } else {
            pa_log_warn("Got invalid mirroring detected value");
            return -1;
        }
    }
    return 0;
}

/*
    Initialize device-status hashmap.
    This is for managing device-status detected through dbus.
    So, if device_type is not detected through dbus, let's initialize them to detected. (ex. spk, rcv,...)
    If not, initialize to not detected.
*/
static pa_hashmap* device_type_status_init(pa_hashmap *type_map) {
    int avail_cond_idx = 0, avail_cond_num = 0, correct_avail_cond = 0;
    audio_device_t device_type;
    void *state = NULL;
    struct device_type_prop *type_item = NULL;
    pa_hashmap *device_status = NULL;

    pa_assert(type_map);

    pa_log_debug("\n==================== Init Device Status ====================");

    device_status = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    PA_HASHMAP_FOREACH_KEY(type_item, type_map, state, device_type) {
        for (avail_cond_idx = 0, avail_cond_num = 0; avail_cond_idx < DEVICE_AVAIL_CONDITION_MAX; avail_cond_idx ++) {
            if (type_item->avail_condition[avail_cond_idx] != AUDIO_DEVICE_AVAIL_CONDITION_NONE) {
                avail_cond_num++;
            }
        }
        if (avail_cond_num == 1 && type_item->avail_condition[correct_avail_cond] == AUDIO_DEVICE_AVAIL_CONDITION_PULSE) {
            /* device types which don't need to be detected from other-side, let's just set 'detected'*/
            pa_log_debug("Set %-17s detected", device_type_to_string(device_type));
            pa_hashmap_put(device_status, PA_INT_TO_PTR(device_type), PA_INT_TO_PTR(AUDIO_DEVICE_DETECTED));
        } else {
            pa_log_debug("Set %-17s not detected", device_type_to_string(device_type));
            pa_hashmap_put(device_status, PA_INT_TO_PTR(device_type), PA_INT_TO_PTR(AUDIO_DEVICE_NOT_DETECTED));
        }
    }
    return device_status;
}

static int device_list_init(pa_device_manager *dm) {
    pa_assert(dm);

    dm->device_list = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    dm->user_device_list = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    return 0;
}

static void device_file_map_set_related_device_type(pa_device_manager *dm) {
    void *file_state = NULL, *type_state = NULL, *role_state = NULL;
    char *device_string = NULL, *types_device_string, *params = NULL;
    struct device_file_prop *file_item = NULL;
    struct device_type_prop *type_item = NULL;
    struct pulse_device_prop *pulse_device_prop = NULL;
    pa_hashmap *params_related_type = NULL;
    audio_device_t device_type;
    audio_device_role_t role = AUDIO_DEVICE_ROLE_NONE;

    pa_assert(dm);
    pa_log_debug("[DEBUG_PARSE] file-map set related device-type");

    PA_HASHMAP_FOREACH_KEY(file_item, dm->file_map->playback, file_state, device_string) {
        pa_log_debug("[DEBUG_PARSE] playback-device-file : '%s'", device_string);
        PA_HASHMAP_FOREACH_KEY(params, file_item->roles, role_state, role) {
            pa_log_debug("[DEBUG_PARSE]    role : '%s'", device_role_to_string(role));
            if (!(params_related_type = pa_hashmap_get(file_item->device_types, params))) {
                params_related_type = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
                pa_hashmap_put(file_item->device_types, params, params_related_type);
            }

            PA_HASHMAP_FOREACH_KEY(type_item, dm->type_map, type_state, device_type) {
                if (type_item->playback_devices) {
                    types_device_string = pa_hashmap_get(type_item->playback_devices, PA_INT_TO_PTR(role));
                    if (types_device_string && !strcmp(types_device_string, device_string)) {
                        if (!(pulse_device_prop = pa_hashmap_get(params_related_type, PA_INT_TO_PTR(device_type)))) {
                            pulse_device_prop = (struct pulse_device_prop*) pa_xmalloc0(sizeof(struct pulse_device_prop));
                            pulse_device_prop->roles = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
                            pulse_device_prop->status = 0;
                            pa_hashmap_put(params_related_type, PA_INT_TO_PTR(device_type), pulse_device_prop);
                        }
                        pa_idxset_put(pulse_device_prop->roles, PA_INT_TO_PTR(role), NULL);
                        pa_log_debug("[TEST] put type(%s) role(%s)", device_type_to_string(device_type), device_role_to_string(role));
                    }
                }
            }
        }
    }

    PA_HASHMAP_FOREACH_KEY(file_item, dm->file_map->capture, file_state, device_string) {
        pa_log_debug("[DEBUG_PARSE] capture-device-file : '%s'", device_string);
        PA_HASHMAP_FOREACH_KEY(params, file_item->roles, role_state, role) {
            pa_log_debug("[DEBUG_PARSE]    role : '%s'", device_role_to_string(role));
            if (!(params_related_type = pa_hashmap_get(file_item->device_types, params))) {
                params_related_type = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
                pa_hashmap_put(file_item->device_types, params, params_related_type);
            }

            PA_HASHMAP_FOREACH_KEY(type_item, dm->type_map, type_state, device_type) {
                if (type_item->capture_devices) {
                    types_device_string = pa_hashmap_get(type_item->capture_devices, PA_INT_TO_PTR(role));
                    if (types_device_string && !strcmp(types_device_string, device_string)) {
                        if (!(pulse_device_prop = pa_hashmap_get(params_related_type, PA_INT_TO_PTR(device_type)))) {
                            pulse_device_prop = (struct pulse_device_prop*) pa_xmalloc0(sizeof(struct pulse_device_prop));
                            pulse_device_prop->roles = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
                            pulse_device_prop->status = 0;
                            pa_hashmap_put(params_related_type, PA_INT_TO_PTR(device_type), pulse_device_prop);
                        }
                        pa_idxset_put(pulse_device_prop->roles, PA_INT_TO_PTR(role), NULL);
                    }
                }
            }
        }
    }

}

#ifdef HAVE_DBUS

static DBusHandlerResult dbus_filter_device_detect_handler(DBusConnection *c, DBusMessage *s, void *userdata) {
    DBusError error;
    int status = 0;

    pa_assert(userdata);

    if (dbus_message_get_type(s) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_info("Dbus device detect handler received msg");
    dbus_error_init(&error);

    if (dbus_message_is_signal(s, DBUS_INTERFACE_DEVICED, "ChangedEarjack")) {
        if (!dbus_message_get_args(s, NULL, DBUS_TYPE_INT32, &status, DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            handle_device_status_changed(userdata, AUDIO_DEVICE_AUDIO_JACK, status);
        }
    } else if (dbus_message_is_signal(s, DBUS_INTERFACE_DEVICED, "ChangedHDMIAudio")) {
        if (!dbus_message_get_args(s, NULL, DBUS_TYPE_INT32, &status, DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            handle_device_status_changed(userdata, AUDIO_DEVICE_HDMI, status);
        }
    } else if (dbus_message_is_signal(s, DBUS_INTERFACE_SOUND_SERVER, "ChangedMirroring")) {
        if (!dbus_message_get_args(s, NULL, DBUS_TYPE_INT32, &status, DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            handle_device_status_changed(userdata, AUDIO_DEVICE_MIRRORING, status);
        }
    } else if (dbus_message_is_signal(s, DBUS_INTERFACE_SOUND_SERVER, "ChangedBTSCO")) {
        if (!dbus_message_get_args(s, NULL, DBUS_TYPE_INT32, &status, DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            handle_device_status_changed(userdata, AUDIO_DEVICE_BT_SCO, status);
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

    if (pa_dbus_add_matches(pa_dbus_connection_get(dm->dbus_conn), &error, FILTER_DEVICED, FILTER_SOUND_SERVER, NULL) < 0) {
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

    pa_dbus_remove_matches(pa_dbus_connection_get(dm->dbus_conn), FILTER_DEVICED, FILTER_SOUND_SERVER, NULL);
    dbus_connection_remove_filter(pa_dbus_connection_get(dm->dbus_conn), dbus_filter_device_detect_handler, dm);
}


static void send_device_connected_signal(user_device_item *device_u, pa_bool_t connected, pa_device_manager *dm) {
    DBusMessage *signal_msg;
    DBusMessageIter msg_iter, device_iter;
    device_item *device;
    dbus_bool_t _connected = connected;

    pa_assert(device_u);
    pa_assert(device_u->profile);
    pa_assert(dm);

    pa_log_debug("Send following device %s signal", connected ? "Connected" : "Disconnected");
    dump_user_device_info(device_u);

    pa_assert_se(signal_msg = dbus_message_new_signal(DBUS_OBJECT_DEVICE_MANAGER, DBUS_INTERFACE_DEVICE_MANAGER, "DeviceConnected"));
    dbus_message_iter_init_append(signal_msg, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_STRUCT, NULL, &device_iter));
    if (!(device = pa_idxset_get_by_index(device_u->profile, device_u->active_profile))) {
        pa_log_error("active profile null");
        return;
    }

    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device_u->id);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device_u->type);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device->direction);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device->state);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &device->name);
    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &device_iter));
    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_BOOLEAN, &_connected);


    pa_assert_se(dbus_connection_send(pa_dbus_connection_get(dm->dbus_conn), signal_msg, NULL));
    dbus_message_unref(signal_msg);
}

static void send_device_info_changed_signal(user_device_item *device_u, int changed_type, pa_device_manager *dm) {
    DBusMessage *signal_msg;
    DBusMessageIter msg_iter, device_iter;
    device_item *device;

    pa_assert(device_u);
    pa_assert(device_u->profile);
    pa_assert(dm);

    pa_log_debug("Send folling device info changed signal");
    dump_user_device_info(device_u);

    pa_assert_se(signal_msg = dbus_message_new_signal(DBUS_OBJECT_DEVICE_MANAGER, DBUS_INTERFACE_DEVICE_MANAGER, "DeviceInfoChanged"));
    dbus_message_iter_init_append(signal_msg, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_STRUCT, NULL, &device_iter));
    if (!(device = pa_idxset_get_by_index(device_u->profile, device_u->active_profile))) {
        pa_log_error("active profile null");
        return;
    }
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device_u->id);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device_u->type);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device->direction);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device->state);
    dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &device->name);
    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &device_iter));
    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_INT32, &changed_type);


    pa_assert_se(dbus_connection_send(pa_dbus_connection_get(dm->dbus_conn), signal_msg, NULL));
    dbus_message_unref(signal_msg);
}

static void handle_get_connected_device_list(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    pa_device_manager *dm;
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter, device_iter;
    user_device_item *device_u;
    device_item *device;
    uint32_t device_idx;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    pa_log_debug("Get connected device list");

    dm = (pa_device_manager*) userdata;

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    dbus_message_iter_init_append(reply, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_ARRAY, "(iiiis)", &device_iter));

    PA_IDXSET_FOREACH(device_u, dm->user_device_list, device_idx) {
        device = pa_idxset_get_by_index(device_u->profile, device_u->active_profile);
        dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device_u->id);
        dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device->type);
        dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device->direction);
        dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_INT32, &device->state);
        dbus_message_iter_append_basic(&device_iter, DBUS_TYPE_STRING, &device->name);
    }

    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &device_iter));

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static void handle_load_sink(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    pa_device_manager *dm;
    dbus_int32_t device_type, role;

    dm = (pa_device_manager *) userdata;
    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_INT32, &device_type,
                                       DBUS_TYPE_INT32, &role,
                                       DBUS_TYPE_INVALID));

    pa_device_manager_load_sink(device_type, role, dm);
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


device_item* pa_device_manager_get_device(pa_device_manager *dm, audio_device_t device_type) {
    device_item *device;
    uint32_t idx;

    pa_assert(dm);

    PA_IDXSET_FOREACH(device, dm->device_list, idx) {
        if (device->type == device_type) {
            return device;
        }
    }
    return NULL;
}

device_item* pa_device_manager_get_device_by_id(pa_device_manager *dm, uint32_t id) {
    device_item *device;
    user_device_item *device_u;
    uint32_t idx;

    pa_assert(dm);

    PA_IDXSET_FOREACH(device_u, dm->user_device_list, idx) {
        if (device_u->id == id) {
            if (device_u->profile) {
                device = pa_idxset_get_by_index(device_u->profile, device_u->active_profile);
                return device;
            } else {
                pa_log_error("Invalid case, this user item is empty");
            }
        }
    }
    return NULL;
}

pa_sink* pa_device_manager_get_sink(device_item *device, audio_device_role_t role) {
    pa_assert(device);

    return pa_hashmap_get(device->playback_devices, PA_INT_TO_PTR(role));
}

pa_source* pa_device_manager_get_source(device_item *device, audio_device_role_t role) {
    pa_assert(device);

    return pa_hashmap_get(device->capture_devices, PA_INT_TO_PTR(role));
}

void pa_device_manager_set_device_state(pa_device_manager *dm, device_item *device, audio_device_status_t state) {
    pa_assert(dm);
    pa_assert(device);

    device->state = state;
    send_device_info_changed_signal(device->device_u, AUDIO_DEVICE_CHANGED_INFO_STATE, dm);
}

audio_device_status_t pa_device_manager_get_device_state(pa_device_manager *dm, device_item *device) {
    pa_assert(dm);
    pa_assert(device);

    return device->state;
}

audio_device_t pa_device_manager_get_device_type(device_item *device) {
    pa_assert(device);

    return device->type;
}

audio_device_direction_t pa_device_manager_get_device_direction(device_item *device) {
    pa_assert(device);

    return device->direction;
}

int pa_device_manager_load_sink(audio_device_t device_type, audio_device_role_t role, pa_device_manager *dm) {
    const char *device_string, *params;
    struct device_type_prop *type_item;
    struct device_file_prop *file_item;
    device_item *device;
    pa_sink *sink;
    uint32_t device_idx;

    pa_assert(dm);
    pa_assert(dm->device_list);

    pa_log_debug("load sink for '%s,%s'", device_type_to_string(device_type), device_role_to_string(role));
    PA_IDXSET_FOREACH(device, dm->device_list, device_idx) {
        if (device->type == device_type) {
            if (pa_hashmap_get(device->playback_devices, PA_INT_TO_PTR(role))) {
                pa_log_warn("Proper sink for '%s:%s' already loaded", device_type_to_string(device_type), device_role_to_string(role));
                return -1;
            }
        }
    }

    if (!(type_item = pa_hashmap_get(dm->type_map, PA_INT_TO_PTR(device_type)))) {
        pa_log_warn("No type-map for '%s'", device_type_to_string(device_type));
        goto failed;
    }

    if (!(device_string = pa_hashmap_get(type_item->playback_devices, PA_INT_TO_PTR(role)))) {
        pa_log_error("No device-string for '%s:%s'", device_type_to_string(device_type), device_role_to_string(role));
        goto failed;
    }

    if (!(file_item = pa_hashmap_get(dm->file_map->playback, device_string))) {
        pa_log_error("No playback file-map for '%s'", device_string);
        goto failed;
    }

    if (!(params = pa_hashmap_get(file_item->roles, PA_INT_TO_PTR(role)))) {
        pa_log_error("No params for '%s,%s'", device_string, device_role_to_string(role));
        goto failed;
    }

    if ((sink = load_playback_device(dm->core, device_string, params))) {
        pa_log_debug("loaded sink '%s' for '%s,%s' success", sink->name, device_type_to_string(device_type), device_role_to_string(role));
    } else {
        pa_log_warn("Cannot load playback device with '%s,%s'", device_string, params);
        goto failed;
    }

    return 0;

failed:
    return -1;
}

int pa_device_manager_load_source(audio_device_t device_type, audio_device_role_t role, pa_device_manager *dm) {
    const char *device_string, *params;
    struct device_type_prop *type_item;
    struct device_file_prop *file_item;
    device_item *device;
    pa_source *source;
    uint32_t device_idx;

    pa_assert(dm);

    pa_log_debug("load source for '%s,%s'", device_type_to_string(device_type), device_role_to_string(role));
    PA_IDXSET_FOREACH(device, dm->device_list, device_idx) {
        if (device->type == device_type) {
            if (pa_hashmap_get(device->capture_devices, PA_INT_TO_PTR(role))) {
                pa_log_warn("Proper source for '%s:%s' already loaded", device_type_to_string(device_type), device_role_to_string(role));
                return -1;
            }
        }
    }

    if (!(type_item = pa_hashmap_get(dm->type_map, PA_INT_TO_PTR(device_type)))) {
        pa_log_warn("No type-map for '%s'", device_type_to_string(device_type));
        goto failed;
    }

    if (!(device_string = pa_hashmap_get(type_item->capture_devices, PA_INT_TO_PTR(role)))) {
        pa_log_error("No device-string for '%s:%s'", device_type_to_string(device_type), device_role_to_string(role));
        goto failed;
    }

    if (!(file_item = pa_hashmap_get(dm->file_map->capture, device_string))) {
        pa_log_error("No capture file-map for '%s'", device_string);
        goto failed;
    }

    if (!(params = pa_hashmap_get(file_item->roles, PA_INT_TO_PTR(role)))) {
        pa_log_error("No params for '%s,%s'", device_string, device_role_to_string(role));
        goto failed;
    }

    if ((source = load_capture_device(dm->core, device_string, params))) {
        pa_log_debug("loaded source '%s' for '%s,%s' success", source->name, device_type_to_string(device_type), device_role_to_string(role));
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

    dm = pa_xnew0(pa_device_manager, 1);
    dm->core = c;

    dbus_init(dm);

    dm->sink_put_hook_slot = pa_hook_connect(&dm->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_LATE+10, (pa_hook_cb_t) sink_put_hook_callback, dm);
    dm->sink_unlink_hook_slot = pa_hook_connect(&dm->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_unlink_hook_callback, dm);
    dm->source_put_hook_slot = pa_hook_connect(&dm->core->hooks[PA_CORE_HOOK_SOURCE_PUT], PA_HOOK_LATE+10, (pa_hook_cb_t) source_put_hook_callback, dm);
    dm->source_unlink_hook_slot = pa_hook_connect(&dm->core->hooks[PA_CORE_HOOK_SOURCE_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) source_unlink_hook_callback, dm);

    dm->comm = pa_communicator_get(dm->core);
    if (!(dm->type_map = parse_device_type_map())) {
        pa_log_error("Parse device-type-map failed");
        return NULL;
    }

    if (!(dm->file_map = parse_device_file_map())) {
        pa_log_error("Parse device-file-map failed");
        return NULL;
    }

    device_file_map_set_related_device_type(dm);


    if (device_list_init(dm) < 0) {
        pa_log_error("Init device list failed");
        return NULL;
    }

    if (!(dm->device_status = device_type_status_init(dm->type_map))) {
        pa_log_error("Init device status failed");
        return NULL;
    }

    if (load_builtin_devices(dm) != 0) {
        pa_log_error("Load Builtin Devices faled");
        return NULL;
    }

    dump_device_list(dm);

    return dm;
}

void pa_device_manager_done(pa_device_manager *dm) {
    if (!dm)
        return;

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

    dbus_deinit(dm);
}
