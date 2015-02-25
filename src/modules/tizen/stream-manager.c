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
#include <pulsecore/namereg.h>
#include <pulsecore/sink.h>
#include <pulsecore/modargs.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#ifdef HAVE_DBUS
#include <pulsecore/dbus-shared.h>
#include <pulsecore/protocol-dbus.h>
#include <pulsecore/dbus-util.h>
#endif

#include <json.h>
#include "tizen-audio.h"
#include "stream-manager.h"
#include "communicator.h"

#ifdef HAVE_DBUS
#define ARR_ARG_MAX  32
#define STREAM_MANAGER_OBJECT_PATH "/org/pulseaudio/Ext/StreamManager"
#define STREAM_MANAGER_INTERFACE   "org.pulseaudio.Ext.StreamManager"
#define STREAM_MANAGER_METHOD_NAME_GET_STREAM_INFO    "GetStreamInfo"
#define STREAM_MANAGER_METHOD_NAME_GET_STREAM_LIST    "GetStreamList"
#define STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_DEVICES      "SetStreamRouteDevices"
#define STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTIONS      "SetStreamRouteOptions"

static DBusHandlerResult method_handler_for_vt(DBusConnection *c, DBusMessage *m, void *userdata);
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_stream_info(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_stream_list(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_stream_route_devices(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_stream_route_options(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum method_handler_index {
    METHOD_HANDLER_GET_STREAM_INFO,
    METHOD_HANDLER_GET_STREAM_LIST,
    METHOD_HANDLER_SET_STREAM_ROUTE_DEVICES,
    METHOD_HANDLER_SET_STREAM_ROUTE_OPTIONS,
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
                                                     { "route_in_devices", "as", "in" },
                                                    { "route_out_devices", "as", "in" },
                                                            { "ret_msg", "s", "out" } };
static pa_dbus_arg_info set_stream_route_options_args[]  = { { "parent_id", "u", "in" },
                                                              { "options", "as", "in" },
                                                            { "ret_msg", "s", "out" } };
static char* signature_args_for_in[] = { "s", "", "uasas", "uas"};

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
    [METHOD_HANDLER_SET_STREAM_ROUTE_OPTIONS] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTIONS,
        .arguments = set_stream_route_options_args,
        .n_arguments = sizeof(set_stream_route_options_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_stream_route_options }
};

const char* stream_manager_dbus_ret_str[] = {"STREAM_MANAGER_RETURN_OK","STREAM_MANAGER_RETURN_ERROR"};
enum {
    RET_MSG_INDEX_OK,
    RET_MSG_INDEX_ERROR
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
    "   <arg name=\"route_in_devices\" direction=\"in\" type=\"as\"/>"       \
    "   <arg name=\"route_out_devices\" direction=\"in\" type=\"as\"/>"      \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTIONS\">"\
    "   <arg name=\"parent_id\" direction=\"in\" type=\"u\"/>"               \
    "   <arg name=\"options\" direction=\"in\" type=\"as\"/>"                \
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
#define STREAM_PROCESSED_USING_PUT_UNLINK_1 "VIRTUAL_STREAM"
#define STREAM_PROCESSED_USING_PUT_UNLINK_2 "SIMPLE_PLAY"

typedef enum pa_process_stream_result {
    PA_PROCESS_STREAM_OK,
    PA_PROCESS_STREAM_STOP,
    PA_PROCESS_STREAM_SKIP,
} pa_process_stream_result_t;

typedef enum _process_command_type {
    PROCESS_COMMAND_PREPARE,
    PROCESS_COMMAND_START,
    PROCESS_COMMAND_END,
} process_command_type;

typedef enum _notify_command_type {
    NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT,
    NOTIFY_COMMAND_CHANGE_ROUTE,
    NOTIFY_COMMAND_UPDATE_ROUTE_OPTIONS,
} notify_command_type;

typedef struct _prior_max_priority_stream {
    pa_sink_input *sink_input;
    pa_source_output *source_output;
} cur_max_priority_stream;

struct _stream_manager {
    pa_core *core;
    pa_hashmap *stream_map;
    pa_hashmap *stream_parents;
    cur_max_priority_stream cur_highest_priority;
    pa_hook_slot
        *sink_input_new_slot,
        *sink_input_put_slot,
        *sink_input_unlink_slot,
        *sink_input_state_changed_slot,
        *source_output_new_slot,
        *source_output_put_slot,
        *source_output_unlink_slot,
        *source_output_state_changed_slot;
#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    pa_dbus_protocol *dbus_protocol;
#else
    pa_dbus_connection *dbus_conn;
#endif
#endif
    pa_subscription *subscription;
    pa_communicator *comm;
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
#define STREAM_MAP_STREAM_CAPTURE_VOLUME_TYPE "capture-volume-type"
#define STREAM_MAP_STREAM_PLAYBACK_VOLUME_TYPE "playback-volume-type"
#define STREAM_MAP_STREAM_AVAIL_IN_DEVICES "avail-in-devices"
#define STREAM_MAP_STREAM_AVAIL_OUT_DEVICES "avail-out-devices"
#define STREAM_MAP_STREAM_AVAIL_FRAMEWORKS "avail-frameworks"

enum stream_direction {
    STREAM_DIRECTION_IN,
    STREAM_DIRECTION_OUT,
    STREAM_DIRECTION_MAX,
};

typedef struct _stream_info {
    char *role;
    int32_t priority;
    char *volume_types[STREAM_DIRECTION_MAX];
    stream_route_type route_type;
    pa_idxset *idx_avail_in_devices;
    pa_idxset *idx_avail_out_devices;
    pa_idxset *idx_avail_frameworks;
} stream_info;

typedef struct _stream_parent {
    pa_idxset *idx_sink_inputs;
    pa_idxset *idx_source_outputs;
    pa_idxset *idx_route_in_devices;
    pa_idxset *idx_route_out_devices;
    pa_idxset *idx_route_options;
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

static void do_notify(notify_command_type command, stream_type type, pa_stream_manager *m, void *user_data);

static int get_available_streams_from_map(pa_stream_manager *m, stream_list *list) {
    void *state;
    stream_info *s;
    int i = 0;
    pa_log_info("get_available_streams_from_map");
    if (m->stream_map) {
        PA_HASHMAP_FOREACH(s, m->stream_map, state) {
            if (i < AVAIL_STREAMS_MAX) {
                list->priorities[i] = s->priority;
                list->types[i++] = s->role;
                pa_log_debug("  [%d] stream_type[%s], priority[%d]", i-1, s->role, s->priority);
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
    void *state = NULL;
    uint32_t idx = 0;
    char *name;
    stream_info *s;
    int i = 0;
    int j = 0;
    int k = 0;
    pa_log_info("get_stream_info_from_map : role[%s]", stream_role);
    if (m->stream_map) {
        PA_HASHMAP_FOREACH(s, m->stream_map, state) {
            if (pa_streq(stream_role, s->role)) {
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
                break;
            }
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
    DBusMessageIter variant_iter;
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
    const char **in_device_list = NULL;
    const char **out_device_list = NULL;
    int list_len_in = 0;
    int list_len_out = 0;
    stream_parent *sp = NULL;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &in_device_list, &list_len_in,
                                       DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &out_device_list, &list_len_out,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_stream_route_devices(), id(%u), in_device_list(%p):length(%d), out_device_list(%p):length(%d)",
            id, in_device_list, list_len_in, out_device_list, list_len_out);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    sp = pa_hashmap_get(m->stream_parents, id);
    if (sp) {
        pa_idxset_remove_all(sp->idx_route_in_devices, NULL);
        pa_idxset_remove_all(sp->idx_route_out_devices, NULL);
        for (i = 0; i < list_len_in; i++) {
            pa_idxset_put(sp->idx_route_in_devices, in_device_list[i], NULL);
            pa_log_debug(" -- [in][%s]", in_device_list[i]);
        }
        for (i = 0; i < list_len_out; i++) {
            pa_idxset_put(sp->idx_route_out_devices, out_device_list[i], NULL);
            pa_log_debug(" -- [out][%s]", out_device_list[i]);
        }

        /* if any stream that belongs to this id has been activated, do notify right away */
        if (m->cur_highest_priority.sink_input) {
            if (pa_idxset_get_by_data(sp->idx_sink_inputs, &((m->cur_highest_priority.sink_input)->index), NULL)) {
                pa_log_debug(" -- cur_highest_priority.sink_input->index[%u] belongs to this parent id[%u], do notify for the route change",
                    (m->cur_highest_priority.sink_input)->index, id);
                do_notify(NOTIFY_COMMAND_CHANGE_ROUTE, STREAM_SINK_INPUT, m, NULL);
            }
        }
        if (m->cur_highest_priority.source_output) {
            if (pa_idxset_get_by_data(sp->idx_source_outputs, &((m->cur_highest_priority.source_output)->index), NULL)) {
                pa_log_debug(" -- cur_highest_priority.source_output->index[%u] belongs to this parent id[%u], do notify for the route change",
                    (m->cur_highest_priority.source_output)->index, id);
                do_notify(NOTIFY_COMMAND_CHANGE_ROUTE, STREAM_SOURCE_OUTPUT, m, NULL);
            }
        }
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    } else {
        pa_log_error("could not find matching client for this parent_id(%d)", id);
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    }

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static void handle_set_stream_route_options(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    uint32_t id = 0;
    int i = 0;
    const char **option_list = NULL;
    int list_len = 0;
    stream_parent *sp = NULL;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &option_list, &list_len,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_stream_route_options(), option_list(%p), list_len(%d)", option_list, list_len);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    sp = pa_hashmap_get(m->stream_parents, id);
    if (sp) {
        pa_idxset_remove_all(sp->idx_route_options, NULL);
        for (i = 0; i < list_len; i++) {
            pa_idxset_put(sp->idx_route_options, option_list[i], NULL);
            pa_log_debug(" -- [option][%s]", option_list[i]);
        }

        /* if any stream that belongs to this id has been activated, do notify right away */
        if (m->cur_highest_priority.sink_input) {
            if (pa_idxset_get_by_data(sp->idx_sink_inputs, &((m->cur_highest_priority.sink_input)->index), NULL)) {
                pa_log_debug(" -- cur_highest_priority.sink_input->index[%u] belongs to this parent id[%u], do notify for the options",
                    (m->cur_highest_priority.sink_input)->index, id);
                do_notify(NOTIFY_COMMAND_UPDATE_ROUTE_OPTIONS, STREAM_SINK_INPUT, m, sp->idx_route_options);
            }
        }
        if (m->cur_highest_priority.source_output) {
            if (pa_idxset_get_by_data(sp->idx_source_outputs, &((m->cur_highest_priority.source_output)->index), NULL)) {
                pa_log_debug(" -- cur_highest_priority.source_output->index[%u] belongs to this parent id[%u], do notify for the options",
                    (m->cur_highest_priority.source_output)->index, id);
                do_notify(NOTIFY_COMMAND_UPDATE_ROUTE_OPTIONS, STREAM_SOURCE_OUTPUT, m, sp->idx_route_options);
            }
        }
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    } else {
        pa_log_error("could not find matching client for this parent_id(%d)", id);
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    }

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata) {
	int idx = 0;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    for (idx = 0; idx < METHOD_HANDLER_MAX; idx++) {
        if (dbus_message_is_method_call(msg, STREAM_MANAGER_INTERFACE, method_handlers[idx].method_name )) {
            pa_log_debug("Message signature %s, Expected %s", dbus_message_get_signature(msg), signature_args_for_in[idx]);
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

static int convert_route_type(stream_route_type *route_type, const char *route_type_string) {
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
static int init_stream_map (pa_stream_manager *m) {
    stream_info *s;
    json_object *o;
    json_object *stream_array_o;
    json_object *role_o;
    json_object *priority_o;
    json_object *route_type_o;
    json_object *volume_types_o;
    json_object *avail_in_devices_o;
    json_object *avail_out_devices_o;
    json_object *avail_frameworks_o;
    int num_of_stream_types = 0;
    int i = 0;
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
            json_object *stream_o;
            if((stream_o = json_object_array_get_idx(stream_array_o, i)) && json_object_is_type(stream_o, json_type_object)) {
                char *string;
                s = pa_xmalloc0(sizeof(stream_info));
                pa_log_debug("stream found [%d]", i);
                if((role_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_ROLE)) && json_object_is_type(role_o, json_type_string)) {
                    s->role = json_object_get_string(role_o);
                    pa_log_debug(" - role : %s", s->role);
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
                    json_object *volume_type_in_o;
                    json_object *volume_type_out_o;
                    if((volume_type_in_o = json_object_object_get(volume_types_o, STREAM_MAP_STREAM_VOLUME_TYPE_IN)) && json_object_is_type(volume_type_in_o, json_type_string)) {
                        s->volume_types[STREAM_DIRECTION_IN] = json_object_get_string(volume_type_in_o);
                    } else {
                        pa_log_error("Get stream volume-type-in failed");
                        goto failed;
                    }
                    if((volume_type_out_o = json_object_object_get(volume_types_o, STREAM_MAP_STREAM_VOLUME_TYPE_OUT)) && json_object_is_type(volume_type_out_o, json_type_string)) {
                        s->volume_types[STREAM_DIRECTION_OUT] = json_object_get_string(volume_type_out_o);
                    } else {
                        pa_log_error("Get stream volume-type-out failed");
                        goto failed;
                    }
                    pa_log_debug(" - volume-types : in[%s], out[%s]", s->volume_types[STREAM_DIRECTION_IN], s->volume_types[STREAM_DIRECTION_OUT]);
                } else {
                    pa_log_error("Get stream volume-types failed");
                    goto failed;
                }
                if((avail_in_devices_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_AVAIL_IN_DEVICES)) && json_object_is_type(avail_in_devices_o, json_type_array)) {
                    int j = 0;
                    json_object *in_device_o;
                    s->idx_avail_in_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    int num_of_avail_in_devices = json_object_array_length(avail_in_devices_o);
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
                    int j = 0;
                    json_object *out_device_o;
                    s->idx_avail_out_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    int num_of_avail_out_devices = json_object_array_length(avail_out_devices_o);
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
                    int j = 0;
                    json_object *framework_o;
                    s->idx_avail_frameworks = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    int num_of_avail_frameworks = json_object_array_length(avail_frameworks_o);
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
                pa_hashmap_put(m->stream_map, s->role, s);
            }
        }
    } else {
        pa_log_error("Get streams object failed");
        goto failed;
    }
    return 0;
failed:
    if (m->stream_map) {
        if (s->idx_avail_in_devices)
            pa_idxset_free(s->idx_avail_in_devices, NULL);
        if (s->idx_avail_out_devices)
            pa_idxset_free(s->idx_avail_out_devices, NULL);
        if (s->idx_avail_frameworks)
            pa_idxset_free(s->idx_avail_frameworks, NULL);
        pa_hashmap_free(m->stream_map);
    }
    return -1;
}

static void deinit_stream_map (pa_stream_manager *m) {
    pa_assert(m);
    if (m->stream_map) {
        stream_info *s;
        void *state = NULL;
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

static void dump_stream_map (pa_stream_manager *m) {
    pa_assert(m);
    pa_log_debug("==========[START stream-map dump]==========");
    if (m->stream_map) {
        stream_info *s;
        char *name;
        void *state = NULL;
        uint32_t idx = 0;
        PA_HASHMAP_FOREACH(s, m->stream_map, state) {
            pa_log_debug("[role : %s]", s->role);
            pa_log_debug("  - prirority  : %d", s->priority);
            pa_log_debug("  - route-type : %d (0:auto,1:auto-all,2:manual,3:manual-all)", s->route_type);
            pa_log_debug("  - volume-types : in[%s], out[%s]", s->volume_types[STREAM_DIRECTION_IN], s->volume_types[STREAM_DIRECTION_OUT]);
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
    }
    pa_log_debug("===========[END stream-map dump]===========");
    return;
}

static pa_bool_t check_role_to_skip(const char *role, pa_stream_manager *m) {
    pa_bool_t ret = TRUE;
    pa_assert(role);
    pa_assert(m);

    if (m->stream_map) {
        void *state;
        stream_info *s;
        PA_HASHMAP_FOREACH(s, m->stream_map, state) {
            if (pa_streq(role, s->role)) {
                ret = FALSE;
                break;
            }
        }
    }

    pa_log_info("role is %s, skip(%d)", role, ret);

    return ret;
}

static pa_bool_t update_priority_of_stream(stream_type type, void *stream, const char *role, pa_stream_manager *m, int32_t *priority) {
    pa_assert(role);
    pa_assert(m);

    if (m->stream_map) {
        stream_info *s;
        s = pa_hashmap_get(m->stream_map, role);
        *priority = s->priority;
    }

    pa_proplist_setf(type==STREAM_SINK_INPUT?((pa_sink_input*)stream)->proplist:((pa_source_output*)stream)->proplist, PA_PROP_MEDIA_ROLE_PRIORITY, "%d", *priority);

    return TRUE;
}

static pa_bool_t update_routing_type_of_stream(stream_type type, void *stream, const char *role, pa_stream_manager *m) {
    stream_route_type route_type = STREAM_ROUTE_TYPE_AUTO;
    pa_assert(role);
    pa_assert(m);

    if (m->stream_map) {
        stream_info *s = pa_hashmap_get(m->stream_map, role);
        route_type = s->route_type;
    }

    pa_proplist_setf(type==STREAM_SINK_INPUT?((pa_sink_input*)stream)->proplist:((pa_source_output*)stream)->proplist, PA_PROP_MEDIA_ROLE_ROUTE_TYPE, "%d", route_type);

    return TRUE;
}

static pa_bool_t update_stream_parent_info(process_command_type command, stream_type type, void *stream, pa_stream_manager *m) {
    char *p_idx;
    uint32_t idx;

    pa_assert(stream);
    pa_assert(m);

    p_idx = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)stream)->proplist:((pa_source_output*)stream)->proplist, PA_PROP_MEDIA_PARENT_ID);
    if (p_idx && !pa_atou(p_idx, &idx)) {
        pa_log_debug("***p_idx(%s), idx(%u)", p_idx, idx);
        stream_parent *sp = NULL;
        sp = pa_hashmap_get(m->stream_parents, idx);
        if (sp) {
            uint32_t *idx_addr = (type==STREAM_SINK_INPUT)?&((pa_sink_input*)stream)->index:&((pa_source_output*)stream)->index;
            if (command == PROCESS_COMMAND_START) {
                /* append this stream to the parent stream info. */
                pa_log_debug(" - append this idx_addr(%p),idx(%u) to the list, sp(%p), stream_type(%d)", idx_addr, *idx_addr, sp, type);
                pa_idxset_put(type==STREAM_SINK_INPUT?(sp->idx_sink_inputs):(sp->idx_source_outputs), idx_addr, NULL);
                return TRUE;
            } else if (command == PROCESS_COMMAND_END) {
                /* remove this stream from the parent stream info. */
                pa_log_debug(" - remove this idx_addr(%p),idx(%u) from the list, sp(%p), stream_type(%d)", idx_addr, *idx_addr, sp, type);
                pa_idxset_remove_by_data(type==STREAM_SINK_INPUT?(sp->idx_sink_inputs):(sp->idx_source_outputs), idx_addr, NULL);
                return TRUE;
            } else {
                pa_log_error("invalid command(%d)", command);
                return FALSE;
            }
        } else {
            pa_log_error("could not find matching client for this parent_id(%u)", idx);
            return FALSE;
        }
    } else {
        pa_log_error("p_idx(%s) or idx(%u) is not valid", p_idx, idx);
        return FALSE;
    }
    return TRUE;
}

static pa_bool_t update_the_highest_priority_stream(stream_type type, void *mine, const char *role, int32_t priority, pa_stream_manager *m, pa_bool_t *need_to_update) {
    uint32_t idx = 0;
    int32_t p_max;
    void *cur_max_stream = NULL;
    char *cur_max_priority = NULL;
    char *cur_max_role = NULL;
    *need_to_update = FALSE;

    pa_assert(mine);
    pa_assert(m);
    if (!role) {
        pa_log_error("invalid input, role(%s)", role);
        return FALSE;
    }

    if (type == STREAM_SINK_INPUT) {
        cur_max_stream = m->cur_highest_priority.sink_input;
    } else if (type == STREAM_SOURCE_OUTPUT) {
        cur_max_stream = m->cur_highest_priority.source_output;
    }

    pa_log_error("stream : type(%d), role(%s), priority(%d) ", type, role, priority);
    if (priority != -1) {
        if (cur_max_stream == NULL) {
            *need_to_update = TRUE;
            pa_log_debug("set cur_highest to mine");
            if (type == STREAM_SINK_INPUT) {
                m->cur_highest_priority.sink_input = mine;
            } else if (type == STREAM_SOURCE_OUTPUT) {
                m->cur_highest_priority.source_output = mine;
            }
        } else {
            /* TODO : need to check if this stream should be played to external devices */
            cur_max_priority = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)cur_max_stream)->proplist:((pa_source_output*)cur_max_stream)->proplist, PA_PROP_MEDIA_ROLE_PRIORITY);
            cur_max_role = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)cur_max_stream)->proplist:((pa_source_output*)cur_max_stream)->proplist, PA_PROP_MEDIA_ROLE);
            if (!cur_max_priority || !cur_max_role) {
                pa_log_error("Failed to pa_proplist_gets() for getting current max priority(%s) and it's role(%s)", cur_max_priority, cur_max_role);
                return FALSE;
            } else {
                if (pa_atoi(cur_max_priority, &p_max)) {
                    pa_log_error("Failed to pa_atoi(), cur_max_priority(%s)", cur_max_priority);
                    return FALSE;
                }
                if (priority < p_max) {
                    /* no need to trigger */
                    return TRUE;
                } else {
                    *need_to_update = TRUE;
                    pa_log_debug("update cur_highest to mine(%s)", role);
                    if (type == STREAM_SINK_INPUT) {
                        m->cur_highest_priority.sink_input = mine;
                    } else if (type == STREAM_SOURCE_OUTPUT) {
                        m->cur_highest_priority.source_output = mine;
                    }
                }
            }
        }
    } else {
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
                if (!(role = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)i)->proplist:((pa_source_output*)i)->proplist, PA_PROP_MEDIA_ROLE))){
                    pa_log_error("Failed to pa_proplist_gets() for role");
                    continue;
                }
                if (!(priority = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)i)->proplist:((pa_source_output*)i)->proplist, PA_PROP_MEDIA_ROLE_PRIORITY))){
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

static void fill_device_info_to_hook_data(void *hook_data, notify_command_type command, stream_type type, void *stream, pa_stream_manager *m) {
    int i = 0;
    char *device_name = NULL;
    char *p_idx = NULL;
    uint32_t idx;
    stream_parent *sp = NULL;
    pa_idxset *devices = NULL;
    uint32_t _idx = 0;
    char *device_type = NULL;
    pa_assert(hook_data);
    pa_assert(m);
    switch (command) {
    case NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT: {
        pa_stream_manager_hook_data_for_select *data = (pa_stream_manager_hook_data_for_select*)hook_data;
        stream_info *si = pa_hashmap_get(m->stream_map, data->stream_role);
        pa_idxset *avail_devices = (type==STREAM_SINK_INPUT)?si->idx_avail_out_devices:si->idx_avail_in_devices;
        int list_len = pa_idxset_size(avail_devices);
        data->route_type = si->route_type;

        if (si->route_type == STREAM_ROUTE_TYPE_AUTO || si->route_type == STREAM_ROUTE_TYPE_AUTO_ALL) {
            device_name = pa_idxset_get_by_data(avail_devices, "none", NULL);
            if (list_len == 1 && pa_streq(device_name, "none")) {
                /* no available devices for this role */
            } else {
                data->avail_devices = avail_devices;
            }
        } else if (si->route_type == STREAM_ROUTE_TYPE_MANUAL) {
            p_idx = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input_new_data*)stream)->proplist:((pa_source_output_new_data*)stream)->proplist, PA_PROP_MEDIA_PARENT_ID);
            if (p_idx && !pa_atou(p_idx, &idx)) {
                /* find parent idx, it's device info. and it's children idxs */
                sp = pa_hashmap_get(m->stream_parents, idx);
                if (sp) {
                    devices = type==STREAM_SINK_INPUT?(sp->idx_route_out_devices):(sp->idx_route_in_devices);
                    if (devices) {
                        PA_IDXSET_FOREACH(device_type, devices, _idx) {
                            /* check if it is in the avail device list */
                            device_name = pa_idxset_get_by_data(avail_devices, device_type, NULL);
                            if (device_name == NULL) {
                                pa_log_error("Failed to get a device for manual routing, (%s) is not available for (%s)", device_type, data->stream_role);
                            } else {
                                device *device = pa_xmalloc0(sizeof(device));
                                /* we restrict only once device temporarily */
                                if (!data->manual_devices)
                                    data->manual_devices = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
                                /* TODO : get id from sound-manager */
                                static int id = 1; /* temporary */
                                device->id = ++id;
                                device->type = device_name;
                                pa_hashmap_put(data->manual_devices, device->id, device);
                                break;
                            }
                        }
                    } else {
                        pa_log_error("Failed to get a device for manual routing");
                    }
                } else {
                    pa_log_error("Failed to get the stream parent of idx(%u)", idx);
                }
            }
        }
        break;
    }
    case NOTIFY_COMMAND_CHANGE_ROUTE: {
        pa_stream_manager_hook_data_for_route *data = (pa_stream_manager_hook_data_for_route*)hook_data;
        stream_info *si = pa_hashmap_get(m->stream_map, data->stream_role);
        pa_hashmap *avail_devices = (type==STREAM_SINK_INPUT)?si->idx_avail_out_devices:si->idx_avail_in_devices;
        int list_len = pa_idxset_size(avail_devices);
        data->route_type = si->route_type;

        p_idx = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)stream)->proplist:((pa_source_output*)stream)->proplist, PA_PROP_MEDIA_PARENT_ID);
        if (p_idx && !pa_atou(p_idx, &idx)) {
            sp = pa_hashmap_get(m->stream_parents, idx);
            if (sp) {
                /* set route options */
                data->route_options = sp->idx_route_options;
                /* set streams that belongs to this idx */
                data->streams = type==STREAM_SINK_INPUT?sp->idx_sink_inputs:sp->idx_source_outputs;
            } else {
                pa_log_error("Failed to get the stream parent of idx(%u)", idx);
                break;
            }
        } else {
            break;
        }

        if (si->route_type == STREAM_ROUTE_TYPE_AUTO || si->route_type == STREAM_ROUTE_TYPE_AUTO_ALL) {
            char *device_name = pa_idxset_get_by_data(avail_devices, "none", NULL);
            if (list_len == 1 && pa_streq(device_name, "none")) {
                /* no available devices for this role */
            } else {
                data->avail_devices = avail_devices;
            }
        } else if (si->route_type == STREAM_ROUTE_TYPE_MANUAL) {
            devices = type==STREAM_SINK_INPUT?(sp->idx_route_out_devices):(sp->idx_route_in_devices);
            if (devices) {
                PA_IDXSET_FOREACH(device_type, devices, _idx) {
                    /* check if it is in the avail device list */
                    device_name = pa_idxset_get_by_data(avail_devices, device_type, NULL);
                    if (device_name == NULL) {
                        pa_log_error("Failed to get a device for manual routing, (%s) is not available for (%s)", device_type, data->stream_role);
                    } else {
                        device *device = pa_xmalloc0(sizeof(device));
                        /* we restrict only once device temporarily */
                        if (!data->manual_devices)
                            data->manual_devices = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
                        /* TODO : get id from sound-manager */
                            static int id = 1; /* temporary */
                            device->id = ++id;
                            device->type = device_name;
                            pa_hashmap_put(data->manual_devices, device->id, device);
                            break;
                        }
                    }
            } else {
                pa_log_error("Failed to get a device for manual routing");
            }
        }
        break;
    }
    default:
        break;
    }
    return;
}

