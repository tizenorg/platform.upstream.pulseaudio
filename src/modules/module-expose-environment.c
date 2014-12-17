/***
  This file is part of PulseAudio.

  Copyright 2014 Krisztian Litkey

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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>

#include <pulse/xmalloc.h>

#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/client.h>
#include <pulsecore/core-util.h>

#include "module-expose-environment-symdef.h"

PA_MODULE_AUTHOR("Krisztian Litkey");
PA_MODULE_DESCRIPTION("Expose a selected set of client environment variables as properties of the client.");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(false);
PA_MODULE_USAGE("variables=<var1[:prop1],...,varN[:propN]> [prefix=<prefix>]");

#define PROP_PID PA_PROP_APPLICATION_PROCESS_ID
#define PROP_NATIVE_PEER "native-protocol.peer"
#define UNIX_CLIENT "UNIX socket client"
#define TCP_CLIENT "TCP/IP client from "
#define PROP_KEY_LEN 512

/*
 * module configuration:
 *
 * - prefix: (default) proplist prefix for exported environment variables
 * - variables: variables to export
 *
 * The variable configuration string has the following syntax:
 *
 *     name1[:[.]prop1],...,nameN[:[.]propN], where
 *
 * name1...nameN are the environment variables to export, and
 * prop1...propN are the property names to use for these.
 *
 * If a property name does not start with a '.' it will be prefixed
 * with the common default prefix. Otherwise the property name will
 * be used verbatim without the leading dot. If property is omitted
 * it defaults to the name of the environment variable (prefixed
 * with the default prefix). For instance,
 *
 *     variables=HOME:.user.home,SHELL:.user.shell,HOSTNAME
 *
 * will set the following properties on the client if the corresponding
 * environment variables are set:
 *
 *     user.home=$HOME,
 *     user.shell=$SHELL,
 *     application.process.environment.HOSTNAME=$HOSTNAME
 */

/* possible module arguments */
static const char *const valid_modargs[] = {
    "prefix",
    "variables",
};

/* environment variable-property mapping */
struct envvar {
    char *name;
    char *prop;
};

/* plugin userdata */
struct userdata {
    char *prefix;
    int prefix_len;
    struct envvar *variables;
    pa_hook_slot *client_new;
    pa_hook_slot *proplist_changed;
};

/* structure for reading and looping through the environment */
struct proc_env {
    char buf[16 * 1024];
    int size;
    char *p;
};

/* parse a single variable configuration (var[:[.]name) */
static int parse_variable(size_t prefix_len,
                          const char *var, size_t vlen,
                          const char **namep, size_t *nlenp,
                          const char **propp, size_t *plenp,
                          char *buf, size_t bufsize)
{
    const char *p;
    int nlen, plen;

    if (!(p = strchr(var, ':')) || (p - var) >= (int)vlen) {
        nlen = vlen;
        plen = vlen;

        if (plen > (int)bufsize) {
        overflow:
            *namep = NULL;
            *propp = NULL;
            return -1;
        }

        strncpy(buf + prefix_len, var, plen);
        buf[prefix_len + plen] = '\0';

        *namep = var;
        *nlenp = nlen;
        *propp = buf;
        *plenp = prefix_len + plen;
    }
    else {
        nlen = p++ - var;
        plen = vlen - nlen - 1;

        if (plen > (int)bufsize)
            goto overflow;

        *namep = var;
        *nlenp = nlen;

        if (p[0] == '.') {
            *propp = p + 1;
            *plenp = plen - 1;
        }
        else {
            strncpy(buf + prefix_len, p, plen);
            buf[prefix_len + plen] = '\0';
            *propp = buf;
            *plenp = prefix_len + plen;
        }
    }

    return 0;
}

