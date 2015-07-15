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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include <pulse/xmalloc.h>
#include <pulse/proplist.h>

#include <pulsecore/module.h>
#include <pulsecore/log.h>
#include <pulsecore/sink.h>
#include <pulsecore/modargs.h>
#include <pulsecore/macro.h>

#include <json.h>
#include "stream-manager.h"
#include "stream-manager-priv.h"
#include "stream-manager-volume-priv.h"

#ifdef HAVE_DBUS
#define ARR_ARG_MAX  32
#define STREAM_MANAGER_OBJECT_PATH "/org/pulseaudio/StreamManager"
#define STREAM_MANAGER_INTERFACE   "org.pulseaudio.StreamManager"
#define STREAM_MANAGER_METHOD_NAME_GET_STREAM_INFO            "GetStreamInfo"
#define STREAM_MANAGER_METHOD_NAME_GET_STREAM_LIST            "GetStreamList"
#define STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_DEVICES   "SetStreamRouteDevices"
#define STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTION    "SetStreamRouteOption"
#define STREAM_MANAGER_METHOD_NAME_SET_VOLUME_LEVEL           "SetVolumeLevel"
#define STREAM_MANAGER_METHOD_NAME_GET_VOLUME_LEVEL           "GetVolumeLevel"
#define STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MAX_LEVEL       "GetVolumeMaxLevel"
#define STREAM_MANAGER_METHOD_NAME_SET_VOLUME_MUTE            "SetVolumeMute"
#define STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MUTE            "GetVolumeMute"
#define STREAM_MANAGER_METHOD_NAME_GET_CURRENT_VOLUME_TYPE    "GetCurrentVolumeType" /* the type that belongs to the stream of the current max priority */

#define GET_STREAM_NEW_PROPLIST(stream, type) \
      (type == STREAM_SINK_INPUT? ((pa_sink_input_new_data*)stream)->proplist : ((pa_source_output_new_data*)stream)->proplist)

#define GET_STREAM_PROPLIST(stream, type) \
      (type == STREAM_SINK_INPUT? ((pa_sink_input*)stream)->proplist : ((pa_source_output*)stream)->proplist)

#define GET_STREAM_NEW_SAMPLE_SPEC(stream, type) \
      (type == STREAM_SINK_INPUT? ((pa_sink_input_new_data*)stream)->sample_spec : ((pa_source_output_new_data*)stream)->sample_spec)

#define GET_STREAM_SAMPLE_SPEC(stream, type) \
      (type == STREAM_SINK_INPUT? ((pa_sink_input*)stream)->sample_spec : ((pa_source_output*)stream)->sample_spec)


