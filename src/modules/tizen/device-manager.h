
#include <pulsecore/core.h>

typedef enum dm_device_direction_type {
    DM_DEVICE_DIRECTION_NONE,
    DM_DEVICE_DIRECTION_IN = 0x1,
    DM_DEVICE_DIRECTION_OUT = 0x2,
    DM_DEVICE_DIRECTION_BOTH = DM_DEVICE_DIRECTION_IN | DM_DEVICE_DIRECTION_OUT
} dm_device_direction_t;

typedef enum dm_device_changed_into_type {
    DM_DEVICE_CHANGED_INFO_STATE,
    DM_DEVICE_CHANGED_INFO_IO_DIRECTION,
    DM_DEVICE_CHANGED_INFO_SUBTYPE,
} dm_device_changed_info_t;

typedef enum dm_device_state_type {
    DM_DEVICE_STATE_DEACTIVATED,
    DM_DEVICE_STATE_ACTIVATED
} dm_device_state_t;

typedef struct pa_device_manager pa_device_manager;
typedef struct dm_device dm_device;

typedef struct _hook_call_data_for_conn_changed {
    pa_bool_t is_connected;
    dm_device *device;
} pa_device_manager_hook_data_for_conn_changed;

typedef struct _hook_call_data_for_info_changed {
    dm_device_changed_info_t changed_info;
    dm_device *device;
} pa_device_manager_hook_data_for_info_changed;

pa_device_manager* pa_device_manager_init(pa_core* core);
void pa_device_manager_done(pa_device_manager *dm);

pa_idxset* pa_device_manager_get_device_list(pa_device_manager *dm);
dm_device* pa_device_manager_get_device(pa_device_manager *dm, const char *device_type);
dm_device* pa_device_manager_get_device_by_id(pa_device_manager *dm, uint32_t id);

pa_sink* pa_device_manager_get_sink(dm_device *device, const char *role);
pa_source* pa_device_manager_get_source(dm_device *device, const char *role);
void pa_device_manager_set_device_state(dm_device *device, dm_device_state_t state);
dm_device_state_t pa_device_manager_get_device_state(dm_device *device);
uint32_t pa_device_manager_get_device_id(dm_device *device);
const char* pa_device_manager_get_device_type(dm_device *device);
const char* pa_device_manager_get_device_subtype(dm_device *device);
dm_device_direction_t pa_device_manager_get_device_direction(dm_device *device);

int pa_device_manager_load_sink(const char *device_type, const char *device_profile, const char *role, pa_device_manager *dm);
int pa_device_manager_load_source(const char *device_type, const char *device_profile, const char *role, pa_device_manager *dm);