static void do_notify(notify_command_type command, stream_type type, pa_stream_manager *m, void *user_data) {
    char *priority = NULL;
    char *role = NULL;
    void *state = NULL;
    device *d = NULL;
    pa_assert(m);
    pa_log_debug("do_notify() : command(%d), type(%d), user_data(%p)", command, type, user_data);

    switch (command) {
    case NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT: {
        pa_assert(user_data);
        pa_stream_manager_hook_data_for_select hook_call_data;
        memset(&hook_call_data, 0, sizeof(pa_stream_manager_hook_data_for_select));
        device* device_list = NULL;
        void *s = user_data;
        hook_call_data.stream_type = type;
        hook_call_data.stream_role = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input_new_data*)s)->proplist:((pa_source_output_new_data*)s)->proplist, PA_PROP_MEDIA_ROLE);
        fill_device_info_to_hook_data(&hook_call_data, command, type, s, m);
        hook_call_data.sample_spec.format = (type==STREAM_SINK_INPUT?((pa_sink_input_new_data*)s)->sample_spec.format:((pa_source_output_new_data*)s)->sample_spec.format);
        hook_call_data.sample_spec.rate = (type==STREAM_SINK_INPUT?((pa_sink_input_new_data*)s)->sample_spec.rate:((pa_source_output_new_data*)s)->sample_spec.rate);
        if (type == STREAM_SINK_INPUT)
            hook_call_data.proper_sink = &(((pa_sink_input_new_data*)s)->sink);
        else if (type == STREAM_SOURCE_OUTPUT)
            hook_call_data.proper_source = &(((pa_source_output_new_data*)s)->source);
        pa_hook_fire(pa_communicator_hook(m->comm, PA_COMMUNICATOR_HOOK_SELECT_INIT_SINK_OR_SOURCE), &hook_call_data);
        if (hook_call_data.manual_devices) {
            PA_HASHMAP_FOREACH(d, hook_call_data.manual_devices, state)
                pa_xfree(d);
            pa_hashmap_free(hook_call_data.manual_devices);
        }
