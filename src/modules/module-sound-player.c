/***
  This file is part of PulseAudio.

  Copyright 2014 Sangchul Lee <sc11.lee@samsung.com>

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

#include "module-sound-player-symdef.h"

#include <pulsecore/core-scache.h>

PA_MODULE_AUTHOR("Sangchul Lee");
PA_MODULE_DESCRIPTION("Sound Player module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);

#ifdef HAVE_DBUS
#define ARR_ARG_MAX  32
#define SOUND_PLAYER_OBJECT_PATH "/org/pulseaudio/SoundPlayer"
#define SOUND_PLAYER_INTERFACE   "org.pulseaudio.SoundPlayer"
#define SOUND_PLAYER_METHOD_NAME_SIMPLE_PLAY      "SimplePlay"
#define SOUND_PLAYER_METHOD_NAME_SAMPLE_PLAY      "SamplePlay"
#define SOUND_PLAYER_SIGNAL_EOS                   "EOS"

static DBusHandlerResult method_handler_for_vt(DBusConnection *c, DBusMessage *m, void *userdata);
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_simple_play(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_sample_play(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum method_handler_index {
    METHOD_HANDLER_SIMPLE_PLAY,
    METHOD_HANDLER_SAMPLE_PLAY,
    METHOD_HANDLER_MAX
};

static pa_dbus_arg_info simple_play_args[]    = { { "uri", "s", "in" },
                                                 { "role", "s", "in" },
                                         { "volume_gain", "s", "in" }};
static pa_dbus_arg_info sample_play_args[]    = { { "sample_name", "s", "in" },
                                                         { "role", "s", "in" },
                                                 { "volume_gain", "s", "in" }};

static const char* signature_args_for_in[] = { "sss", "sss" };

static pa_dbus_method_handler method_handlers[METHOD_HANDLER_MAX] = {
    [METHOD_HANDLER_SIMPLE_PLAY] = {
        .method_name = SOUND_PLAYER_METHOD_NAME_SIMPLE_PLAY,
        .arguments = simple_play_args,
        .n_arguments = sizeof(simple_play_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_simple_play },
    [METHOD_HANDLER_SAMPLE_PLAY] = {
        .method_name = SOUND_PLAYER_METHOD_NAME_SAMPLE_PLAY,
        .arguments = sample_play_args,
        .n_arguments = sizeof(sample_play_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_sample_play }
};

#ifdef USE_DBUS_PROTOCOL

static pa_dbus_interface_info sound_player_interface_info = {
    .name = SOUND_PLAYER_INTERFACE,
    .method_handlers = method_handlers,
    .n_method_handlers = METHOD_HANDLER_MAX,
    .property_handlers = ,
    .n_property_handlers = ,
    .get_all_properties_cb =,
    .signals =,
    .n_signals =
};

#else

#define SOUND_PLAYER_INTROSPECT_XML                                     \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                           \
    "<node>"                                                            \
    " <interface name=\"SOUND_PLAYER_INTERFACE\">"                      \
    "  <method name=\"SOUND_PLAYER_METHOD_NAME_SIMPLE_PLAY\">"          \
    "   <arg name=\"uri\" direction=\"in\" type=\"s\"/>"                \
    "   <arg name=\"role\" direction=\"in\" type=\"s\"/>"               \
    "   <arg name=\"volume_gain\" direction=\"in\" type=\"s\"/>"        \
    "  </method>"                                                       \
    "  <method name=\"SOUND_PLAYER_METHOD_NAME_SAMPLE_PLAY\">"          \
    "   <arg name=\"sample_name\" direction=\"in\" type=\"s\"/>"        \
    "   <arg name=\"role\" direction=\"in\" type=\"s\"/>"               \
    "   <arg name=\"volume_gain\" direction=\"in\" type=\"s\"/>"        \
    "  </method>"                                                       \
    " </interface>"                                                     \
    " <interface name=\"org.freedesktop.DBus.Introspectable\">"         \
    "  <method name=\"Introspect\">"                                    \
    "   <arg name=\"data\" type=\"s\" direction=\"out\"/>"              \
    "  </method>"                                                       \
    " </interface>"                                                     \
    "</node>"
#endif

#endif

static void io_event_callback(pa_mainloop_api *io, pa_io_event *e, int fd, pa_io_event_flags_t events, void*userdata);

struct userdata {
    int fd;
    pa_io_event *io;
    pa_module *module;
    pa_hook_slot *sink_input_unlink_slot;
#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    pa_dbus_protocol *dbus_protocol;
#else
    pa_dbus_connection *dbus_conn;
#endif
    pa_idxset *stream_idxs;
#endif
};

#define FILE_FULL_PATH 1024        /* File path length */
#define ROLE_NAME_LEN 64                /* Role name length */
#define VOLUME_GAIN_TYPE_LEN 64    /* Volume gain type length */

