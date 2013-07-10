/***
  This file is part of PulseAudio.

  Copyright 2013 João Paulo Rechi Vita

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core-util.h>
#include <pulsecore/dbus-shared.h>

#include "hfaudioagent.h"

#define OFONO_SERVICE "org.ofono"
#define HF_AUDIO_AGENT_INTERFACE OFONO_SERVICE ".HandsfreeAudioAgent"
#define HF_AUDIO_MANAGER_INTERFACE OFONO_SERVICE ".HandsfreeAudioManager"

#define HF_AUDIO_AGENT_PATH "/HandsfreeAudioAgent"

#define HF_AUDIO_AGENT_XML                                          \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                       \
    "<node>"                                                        \
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">"    \
    "    <method name=\"Introspect\">"                              \
    "      <arg direction=\"out\" type=\"s\" />"                    \
    "    </method>"                                                 \
    "  </interface>"                                                \
    "  <interface name=\"org.ofono.HandsfreeAudioAgent\">"          \
    "    <method name=\"Release\">"                                 \
    "    </method>"                                                 \
    "    <method name=\"NewConnection\">"                           \
    "      <arg direction=\"in\"  type=\"o\" name=\"card_path\" />" \
    "      <arg direction=\"in\"  type=\"h\" name=\"sco_fd\" />"    \
    "      <arg direction=\"in\"  type=\"y\" name=\"codec\" />"     \
    "    </method>"                                                 \
    "  </interface>"                                                \
    "</node>"

struct hf_audio_agent_data {
    pa_core *core;
    pa_dbus_connection *connection;

    bool filter_added;
    pa_hashmap *hf_audio_cards;
};

static DBusHandlerResult filter_cb(DBusConnection *bus, DBusMessage *m, void *data) {
    pa_assert(bus);
    pa_assert(m);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusMessage *hf_audio_agent_release(DBusConnection *c, DBusMessage *m, void *data) {
    DBusMessage *r = dbus_message_new_error(m, "org.ofono.Error.NotImplemented", "Operation is not implemented");
    return r;
}

static DBusMessage *hf_audio_agent_new_connection(DBusConnection *c, DBusMessage *m, void *data) {
    DBusMessage *r = dbus_message_new_error(m, "org.ofono.Error.NotImplemented", "Operation is not implemented");
    return r;
}

static DBusHandlerResult hf_audio_agent_handler(DBusConnection *c, DBusMessage *m, void *data) {
    hf_audio_agent_data *hfdata = data;
    DBusMessage *r = NULL;
    const char *path, *interface, *member;

    pa_assert(hfdata);

    path = dbus_message_get_path(m);
    interface = dbus_message_get_interface(m);
    member = dbus_message_get_member(m);

    if (!pa_streq(path, HF_AUDIO_AGENT_PATH))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_debug("dbus: path=%s, interface=%s, member=%s", path, interface, member);

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        const char *xml = HF_AUDIO_AGENT_XML;

        pa_assert_se(r = dbus_message_new_method_return(m));
        pa_assert_se(dbus_message_append_args(r, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID));

    } else if (dbus_message_is_method_call(m, HF_AUDIO_AGENT_INTERFACE, "NewConnection"))
        r = hf_audio_agent_new_connection(c, m, data);
    else if (dbus_message_is_method_call(m, HF_AUDIO_AGENT_INTERFACE, "Release"))
        r = hf_audio_agent_release(c, m, data);
    else
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (r) {
        pa_assert_se(dbus_connection_send(pa_dbus_connection_get(hfdata->connection), r, NULL));
        dbus_message_unref(r);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

hf_audio_agent_data *hf_audio_agent_init(pa_core *c) {
    hf_audio_agent_data *hfdata;
    DBusError err;
    static const DBusObjectPathVTable vtable_hf_audio_agent = {
        .message_function = hf_audio_agent_handler,
    };

    pa_assert(c);

    hfdata = pa_xnew0(hf_audio_agent_data, 1);
    hfdata->core = c;
    hfdata->hf_audio_cards = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    dbus_error_init(&err);

    if (!(hfdata->connection = pa_dbus_bus_get(c, DBUS_BUS_SYSTEM, &err))) {
        pa_log("Failed to get D-Bus connection: %s", err.message);
        dbus_error_free(&err);
        return NULL;
    }

    /* dynamic detection of handsfree audio cards */
    if (!dbus_connection_add_filter(pa_dbus_connection_get(hfdata->connection), filter_cb, hfdata, NULL)) {
        pa_log_error("Failed to add filter function");
        hf_audio_agent_done(hfdata);
        return NULL;
    }
    hfdata->filter_added = true;

    if (pa_dbus_add_matches(pa_dbus_connection_get(hfdata->connection), &err,
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',"
            "arg0='" OFONO_SERVICE "'",
            "type='signal',sender='" OFONO_SERVICE "',interface='" HF_AUDIO_MANAGER_INTERFACE "',member='CardAdded'",
            "type='signal',sender='" OFONO_SERVICE "',interface='" HF_AUDIO_MANAGER_INTERFACE "',member='CardRemoved'",
            NULL) < 0) {
        pa_log("Failed to add oFono D-Bus matches: %s", err.message);
        hf_audio_agent_done(hfdata);
        return NULL;
    }

    pa_assert_se(dbus_connection_register_object_path(pa_dbus_connection_get(hfdata->connection), HF_AUDIO_AGENT_PATH,
                                                      &vtable_hf_audio_agent, hfdata));

    return hfdata;
}

void hf_audio_agent_done(hf_audio_agent_data *data) {
    hf_audio_agent_data *hfdata = data;

    pa_assert(hfdata);

    if (hfdata->hf_audio_cards) {
        pa_hashmap_free(hfdata->hf_audio_cards, NULL);
        hfdata->hf_audio_cards = NULL;
    }

    if (hfdata->connection) {

        pa_dbus_remove_matches(
            pa_dbus_connection_get(hfdata->connection),
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',"
            "arg0='" OFONO_SERVICE "'",
            "type='signal',sender='" OFONO_SERVICE "',interface='" HF_AUDIO_MANAGER_INTERFACE "',member='CardAdded'",
            "type='signal',sender='" OFONO_SERVICE "',interface='" HF_AUDIO_MANAGER_INTERFACE "',member='CardRemoved'",
            NULL);

        if (hfdata->filter_added)
            dbus_connection_remove_filter(pa_dbus_connection_get(hfdata->connection), filter_cb, hfdata);

        dbus_connection_unregister_object_path(pa_dbus_connection_get(hfdata->connection), HF_AUDIO_AGENT_PATH);

        pa_dbus_connection_unref(hfdata->connection);
    }

    pa_xfree(hfdata);
}