static DBusHandlerResult method_handler_for_vt(DBusConnection *c, DBusMessage *m, void *userdata);
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_stream_info(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_stream_list(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_stream_route_devices(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_stream_route_option(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_volume_level(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_volume_level(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_volume_max_level(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_volume_mute(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_volume_mute(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_current_volume_type(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum method_handler_index {
    METHOD_HANDLER_GET_STREAM_INFO,
    METHOD_HANDLER_GET_STREAM_LIST,
    METHOD_HANDLER_SET_STREAM_ROUTE_DEVICES,
    METHOD_HANDLER_SET_STREAM_ROUTE_OPTION,
    METHOD_HANDLER_SET_VOLUME_LEVEL,
    METHOD_HANDLER_GET_VOLUME_LEVEL,
    METHOD_HANDLER_GET_VOLUME_MAX_LEVEL,
    METHOD_HANDLER_SET_VOLUME_MUTE,
    METHOD_HANDLER_GET_VOLUME_MUTE,
    METHOD_HANDLER_GET_CURRENT_VOLUME_TYPE,
    METHOD_HANDLER_MAX
};

static pa_dbus_arg_info get_stream_info_args[]  = { { "stream_type", "s", "in" },
                                                      { "priority", "i", "out" },
                                                    { "route_type", "i", "out" },
                                             { "avail_in_devices", "as", "out" },
                                            { "avail_out_devices", "as", "out" },
                                            { "avail_frameworks", "as", "out"} };
static pa_dbus_arg_info get_stream_list_args[]  = { { "stream_type", "as", "out" },
                                                     { "priority", "ai", "out" } };
static pa_dbus_arg_info set_stream_route_devices_args[]  = { { "parent_id", "u", "in" },
                                                     { "route_in_devices", "au", "in" },
                                                    { "route_out_devices", "au", "in" },
                                                            { "ret_msg", "s", "out" } };
static pa_dbus_arg_info set_stream_route_option_args[]  = { { "parent_id", "u", "in" },
                                                              { "name", "s", "in" },
                                                              { "value", "i", "in" },
                                                            { "ret_msg", "s", "out" } };
static pa_dbus_arg_info set_volume_level_args[]  = { { "io_direction", "s", "in" },
                                                             { "type", "s", "in" },
                                                            { "level", "u", "in" },
                                                       { "ret_msg", "s", "out" } };
static pa_dbus_arg_info get_volume_level_args[]  = { { "io_direction", "s", "in" },
                                                             { "type", "s", "in" },
                                                           { "level", "u", "out" },
                                                       { "ret_msg", "s", "out" } };
static pa_dbus_arg_info get_volume_max_level_args[]  = { { "io_direction", "s", "in" },
                                                                 { "type", "s", "in" },
                                                               { "level", "u", "out" },
                                                           { "ret_msg", "s", "out" } };
static pa_dbus_arg_info set_volume_mute_args[]  = { { "io_direction", "s", "in" },
                                                             { "type", "s", "in" },
                                                           { "on/off", "u", "in" },
                                                       { "ret_msg", "s", "out" } };
static pa_dbus_arg_info get_volume_mute_args[]  = { { "io_direction", "s", "in" },
                                                            { "type", "s", "in" },
                                                         { "on/off", "u", "out" },
                                                      { "ret_msg", "s", "out" } };
static pa_dbus_arg_info get_current_volume_type_args[]  = { { "io_direction", "s", "in" },
                                                            { "type", "s", "out" },
                                                      { "ret_msg", "s", "out" } };
static char* signature_args_for_in[] = { "s", "", "uauau", "usi", "ssu", "ss", "ss", "ssu", "ss", "s"};

static pa_dbus_method_handler method_handlers[METHOD_HANDLER_MAX] = {
    [METHOD_HANDLER_GET_STREAM_INFO] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_STREAM_INFO,
        .arguments = get_stream_info_args,
        .n_arguments = sizeof(get_stream_info_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_stream_info },
    [METHOD_HANDLER_GET_STREAM_LIST] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_STREAM_LIST,
        .arguments = get_stream_list_args,
        .n_arguments = sizeof(get_stream_list_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_stream_list },
    [METHOD_HANDLER_SET_STREAM_ROUTE_DEVICES] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_DEVICES,
        .arguments = set_stream_route_devices_args,
        .n_arguments = sizeof(set_stream_route_devices_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_stream_route_devices },
    [METHOD_HANDLER_SET_STREAM_ROUTE_OPTION] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTION,
        .arguments = set_stream_route_option_args,
        .n_arguments = sizeof(set_stream_route_option_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_stream_route_option },
    [METHOD_HANDLER_SET_VOLUME_LEVEL] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_VOLUME_LEVEL,
        .arguments = set_volume_level_args,
        .n_arguments = sizeof(set_volume_level_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_volume_level },
    [METHOD_HANDLER_GET_VOLUME_LEVEL] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_VOLUME_LEVEL,
        .arguments = get_volume_level_args,
        .n_arguments = sizeof(get_volume_level_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_volume_level },
    [METHOD_HANDLER_GET_VOLUME_MAX_LEVEL] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MAX_LEVEL,
        .arguments = get_volume_max_level_args,
        .n_arguments = sizeof(get_volume_max_level_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_volume_max_level },
    [METHOD_HANDLER_SET_VOLUME_MUTE] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_VOLUME_MUTE,
        .arguments = set_volume_mute_args,
        .n_arguments = sizeof(set_volume_mute_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_volume_mute },
    [METHOD_HANDLER_GET_VOLUME_MUTE] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MUTE,
        .arguments = get_volume_mute_args,
        .n_arguments = sizeof(get_volume_mute_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_volume_mute },
    [METHOD_HANDLER_GET_CURRENT_VOLUME_TYPE] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_CURRENT_VOLUME_TYPE,
        .arguments = get_current_volume_type_args,
        .n_arguments = sizeof(get_current_volume_type_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_current_volume_type },
};

const char *dbus_str_none = "none";
const char* stream_manager_dbus_ret_str[] = {"STREAM_MANAGER_RETURN_OK","STREAM_MANAGER_RETURN_ERROR", "STREAM_MANAGER_RETURN_ERROR_NO_STREAM"};
enum {
    RET_MSG_INDEX_OK,
    RET_MSG_INDEX_ERROR,
    RET_MSG_INDEX_ERROR_NO_STREAM,
};

#ifdef USE_DBUS_PROTOCOL

static pa_dbus_interface_info stream_manager_interface_info = {
    .name = STREAM_MANAGER_INTERFACE,
    .method_handlers = method_handlers,
    .n_method_handlers = METHOD_HANDLER_MAX,
    .property_handlers = ,
    .n_property_handlers = ,
    .get_all_properties_cb =,
    .signals =,
    .n_signals =
};

#else

#define STREAM_MGR_INTROSPECT_XML                                            \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                                \
    "<node>"                                                                 \
    " <interface name=\"STREAM_MANAGER_INTERFACE\">"                         \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_STREAM_INFO\">"         \
    "   <arg name=\"stream_type\" direction=\"in\" type=\"s\"/>"             \
    "   <arg name=\"priority\" direction=\"out\" type=\"i\"/>"               \
    "   <arg name=\"route_type\" direction=\"out\" type=\"i\"/>"             \
    "   <arg name=\"avail_in_devices\" direction=\"out\" type=\"as\"/>"      \
    "   <arg name=\"avail_out_devices\" direction=\"out\" type=\"as\"/>"     \
    "   <arg name=\"avail_frameworks\" direction=\"out\" type=\"as\"/>"      \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_STREAM_LIST\">"         \
    "   <arg name=\"stream_type\" direction=\"in\" type=\"as\"/>"            \
    "   <arg name=\"priority\" direction=\"in\" type=\"ai\"/>"               \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_DEVICES\">"\
    "   <arg name=\"parent_id\" direction=\"in\" type=\"u\"/>"               \
    "   <arg name=\"route_in_devices\" direction=\"in\" type=\"au\"/>"       \
    "   <arg name=\"route_out_devices\" direction=\"in\" type=\"au\"/>"      \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTION\">" \
    "   <arg name=\"parent_id\" direction=\"in\" type=\"u\"/>"               \
    "   <arg name=\"name\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"value\" direction=\"in\" type=\"i\"/>"                   \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_VOLUME_LEVEL\">"        \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"level\" direction=\"in\" type=\"u\"/>"                   \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_VOLUME_LEVEL\">"        \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"level\" direction=\"out\" type=\"u\"/>"                  \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MAX_LEVEL\">"    \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"level\" direction=\"out\" type=\"u\"/>"                  \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_VOLUME_MUTE\">"         \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"on/off\" direction=\"in\" type=\"u\"/>"                  \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MUTE\">"         \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"on/off\" direction=\"out\" type=\"u\"/>"                 \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_CURRENT_VOLUME_TYPE\">" \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"out\" type=\"s\"/>"                    \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    " </interface>"                                                          \
    " <interface name=\"org.freedesktop.DBus.Introspectable\">"              \
    "  <method name=\"Introspect\">"                                         \
    "   <arg name=\"data\" type=\"s\" direction=\"out\"/>"                   \
    "  </method>"                                                            \
    " </interface>"                                                          \
    "</node>"
#endif

#endif

#define STREAM_MANAGER_CLIENT_NAME "SOUND_MANAGER_STREAM_INFO"

typedef enum pa_process_stream_result {
    PA_PROCESS_STREAM_OK,
    PA_PROCESS_STREAM_STOP,
    PA_PROCESS_STREAM_SKIP,
} pa_process_stream_result_t;

typedef enum _process_command_type {
    PROCESS_COMMAND_PREPARE,
    PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA,
    PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED,
    PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED,
    PROCESS_COMMAND_UPDATE_VOLUME,
    PROCESS_COMMAND_ADD_PARENT_ID,
    PROCESS_COMMAND_REMOVE_PARENT_ID,
} process_command_type_t;

typedef enum _notify_command_type {
    NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT,
    NOTIFY_COMMAND_CHANGE_ROUTE_START_WITH_NEW_DATA,
    NOTIFY_COMMAND_CHANGE_ROUTE_START,
    NOTIFY_COMMAND_CHANGE_ROUTE_END,
    NOTIFY_COMMAND_UPDATE_ROUTE_OPTION,
    NOTIFY_COMMAND_INFORM_STREAM_CONNECTED,
    NOTIFY_COMMAND_INFORM_STREAM_DISCONNECTED,
} notify_command_type_t;

const char* process_command_type_str[] = {
    "PREPARE",
    "CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA",
    "CHANGE_ROUTE_BY_STREAM_STARTED",
    "CHANGE_ROUTE_BY_STREAM_ENDED",
    "UPDATE_VOLUME",
    "ADD_PARENT_ID",
    "REMOVE_PARENT_ID",
};

const char* notify_command_type_str[] = {
    "SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT",
    "CHANGE_ROUTE_START_WITH_NEW_DATA",
    "CHANGE_ROUTE_START",
    "CHANGE_ROUTE_END",
    "UPDATE_ROUTE_OPTION",
    "INFORM_STREAM_CONNECTED",
    "INFORM_STREAM_DISCONNECTED",
};

#define STREAM_MAP_FILE "/etc/pulse/stream-map.json"
#define STREAM_MAP_STREAMS "streams"
#define STREAM_MAP_STREAM_ROLE "role"
#define STREAM_MAP_STREAM_PRIORITY "priority"
#define STREAM_MAP_STREAM_ROUTE_TYPE "route-type"
#define STREAM_MAP_STREAM_DIRECTIONS "directions"
#define STREAM_MAP_STREAM_VOLUME_TYPES "volume-types"
#define STREAM_MAP_STREAM_VOLUME_TYPE_IN "in"
#define STREAM_MAP_STREAM_VOLUME_TYPE_OUT "out"
#define STREAM_MAP_STREAM_VOLUME_IS_FOR_HAL "is-hal-volume"
#define STREAM_MAP_STREAM_CAPTURE_VOLUME_TYPE "capture-volume-type"
#define STREAM_MAP_STREAM_PLAYBACK_VOLUME_TYPE "playback-volume-type"
#define STREAM_MAP_STREAM_AVAIL_IN_DEVICES "avail-in-devices"
#define STREAM_MAP_STREAM_AVAIL_OUT_DEVICES "avail-out-devices"
#define STREAM_MAP_STREAM_AVAIL_FRAMEWORKS "avail-frameworks"

typedef struct _stream_parent {
    pa_idxset *idx_sink_inputs;
    pa_idxset *idx_source_outputs;
    pa_idxset *idx_route_in_devices;
    pa_idxset *idx_route_out_devices;
} stream_parent;

#define AVAIL_DEVICES_MAX 16
#define AVAIL_FRAMEWORKS_MAX 16
#define AVAIL_STREAMS_MAX 32
typedef struct _stream_info_per_type {
    int32_t priority;
    int32_t route_type;
    int32_t num_of_in_devices;
    int32_t num_of_out_devices;
    int32_t num_of_frameworks;
    char *avail_in_devices[AVAIL_DEVICES_MAX];
    char *avail_out_devices[AVAIL_DEVICES_MAX];
    char *avail_frameworks[AVAIL_FRAMEWORKS_MAX];
} stream_info_per_type;
typedef struct _stream_list {
    int32_t num_of_streams;
    char* types[AVAIL_STREAMS_MAX];
    int32_t priorities[AVAIL_STREAMS_MAX];
} stream_list;
typedef struct _stream_route_option {
    const char *name;
    int32_t value;
} stream_route_option;

static void do_notify(pa_stream_manager *m, notify_command_type_t command, stream_type_t type, void *user_data);
static pa_process_stream_result_t process_stream(stream_type_t type, void *stream, process_command_type_t command, pa_stream_manager *m);

static int get_available_streams_from_map(pa_stream_manager *m, stream_list *list) {
    void *state = NULL;
    stream_info *s = NULL;
    const char *role = NULL;
    int i = 0;

    pa_log_info("get_available_streams_from_map");
    if (m->stream_map) {
        while ((s = pa_hashmap_iterate(m->stream_map, &state, (const void**)&role))) {
            if (i < AVAIL_STREAMS_MAX) {
                list->priorities[i] = s->priority;
                list->types[i++] = role;
                pa_log_debug("  [%d] stream_type[%s], priority[%d]", i-1, role, s->priority);
            } else {
                pa_log_error("  out of range, [%d]", i);
                break;
            }
        }
        list->num_of_streams = i;
        pa_log_debug("  num_of_streams[%d]",i);
    } else {
        pa_log_error("stream_map is not initialized..");
        return -1;
    }
    return 0;
}

static int get_stream_info_from_map(pa_stream_manager *m, const char *stream_role, stream_info_per_type *info) {
    uint32_t idx = 0;
    char *name;
    stream_info *s = NULL;
    int i = 0;
    int j = 0;
    int k = 0;
    pa_log_info("get_stream_info_from_map : role[%s]", stream_role);
    if (m->stream_map) {
        s = pa_hashmap_get(m->stream_map, stream_role);
        if (s) {
            info->priority = s->priority;
            info->route_type = s->route_type;
            PA_IDXSET_FOREACH(name, s->idx_avail_in_devices, idx) {
                pa_log_debug("  avail-in-device[%d] name  : %s", i, name);
                if (i < AVAIL_DEVICES_MAX)
                    info->avail_in_devices[i++] = name;
                else
                    pa_log_error("  avail-in-devices, out of range, [%d]", i);
            }
            info->num_of_in_devices = i;
            PA_IDXSET_FOREACH(name, s->idx_avail_out_devices, idx) {
                pa_log_debug("  avail-out-device[%d] name  : %s", j, name);
                if (j < AVAIL_DEVICES_MAX)
                    info->avail_out_devices[j++] = name;
                else
                    pa_log_error("  avail-out-devices, out of range, [%d]", j);
            }
            info->num_of_out_devices = j;
            PA_IDXSET_FOREACH(name, s->idx_avail_frameworks, idx) {
                pa_log_debug("  avail-frameworks[%d] name  : %s", k, name);
                if (k < AVAIL_FRAMEWORKS_MAX)
                    info->avail_frameworks[k++] = name;
                else
                    pa_log_error("  avail-frameworks, out of range, [%d]", k);
            }
            info->num_of_frameworks = k;
        }
    } else {
        pa_log_error("stream_map is not initialized..");
        return -1;
    }
    return 0;
}

#ifdef HAVE_DBUS
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *xml = STREAM_MGR_INTROSPECT_XML;
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

static void handle_get_stream_list(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    stream_list list;
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;
    pa_stream_manager *m = (pa_stream_manager*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_stream_list() dbus method is called");

    memset(&list, 0, sizeof(stream_list));
    pa_assert_se((reply = dbus_message_new_method_return(msg)));
    dbus_message_iter_init_append(reply, &msg_iter);
    if(!get_available_streams_from_map(m, &list)) {
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, &list.types, list.num_of_streams);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_INT32, &list.priorities, list.num_of_streams);
    } else {
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, NULL, 0);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_INT32, NULL, 0);
    }
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static void handle_get_stream_info(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    char *type;
    stream_info_per_type info;
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_stream_info(), type[%s]", type);
    memset(&info, 0, sizeof(stream_info_per_type));
    pa_assert_se((reply = dbus_message_new_method_return(msg)));
    dbus_message_iter_init_append(reply, &msg_iter);
    if(!get_stream_info_from_map(m, type, &info)) {
        pa_dbus_append_basic_variant(&msg_iter, DBUS_TYPE_INT32, &info.priority);
        pa_dbus_append_basic_variant(&msg_iter, DBUS_TYPE_INT32, &info.route_type);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, &info.avail_in_devices, info.num_of_in_devices);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, &info.avail_out_devices, info.num_of_out_devices);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, &info.avail_frameworks, info.num_of_frameworks);
    } else {
        pa_dbus_append_basic_variant(&msg_iter, DBUS_TYPE_INT32, 0);
        pa_dbus_append_basic_variant(&msg_iter, DBUS_TYPE_INT32, 0);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, NULL, 0);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, NULL, 0);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, NULL, 0);
    }
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static void handle_set_stream_route_devices(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    uint32_t id = 0;
    int i = 0;
    uint32_t *in_device_list = NULL;
    uint32_t *out_device_list = NULL;
    int list_len_in = 0;
    int list_len_out = 0;
    uint32_t idx = 0;
    uint32_t *device_id = NULL;
    stream_parent *sp = NULL;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &in_device_list, &list_len_in,
                                       DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &out_device_list, &list_len_out,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_stream_route_devices(), id[%u], in_device_list[%p]:length[%d], out_device_list[%p]:length[%d]",
            id, in_device_list, list_len_in, out_device_list, list_len_out);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    sp = pa_hashmap_get(m->stream_parents, id);
    if (sp) {
        if (!in_device_list && !out_device_list) {
            pa_log_error("invalid arguments");
            goto FAILURE;
        }

        if (sp->idx_route_in_devices) {
            PA_IDXSET_FOREACH(device_id, sp->idx_route_in_devices, idx) {
                pa_idxset_remove_by_data(sp->idx_route_in_devices, device_id, NULL);
                pa_xfree(device_id);
            }
            if (in_device_list && list_len_in) {
                for (i = 0; i < list_len_in; i++) {
                    pa_idxset_put(sp->idx_route_in_devices, pa_xmemdup(&in_device_list[i], sizeof(uint32_t)), NULL);
                    pa_log_debug(" -- [in] device id:%u", in_device_list[i]);
                }
            }
            if (m->cur_highest_priority.source_output) {
            /* if any stream that belongs to this id has been activated, do notify right away */
                if (pa_idxset_get_by_data(sp->idx_source_outputs, m->cur_highest_priority.source_output, NULL)) {
                    pa_log_debug(" -- cur_highest_priority.source_output->index[%u] belongs to this parent id[%u], do notify for the route change",
                            (m->cur_highest_priority.source_output)->index, id);
                    do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, STREAM_SOURCE_OUTPUT, m->cur_highest_priority.source_output);
                }
            }
        } else {
            pa_log_error("failed to update, idx_route_in_devices[%p]", sp->idx_route_in_devices);
            goto FAILURE;
        }

        if (sp->idx_route_out_devices) {
            PA_IDXSET_FOREACH(device_id, sp->idx_route_out_devices, idx) {
                pa_idxset_remove_by_data(sp->idx_route_out_devices, device_id, NULL);
                pa_xfree(device_id);
            }
            if (out_device_list && list_len_out) {
                for (i = 0; i < list_len_out; i++) {
                    pa_idxset_put(sp->idx_route_out_devices, pa_xmemdup(&out_device_list[i], sizeof(uint32_t)), NULL);
                    pa_log_debug(" -- [out] device id:%u", out_device_list[i]);
                }
            }
            if (m->cur_highest_priority.sink_input) {
            /* if any stream that belongs to this id has been activated, do notify right away */
                if (pa_idxset_get_by_data(sp->idx_sink_inputs, m->cur_highest_priority.sink_input, NULL)) {
                    pa_log_debug(" -- cur_highest_priority.sink_input->index[%u] belongs to this parent id[%u], do notify for the route change",
                            (m->cur_highest_priority.sink_input)->index, id);
                    do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, STREAM_SINK_INPUT, m->cur_highest_priority.sink_input);
                }
            }
        } else {
            pa_log_error("failed to update, idx_route_out_devices[%p]", sp->idx_route_out_devices);
            goto FAILURE;
        }
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    } else {
        pa_log_error("could not find matching client for this parent_id[%u]", id);
        goto FAILURE;
    }

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
FAILURE:
    pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_set_stream_route_option(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    uint32_t id = 0;
    int i = 0;
    const char *name = NULL;
    int32_t value = 0;
    int list_len = 0;
    pa_bool_t updated = FALSE;
    stream_parent *sp = NULL;
    stream_route_option route_option;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_STRING, &name,
                                       DBUS_TYPE_INT32, &value,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_stream_route_option(), name[%s], value[%d]", name, value);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    sp = pa_hashmap_get(m->stream_parents, id);
    if (sp) {
        if (name) {
            route_option.name = name;
            route_option.value = value;

            /* if any stream that belongs to this id has been activated, do notify right away */
            if (m->cur_highest_priority.sink_input) {
                if (pa_idxset_get_by_data(sp->idx_sink_inputs, m->cur_highest_priority.sink_input, NULL)) {
                    pa_log_debug(" -- cur_highest_priority.sink_input->index[%u] belongs to this parent id[%u], do notify for the options",
                        (m->cur_highest_priority.sink_input)->index, id);
                    do_notify(m, NOTIFY_COMMAND_UPDATE_ROUTE_OPTION, STREAM_SINK_INPUT, &route_option);
                    updated = TRUE;
                }
            }
            if (m->cur_highest_priority.source_output) {
                if (pa_idxset_get_by_data(sp->idx_source_outputs, m->cur_highest_priority.source_output, NULL)) {
                    pa_log_debug(" -- cur_highest_priority.source_output->index[%u] belongs to this parent id[%u], do notify for the options",
                        (m->cur_highest_priority.source_output)->index, id);
                    do_notify(m, NOTIFY_COMMAND_UPDATE_ROUTE_OPTION, STREAM_SOURCE_OUTPUT, &route_option);
                    updated = TRUE;
                }
            }
            if (!updated) {
                pa_log_error("invalid state");
                pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR_NO_STREAM], DBUS_TYPE_INVALID));
            } else
                pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
        } else {
            pa_log_error("invalid arguments");
            pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        }

    } else {
        pa_log_error("could not find matching client for this parent_id[%u]", id);
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    }

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void send_volume_changed_signal(DBusConnection *conn, const char *direction, const char *volume_type, const uint32_t volume_level) {
    DBusMessage *signal_msg;
    DBusMessageIter msg_iter;

    pa_assert(conn);
    pa_assert(volume_type);

    pa_log_debug("Send volume changed signal : direction %s, type %s, level %d", direction, volume_type, volume_level);

    pa_assert_se(signal_msg = dbus_message_new_signal(STREAM_MANAGER_OBJECT_PATH, STREAM_MANAGER_INTERFACE, "VolumeChanged"));
    dbus_message_iter_init_append(signal_msg, &msg_iter);

    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_STRING, &direction);
    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_STRING, &volume_type);
    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_UINT32, &volume_level);

    pa_assert_se(dbus_connection_send(conn, signal_msg, NULL));
    dbus_message_unref(signal_msg);
    return;
}