/* parse the configured set of environment variables */
static int parse_variables(struct userdata *u, const char *variables)
{
    char propbuf[PROP_KEY_LEN];
    struct envvar *vars;
    const char *v, *next, *end, *prop, *name;
    size_t nlen, plen;
    int n, l, prefix_len;

    vars = u->variables = NULL;

    if (!variables || !*variables)
        return 0;

    /* prefill prop buffer with prefix if we have one */
    if (u->prefix_len > 0)
        prefix_len = snprintf(propbuf, sizeof(propbuf), "%s.", u->prefix);
    else
        prefix_len = 0;

    n = 0;
    v = variables;

    /* loop through configuration (var1[:[.]name1],...,varN[:[.]nameN]) */
    while (v && *v) {
        while (*v == ',' || *v == ' ' || *v == '\t')
            v++;

        next = strchr(v, ',');

        if (next != NULL) {
            end = next - 1;
            while (end > v && (*end == ' ' || *end == '\t'))
                end--;

            l = end - v + 1;
        }
        else
            l = strlen(v);

        vars = pa_xrealloc(vars, sizeof(*vars) * (n + 1));

        if (parse_variable(prefix_len, v, l, &name, &nlen, &prop, &plen,
                           propbuf, sizeof(propbuf)) < 0) {
            vars[n].name = vars[n].prop = NULL;
            return -1;
        }

        vars[n].name = pa_xstrndup(name, nlen);
        vars[n].prop = pa_xstrndup(prop, plen);

        pa_log_debug("export environment variable '%s' as '%s'", vars[n].name,
                     vars[n].prop);

        n++;

        v = next ? next + 1 : NULL;
    }

    vars = pa_xrealloc(vars, sizeof(*vars) * (n + 1));
    vars[n].name = vars[n].prop = NULL;

    u->variables = vars;

    return 0;
}

/* read the environment for the given process */
static int proc_env_read(struct proc_env *e, const char *pid)
{
    char path[PATH_MAX];
    int fd;

    e->buf[0] = '\0';
    e->size = 0;
    e->p = NULL;

    if (snprintf(path, sizeof(path),
                 "/proc/%s/environ", pid) >= (ssize_t)sizeof(path))
        return -1;

    if ((fd = open(path, O_RDONLY)) < 0)
        return -1;
    e->size = read(fd, e->buf, sizeof(e->buf) - 1);
    close(fd);

    if (e->size < 0)
        return -1;

    e->buf[e->size] = '\0';
    e->p = e->buf;

    return 0;
}

/* loop through the given environment */
static char *proc_env_foreach(struct proc_env *e, char *key, size_t size,
                              size_t *klenp)
{
    char *k, *v;
    size_t klen, vlen;

    k = e->p;
    v = strchr(k, '=');

    if (!v)
        return NULL;

    klen = v++ - k;
    vlen = strlen(v);

    if (klen > size - 1)
        return NULL;

    strncpy(key, k, klen);
    key[klen] = '\0';

    if ((e->p = v + vlen + 1) >= e->buf + e->size)
        e->p = e->buf;

    if (klenp != NULL)
        *klenp = klen;

    return v;
}

/* export the given environment variables for the given process */
static void export_client_variables(const char *pid, struct userdata *u,
                                    pa_proplist *proplist)
{
    struct proc_env e;
    struct envvar *var;
    char *val, *guard, key[PROP_KEY_LEN];

    pa_log_debug("exporting environment for client %s...", pid);

    if (proc_env_read(&e, pid) < 0)
        return;

    /*
     * Notes:
     *   The search algorithm is designed to run O(n) if the order of
     *   variables in your configuration matches the order of variables
     *   of interest in the environment...
     */

    for (var = u->variables; var->name; var++) {
        guard = NULL;

        while ((val = proc_env_foreach(&e, key, sizeof(key), NULL)) != NULL) {
            if (!strcmp(key, var->name)) {
                pa_log_debug("exporting %s as %s=%s", key, var->name, val);
                pa_proplist_sets(proplist, var->prop, val);
                break;
            }

            if (!guard)
                guard = val;
            else if (guard == val)
                break;
        }
    }
}

static bool looks_local_address(const char *addrstr)
{
    /* XXX TODO: implement me... */
    return false;
}

static bool looks_local_client(pa_proplist *proplist)
{
    const char *peer;
    int len;

    /*
     * Notes:
     *   Yes, this is an insufficient kludge. It does not treat TPC
     *   clients coming from the same host as local ones...
     *
     *   For detecting the client socket type I could not find any other
     *   way then this. There seems to be no API for querying it. It'd
     *   be nice if we could query the peer address directly from pa_client
     *   and pa_client_new_data...
     */

    if (!proplist)
        return false;

    if (!(peer = pa_proplist_gets(proplist, PROP_NATIVE_PEER)))
        return false;

    if (!strcmp(peer, UNIX_CLIENT))
        return true;

    if (!strncmp(peer, TCP_CLIENT, len = sizeof(TCP_CLIENT) - 1)) {
        if (looks_local_address(peer + len))
            return true;
    }

    return false;
}