#if 0
        {
            /* TODO : need to notify to change route if needed                               */
            /* check if 1. this new role is needed to change route before opening the device */
            /*          2. if yes, do others exist? if no, do below                          */
            pa_stream_manager_hook_data_for_route hook_call_data;
            pa_hook_fire(pa_communicator_hook(m->comm, PA_COMMUNICATOR_HOOK_CHANGE_ROUTE), &hook_call_data);
        }
#endif
        break;
    }
    case NOTIFY_COMMAND_CHANGE_ROUTE: {
        pa_stream_manager_hook_data_for_route hook_call_data;
        memset(&hook_call_data, 0, sizeof(pa_stream_manager_hook_data_for_route));
        device* device_list = NULL;
        void *s = (type==STREAM_SINK_INPUT)?m->cur_highest_priority.sink_input:m->cur_highest_priority.source_output;
        if (s) {
            priority = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)s)->proplist:((pa_source_output*)s)->proplist, PA_PROP_MEDIA_ROLE_PRIORITY);
            role = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)s)->proplist:((pa_source_output*)s)->proplist, PA_PROP_MEDIA_ROLE);
            hook_call_data.stream_type = type;
            hook_call_data.stream_role = role;
            hook_call_data.sample_spec.format = (type==STREAM_SINK_INPUT?((pa_sink_input*)s)->sample_spec.format:((pa_source_output*)s)->sample_spec.format);
            hook_call_data.sample_spec.rate = (type==STREAM_SINK_INPUT?((pa_sink_input*)s)->sample_spec.rate:((pa_source_output*)s)->sample_spec.rate);
            fill_device_info_to_hook_data(&hook_call_data, command, type, s, m);
        } else {
            pa_log_info("no stream for this type(%d), need to unset route", type);
            hook_call_data.stream_type = type;
            hook_call_data.stream_role = "reset";
        }
        pa_hook_fire(pa_communicator_hook(m->comm, PA_COMMUNICATOR_HOOK_CHANGE_ROUTE), &hook_call_data);
        if (hook_call_data.manual_devices) {
            PA_HASHMAP_FOREACH(d, hook_call_data.manual_devices, state)
                pa_xfree(d);
            pa_hashmap_free(hook_call_data.manual_devices);
        }
        break;
    }
    case NOTIFY_COMMAND_UPDATE_ROUTE_OPTIONS: {
        pa_assert(user_data);
        pa_stream_manager_hook_data_for_options hook_call_data;
        memset(&hook_call_data, 0, sizeof(pa_stream_manager_hook_data_for_options));
        void *s = (type==STREAM_SINK_INPUT)?m->cur_highest_priority.sink_input:m->cur_highest_priority.source_output;
        if (s) {
            role = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)s)->proplist:((pa_source_output*)s)->proplist, PA_PROP_MEDIA_ROLE);
            hook_call_data.stream_role = role;
        }
        hook_call_data.route_options = (pa_idxset*)user_data;
        pa_hook_fire(pa_communicator_hook(m->comm, PA_COMMUNICATOR_HOOK_UPDATE_ROUTE_OPTIONS), &hook_call_data);
        break;
    }
    }

    return;
}