static void handle_set_volume_level(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t level = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;
    int ret = 0;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_UINT32, &level,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_volume_level(), direction[%s], type[%s], level[%u]", direction, type, level);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if ((ret = set_volume_level_by_type(m, stream_type, type, level)))
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    else
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);

    if (!ret)
        send_volume_changed_signal(conn, direction, type, level);
    return;
}

static void handle_get_volume_level(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t level = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_volume_level(), direction(%s), type(%s)", direction, type);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if (get_volume_level_by_type(m, GET_VOLUME_CURRENT_LEVEL, stream_type, type, &level)) {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    } else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, &level, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    }

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_get_volume_max_level(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t level = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_volume_max_level(), direction[%s], type[%s]", direction, type);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if (get_volume_level_by_type(m, GET_VOLUME_MAX_LEVEL, stream_type, type, &level)) {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    } else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, &level, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    }
FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_set_volume_mute(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t do_mute = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_UINT32, &do_mute,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_volume_mute(), direction[%s], type[%s], do_mute[%u]", direction, type, do_mute);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if (set_volume_mute_by_type(m, stream_type, type, (pa_bool_t)do_mute))
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    else
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_get_volume_mute(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t is_muted = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;

    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_volume_mute(), direction[%s], type[%s]", direction, type);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if (get_volume_mute_by_type(m, stream_type, type, (uint32_t*)&is_muted)) {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    } else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, &is_muted, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    }

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_get_current_volume_type(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    void *s = NULL;
    stream_type_t stream_type = STREAM_SINK_INPUT;

    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_current_volume_type(), direction[%s]", direction);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &dbus_str_none, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    s = (stream_type == STREAM_SINK_INPUT)?m->cur_highest_priority.sink_input:m->cur_highest_priority.source_output;
    if (s) {
        type = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE);
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &type, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    } else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &dbus_str_none, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR_NO_STREAM], DBUS_TYPE_INVALID));
    }

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata) {
	int idx = 0;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    for (idx = 0; idx < METHOD_HANDLER_MAX; idx++) {
        if (dbus_message_is_method_call(msg, STREAM_MANAGER_INTERFACE, method_handlers[idx].method_name )) {
            pa_log_debug("Message signature [%s] (Expected [%s])", dbus_message_get_signature(msg), signature_args_for_in[idx]);
            if (pa_streq(dbus_message_get_signature(msg), signature_args_for_in[idx])) {
                method_handlers[idx].receive_cb(conn, msg, userdata);
                return DBUS_HANDLER_RESULT_HANDLED;
            } else {
                pa_log_warn("Wrong Argument Signature");
                pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected %s", signature_args_for_in[idx]);
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult method_handler_for_vt(DBusConnection *c, DBusMessage *m, void *userdata) {
    pa_stream_manager *u = (pa_stream_manager*)userdata;
    const char *path, *interface, *member;

    pa_assert(c);
    pa_assert(m);
    pa_assert(u);

    path = dbus_message_get_path(m);
    interface = dbus_message_get_interface(m);
    member = dbus_message_get_member(m);

    pa_log_debug("dbus: path=%s, interface=%s, member=%s", path, interface, member);

    if (!pa_streq(path, STREAM_MANAGER_OBJECT_PATH))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        return handle_introspect(c, m, u);
    } else {
        return handle_methods(c, m, u);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}
#endif

static int convert_route_type(stream_route_type_t *route_type, const char *route_type_string) {
    int ret = 0;

    pa_assert(route_type);
    pa_assert(route_type_string);

    if (pa_streq("auto", route_type_string))
        *route_type = STREAM_ROUTE_TYPE_AUTO;
    else if (pa_streq("auto-all", route_type_string))
        *route_type = STREAM_ROUTE_TYPE_AUTO_ALL;
    else if (pa_streq("manual", route_type_string))
        *route_type = STREAM_ROUTE_TYPE_MANUAL;
    else {
        ret = -1;
        pa_log_error("Not supported route_type(%s)", route_type_string);
    }

    return ret;
}

static void dump_stream_map (pa_stream_manager *m) {
    stream_info *s = NULL;
    const char *role = NULL;
    char *name = NULL;
    void *state = NULL;
    uint32_t idx = 0;
    pa_assert(m);
    pa_log_debug("==========[START stream-map dump]==========");
    while (m->stream_map && (s = pa_hashmap_iterate(m->stream_map, &state, (const void **)&role))) {
        pa_log_debug("[role : %s]", role);
        pa_log_debug("  - priority   : %d", s->priority);
        pa_log_debug("  - route-type : %d (0:auto,1:auto-all,2:manual,3:manual-all)", s->route_type);
        pa_log_debug("  - volume-types : in[%s], out[%s]", s->volume_type[STREAM_DIRECTION_IN], s->volume_type[STREAM_DIRECTION_OUT]);
        pa_log_debug("  - avail-in-devices");
        PA_IDXSET_FOREACH(name, s->idx_avail_in_devices, idx)
            pa_log_debug("      name[%d]  : %s", idx, name);
        pa_log_debug("  - avail-out-devices");
        PA_IDXSET_FOREACH(name, s->idx_avail_out_devices, idx)
            pa_log_debug("      name[%d]  : %s", idx, name);
        pa_log_debug("  - avail-frameworks");
        PA_IDXSET_FOREACH(name, s->idx_avail_frameworks, idx)
            pa_log_debug("      name[%d]  : %s", idx, name);
    }
    pa_log_debug("===========[END stream-map dump]===========");
    return;
}

static int init_stream_map (pa_stream_manager *m) {
    stream_info *s;
    json_object *o;
    json_object *stream_array_o;
    json_object *role_o;
    json_object *priority_o;
    json_object *route_type_o;
    json_object *volume_types_o;
    json_object *is_hal_volume_o;
    json_object *avail_in_devices_o;
    json_object *avail_out_devices_o;
    json_object *avail_frameworks_o;
    int num_of_stream_types = 0;
    const char *role = NULL;
    int i = 0, j = 0;
    int num_of_avail_in_devices;
    int num_of_avail_out_devices;
    int num_of_avail_frameworks;
    json_object *out_device_o;
    json_object *in_device_o;
    json_object *framework_o;
    json_object *stream_o;
    json_object *is_hal_volume_in_o;
    json_object *is_hal_volume_out_o;
    char *volume_type_in_str = NULL;
    char *volume_type_out_str = NULL;
    json_object *volume_type_in_o;
    json_object *volume_type_out_o;
    void *state = NULL;

    pa_assert(m);

    o = json_object_from_file(STREAM_MAP_FILE);
    if(is_error(o)) {
        pa_log_error("Read stream-map file(%s) failed", STREAM_MAP_FILE);
        return -1;
    }
    m->stream_map = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    if((stream_array_o = json_object_object_get(o, STREAM_MAP_STREAMS)) && json_object_is_type(stream_array_o, json_type_array)){
        num_of_stream_types = json_object_array_length(stream_array_o);
        for (i = 0; i < num_of_stream_types; i++) {

            if((stream_o = json_object_array_get_idx(stream_array_o, i)) && json_object_is_type(stream_o, json_type_object)) {
                s = pa_xmalloc0(sizeof(stream_info));
                pa_log_debug("stream found [%d]", i);
                if((role_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_ROLE)) && json_object_is_type(role_o, json_type_string)) {
                    role = json_object_get_string(role_o);
                    pa_log_debug(" - role : %s", role);
                } else {
                    pa_log_error("Get stream role failed");
                    goto failed;
                }
                if((priority_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_PRIORITY)) && json_object_is_type(priority_o, json_type_int)) {
                    s->priority = json_object_get_int(priority_o);
                    pa_log_debug(" - priority : %d", s->priority);
                } else {
                    pa_log_error("Get stream priority failed");
                    goto failed;
                }
                if((route_type_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_ROUTE_TYPE)) && json_object_is_type(route_type_o, json_type_string)) {
                    if (convert_route_type(&(s->route_type), json_object_get_string(route_type_o))) {
                        pa_log_error("convert stream route-type failed");
                        goto failed;
                    }
                    pa_log_debug(" - route-type : %d", s->route_type);
                } else {
                    pa_log_error("Get stream route-type failed");
                    goto failed;
                }
                if((volume_types_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_VOLUME_TYPES)) && json_object_is_type(volume_types_o, json_type_object)) {
                    if((volume_type_in_o = json_object_object_get(volume_types_o, STREAM_MAP_STREAM_VOLUME_TYPE_IN)) && json_object_is_type(volume_type_in_o, json_type_string)) {
                        volume_type_in_str = json_object_get_string(volume_type_in_o);
                        if (!pa_streq(volume_type_in_str, "none"))
                            s->volume_type[STREAM_DIRECTION_IN] = volume_type_in_str;
                    } else {
                        pa_log_error("Get stream volume-type-in failed");
                        goto failed;
                    }
                    if((volume_type_out_o = json_object_object_get(volume_types_o, STREAM_MAP_STREAM_VOLUME_TYPE_OUT)) && json_object_is_type(volume_type_out_o, json_type_string)) {
                        volume_type_out_str = json_object_get_string(volume_type_out_o);
                        if (!pa_streq(volume_type_out_str, "none"))
                            s->volume_type[STREAM_DIRECTION_OUT] = volume_type_out_str;
                    } else {
                        pa_log_error("Get stream volume-type-out failed");
                        goto failed;
                    }
                    pa_log_debug(" - volume-types : in[%s], out[%s]", s->volume_type[STREAM_DIRECTION_IN], s->volume_type[STREAM_DIRECTION_OUT]);
                } else {
                    pa_log_error("Get stream volume-types failed");
                    goto failed;
                }
                if((is_hal_volume_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_VOLUME_IS_FOR_HAL)) && json_object_is_type(is_hal_volume_o, json_type_object)) {
                    if((is_hal_volume_in_o = json_object_object_get(is_hal_volume_o, STREAM_MAP_STREAM_VOLUME_TYPE_IN)) && json_object_is_type(is_hal_volume_in_o, json_type_int))
                        s->is_hal_volume[STREAM_DIRECTION_IN] = (pa_bool_t)json_object_get_int(is_hal_volume_in_o);
                    else {
                        pa_log_error("Get stream is-hal-volume-in failed");
                        goto failed;
                    }
                    if((is_hal_volume_out_o = json_object_object_get(is_hal_volume_o, STREAM_MAP_STREAM_VOLUME_TYPE_OUT)) && json_object_is_type(is_hal_volume_out_o, json_type_int))
                        s->is_hal_volume[STREAM_DIRECTION_OUT] = (pa_bool_t)json_object_get_int(is_hal_volume_out_o);
                    else {
                        pa_log_error("Get stream is-hal-volume-out failed");
                        goto failed;
                    }
                    pa_log_debug(" - is-hal-volume : in[%d], out[%d]", s->is_hal_volume[STREAM_DIRECTION_IN], s->is_hal_volume[STREAM_DIRECTION_OUT]);
                } else {
                    pa_log_error("Get stream volume-types failed");
                    goto failed;
                }
                if((avail_in_devices_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_AVAIL_IN_DEVICES)) && json_object_is_type(avail_in_devices_o, json_type_array)) {
                    j = 0;
                    s->idx_avail_in_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    num_of_avail_in_devices = json_object_array_length(avail_in_devices_o);
                    pa_log_debug(" - avail-in-devices");
                    for (j = 0; j < num_of_avail_in_devices; j++) {
                        if((in_device_o = json_object_array_get_idx(avail_in_devices_o, j)) && json_object_is_type(in_device_o, json_type_string)) {
                            pa_idxset_put(s->idx_avail_in_devices, json_object_get_string(in_device_o), NULL);
                            pa_log_debug("      device[%d] : %s", j, json_object_get_string(in_device_o));
                           }
                       }
                } else {
                    pa_log_error("Get stream avail-in-devices failed");
                    goto failed;
                }
                if((avail_out_devices_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_AVAIL_OUT_DEVICES)) && json_object_is_type(avail_out_devices_o, json_type_array)) {
                    j = 0;
                    s->idx_avail_out_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    num_of_avail_out_devices = json_object_array_length(avail_out_devices_o);
                    pa_log_debug(" - avail-out-devices");
                    for (j = 0; j < num_of_avail_out_devices; j++) {
                        if((out_device_o = json_object_array_get_idx(avail_out_devices_o, j)) && json_object_is_type(out_device_o, json_type_string)) {
                            pa_idxset_put(s->idx_avail_out_devices, json_object_get_string(out_device_o), NULL);
                            pa_log_debug("      device[%d] : %s", j, json_object_get_string(out_device_o));
                           }
                       }
                } else {
                    pa_log_error("Get stream avail-out-devices failed");
                    goto failed;
                }
                if((avail_frameworks_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_AVAIL_FRAMEWORKS)) && json_object_is_type(avail_frameworks_o, json_type_array)) {
                    j = 0;
                    s->idx_avail_frameworks = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    num_of_avail_frameworks = json_object_array_length(avail_frameworks_o);
                    pa_log_debug(" - avail-frameworks");
                    for (j = 0; j < num_of_avail_frameworks; j++) {
                        if((framework_o = json_object_array_get_idx(avail_frameworks_o, j)) && json_object_is_type(framework_o, json_type_string)) {
                            pa_idxset_put(s->idx_avail_frameworks, json_object_get_string(framework_o), NULL);
                            pa_log_debug("      framework[%d] : %s", j, json_object_get_string(framework_o));
                           }
                       }
                } else {
                    pa_log_error("Get stream avail-frameworks failed");
                    goto failed;
                }
                pa_hashmap_put(m->stream_map, role, s);
            }
        }
    } else {
        pa_log_error("Get streams object failed");
        goto failed;
    }

    dump_stream_map(m);

    return 0;