/* new client creation hook */
static pa_hook_result_t client_cb(pa_core *core, pa_client_new_data *data,
                                  struct userdata *u)
{
    const char *pid;

    pa_core_assert_ref(core);
    pa_assert(data);
    pa_assert(u);

    /*
     * Unfortunately we never have the client PID set here yet, so
     * this ends up always failing... That's the only reason why we
     * need to hook ourselves up to PROPLIST_CHANGED.
     */

    if (!looks_local_client(data->proplist)) {
        pa_log_debug("new client looks like a non-local one, ignoring...");
        return PA_HOOK_OK;
    }

    if (!(pid = pa_proplist_gets(data->proplist, PROP_PID))) {
        pa_log_debug("no %s in client proplist, ignoring...", PROP_PID);
        return PA_HOOK_OK;
    }

    export_client_variables(pid, u, data->proplist);

    return PA_HOOK_OK;
}

/* property list change hook */
static pa_hook_result_t proplist_cb(pa_core *core, pa_client *client,
                                    struct userdata *u)
{
    const char *pid;

    /*
     * Note:
     *     Maybe we should administer the fact that we have already
     *     exported the necessary variables for a client (hook up
     *     to CLIENT_UNLINK too, hash the client pointer in the first
     *     time we update the proplist (here) and unhash it from the
     *     CLIENT_UNLINK hook). Probably not worth bothering though
     *     as client properties are not changed that often...
     */

    pa_core_assert_ref(core);
    pa_assert(client);
    pa_assert(u);

    if (!looks_local_client(client->proplist))
        return PA_HOOK_OK;

    if (!(pid = pa_proplist_gets(client->proplist, PROP_PID)))
        return PA_HOOK_OK;

    export_client_variables(pid, u, client->proplist);

    return PA_HOOK_OK;
}


int pa__init(pa_module *m)
{
    pa_modargs *ma = NULL;
    const char *variables, *prefix;
    struct userdata *u;
    pa_hook *hook;
    int type;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("failed to parse module arguments");
        goto fail;
    }

    variables = pa_modargs_get_value(ma, "variables", NULL);
    prefix = pa_modargs_get_value(ma, "prefix",
                                  PA_PROP_APPLICATION_PROCESS_ENVIRONMENT);

    if (!variables || !*variables)
        goto done;

    m->userdata = u = pa_xnew0(struct userdata, 1);

    u->prefix = pa_xstrdup(prefix);
    u->prefix_len = strlen(u->prefix);

    if (u->prefix_len > PROP_KEY_LEN / 2)
        goto fail;

    if (parse_variables(u, variables) < 0)
        goto fail;

    if (u->variables == NULL)
        goto done;

    hook = &m->core->hooks[PA_CORE_HOOK_CLIENT_NEW];
    type = PA_HOOK_EARLY;
    u->client_new = pa_hook_connect(hook, type, (pa_hook_cb_t)client_cb, u);

    hook = &m->core->hooks[PA_CORE_HOOK_CLIENT_PROPLIST_CHANGED];
    type = PA_HOOK_EARLY;
    u->proplist_changed = pa_hook_connect(hook, type,
                                          (pa_hook_cb_t)proplist_cb, u);

 done:
    pa_modargs_free(ma);

    return 0;

 fail:
    pa__done(m);

    if (ma)
        pa_modargs_free(ma);

    return -1;
}


void pa__done(pa_module *m)
{
    struct userdata *u;
    struct envvar *v;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    pa_xfree(u->prefix);

    if (u->variables) {
        for (v = u->variables; v->name; v++) {
            pa_xfree(v->name);
            pa_xfree(v->prop);
        }

        pa_xfree(u->variables);
    }

    if (u->client_new)
        pa_hook_slot_free(u->client_new);
    if (u->proplist_changed)
        pa_hook_slot_free(u->proplist_changed);

    pa_xfree(u);
}