static pa_process_stream_result_t process_stream(stream_type type, void *stream, process_command_type command, pa_stream_manager *m) {
    const char *role;
    pa_bool_t ret = TRUE;
    pa_bool_t need_update = FALSE;

    pa_log_info("START process_stream(): stream_type(%d), stream(%p), m(%p), command(%d)", type, stream, m, command);
    pa_assert(stream);
    pa_assert(m);

    if (command == PROCESS_COMMAND_PREPARE) {
        role = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input_new_data*)stream)->proplist:((pa_source_output_new_data*)stream)->proplist, PA_PROP_MEDIA_ROLE);
        if (!role) {
            /* set default value for role and priority */
            #define DEFAULT_ROLE "media"
            pa_proplist_sets(type==STREAM_SINK_INPUT?((pa_sink_input_new_data *)stream)->proplist:((pa_source_output_new_data *)stream)->proplist, PA_PROP_MEDIA_ROLE, DEFAULT_ROLE);
            pa_log_error("role is null, set default to (%s)", DEFAULT_ROLE);
        } else {
            /* skip roles */
            if (check_role_to_skip(role, m))
                return PA_PROCESS_STREAM_SKIP;
        }

        /* notify to update */
        do_notify(NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT, type, m, stream);

    } else {
        role = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)stream)->proplist:((pa_source_output*)stream)->proplist, PA_PROP_MEDIA_ROLE);
        if (command == PROCESS_COMMAND_START) {
            pa_log_debug("stream(%s) is about to be started", role);
            int32_t priority = 0;
            pa_log_error("role is (%s)", role);

            /* skip roles */
            if (check_role_to_skip(role, m))
                return PA_PROCESS_STREAM_SKIP;

            /* update the priority of this stream */
            ret = update_priority_of_stream(type, stream, role, m, &priority);
            if (ret == FALSE) {
                pa_log_error("could not update the priority of '%s' role.", role);
                return PA_PROCESS_STREAM_STOP;
            }
            /* update the routing type of this stream */
            ret = update_routing_type_of_stream(type, stream, role, m);
            if (ret == FALSE) {
                pa_log_error("could not update the route type of '%s' role.", role);
                return PA_PROCESS_STREAM_STOP;
            }
            /* update the highest priority */
            ret = update_the_highest_priority_stream(type, stream, role, priority, m, &need_update);
            if (ret == FALSE) {
                pa_log_error("could not update the highest priority stream");
                return PA_PROCESS_STREAM_STOP;
            }
            /* update parent stream info. */
            ret = update_stream_parent_info(command, type, stream, m);
            if (ret == FALSE) {
                pa_log_error("could not update the parent information of this stream");
                return PA_PROCESS_STREAM_STOP;
            }
            /* need to skip if this stream does not belong to internal device */
            /* if needed, notify to update */
            if (need_update)
                do_notify(NOTIFY_COMMAND_CHANGE_ROUTE, type, m, NULL);

        } else if (command == PROCESS_COMMAND_END) {
            pa_log_debug("stream(%s) is about to be ended", role);
            if (role) {
                /* skip roles */
                if (check_role_to_skip(role, m))
                    return PA_PROCESS_STREAM_SKIP;
                /* mark the priority of this stream to -1 */
                pa_proplist_setf(type==STREAM_SINK_INPUT?((pa_sink_input*)stream)->proplist:((pa_source_output*)stream)->proplist, PA_PROP_MEDIA_ROLE_PRIORITY, "%d", -1);
                ret = update_the_highest_priority_stream(type, stream, role, -1, m, &need_update);
                if (ret == FALSE) {
                    pa_log_error("could not update the highest priority stream");
                    return PA_PROCESS_STREAM_STOP;
                }
                /* update parent stream info. */
                ret = update_stream_parent_info(command, type, stream, m);
                if (ret == FALSE) {
                    pa_log_error("could not update the parent information of this stream");
                    return PA_PROCESS_STREAM_STOP;
                }
                /* need to skip if this stream does not belong to internal device */
                /* if needed, notify to update */
                if (need_update)
                    do_notify(NOTIFY_COMMAND_CHANGE_ROUTE, type, m, NULL);
            } else {
                pa_log_error("role is null, skip it");
            }
        }
    }
    pa_log_info("END process_stream()");
    return PA_PROCESS_STREAM_OK;
}