failed:
    if (m->stream_map) {
        PA_HASHMAP_FOREACH(s, m->stream_map, state) {
            if (s->idx_avail_in_devices)
                pa_idxset_free(s->idx_avail_in_devices, NULL);
            if (s->idx_avail_out_devices)
                pa_idxset_free(s->idx_avail_out_devices, NULL);
            if (s->idx_avail_frameworks)
                pa_idxset_free(s->idx_avail_frameworks, NULL);
            pa_xfree(s);
        }
        pa_hashmap_free(m->stream_map);
    }
    return -1;
}

static void deinit_stream_map (pa_stream_manager *m) {
    stream_info *s = NULL;
    void *state = NULL;

    pa_assert(m);
    if (m->stream_map) {
        PA_HASHMAP_FOREACH(s, m->stream_map, state) {
            if (s->idx_avail_in_devices)
                pa_idxset_free(s->idx_avail_in_devices, NULL);
            if (s->idx_avail_out_devices)
                pa_idxset_free(s->idx_avail_out_devices, NULL);
            if (s->idx_avail_frameworks)
                pa_idxset_free(s->idx_avail_frameworks, NULL);
            pa_xfree(s);
        }
        pa_hashmap_free(m->stream_map);
    }

    return;
}

static pa_bool_t check_role_to_skip(pa_stream_manager *m, const char *role) {
    pa_bool_t ret = TRUE;
    stream_info *s = NULL;

    pa_assert(m);
    pa_assert(role);

    if (m->stream_map) {
        s = pa_hashmap_get(m->stream_map, role);
        if (s)
            ret = FALSE;
    }

    pa_log_info("role is [%s], skip(%d)", role, ret);

    return ret;
}

static pa_bool_t update_priority_of_stream(pa_stream_manager *m, process_command_type_t command, stream_type_t type, void *stream, const char *role) {
    stream_info *s = NULL;

    pa_assert(m);
    pa_assert(role);

    if (m->stream_map)
        s = pa_hashmap_get(m->stream_map, role);
    else
        return FALSE;

    if (s) {
        if (command == PROCESS_COMMAND_PREPARE)
            pa_proplist_setf(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_PRIORITY, "%d", s->priority);
        else
            pa_proplist_setf(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_PRIORITY, "%d", s->priority);
    }

    return TRUE;
}

static pa_bool_t update_routing_type_of_stream(pa_stream_manager *m, void *stream, stream_type_t type, const char *role) {
    stream_route_type_t route_type = STREAM_ROUTE_TYPE_AUTO;
    stream_info *s = NULL;

    pa_assert(m);
    pa_assert(role);

    if (m->stream_map) {
        s = pa_hashmap_get(m->stream_map, role);
        if (s)
            route_type = s->route_type;
    } else
        return FALSE;

    pa_proplist_setf(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE, "%d", route_type);

    return TRUE;
}