struct ipc_data {
    char filename[FILE_FULL_PATH];
    char role[ROLE_NAME_LEN];
    char volume_gain_type[VOLUME_GAIN_TYPE_LEN];
};

#define KEYTONE_PATH        "/tmp/keytone"  /* Keytone pipe path */
#define KEYTONE_GROUP       6526            /* Keytone group : assigned by security */
#define DEFAULT_IPC_TYPE    IPC_TYPE_PIPE

#define MAX_NAME_LEN 256
static int _simple_play(struct userdata *u, const char *file_path, const char *role, const char *vol_gain_type) {
    int ret = 0;
    pa_sink *sink = NULL;
    pa_proplist *p;
    const char *name_prefix = "SIMPLE_PLAY";

    char name[MAX_NAME_LEN] = {0};

    uint32_t stream_idx = 0;
    uint32_t scache_idx = 0;

    p = pa_proplist_new();

    /* Set role type of stream */
    if (role)
        pa_proplist_sets(p, PA_PROP_MEDIA_ROLE, role);

    /* Set volume gain type of stream */
    if (vol_gain_type)
        pa_proplist_sets(p, PA_PROP_MEDIA_TIZEN_VOLUME_GAIN_TYPE, vol_gain_type);

    sink = pa_namereg_get_default_sink(u->module->core);

    pa_log_debug("role[%s], volume_gain_type[%s]", role, vol_gain_type);
    snprintf(name, sizeof(name)-1, "%s_%s", name_prefix, file_path);
    scache_idx = pa_scache_get_id_by_name(u->module->core, name);
    if (scache_idx != PA_IDXSET_INVALID) {
        pa_log_debug("found cached index [%u] for name [%s]", scache_idx, file_path);
    } else {
        /* for more precision, need to update volume value here */
        if ((ret = pa_scache_add_file_lazy(u->module->core, name, file_path, &scache_idx)) != 0) {
            pa_log_error("failed to add file [%s]", file_path);
            goto exit;
        } else {
            pa_log_debug("success to add file [%s], index [%u]", file_path, scache_idx);
        }
    }

    pa_log_debug("pa_scache_play_item() start");
    if ((ret = pa_scache_play_item(u->module->core, name, sink, PA_VOLUME_NORM, p, &stream_idx) < 0)) {
        pa_log_error("pa_scache_play_item fail");
        goto exit;
    }
    pa_log_debug("pa_scache_play_item() end, stream_idx(%u)", stream_idx);

    if (!ret)
        ret = (int32_t)stream_idx;
exit:
    pa_proplist_free(p);
    return ret;
}

static int _sample_play(struct userdata *u, const char *sample_name, const char *role, const char *vol_gain_type) {
    int ret = 0;
    pa_sink *sink = NULL;
    pa_proplist *p;

    uint32_t stream_idx = 0;
    uint32_t scache_idx = 0;

    p = pa_proplist_new();

    /* Set role type of stream */
    if (role)
        pa_proplist_sets(p, PA_PROP_MEDIA_ROLE, role);

    /* Set volume gain type of stream */
    if (vol_gain_type)
        pa_proplist_sets(p, PA_PROP_MEDIA_TIZEN_VOLUME_GAIN_TYPE, vol_gain_type);

    sink = pa_namereg_get_default_sink(u->module->core);

    pa_log_debug("role[%s], volume_gain_type[%s]", role, vol_gain_type);

    scache_idx = pa_scache_get_id_by_name(u->module->core, sample_name);
    if (scache_idx != PA_IDXSET_INVALID) {
        pa_log_debug("pa_scache_play_item() start, scache idx[%u] for name[%s]", scache_idx, sample_name);
        /* for more precision, need to update volume value here */
        if ((ret = pa_scache_play_item(u->module->core, sample_name, sink, PA_VOLUME_NORM, p, &stream_idx) < 0)) {
            pa_log_error("pa_scache_play_item fail");
            goto exit;
        }
        pa_log_debug("pa_scache_play_item() end, stream_idx(%u)", stream_idx);
    } else
        pa_log_error("could not find the scache item for [%s]", sample_name);

    if (!ret)
        ret = (int32_t)stream_idx;
exit:
    pa_proplist_free(p);
    return ret;
}

#ifdef HAVE_DBUS
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *xml = SOUND_PLAYER_INTROSPECT_XML;
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

