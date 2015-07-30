#ifndef foohalmanagerfoo
#define foohalmanagerfoo
#include <dlfcn.h>
#include <pulsecore/core.h>

#include "tizen-audio.h"
/* TODO : move below structure to hal-manager.c */
struct _pa_hal_manager {
    PA_REFCNT_DECLARE;

    pa_core *core;
    void *dl_handle;
    void *data;
    audio_interface_t intf;
};

typedef struct _pa_hal_manager pa_hal_manager;

typedef enum _io_direction {
    DIRECTION_IN,
    DIRECTION_OUT,
} io_direction_t;

typedef struct _hal_device_info {
    const char *type;
    uint32_t direction;
    uint32_t id;
} hal_device_info;

typedef struct _hal_route_info {
    const char *role;
    hal_device_info *device_infos;
    uint32_t num_of_devices;
} hal_route_info;

typedef struct _hal_route_option {
    const char *role;
    const char *name;
    int32_t value;
} hal_route_option;

typedef struct _hal_stream_connection_info {
    const char *role;
    uint32_t direction;
    uint32_t idx;
    pa_bool_t is_connected;
} hal_stream_connection_info;

pa_hal_manager* pa_hal_manager_get(pa_core *core, void *user_data);
pa_hal_manager* pa_hal_manager_ref(pa_hal_manager *h);
void pa_hal_manager_unref(pa_hal_manager *h);
int32_t pa_hal_manager_get_buffer_attribute(pa_hal_manager *h, io_direction_t direction, const char *latency, void *new_data, uint32_t *maxlength, uint32_t *tlength, uint32_t *prebuf, uint32_t* minreq, uint32_t *fragsize);
int32_t pa_hal_manager_reset_volume (pa_hal_manager *h);
int32_t pa_hal_manager_get_volume_level_max (pa_hal_manager *h, const char *volume_type, io_direction_t direction, uint32_t *level);
int32_t pa_hal_manager_get_volume_level (pa_hal_manager *h, const char *volume_type, io_direction_t direction, uint32_t *level);
int32_t pa_hal_manager_set_volume_level (pa_hal_manager *h, const char *volume_type, io_direction_t direction, uint32_t level);
int32_t pa_hal_manager_get_volume_value (pa_hal_manager *h, const char *volume_type, const char *gain_type, io_direction_t direction, uint32_t level, double *value);
int32_t pa_hal_manager_get_mute (pa_hal_manager *h, const char *volume_type, io_direction_t direction, uint32_t *mute);
int32_t pa_hal_manager_set_mute (pa_hal_manager *h, const char *volume_type, io_direction_t direction, uint32_t mute);
int32_t pa_hal_manager_do_route (pa_hal_manager *h, hal_route_info *info);
int32_t pa_hal_manager_update_route_option (pa_hal_manager *h, hal_route_option *option);
int32_t pa_hal_manager_update_stream_connection_info (pa_hal_manager *h, hal_stream_connection_info *info);

#endif