static pa_bool_t update_volume_type_of_stream(pa_stream_manager *m, stream_type_t type, void *stream, const char *role) {
    const char *volume_type = NULL;
    stream_info *s = NULL;

    pa_assert(m);
    pa_assert(role);

    if (m->stream_map) {
        s = pa_hashmap_get(m->stream_map, role);
        if (s)
            volume_type = s->volume_type[!type];
    } else
        return FALSE;

    if (volume_type)
        pa_proplist_sets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE, volume_type);
    else
        pa_log_warn("this stream[%p] does not have any volume type, skip updating volume type. stream_type[%d], role[%s]", stream, type, role);

    return TRUE;
}

static pa_bool_t update_stream_parent_info(pa_stream_manager *m, process_command_type_t command, stream_type_t type, void *stream) {
    char *p_idx;
    uint32_t parent_idx;
    stream_parent *sp = NULL;

    pa_assert(m);
    pa_assert(stream);

    p_idx = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
    if (p_idx && !pa_atou(p_idx, &parent_idx)) {
        pa_log_debug("p_idx(%s), idx(%u)", p_idx, parent_idx);
        sp = pa_hashmap_get(m->stream_parents, parent_idx);
        if (sp) {
            uint32_t idx = (type==STREAM_SINK_INPUT)?((pa_sink_input*)stream)->index:((pa_source_output*)stream)->index;
            if (command == PROCESS_COMMAND_ADD_PARENT_ID) {
                /* append this stream to the parent stream info. */
                pa_log_debug(" - append this stream(%p, %u) to the list. sp(%p), stream_type(%d)", stream, idx, sp, type);
                pa_idxset_put(type==STREAM_SINK_INPUT?(sp->idx_sink_inputs):(sp->idx_source_outputs), stream, NULL);
                return TRUE;
            } else if (command == PROCESS_COMMAND_REMOVE_PARENT_ID) {
                /* remove this stream from the parent stream info. */
                pa_log_debug(" - remove this stream(%p, %u) from the list. sp(%p), stream_type(%d)", stream, idx, sp, type);
                pa_idxset_remove_by_data(type==STREAM_SINK_INPUT?(sp->idx_sink_inputs):(sp->idx_source_outputs), stream, NULL);
                return TRUE;
            } else {
                pa_log_error("invalid command(%d)", command);
                return FALSE;
            }
        } else {
            pa_log_error("could not find matching client for this parent_id(%u)", parent_idx);
            return FALSE;
        }
    } else {
        pa_log_warn("p_idx(%s) or idx(%u) is not valid", p_idx, parent_idx);
        return FALSE;
    }
    return TRUE;
}

static pa_bool_t update_the_highest_priority_stream(pa_stream_manager *m, process_command_type_t command, void *mine,
                                                    stream_type_t type, const char *role, pa_bool_t *need_to_update) {
    uint32_t idx = 0;
    int32_t p_max = 0;
    int32_t p_mine = 0;
    char *priority = NULL;
    void *cur_max_stream = NULL;
    char *cur_max_priority = NULL;
    char *cur_max_role = NULL;

    pa_assert(m);
    pa_assert(mine);
    if (!role) {
        pa_log_error("invalid input, role(%s)", role);
        return FALSE;
    }

    *need_to_update = FALSE;

    if (type == STREAM_SINK_INPUT) {
        cur_max_stream = m->cur_highest_priority.sink_input;
    } else if (type == STREAM_SOURCE_OUTPUT) {
        cur_max_stream = m->cur_highest_priority.source_output;
    }

    pa_log_info("update_the_highest_priority_stream(), stream_type(%d), role(%s), command(%d)", type, role, command);
    if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA || command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED) {
        if (cur_max_stream == NULL) {
            *need_to_update = TRUE;
            pa_log_debug("set cur_highest to mine");
        } else {
            /* TODO : need to check if this stream should be played to external devices */
            if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA)
                priority = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(mine, type), PA_PROP_MEDIA_ROLE_PRIORITY);
            else {
                if (cur_max_stream == (type==STREAM_SINK_INPUT)?((pa_sink_input*)mine):((pa_source_output*)mine)) {
                    pa_log_debug("it has already been processed for PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED, skip it..");
                    return FALSE;
                }
                priority = pa_proplist_gets(GET_STREAM_PROPLIST(mine, type), PA_PROP_MEDIA_ROLE_PRIORITY);
            }
            cur_max_priority = pa_proplist_gets(GET_STREAM_PROPLIST(cur_max_stream, type), PA_PROP_MEDIA_ROLE_PRIORITY);
            cur_max_role = pa_proplist_gets(GET_STREAM_PROPLIST(cur_max_stream, type), PA_PROP_MEDIA_ROLE);
            if (!cur_max_priority || !cur_max_role) {
                pa_log_error("Failed to pa_proplist_gets() for getting current max priority(%s) and it's role(%s)", cur_max_priority, cur_max_role);
                return FALSE;
            } else {
                if (pa_atoi(priority, &p_mine)) {
                    pa_log_error("Failed to pa_atoi(), priority(%s)", priority);
                    return FALSE;
                }
                if (pa_atoi(cur_max_priority, &p_max)) {
                    pa_log_error("Failed to pa_atoi(), cur_max_priority(%s)", cur_max_priority);
                    return FALSE;
                }
                if (p_mine < p_max) {
                    /* no need to trigger */
                    return TRUE;
                } else {
                    *need_to_update = TRUE;
                    pa_log_debug("update cur_highest to mine(%s)", role);
                }
            }
        }
    } else if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED) {
        void *cur_max_stream_tmp = NULL;
        void *i = NULL;
        char *role = NULL;
        char *priority = NULL;
        int32_t p;
        pa_idxset *streams = NULL;
        if (cur_max_stream == mine) {
            if (type == STREAM_SINK_INPUT) {
                streams = ((pa_sink_input*)mine)->sink->inputs;
            } else if (type == STREAM_SOURCE_OUTPUT) {
                streams = ((pa_source_output*)mine)->source->outputs;
            }
            /* find the next highest priority input */
            //PA_IDXSET_FOREACH(i, m->core->sinks, idx) { /* need to check a sink which this stream belongs to */
            PA_IDXSET_FOREACH(i, streams, idx) {
                if (!(role = pa_proplist_gets(GET_STREAM_PROPLIST(i, type), PA_PROP_MEDIA_ROLE))){
                    pa_log_error("Failed to pa_proplist_gets() for role");
                    continue;
                }
                if (!(priority = pa_proplist_gets(GET_STREAM_PROPLIST(i, type), PA_PROP_MEDIA_ROLE_PRIORITY))){
                    pa_log_error("Failed to pa_proplist_gets() for priority");
                    continue;
                }
                pa_log_debug("role(%s)/priority(%s)/stream(%p)", role, priority, i);
                if (cur_max_priority == NULL) {
                    cur_max_priority = priority;
                    cur_max_stream_tmp = i;
                }
                if (pa_atoi(cur_max_priority, &p_max)) {
                    pa_log_error("Failed to pa_atoi(), cur_max_priority(%s)", cur_max_priority);
                    continue;
                }
                if (pa_atoi(priority, &p)) {
                    pa_log_error("Failed to pa_atoi(), priority(%s)", priority);
                    continue;
                }
                if (p_max <= p) {
                    cur_max_priority = priority;
                    cur_max_stream_tmp = i;
                    p_max = p;
                }
            }
            pa_log_debug("updated max priority(%s)/stream(%p)", cur_max_priority, cur_max_stream_tmp);
            if ((p_max > -1) && cur_max_stream_tmp) {
                if (type == STREAM_SINK_INPUT) {
                    m->cur_highest_priority.sink_input = cur_max_stream_tmp;
                } else if (type == STREAM_SOURCE_OUTPUT) {
                    m->cur_highest_priority.source_output = cur_max_stream_tmp;
                }
            } else {
                if (type == STREAM_SINK_INPUT) {
                    m->cur_highest_priority.sink_input = NULL;
                } else if (type == STREAM_SOURCE_OUTPUT) {
                    m->cur_highest_priority.source_output = NULL;
                }
            }
            *need_to_update = TRUE;
            pa_log_info("need to update: type(%d), cur_highest_priority(sink_input=%p/source_output=%p)",
                        type, (void*)m->cur_highest_priority.sink_input, (void*)m->cur_highest_priority.sink_input);
        } else {
            /* no need to trigger */
            return TRUE;
        }
    }
    return TRUE;
}

static void fill_device_info_to_hook_data(void *hook_data, notify_command_type_t command, stream_type_t type, void *stream, pa_stream_manager *m) {
    char *device_name = NULL;
    char *p_idx = NULL;
    uint32_t parent_idx = 0;
    stream_parent *sp = NULL;
    uint32_t idx = 0;
    pa_stream_manager_hook_data_for_select *select_data = NULL;
    pa_stream_manager_hook_data_for_route *route_data = NULL;
    stream_info *si;
    pa_hashmap *avail_devices;
    int list_len;

    pa_assert(hook_data);
    pa_assert(m);
    pa_log_warn("fill_device_info_to_hook_data() for %s", notify_command_type_str[command]);

    switch (command) {
    case NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT: {
        select_data = (pa_stream_manager_hook_data_for_select*)hook_data;
        si = pa_hashmap_get(m->stream_map, select_data->stream_role);
        avail_devices = (type==STREAM_SINK_INPUT)?si->idx_avail_out_devices:si->idx_avail_in_devices;
        list_len = pa_idxset_size(avail_devices);
        select_data->route_type = si->route_type;
        device_name = pa_idxset_get_by_data(avail_devices, "none", NULL);
        if (list_len == 1 && pa_streq(device_name, "none")) {
            /* no available devices for this role */
        } else {
            select_data->idx_avail_devices = avail_devices;
            if (si->route_type == STREAM_ROUTE_TYPE_MANUAL) {
                p_idx = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
                if (p_idx && !pa_atou(p_idx, &parent_idx)) {
                    /* find parent idx, it's device info. and it's stream idxs */
                    sp = pa_hashmap_get(m->stream_parents, parent_idx);
                    if (sp)
                        select_data->idx_manual_devices = (type==STREAM_SINK_INPUT)?(sp->idx_route_out_devices):(sp->idx_route_in_devices);
                    else
                        pa_log_warn("Failed to get the stream parent of idx(%u)", idx);
                }
            }
        }
        break;
    }
    case NOTIFY_COMMAND_CHANGE_ROUTE_START_WITH_NEW_DATA:
    case NOTIFY_COMMAND_CHANGE_ROUTE_START:
    case NOTIFY_COMMAND_CHANGE_ROUTE_END: {
        route_data = (pa_stream_manager_hook_data_for_route*)hook_data;
        si = pa_hashmap_get(m->stream_map, route_data->stream_role);
        avail_devices = (type==STREAM_SINK_INPUT)?si->idx_avail_out_devices:si->idx_avail_in_devices;
        list_len = pa_idxset_size(avail_devices);
        route_data->route_type = si->route_type;

        if (command == NOTIFY_COMMAND_CHANGE_ROUTE_START_WITH_NEW_DATA)
            p_idx = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
        else if (command == NOTIFY_COMMAND_CHANGE_ROUTE_START || command == NOTIFY_COMMAND_CHANGE_ROUTE_END)
            p_idx = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
        if (p_idx && !pa_atou(p_idx, &parent_idx)) {
            sp = pa_hashmap_get(m->stream_parents, parent_idx);
            if (!sp)
                pa_log_warn("Failed to get the stream parent of idx(%u)", parent_idx);
        } else
           pa_log_warn("Could not get the parent id of this stream, but keep going...");

        device_name = pa_idxset_get_by_data(avail_devices, "none", NULL);
        if (list_len == 1 && pa_streq(device_name, "none")) {
            /* no available devices for this role */
        } else {
            route_data->idx_avail_devices = avail_devices;
            if (si->route_type == STREAM_ROUTE_TYPE_MANUAL) {
                if (sp) {
                    route_data->idx_manual_devices = (type==STREAM_SINK_INPUT)?(sp->idx_route_out_devices):(sp->idx_route_in_devices);
                    route_data->idx_streams = (type==STREAM_SINK_INPUT)?(sp->idx_sink_inputs):(sp->idx_source_outputs);
                } else
                    pa_log_warn("Failed to get the stream parent of idx(%u)", parent_idx);
            }
        }
        break;
    }
    default:
        break;
    }
    return;
}

