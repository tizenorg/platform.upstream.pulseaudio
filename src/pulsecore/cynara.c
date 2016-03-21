#include <pulsecore/cynara.h>

#include <config.h>
#include <pulsecore/log.h>

#include <cynara-creds-socket.h>
#include <cynara-client.h>
#include <cynara-session.h>

void cynara_log(const char *string, int cynara_status) {
    const int buflen = 255;
    char buf[buflen];

    int ret = cynara_strerror(cynara_status, buf, buflen);
    if (ret != CYNARA_API_SUCCESS) {
        strncpy(buf, "cynara_strerror failed", buflen);
        buf[buflen - 1] = '\0';
    }
    if (cynara_status < 0)
        pa_log_error("%s: %s", string, buf);
    else
        pa_log_debug("%s: %s", string, buf);
}

bool cynara_check_privilege(int fd, const char *privilege) {
    cynara *p_cynara = NULL;
    cynara_configuration *p_conf = NULL;

    int ret = 0;
    int result = false;

    char *user = NULL;
    char *client = NULL;
    char *session = NULL;
    int pid = 0;

    ret = cynara_configuration_create(&p_conf);
    cynara_log("cynara_configuration_create()", ret);
    if (ret != CYNARA_API_SUCCESS) {
        goto CLEANUP;
    }

    ret = cynara_configuration_set_cache_size(p_conf, 0);
    cynara_log("cynara_configuration_set_cache_size()", ret);
    if (ret != CYNARA_API_SUCCESS) {
        goto CLEANUP;
    }

    ret = cynara_initialize(&p_cynara, p_conf);
    cynara_log("cynara_initialize()", ret);
    if (ret != CYNARA_API_SUCCESS) {
        goto CLEANUP;
    }

    ret = cynara_creds_socket_get_user(fd, USER_METHOD_DEFAULT, &user);
    cynara_log("cynara_creds_socket_get_user()", ret);
    if (ret != CYNARA_API_SUCCESS) {
        goto CLEANUP;
    }

    ret = cynara_creds_socket_get_pid(fd, &pid);
    cynara_log("cynara_creds_socket_get_pid()", ret);
    if (ret != CYNARA_API_SUCCESS) {
        goto CLEANUP;
    }

    ret = cynara_creds_socket_get_client(fd, CLIENT_METHOD_DEFAULT, &client);
    cynara_log("cynara_creds_socket_get_client()", ret);
    if (ret != CYNARA_API_SUCCESS) {
        goto CLEANUP;
    }

    session = cynara_session_from_pid(pid);
    if (session == NULL) {
        pa_log_error("cynara_session_from_pid(): failed");
        goto CLEANUP;
    }


    pa_log_debug("cynara credentials - client: %s, session: %s, user: %s, privilege: %s", client, session, user, privilege);

    ret = cynara_check(p_cynara, client, session, user, privilege);
    cynara_log("cynara_check()", ret);
    if (ret == CYNARA_API_ACCESS_ALLOWED) {
        result = true;
    }

CLEANUP:
    cynara_configuration_destroy(p_conf);
    cynara_finish(p_cynara);
    free(user);
    free(session);
    free(client);
    return result;
}
