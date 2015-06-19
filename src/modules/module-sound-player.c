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
#include "tizen-audio.h"

PA_MODULE_AUTHOR("Sangchul Lee");
PA_MODULE_DESCRIPTION("Sound Player module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);
PA_MODULE_USAGE("ipc_type=<pipe or dbus>");

#ifdef HAVE_DBUS
#define ARR_ARG_MAX  32
#define SOUND_PLAYER_OBJECT_PATH "/org/pulseaudio/Ext/SoundPlayer"
#define SOUND_PLAYER_INTERFACE   "org.pulseaudio.Ext.SoundPlayer"
#define SOUND_PLAYER_METHOD_NAME_SIMPLE_PLAY      "SimplePlay"

static DBusHandlerResult method_handler_for_vt(DBusConnection *c, DBusMessage *m, void *userdata);
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_simple_play(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum method_handler_index {
    METHOD_HANDLER_SIMPLE_PLAY,
    METHOD_HANDLER_MAX
};

static pa_dbus_arg_info simple_play_args[]    = { { "uri", "s", "in" },
                                                 { "volume_conf", "i", "in" } };

static char* signature_args_for_in[] = { "si" };

static pa_dbus_method_handler method_handlers[METHOD_HANDLER_MAX] = {
    [METHOD_HANDLER_SIMPLE_PLAY] = {
        .method_name = SOUND_PLAYER_METHOD_NAME_SIMPLE_PLAY,
        .arguments = simple_play_args,
        .n_arguments = sizeof(simple_play_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_simple_play }
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

#define SOUND_PLAYER_INTROSPECT_XML                                       \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                           \
    "<node>"                                                            \
    " <interface name=\"SOUND_PLAYER_INTERFACE\">"                        \
    "  <method name=\"SOUND_PLAYER_METHOD_NAME_SIMPLE_PLAY\">"            \
    "   <arg name=\"uri\" direction=\"in\" type=\"s\"/>"                \
    "   <arg name=\"volume_conf\" direction=\"in\" type=\"i\"/>"        \
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

static const char* const valid_modargs[] = {
    "ipc_type",
    NULL,
};

struct userdata {
    int fd;
    pa_io_event *io;
    pa_module *module;
#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    pa_dbus_protocol *dbus_protocol;
#else
    pa_dbus_connection *dbus_conn;
#endif
#endif
};

#define FILE_FULL_PATH 1024        /* File path lenth */

struct ipc_data {
    char filename[FILE_FULL_PATH];
    int volume_config;
};

#define KEYTONE_PATH        "/tmp/keytone"  /* Keytone pipe path */
#define KEYTONE_GROUP       6526            /* Keytone group : assigned by security */
#define DEFAULT_IPC_TYPE    IPC_TYPE_PIPE
#define AUDIO_VOLUME_CONFIG_TYPE(vol) (vol & 0x00FF)
#define AUDIO_VOLUME_CONFIG_GAIN(vol) (vol & 0xFF00)

#define MAX_GAIN_TYPE  4
static const char* get_str_gain_type[MAX_GAIN_TYPE] =
{
    "GAIN_TYPE_DEFAULT",
    "GAIN_TYPE_DIALER",
    "GAIN_TYPE_TOUCH",
    "GAIN_TYPE_OTHERS"
};

#define MAX_NAME_LEN 256
static int _simple_play(struct userdata *u, const char *file_path, uint32_t volume_config) {
    int ret = 0;
    pa_sink *sink = NULL;
    pa_proplist *p;
    const char *name_prefix = "SIMPLE_PLAY";
    double volume_linear = 1.0f;
    int volume_type =  AUDIO_VOLUME_CONFIG_TYPE(volume_config);
    int volume_gain =  AUDIO_VOLUME_CONFIG_GAIN(volume_config)>>8;
    char name[MAX_NAME_LEN] = {0};

    uint32_t stream_idx;
    uint32_t play_idx = 0;

    p = pa_proplist_new();

    /* Set role type of stream, temporarily fixed */
    /* Later on, we need to get it from an argument */
    pa_proplist_sets(p, PA_PROP_MEDIA_ROLE, "system");
    /* Set volume type of stream */
    pa_proplist_setf(p, PA_PROP_MEDIA_TIZEN_VOLUME_TYPE, "%d", volume_type);
    /* Set gain type of stream */
    pa_proplist_setf(p, PA_PROP_MEDIA_TIZEN_GAIN_TYPE, "%d", volume_gain);
    /* Set policy type of stream */
    pa_proplist_sets(p, PA_PROP_MEDIA_POLICY, "auto");
    /* Set policy for selecting sink */
    pa_proplist_sets(p, PA_PROP_MEDIA_POLICY_IGNORE_PRESET_SINK, "yes");
    sink = pa_namereg_get_default_sink(u->module->core);

    pa_log_debug("volume_config[type:0x%x,gain:0x%x[%s]]", volume_type, volume_gain,
                 volume_gain > (MAX_GAIN_TYPE-2) ? get_str_gain_type[MAX_GAIN_TYPE-1]: get_str_gain_type[volume_gain]);
    snprintf(name, sizeof(name)-1, "%s_%s", name_prefix, file_path);
    play_idx = pa_scache_get_id_by_name(u->module->core, name);
    if (play_idx != PA_IDXSET_INVALID) {
        pa_log_debug("found cached index [%u] for name [%s]", play_idx, file_path);
    } else {
        if ((ret = pa_scache_add_file_lazy(u->module->core, name, file_path, &play_idx)) != 0) {
            pa_log_error("failed to add file [%s]", file_path);
            goto exit;
        } else {
            pa_log_debug("success to add file [%s], index [%u]", file_path, play_idx);
        }
    }

    pa_log_debug("pa_scache_play_item() start");
    if ((ret = pa_scache_play_item(u->module->core, name, sink, PA_VOLUME_NORM, p, &stream_idx) < 0)) {
        pa_log_error("pa_scache_play_item fail");
        goto exit;
    }
    pa_log_debug("pa_scache_play_item() end");

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
    char *uri;
    dbus_int32_t volume_conf, result;
    struct userdata *u =  (struct userdata*)userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &uri,
                                       DBUS_TYPE_INT32, &volume_conf,
                                       DBUS_TYPE_INVALID));
    pa_log_warn("uri[%s], volume_conf[0x%x]", uri, volume_conf);
    _simple_play(u, uri, (uint32_t)volume_conf);

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
#endif

static int init_ipc (struct userdata *u, const char *type) {

    pa_assert(u);
    pa_assert(type);

    pa_log_info("Initialization for IPC, type:[%s]", type);

    if(!strncmp(type, "pipe", 4)) {
        int pre_mask;

        pre_mask = umask(0);
        if (mknod(KEYTONE_PATH,S_IFIFO|0660,0)<0) {
            pa_log_warn("mknod failed. errno=[%d][%s]", errno, strerror(errno));
        }
        umask(pre_mask);

        u->fd = open(KEYTONE_PATH, O_RDWR);
        if (u->fd == -1) {
            pa_log_warn("Check ipc node %s\n", KEYTONE_PATH);
            goto fail;
        }

        /* change access mode so group can use keytone pipe */
        if (fchmod (u->fd, 0666) == -1) {
            pa_log_warn("Changing keytone access mode is failed. errno=[%d][%s]", errno, strerror(errno));
        }

        /* change group due to security request */
        if (fchown (u->fd, -1, KEYTONE_GROUP) == -1) {
            pa_log_warn("Changing keytone group is failed. errno=[%d][%s]", errno, strerror(errno));
        }

        u->io = u->module->core->mainloop->io_new(u->module->core->mainloop, u->fd, PA_IO_EVENT_INPUT|PA_IO_EVENT_HANGUP, io_event_callback, u);

    } else if (!strncmp(type, "dbus", 4)) {
#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
        u->dbus_protocol = pa_dbus_protocol_get(u->module->core);
        pa_assert_se(pa_dbus_protocol_add_interface(u->dbus_protocol, SOUND_PLAYER_OBJECT_PATH, &sound_player_interface_info, u) >= 0);
        pa_assert_se(pa_dbus_protocol_register_extension(u->dbus_protocol, SOUND_PLAYER_INTERFACE) >= 0);
#else
        DBusError err;
        pa_dbus_connection *conn = NULL;
        static const DBusObjectPathVTable vtable = {
            .message_function = method_handler_for_vt,
        };
        dbus_error_init(&err);

        if (!(conn = pa_dbus_bus_get(u->module->core, DBUS_BUS_SYSTEM, &err)) || dbus_error_is_set(&err)) {
            if (conn) {
                pa_dbus_connection_unref(conn);
            }
            pa_log_error("Unable to contact D-Bus system bus: %s: %s", err.name, err.message);
            goto fail;
        } else {
            pa_log_notice("Got dbus connection");
        }
        u->dbus_conn = conn;
        pa_assert_se(dbus_connection_register_object_path(pa_dbus_connection_get(conn), SOUND_PLAYER_OBJECT_PATH, &vtable, u));
#endif
#else
        pa_log_error("DBUS is not supported\n");
        goto fail;
#endif

    } else {
        pa_log_error("Unknown type(%s) for IPC", type);
        goto fail;
    }

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

    int gain = 0;

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
            gain = AUDIO_VOLUME_CONFIG_GAIN(data.volume_config)>>8;
            pa_log_info("name(%s), volume_config(0x%x)", data.filename, data.volume_config);
            _simple_play(u, data.filename, data.volume_config);

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


int pa__init(pa_module *m) {
    struct userdata *u;
    pa_modargs *ma = NULL;
    const char *ipc_type = NULL;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    ipc_type = pa_modargs_get_value(ma, "ipc_type", "pipe");

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
#endif
    if (init_ipc(u, ipc_type))
        goto fail;

    pa_modargs_free(ma);

    return 0;

fail:

    pa_modargs_free(ma);
    pa__done(m);
    return -1;
}


void pa__done(pa_module *m) {
    struct userdata *u;
    pa_assert(m);

    if (!(u = m->userdata))
        return;

    deinit_ipc(u);

    pa_xfree(u);
}