static void handle_simple_play(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    char *uri = NULL;
    char *role = NULL;
    char *volume_gain = NULL;
    dbus_int32_t result = 0;
    struct userdata *u =  (struct userdata*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &uri,
                                       DBUS_TYPE_STRING, &role,
                                       DBUS_TYPE_STRING, &volume_gain,
                                       DBUS_TYPE_INVALID));
    pa_log_info("uri[%s], role[%s], volume_gain[%s]", uri, role, volume_gain);
    if (uri)
        result = _simple_play(u, uri, role, volume_gain);
    else
        result = -1;

    if (result != -1) {
        uint32_t idx = 0;
        int32_t *stream_idx = NULL;
        stream_idx = pa_xmalloc0(sizeof(int32_t));
        *stream_idx = result;
        pa_idxset_put(u->stream_idxs, stream_idx, &idx);
    }

    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_INT32, &result);
}

static void handle_sample_play(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    char *sample_name = NULL;
    char *role = NULL;
    char *volume_gain = NULL;
    dbus_int32_t result = 0;
    struct userdata *u =  (struct userdata*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &sample_name,
                                       DBUS_TYPE_STRING, &role,
                                       DBUS_TYPE_STRING, &volume_gain,
                                       DBUS_TYPE_INVALID));
    pa_log_info("sample_name[%s], role[%s], volume_gain[%s]", sample_name, role, volume_gain);
    if (sample_name)
        result = _sample_play(u, sample_name, role, volume_gain);
    else
        result = -1;

    if (result != -1) {
        uint32_t idx = 0;
        int32_t *stream_idx = NULL;
        stream_idx = pa_xmalloc0(sizeof(int32_t));
        *stream_idx = result;
        pa_idxset_put(u->stream_idxs, stream_idx, &idx);
    }

    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_INT32, &result);
}