static void do_notify(pa_stream_manager *m, notify_command_type_t command, stream_type_t type, void *user_data) {
    char *priority = NULL;
    char *role = NULL;
    pa_stream_manager_hook_data_for_select hook_call_select_data;
    pa_stream_manager_hook_data_for_route hook_call_route_data;
    pa_stream_manager_hook_data_for_option hook_call_option_data;
    hal_stream_connection_info stream_conn_info;
    void *s;

    pa_assert(m);
    pa_log_debug("do_notify(%s): type(%d), user_data(%p)", notify_command_type_str[command], type, user_data);

    switch (command) {
    case NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT: {
        pa_assert(user_data);
        memset(&hook_call_select_data, 0, sizeof(pa_stream_manager_hook_data_for_select));
        s = user_data;
        if (s) {
            hook_call_select_data.stream_type = type;
            hook_call_select_data.stream_role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
            fill_device_info_to_hook_data(&hook_call_select_data, command, type, s, m);
            hook_call_select_data.sample_spec = GET_STREAM_NEW_SAMPLE_SPEC(s, type);
            if (type == STREAM_SINK_INPUT)
                hook_call_select_data.proper_sink = &(((pa_sink_input_new_data*)s)->sink);
            else if (type == STREAM_SOURCE_OUTPUT)
                hook_call_select_data.proper_source = &(((pa_source_output_new_data*)s)->source);
            pa_hook_fire(pa_communicator_hook(m->comm.comm, PA_COMMUNICATOR_HOOK_SELECT_INIT_SINK_OR_SOURCE), &hook_call_select_data);
        }
        break;
    }
    case NOTIFY_COMMAND_CHANGE_ROUTE_START_WITH_NEW_DATA:
    case NOTIFY_COMMAND_CHANGE_ROUTE_START: {
        memset(&hook_call_route_data, 0, sizeof(pa_stream_manager_hook_data_for_route));
        s = user_data;
        if (s) {
            if (command == NOTIFY_COMMAND_CHANGE_ROUTE_START_WITH_NEW_DATA) {
                hook_call_route_data.origins_from_new_data = TRUE;
                priority = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(s, type), PA_PROP_MEDIA_ROLE_PRIORITY);
                role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
                hook_call_route_data.sample_spec = GET_STREAM_NEW_SAMPLE_SPEC(s, type);
                if (type == STREAM_SINK_INPUT) {
                    hook_call_route_data.proper_sink = &(((pa_sink_input_new_data*)s)->sink);
                } else if (type == STREAM_SOURCE_OUTPUT) {
                    hook_call_route_data.proper_source = &(((pa_source_output_new_data*)s)->source);
                }
            } else {
                priority = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE_PRIORITY);
                role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
                hook_call_route_data.sample_spec = GET_STREAM_SAMPLE_SPEC(s, type);
                if (type == STREAM_SINK_INPUT) {
                    if (((pa_sink_input*)s)->sink)
                        hook_call_route_data.idx_streams = ((pa_sink_input*)s)->sink->inputs;
                } else if (type == STREAM_SOURCE_OUTPUT) {
                    if (((pa_source_output*)s)->source)
                        hook_call_route_data.idx_streams = ((pa_source_output*)s)->source->outputs;
                }
            }
            hook_call_route_data.stream_type = type;
            hook_call_route_data.stream_role = role;
            fill_device_info_to_hook_data(&hook_call_route_data, command, type, s, m);
            if (hook_call_route_data.route_type == STREAM_ROUTE_TYPE_MANUAL) {
                if (hook_call_route_data.idx_manual_devices && !pa_idxset_size(hook_call_route_data.idx_manual_devices)) {
                    pa_log_info("no manual device for this type(%d), need to unset route", type);
                    hook_call_route_data.stream_role = "reset";
                }
            }
            pa_hook_fire(pa_communicator_hook(m->comm.comm, PA_COMMUNICATOR_HOOK_CHANGE_ROUTE), &hook_call_route_data);
        }
        break;
    }
    case NOTIFY_COMMAND_CHANGE_ROUTE_END: {
        memset(&hook_call_route_data, 0, sizeof(pa_stream_manager_hook_data_for_route));
        s = (type==STREAM_SINK_INPUT)?m->cur_highest_priority.sink_input:m->cur_highest_priority.source_output;
        if (s) {
            priority = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE_PRIORITY);
            role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
            hook_call_route_data.stream_type = type;
            hook_call_route_data.stream_role = role;
            hook_call_route_data.sample_spec = GET_STREAM_SAMPLE_SPEC(s, type);
            hook_call_route_data.idx_streams = (type==STREAM_SINK_INPUT)?((pa_sink_input*)s)->sink->inputs:((pa_source_output*)s)->source->outputs;
            fill_device_info_to_hook_data(&hook_call_route_data, command, type, s, m);
        } else {
            pa_log_info("no stream for this type(%d), need to unset route", type);
            hook_call_route_data.stream_type = type;
            hook_call_route_data.stream_role = "reset";
        }
        pa_hook_fire(pa_communicator_hook(m->comm.comm, PA_COMMUNICATOR_HOOK_CHANGE_ROUTE), &hook_call_route_data);
        break;
    }
    case NOTIFY_COMMAND_UPDATE_ROUTE_OPTION: {
        pa_assert(user_data);
        memset(&hook_call_option_data, 0, sizeof(pa_stream_manager_hook_data_for_option));
        s = (type==STREAM_SINK_INPUT)?m->cur_highest_priority.sink_input:m->cur_highest_priority.source_output;
        if (s) {
            role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
            hook_call_option_data.stream_role = role;
            hook_call_option_data.name = ((stream_route_option*)user_data)->name;
            hook_call_option_data.value = ((stream_route_option*)user_data)->value;
            pa_hook_fire(pa_communicator_hook(m->comm.comm, PA_COMMUNICATOR_HOOK_UPDATE_ROUTE_OPTION), &hook_call_option_data);
        }
        break;
    }
    case NOTIFY_COMMAND_INFORM_STREAM_CONNECTED:
    case NOTIFY_COMMAND_INFORM_STREAM_DISCONNECTED: {
        pa_assert(user_data);
        memset(&stream_conn_info, 0, sizeof(hal_stream_connection_info));
        s = user_data;
        if (s) {
            stream_conn_info.role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
            stream_conn_info.direction = (type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN;
            stream_conn_info.idx = (type==STREAM_SINK_INPUT)?((pa_sink_input*)s)->index:((pa_source_output*)s)->index;
            stream_conn_info.is_connected = (command == NOTIFY_COMMAND_INFORM_STREAM_CONNECTED)?TRUE:FALSE;
            pa_hal_manager_update_stream_connection_info(m->hal, &stream_conn_info);
        }
        break;
    }
    }
    return;
}