static pa_bool_t is_good_to_process(stream_type type, void *stream) {
    /* Normally, routing process is on input/output state changed cb.      */
    /* but if a stream named as below, routing process is on put/unlink cb.*/
    /* Later on it could be changed if it is possible to get notified via  */
    /* input/output state change cb.                                       */
    const char *name = pa_proplist_gets(type==STREAM_SINK_INPUT?((pa_sink_input*)stream)->proplist:((pa_source_output*)stream)->proplist, PA_PROP_MEDIA_NAME);
    if (strncmp (name, STREAM_PROCESSED_USING_PUT_UNLINK_1, strlen(STREAM_PROCESSED_USING_PUT_UNLINK_1)) ||
        strncmp (name, STREAM_PROCESSED_USING_PUT_UNLINK_2, strlen(STREAM_PROCESSED_USING_PUT_UNLINK_2)) ) {
        return TRUE;
    }
    return FALSE;
}

static pa_hook_result_t sink_input_new_cb(pa_core *core, pa_sink_input_new_data *new_data, pa_stream_manager *m) {
    pa_log_info("start sink_input_new_cb");
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
    pa_core_assert_ref(core);

    process_result = process_stream(STREAM_SINK_INPUT, new_data, PROCESS_COMMAND_PREPARE, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_put_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_log_info("start sink_input_put_cb, i(%p, index:%u)", i, i->index);
    pa_bool_t ret = FALSE;
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    ret = is_good_to_process(STREAM_SINK_INPUT, i);
    if (ret) {
        pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
        process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_START, m);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_unlink_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_log_info("start sink_input_unlink_cb, i(%p, index:%u)", i, i->index);
    pa_bool_t ret = FALSE;
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    ret = is_good_to_process(STREAM_SINK_INPUT, i);
    if (ret) {
        pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
        process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_END, m);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_state_changed_hook_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_sink_input_state_t state;

    pa_assert(i);
    pa_assert(m);

    state = pa_sink_input_get_state(i);
    pa_log_debug("start sink_input_state_changed_hook_cb(), sink-input(%p), state(%d)", i, state);

    switch(state) {
    case PA_SINK_INPUT_CORKED: {
        pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
        process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_END, m);
        break;
    }
    case PA_SINK_INPUT_DRAINED:
    case PA_SINK_INPUT_RUNNING: {
        pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
        process_result = process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_START, m);
        break;
    }
    default:
        break;
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_new_cb(pa_core *core, pa_source_output_new_data *new_data, pa_stream_manager *m) {
    pa_log_info("start source_output_new_new_cb");
    pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
    pa_core_assert_ref(core);

    process_result = process_stream(STREAM_SOURCE_OUTPUT, new_data, PROCESS_COMMAND_PREPARE, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_put_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_log_info("start source_output_put_cb, o(%p, index:%u)", o, o->index);
    pa_bool_t ret = FALSE;
    pa_core_assert_ref(core);
    pa_source_output_assert_ref(o);

    ret = is_good_to_process(STREAM_SOURCE_OUTPUT, o);
    if (ret) {
        pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
        process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_START, m);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_unlink_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_log_info("start source_output_unlink_cb, o(%p, index:%u)", o, o->index);
    pa_bool_t ret = FALSE;
    pa_core_assert_ref(core);
    pa_source_output_assert_ref(o);

    ret = is_good_to_process(STREAM_SOURCE_OUTPUT, o);
    if (ret) {
        pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
        process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_END, m);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_state_changed_hook_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_source_output_state_t state;

    pa_assert(o);
    pa_assert(m);

    state = pa_source_output_get_state(o);
    pa_log_debug("start source_output_state_changed_hook_cb(), source-output(%p), state(%d)", o, state);

    switch(state) {
    case PA_SINK_INPUT_CORKED: {
        pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
        process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_END, m);
        break;
    }
    case PA_SINK_INPUT_DRAINED:
    case PA_SINK_INPUT_RUNNING: {
        pa_process_stream_result_t process_result = PA_PROCESS_STREAM_OK;
        process_result = process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_START, m);
        break;
    }
    default:
        break;
    }

    return PA_HOOK_OK;
}