static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata) {
	int idx = 0;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    for (idx = 0; idx < METHOD_HANDLER_MAX; idx++) {
        if (dbus_message_is_method_call(msg, SOUND_PLAYER_INTERFACE, method_handlers[idx].method_name )) {
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
    struct userdata *u = userdata;
    const char *path, *interface, *member;

    pa_assert(c);
    pa_assert(m);
    pa_assert(u);

    path = dbus_message_get_path(m);
    interface = dbus_message_get_interface(m);
    member = dbus_message_get_member(m);

    pa_log_debug("dbus: path=%s, interface=%s, member=%s", path, interface, member);

    if (!pa_streq(path, SOUND_PLAYER_OBJECT_PATH))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        return handle_introspect(c, m, u);
    } else {
        return handle_methods(c, m, u);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void send_signal_for_eos(struct userdata *u, int32_t stream_idx) {
    DBusMessage *signal_msg = NULL;

    pa_assert(u);

    pa_log_debug("Send EOS signal for stream_idx(%d)", stream_idx);

    pa_assert_se(signal_msg = dbus_message_new_signal(SOUND_PLAYER_OBJECT_PATH, SOUND_PLAYER_INTERFACE, SOUND_PLAYER_SIGNAL_EOS));
    pa_assert_se(dbus_message_append_args(signal_msg, DBUS_TYPE_INT32, &stream_idx, DBUS_TYPE_INVALID));
    pa_assert_se(dbus_connection_send(pa_dbus_connection_get(u->dbus_conn), signal_msg, NULL));
    dbus_message_unref(signal_msg);
}
#endif

static int init_ipc (struct userdata *u) {
    int pre_mask;
#ifdef HAVE_DBUS
#ifndef USE_DBUS_PROTOCOL
    DBusError err;
    pa_dbus_connection *conn = NULL;
    static const DBusObjectPathVTable vtable = {
        .message_function = method_handler_for_vt,
    };
#endif
#endif
    pa_assert(u);

    pa_log_info("Initialization for IPC");

    pre_mask = umask(0);
    if (mknod(KEYTONE_PATH,S_IFIFO|0660,0)<0)
        pa_log_warn("mknod failed. errno=[%d][%s]", errno, strerror(errno));

    umask(pre_mask);

    u->fd = open(KEYTONE_PATH, O_RDWR);
    if (u->fd == -1) {
        pa_log_warn("Check ipc node %s\n", KEYTONE_PATH);
        goto fail;
    }

    /* change access mode so group can use keytone pipe */
    if (fchmod (u->fd, 0666) == -1)
        pa_log_warn("Changing keytone access mode is failed. errno=[%d][%s]", errno, strerror(errno));

    /* change group due to security request */
    if (fchown (u->fd, -1, KEYTONE_GROUP) == -1)
        pa_log_warn("Changing keytone group is failed. errno=[%d][%s]", errno, strerror(errno));

    u->io = u->module->core->mainloop->io_new(u->module->core->mainloop, u->fd, PA_IO_EVENT_INPUT|PA_IO_EVENT_HANGUP, io_event_callback, u);

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    u->dbus_protocol = pa_dbus_protocol_get(u->module->core);
    pa_assert_se(pa_dbus_protocol_add_interface(u->dbus_protocol, SOUND_PLAYER_OBJECT_PATH, &sound_player_interface_info, u) >= 0);
    pa_assert_se(pa_dbus_protocol_register_extension(u->dbus_protocol, SOUND_PLAYER_INTERFACE) >= 0);
#else
    dbus_error_init(&err);

    if (!(conn = pa_dbus_bus_get(u->module->core, DBUS_BUS_SYSTEM, &err)) || dbus_error_is_set(&err)) {
        if (conn) {
            pa_dbus_connection_unref(conn);
        }
        pa_log_error("Unable to contact D-Bus system bus: %s: %s", err.name, err.message);
        goto fail;
    } else
        pa_log_notice("Got dbus connection");

    u->dbus_conn = conn;
    pa_assert_se(dbus_connection_register_object_path(pa_dbus_connection_get(conn), SOUND_PLAYER_OBJECT_PATH, &vtable, u));
#endif
#else
    pa_log_error("DBUS is not supported\n");
    goto fail;
#endif

    return 0;
fail:
    return -1;
}

static void deinit_ipc (struct userdata *u) {

    pa_assert(u);

    if (u->io)
        u->module->core->mainloop->io_free(u->io);

    if (u->fd > -1)
        close(u->fd);

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    if (u->dbus_protocol) {
        pa_assert_se(pa_dbus_protocol_unregister_extension(u->dbus_protocol, SOUND_PLAYER_INTERFACE) >= 0);
        pa_assert_se(pa_dbus_protocol_remove_interface(u->dbus_protocol, SOUND_PLAYER_OBJECT_PATH, sound_player_interface_info.name) >= 0);
        pa_dbus_protocol_unref(u->dbus_protocol);
        u->dbus_protocol = NULL;
    }
#else
    if (u->dbus_conn) {
        if(!dbus_connection_unregister_object_path(pa_dbus_connection_get(u->dbus_conn), SOUND_PLAYER_OBJECT_PATH))
            pa_log_error("Failed to unregister object path");
        u->dbus_conn = NULL;
    }
#endif
#endif
}

static void io_event_callback(pa_mainloop_api *io, pa_io_event *e, int fd, pa_io_event_flags_t events, void*userdata) {
    struct userdata *u = userdata;
    struct ipc_data data;
    int ret = 0;
    int size = 0;

    pa_assert(io);
    pa_assert(u);

    if (events & (PA_IO_EVENT_HANGUP|PA_IO_EVENT_ERROR)) {
        pa_log_warn("Lost connection to client side");
        goto fail;
    }

    if (events & PA_IO_EVENT_INPUT) {
        size = sizeof(data);
        memset(&data, 0, size);
        ret = read(fd, (void *)&data, size);
        if(ret != -1) {
            pa_log_info("name(%s), role(%s), volume_gain_type(%s)", data.filename, data.role, data.volume_gain_type);
            _simple_play(u, data.filename, data.role, data.volume_gain_type);

        } else {
            pa_log_warn("Fail to read file");
        }
    }

    return;

fail:
    u->module->core->mainloop->io_free(u->io);
    u->io = NULL;

    pa_module_unload_request(u->module, TRUE);
}

static pa_hook_result_t sink_input_unlink_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
    int32_t *stream_idx = NULL;
    uint32_t idx = 0;
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    pa_log_info("start sink_input_unlink_cb, i(%p, index:%u)", i, i->index);

#ifdef HAVE_DBUS
    PA_IDXSET_FOREACH(stream_idx, u->stream_idxs, idx) {
        if (*stream_idx == (int32_t)(i->index)) {
#ifndef USE_DBUS_PROTOCOL
            /* Send EOS signal for this stream */
            send_signal_for_eos(u, *stream_idx);
#endif
            pa_idxset_remove_by_data(u->stream_idxs, stream_idx, NULL);
            pa_xfree(stream_idx);
        }
    }
#endif

    return PA_HOOK_OK;
}

int pa__init(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->module = m;
    u->io = NULL;
    u->fd = -1;
#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    u->dbus_protocol = NULL;
#else
    u->dbus_conn = NULL;
#endif
    u->stream_idxs = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
#endif
    if (init_ipc(u))
        goto fail;

    u->sink_input_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_unlink_cb, u);

    return 0;

fail:
    pa__done(m);
    return -1;
}

void pa__done(pa_module *m) {
    struct userdata *u;
    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink_input_unlink_slot)
        pa_hook_slot_free(u->sink_input_unlink_slot);

#ifdef HAVE_DBUS
    if (u->stream_idxs)
        pa_idxset_free(u->stream_idxs, NULL);
#endif

    deinit_ipc(u);

    pa_xfree(u);
}