static pa_process_stream_result_t process_stream(stream_type_t type, void *stream, process_command_type_t command, pa_stream_manager *m) {
    const char *role;
    pa_bool_t ret = TRUE;
    pa_process_stream_result_t result = PA_PROCESS_STREAM_OK;
    pa_bool_t need_update = FALSE;
    int32_t volume_ret = 0;
    volume_info *v = NULL;
    const char *si_volume_type_str = NULL;
    const char *prior_priority = NULL;
    int32_t prior_p = 0;

    pa_log_info("START process_stream(%s): stream_type(%d), stream(%p), m(%p)", process_command_type_str[command], type, stream, m);
    pa_assert(stream);
    pa_assert(m);

    if (command == PROCESS_COMMAND_PREPARE) {
        if (type == STREAM_SINK_INPUT) {
            /* Parse request formats for samplerate, channel, format infomation */
            if (((pa_sink_input_new_data*)stream)->req_formats) {
                const char *format_str = NULL;
                pa_format_info* req_format = pa_idxset_first(((pa_sink_input_new_data*)stream)->req_formats, NULL);
                if (req_format && req_format->plist) {
                    const char *rate_str = pa_proplist_gets(req_format->plist, PA_PROP_FORMAT_RATE);
                    const char *ch_str = pa_proplist_gets(req_format->plist, PA_PROP_FORMAT_CHANNELS);
                    if (pa_format_info_get_prop_string(req_format, PA_PROP_FORMAT_SAMPLE_FORMAT, &format_str)==0)
                        ((pa_sink_input_new_data*)stream)->sample_spec.format = pa_parse_sample_format(format_str);
                    pa_log_info("req rate(%s), req ch(%s), req format(%s)", rate_str, ch_str, format_str);
                    if (ch_str)
                        ((pa_sink_input_new_data*)stream)->sample_spec.channels = atoi (ch_str);
                    if (rate_str)
                        ((pa_sink_input_new_data*)stream)->sample_spec.rate = atoi (rate_str);
                }
            } else {
                pa_log_debug("no request formats available");
            }
        }
        role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        if (!role) {
            /* set default value for role and priority */
            #define DEFAULT_ROLE "media"
            role = DEFAULT_ROLE;
            pa_proplist_sets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE, DEFAULT_ROLE);
            pa_log_warn("role is null, set default to [%s]", role);
        } else {
            /* skip roles */
            if (check_role_to_skip(m, role)) {
                result = PA_PROCESS_STREAM_SKIP;
                goto FAILURE;
            }
        }
        /* update the priority of this stream */
        ret = update_priority_of_stream(m, command, type, stream, role);
        if (ret == FALSE) {
            pa_log_error("could not update the priority of '%s' role.", role);
            result = PA_PROCESS_STREAM_STOP;
            goto FAILURE;
        }
        /* update the volume type of this stream */
        ret = update_volume_type_of_stream(m, type, stream, role);
        if (ret == FALSE) {
            pa_log_error("could not update the volume type of '%s' role.", role);
            result = PA_PROCESS_STREAM_STOP;
            goto FAILURE;
        }
        /* update the routing type of this stream */
        ret = update_routing_type_of_stream(m, stream, type, role);
        if (ret == FALSE) {
            pa_log_error("could not update the route type of '%s' role.", role);
            result = PA_PROCESS_STREAM_STOP;
            goto FAILURE;
        }

        /* notify to select sink or source */
        do_notify(m, NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT, type, stream);

    } else if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA || command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED) {
        if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA)
            role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        else
            role = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);

        /* skip roles */
        if (check_role_to_skip(m, role)) {
            result = PA_PROCESS_STREAM_SKIP;
            goto FAILURE;
        }

        if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED) {
            /* update the priority of this stream */
            ret = update_priority_of_stream(m, command, type, stream, role);
            if (ret == FALSE) {
                pa_log_error("could not update the priority of '%s' role.", role);
                result = PA_PROCESS_STREAM_STOP;
                goto FAILURE;
            }
        }

        /* update the highest priority */
        ret = update_the_highest_priority_stream(m, command, stream, type,  role, &need_update);
        if (ret == FALSE) {
            pa_log_error("could not update the highest priority stream");
            result = PA_PROCESS_STREAM_SKIP;
            goto FAILURE;
        }

        /* need to skip if this stream does not belong to internal device */
        /* if needed, notify to update */
        if (need_update) {
            if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA) {
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START_WITH_NEW_DATA, type, stream);
                if (type==STREAM_SINK_INPUT)
                    m->cur_highest_priority.need_to_update_si = TRUE;
                else
                    m->cur_highest_priority.need_to_update_so = TRUE;
            } else {
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, type, stream);
                if (type==STREAM_SINK_INPUT)
                    m->cur_highest_priority.sink_input = stream;
                else
                    m->cur_highest_priority.source_output = stream;
            }
        }
        if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED)
            do_notify(m, NOTIFY_COMMAND_INFORM_STREAM_CONNECTED, type, stream);

    } else if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED) {
        role = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        if (role) {
            /* skip roles */
            if (check_role_to_skip(m, role)) {
                result = PA_PROCESS_STREAM_SKIP;
                goto FAILURE;
            }

            /* check if it has already been processed (unlink or state_changed_cb) */
            prior_priority = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)stream)->proplist:((pa_source_output*)stream)->proplist, PA_PROP_MEDIA_ROLE_PRIORITY);
            if (prior_priority && !pa_atoi(prior_priority, &prior_p) && (prior_p == -1)) {
                pa_log_debug("it has already been processed for PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, skip it..");
                result = PA_PROCESS_STREAM_SKIP;
                goto FAILURE;
            }

            /* mark the priority of this stream to -1 */
            pa_proplist_setf(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_PRIORITY, "%d", -1);
            ret = update_the_highest_priority_stream(m, command, stream, type, role, &need_update);
            if (ret == FALSE) {
                pa_log_error("could not update the highest priority stream");
                result = PA_PROCESS_STREAM_STOP;
                goto FAILURE;
            }

            do_notify(m, NOTIFY_COMMAND_INFORM_STREAM_DISCONNECTED, type, stream);

            /* need to skip if this stream does not belong to internal device */
            /* if needed, notify to update */
            if (need_update)
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_END, type, NULL);

        } else {
            pa_log_error("role is null, skip it");
        }

    } else if (command == PROCESS_COMMAND_UPDATE_VOLUME) {
        if ((si_volume_type_str = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
            v = pa_hashmap_get(m->volume_map.out_volumes, si_volume_type_str);
            if (v && v->idx_volume_values) {
                /* Update volume-level */
                volume_ret = set_volume_level_with_new_data(m, type, stream, v->current_level);
                if (volume_ret)
                    pa_log_error("failed to set_volume_level_by_idx(), stream_type(%d), level(%u), ret(0x%x)",
                            STREAM_SINK_INPUT, v->current_level, volume_ret);
                /* Update volume-mute */
                volume_ret = set_volume_mute_with_new_data(m, type, stream, v->is_muted);
                if (volume_ret)
                    pa_log_error("failed to set_volume_mute_by_idx(), stream_type(%d), mute(%d), ret(0x%x)",
                            STREAM_SINK_INPUT, v->is_muted, volume_ret);
            }
        }

    } else if (command == PROCESS_COMMAND_ADD_PARENT_ID || command == PROCESS_COMMAND_REMOVE_PARENT_ID) {
        if (command == PROCESS_COMMAND_ADD_PARENT_ID) {
            if (type == STREAM_SINK_INPUT && m->cur_highest_priority.need_to_update_si) {
                m->cur_highest_priority.sink_input = stream;
                m->cur_highest_priority.need_to_update_si = FALSE;
            }
            if (type == STREAM_SOURCE_OUTPUT && m->cur_highest_priority.need_to_update_so) {
                m->cur_highest_priority.source_output = stream;
                m->cur_highest_priority.need_to_update_so = FALSE;
            }
            if (command == PROCESS_COMMAND_ADD_PARENT_ID)
                do_notify(m, NOTIFY_COMMAND_INFORM_STREAM_CONNECTED, type, stream);
        }
        /* update parent stream info. */
        ret = update_stream_parent_info(m, command, type, stream);
        if (ret == FALSE) {
            pa_log_warn("could not update the parent information of this stream");
            //return PA_PROCESS_STREAM_STOP;
        }
    }

FAILURE:
    pa_log_info("END process_stream(%s): result(%d)", process_command_type_str[command], result);
    return result;
}

static void update_buffer_attribute(pa_hal_manager *h, pa_sink_input_new_data *new_data) {
    int32_t latency = 0;
    int32_t maxlength = -1;
    int32_t tlength = -1;
    int32_t prebuf = -1;
    int32_t minreq = -1;
    int32_t fragsize = -1;
    const char* audio_latency = NULL;

    pa_assert(new_data->proplist);

    if (h == NULL)
        return;

    audio_latency = pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_TIZEN_AUDIO_LATENCY);
    pa_log_info("auido_latency : %s", audio_latency);
    if (audio_latency == NULL)
        return;

    if(pa_atoi(audio_latency, &latency))
        return;

    if (!pa_hal_manager_get_buffer_attribute(h, latency, new_data, &maxlength, &tlength, &prebuf, &minreq, &fragsize)) {
        pa_log_info(" - maxlength:%d, tlength:%d, prebuf:%d, minreq:%d, fragsize:%d", maxlength, tlength, prebuf, minreq, fragsize);
        pa_proplist_setf(new_data->proplist, "maxlength", "%d", maxlength);
        pa_proplist_setf(new_data->proplist, "tlength",   "%d", tlength);
        pa_proplist_setf(new_data->proplist, "prebuf",    "%d", prebuf);
        pa_proplist_setf(new_data->proplist, "minreq",    "%d", minreq);
        pa_proplist_setf(new_data->proplist, "fragsize",  "%d", fragsize);
    }

    return;
}

static pa_hook_result_t sink_input_new_cb(pa_core *core, pa_sink_input_new_data *new_data, pa_stream_manager *m) {
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;

    pa_core_assert_ref(core);

    pa_log_info("start sink_input_new_cb");
    process_result = process_stream(STREAM_SINK_INPUT, new_data, PROCESS_COMMAND_PREPARE, m);
    /* Update buffer attributes from HAL */
    update_buffer_attribute(m->hal, new_data);
    process_result = process_stream(STREAM_SINK_INPUT, new_data, PROCESS_COMMAND_UPDATE_VOLUME, m);
    process_result = process_stream(STREAM_SINK_INPUT, new_data, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_put_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    const char *si_volume_type_str = NULL;
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;

    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    pa_log_info("start sink_input_put_cb, i(%p, index:%u)", i, i->index);
    process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_ADD_PARENT_ID, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_unlink_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;

    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    pa_log_info("start sink_input_unlink_cb, i(%p, index:%u)", i, i->index);
    process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_REMOVE_PARENT_ID, m);
    process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_state_changed_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_sink_input_state_t state;
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;

    pa_assert(i);
    pa_assert(m);

    state = pa_sink_input_get_state(i);
    pa_log_info("start sink_input_state_changed_cb(), sink-input(%p), state(%d)", i, state);

    switch(state) {
    case PA_SINK_INPUT_CORKED: {
        process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, m);
        break;
    }
    case PA_SINK_INPUT_DRAINED:
    case PA_SINK_INPUT_RUNNING: {
        process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED, m);
        break;
    }
    default:
        break;
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_move_start_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    int32_t audio_ret = 0;

    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    /* There's no point in doing anything if the core is shut down anyway */
    if (core->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    pa_log_debug ("sink_input_move_start_cb, i(%p, index:%u)", i, i->index);

    set_volume_mute_by_idx(m, STREAM_SINK_INPUT, i->index, TRUE);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_move_finish_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    int32_t audio_ret = 0;

    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    /* There's no point in doing anything if the core is shut down anyway */
    if (core->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    pa_log_debug ("sink_input_move_finish_cb, i(%p, index:%u)", i, i->index);

    set_volume_mute_by_idx(m, STREAM_SINK_INPUT, i->index, FALSE);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_new_cb(pa_core *core, pa_source_output_new_data *new_data, pa_stream_manager *m) {
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;

    pa_core_assert_ref(core);

    pa_log_info("start source_output_new_new_cb");

    process_result = process_stream(STREAM_SOURCE_OUTPUT, new_data, PROCESS_COMMAND_PREPARE, m);
    /* Update buffer attributes from HAL */
    update_buffer_attribute(m->hal, new_data);
    process_result = process_stream(STREAM_SOURCE_OUTPUT, new_data, PROCESS_COMMAND_UPDATE_VOLUME, m);
    process_result = process_stream(STREAM_SOURCE_OUTPUT, new_data, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_put_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;

    pa_core_assert_ref(core);
    pa_source_output_assert_ref(o);

    pa_log_info("start source_output_put_cb, o(%p, index:%u)", o, o->index);

    process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_ADD_PARENT_ID, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_unlink_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;

    pa_core_assert_ref(core);
    pa_source_output_assert_ref(o);

    pa_log_info("start source_output_unlink_cb, o(%p, index:%u)", o, o->index);

    process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_REMOVE_PARENT_ID, m);
    process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_state_changed_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_source_output_state_t state;
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;

    pa_assert(o);
    pa_assert(m);

    state = pa_source_output_get_state(o);
    pa_log_debug("start source_output_state_changed_cb(), source-output(%p), state(%d)", o, state);

    switch(state) {
    case PA_SOURCE_OUTPUT_CORKED: {
        process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, m);
        break;
    }
    case PA_SOURCE_OUTPUT_RUNNING: {
        process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED, m);
        break;
    }
    default:
        break;
    }

    return PA_HOOK_OK;
}