static void subscribe_cb(pa_core *core, pa_subscription_event_type_t t, uint32_t idx, pa_stream_manager *m) {
    pa_core_assert_ref(core);
    pa_assert(m);
    pa_client *client = NULL;
    const char *name = NULL;
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
        stream_parent *sp = pa_xmalloc0(sizeof(stream_parent));
        sp->idx_sink_inputs = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
        sp->idx_source_outputs = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
        sp->idx_route_in_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        sp->idx_route_out_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        sp->idx_route_options = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        pa_hashmap_put(m->stream_parents, idx, sp);
        pa_log_debug(" - add sp(%p), idx(%u)", sp, idx);
     } else if (t == (PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_REMOVE)) {
        /* remove the stream parent */
        stream_parent *sp = NULL;
        sp = pa_hashmap_get(m->stream_parents, idx);
        if (sp) {
            pa_log_debug(" - remove sp(%p), idx(%u)", sp, idx);
            pa_hashmap_remove(m->stream_parents, idx);
            pa_idxset_free(sp->idx_sink_inputs, NULL);
            pa_idxset_free(sp->idx_source_outputs, NULL);
            pa_idxset_free(sp->idx_route_in_devices, NULL);
            pa_idxset_free(sp->idx_route_out_devices, NULL);
            pa_idxset_free(sp->idx_route_options, NULL);
            pa_xfree(sp);
        } else {
            pa_log_error(" - could not find any stream_parent that has idx(%u)", idx);
        }
    }
}