/* Reorganize routing when a device has been connected or disconnected */
static pa_hook_result_t device_connection_changed_hook_cb(pa_core *c, pa_device_manager_hook_data_for_conn_changed *conn, pa_stream_manager *m) {
    const char *route_type_str = NULL;
    stream_route_type_t route_type;
    dm_device_direction_t device_direction = DM_DEVICE_DIRECTION_OUT;

    device_direction = pa_device_manager_get_device_direction(conn->device);
    pa_log_info("device_connection_changed_hook_cb is called. conn(%p), is_connected(%d), device(%p), direction(%p)",
            conn, conn->is_connected, conn->device, device_direction);

    /* If the route type of the stream is not manual, notify again */
    if (m->cur_highest_priority.source_output && (device_direction & DM_DEVICE_DIRECTION_IN)) {
        route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(m->cur_highest_priority.source_output, STREAM_SOURCE_OUTPUT), PA_PROP_MEDIA_ROLE_ROUTE_TYPE);
        if(!pa_atoi(route_type_str, &route_type))
            if (route_type != STREAM_ROUTE_TYPE_MANUAL)
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, STREAM_SOURCE_OUTPUT, m->cur_highest_priority.source_output);
    } if (m->cur_highest_priority.sink_input && (device_direction & DM_DEVICE_DIRECTION_OUT)) {
        route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(m->cur_highest_priority.sink_input, STREAM_SINK_INPUT), PA_PROP_MEDIA_ROLE_ROUTE_TYPE);
        if(!pa_atoi(route_type_str, &route_type))
            if (route_type != STREAM_ROUTE_TYPE_MANUAL)
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, STREAM_SINK_INPUT, m->cur_highest_priority.sink_input);
    }

    return PA_HOOK_OK;
}

/* Reorganize routing when device information has been changed */
static pa_hook_result_t device_information_changed_hook_cb(pa_core *c, pa_device_manager_hook_data_for_info_changed *info, pa_stream_manager *m) {
    pa_log_info("device_information_changed_hook_cb is called. info(%p), changed_info(%d), device(%p)",
            info, info->changed_info, info->device);

    return PA_HOOK_OK;
}

static void subscribe_cb(pa_core *core, pa_subscription_event_type_t t, uint32_t idx, pa_stream_manager *m) {
    pa_client *client = NULL;
    stream_parent *sp = NULL;
    const char *name = NULL;
    uint32_t *device_id = NULL;
    uint32_t _idx = 0;
    pa_core_assert_ref(core);
    pa_assert(m);
    pa_log_info("subscribe_cb() is called, t(%x), idx(%u)", t, idx);

    if (t == (PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_CHANGE)) {
        client = pa_idxset_get_by_index(core->clients, idx);
        if (client == NULL) {
            pa_log_error(" - could not find any client that has idx(%u)", idx);
            return;
        }
        name = pa_proplist_gets(client->proplist, PA_PROP_APPLICATION_NAME);
        if (strncmp (name, STREAM_MANAGER_CLIENT_NAME, strlen(STREAM_MANAGER_CLIENT_NAME))) {
            pa_log_warn(" - this is not a client(%s) that we should take care of, skip it", name);
            return;
        }
        /* add a stream parent */
        sp = pa_xmalloc0(sizeof(stream_parent));
        sp->idx_sink_inputs = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
        sp->idx_source_outputs = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
        sp->idx_route_in_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        sp->idx_route_out_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        pa_hashmap_put(m->stream_parents, idx, sp);
        pa_log_debug(" - add sp(%p), idx(%u)", sp, idx);
     } else if (t == (PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_REMOVE)) {
        /* remove the stream parent */
        sp = pa_hashmap_get(m->stream_parents, idx);
        if (sp) {
            pa_log_debug(" - remove sp(%p), idx(%u)", sp, idx);
            pa_hashmap_remove(m->stream_parents, idx);
            if (sp->idx_route_in_devices)
                PA_IDXSET_FOREACH(device_id, sp->idx_route_in_devices, _idx)
                    pa_xfree(device_id);
            if (sp->idx_route_out_devices)
                PA_IDXSET_FOREACH(device_id, sp->idx_route_out_devices, _idx)
                    pa_xfree(device_id);
            pa_idxset_free(sp->idx_sink_inputs, NULL);
            pa_idxset_free(sp->idx_source_outputs, NULL);
            pa_idxset_free(sp->idx_route_in_devices, NULL);
            pa_idxset_free(sp->idx_route_out_devices, NULL);
            pa_xfree(sp);
        } else {
            pa_log_error(" - could not find any stream_parent that has idx(%u)", idx);
        }
    }
}

static int init_ipc (pa_stream_manager *m) {
#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    pa_assert(m);
    pa_log_info("Initialization for IPC");
    m->dbus_protocol = pa_dbus_protocol_get(m->core);
    pa_assert_se(pa_dbus_protocol_add_interface(m->dbus_protocol, STREAM_MANAGER_OBJECT_PATH, &stream_manager_interface_info, m) >= 0);
    pa_assert_se(pa_dbus_protocol_register_extension(m->dbus_protocol, STREAM_MANAGER_INTERFACE) >= 0);
#else
    DBusError err;
    pa_dbus_connection *conn = NULL;
    static const DBusObjectPathVTable vtable = {
        .message_function = method_handler_for_vt,
    };

    pa_assert(m);
    pa_log_info("Initialization for IPC");

    dbus_error_init(&err);

    if (!(conn = pa_dbus_bus_get(m->core, DBUS_BUS_SYSTEM, &err)) || dbus_error_is_set(&err)) {
        if (conn) {
            pa_dbus_connection_unref(conn);
        }
        pa_log_error("Unable to contact D-Bus system bus: %s: %s", err.name, err.message);
        goto fail;
    } else {
        pa_log_notice("Got dbus connection");
    }
    m->dbus_conn = conn;
    pa_assert_se(dbus_connection_register_object_path(pa_dbus_connection_get(conn), STREAM_MANAGER_OBJECT_PATH, &vtable, m));
#endif
#else
    pa_log_error("DBUS is not supported\n");
    goto fail;
#endif

    return 0;
fail:
    return -1;
}

static void deinit_ipc (pa_stream_manager *m) {
    pa_assert(m);

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    if (m->dbus_protocol) {
        pa_assert_se(pa_dbus_protocol_unregister_extension(m->dbus_protocol, STREAM_MANAGER_INTERFACE) >= 0);
        pa_assert_se(pa_dbus_protocol_remove_interface(m->dbus_protocol, STREAM_MANAGER_OBJECT_PATH, stream_manager_interface_info.name) >= 0);
        pa_dbus_protocol_unref(m->dbus_protocol);
        m->dbus_protocol = NULL;
    }
#else
    if (m->dbus_conn) {
        if(!dbus_connection_unregister_object_path(pa_dbus_connection_get(m->dbus_conn), STREAM_MANAGER_OBJECT_PATH))
            pa_log_error("Failed to unregister object path");
        m->dbus_conn = NULL;
    }
#endif
#endif
    return;
}

pa_stream_manager* pa_stream_manager_init(pa_core *c) {
    pa_stream_manager *m;

    pa_assert(c);

    m = pa_xnew0(pa_stream_manager, 1);
    m->core = c;

    m->hal = pa_hal_manager_get(c, NULL);

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    m->dbus_protocol = NULL;
#else
    m->dbus_conn = NULL;
#endif
#endif
    if (init_ipc(m))
        goto fail;

    if (init_stream_map(m))
        goto fail;

    if (init_volume_map(m))
        goto fail;

    m->stream_parents = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    m->sink_input_new_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_new_cb, m);
    m->sink_input_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_put_cb, m);
    m->sink_input_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_unlink_cb, m);
    m->sink_input_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_STATE_CHANGED], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_state_changed_cb, m);
    m->sink_input_move_start_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_START], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_move_start_cb, m);
    m->sink_input_move_finish_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_FINISH], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_move_finish_cb, m);
    m->source_output_new_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_NEW], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_new_cb, m);
    m->source_output_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_PUT], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_put_cb, m);
    m->source_output_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_unlink_cb, m);
    m->source_output_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_STATE_CHANGED], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_state_changed_cb, m);


    m->subscription = pa_subscription_new(m->core, PA_SUBSCRIPTION_MASK_CLIENT | PA_SUBSCRIPTION_MASK_SAMPLE_CACHE, subscribe_cb, m);

    m->comm.comm = pa_communicator_get(c);
    m->comm.comm_hook_device_connection_changed_slot = pa_hook_connect(pa_communicator_hook(m->comm.comm,PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED),
            PA_HOOK_EARLY + 10, (pa_hook_cb_t) device_connection_changed_hook_cb, m);
    m->comm.comm_hook_device_information_changed_slot = pa_hook_connect(pa_communicator_hook(m->comm.comm,PA_COMMUNICATOR_HOOK_DEVICE_INFORMATION_CHANGED),
            PA_HOOK_EARLY, (pa_hook_cb_t) device_information_changed_hook_cb, m);

    return m;

fail:
    pa_log_error("Failed to initialize stream-manager");
    deinit_volume_map(m);
    deinit_stream_map(m);
    deinit_ipc(m);
    if (m->hal)
        pa_hal_manager_unref(m->hal);
    pa_xfree(m);
    return 0;
}

void pa_stream_manager_done(pa_stream_manager *m) {
    pa_assert(m);

    if (m->comm.comm) {
        if (m->comm.comm_hook_device_connection_changed_slot)
            pa_hook_slot_free(m->comm.comm_hook_device_connection_changed_slot);
        if (m->comm.comm_hook_device_information_changed_slot)
            pa_hook_slot_free(m->comm.comm_hook_device_information_changed_slot);
        pa_communicator_unref(m->comm.comm);
    }

    if (m->subscription)
        pa_subscription_free(m->subscription);

    if (m->sink_input_new_slot)
        pa_hook_slot_free(m->sink_input_new_slot);
    if (m->sink_input_put_slot)
        pa_hook_slot_free(m->sink_input_put_slot);
    if (m->sink_input_unlink_slot)
        pa_hook_slot_free(m->sink_input_unlink_slot);
    if (m->sink_input_state_changed_slot)
        pa_hook_slot_free(m->sink_input_state_changed_slot);
    if (m->sink_input_move_start_slot)
        pa_hook_slot_free(m->sink_input_move_start_slot);
    if (m->sink_input_move_finish_slot)
        pa_hook_slot_free(m->sink_input_move_finish_slot);
    if (m->source_output_new_slot)
        pa_hook_slot_free(m->source_output_new_slot);
    if (m->source_output_put_slot)
        pa_hook_slot_free(m->source_output_put_slot);
    if (m->source_output_unlink_slot)
        pa_hook_slot_free(m->source_output_unlink_slot);
    if (m->source_output_state_changed_slot)
        pa_hook_slot_free(m->source_output_state_changed_slot);

    if (m->stream_parents)
        pa_hashmap_free(m->stream_parents);

    deinit_volume_map(m);
    deinit_stream_map(m);
    deinit_ipc(m);

    if (m->hal)
        pa_hal_manager_unref(m->hal);

    pa_xfree(m);
}