static int init_ipc (pa_stream_manager *m) {

    pa_assert(m);

    pa_log_info("Initialization for IPC");

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    m->dbus_protocol = pa_dbus_protocol_get(m->core);
    pa_assert_se(pa_dbus_protocol_add_interface(m->dbus_protocol, STREAM_MANAGER_OBJECT_PATH, &stream_manager_interface_info, m) >= 0);
    pa_assert_se(pa_dbus_protocol_register_extension(m->dbus_protocol, STREAM_MANAGER_INTERFACE) >= 0);
#else
    DBusError err;
    pa_dbus_connection *conn = NULL;
    static const DBusObjectPathVTable vtable = {
        .message_function = method_handler_for_vt,
    };
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
    const char *ipc_type = NULL;

    pa_assert(c);

    m = pa_xnew0(pa_stream_manager, 1);
    m->core = c;

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    m->dbus_protocol = NULL;
#else
    m->dbus_conn = NULL;
#endif
#endif
    if (init_ipc(m))
        goto fail;
#if 1
    if (init_stream_map(m))
        goto fail;
#endif
    dump_stream_map(m);

    m->stream_parents = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    m->sink_input_new_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_new_cb, m);
    m->sink_input_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_put_cb, m);
    m->sink_input_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_unlink_cb, m);
    m->sink_input_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_STATE_CHANGED], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_state_changed_hook_cb, m);
    m->source_output_new_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_NEW], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_new_cb, m);
    m->source_output_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_PUT], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_put_cb, m);
    m->source_output_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_unlink_cb, m);
    m->source_output_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_STATE_CHANGED], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_state_changed_hook_cb, m);

    m->subscription = pa_subscription_new(m->core, PA_SUBSCRIPTION_MASK_CLIENT | PA_SUBSCRIPTION_MASK_SAMPLE_CACHE, subscribe_cb, m);

    m->comm = pa_communicator_get(c);

    return m;

fail:
    deinit_stream_map(m);
    deinit_ipc(m);
    pa_xfree(m);
    return 0;
}

void pa_stream_manager_done(pa_stream_manager *m) {
    pa_assert(m);

    if (m->comm)
        pa_communicator_unref(m->comm);

    if (m->subscription)
        pa_subscription_free(m->subscription);

    if (m->sink_input_new_slot)
        pa_hook_slot_free(m->sink_input_new_slot);
    if (m->sink_input_put_slot)
        pa_hook_slot_free(m->sink_input_put_slot);
    if (m->sink_input_unlink_slot)
        pa_hook_slot_free(m->sink_input_unlink_slot);
    if (m->source_output_new_slot)
        pa_hook_slot_free(m->source_output_new_slot);
    if (m->source_output_put_slot)
        pa_hook_slot_free(m->source_output_put_slot);
    if (m->source_output_unlink_slot)
        pa_hook_slot_free(m->source_output_unlink_slot);

    if (m->stream_parents)
        pa_hashmap_free(m->stream_parents);

    deinit_stream_map(m);

    deinit_ipc(m);

    pa_xfree(m);
}
