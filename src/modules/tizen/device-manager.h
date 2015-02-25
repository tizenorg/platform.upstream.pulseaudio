
#include <pulsecore/core.h>
#include "communicator.h"

typedef enum audio_device_type {
    AUDIO_DEVICE_NONE,
    AUDIO_DEVICE_SPEAKER,
    AUDIO_DEVICE_RECEIVER,
    AUDIO_DEVICE_MIC,
    AUDIO_DEVICE_AUDIO_JACK,
    AUDIO_DEVICE_BT_A2DP,
    AUDIO_DEVICE_BT_SCO,
    AUDIO_DEVICE_HDMI,
    AUDIO_DEVICE_MIRRORING,
    AUDIO_DEVICE_USB_AUDIO,
    AUDIO_DEVICE_MAX
} audio_device_t;

typedef enum audio_device_role_type {
    AUDIO_DEVICE_ROLE_NONE,
    AUDIO_DEVICE_ROLE_NORMAL,
    AUDIO_DEVICE_ROLE_VOIP,
    AUDIO_DEVICE_ROLE_LOW_LATENCY,
    AUDIO_DEVICE_ROLE_HIGH_LATENCY,
    AUDIO_DEVICE_ROLE_UHQA,
    AUDIO_DEVICE_ROLE_MAX
} audio_device_role_t;

typedef enum audio_device_direction_type {
    AUDIO_DEVICE_DIRECTION_NONE,
    AUDIO_DEVICE_DIRECTION_IN,
    AUDIO_DEVICE_DIRECTION_OUT,
    AUDIO_DEVICE_DIRECTION_BOTH
} audio_device_direction_t;

typedef enum audio_device_changed_into_type {
    AUDIO_DEVICE_CHANGED_INFO_STATE,
    AUDIO_DEVICE_CHANGED_INFO_IO_DIRECTION
} audio_device_changed_info_t;

typedef enum audio_device_status_type {
    AUDIO_DEVICE_STATE_DEACTIVATED,
    AUDIO_DEVICE_STATE_ACTIVATED
} audio_device_status_t;

typedef struct pa_device_manager pa_device_manager;
typedef struct device_item device_item;

pa_device_manager* device_manager_init(pa_core* core);
void device_manager_done(pa_device_manager *dm);

device_item* device_manager_get_device_item(pa_device_manager *dm, audio_device_t device_type);
device_item* device_manager_get_device_item_with_id(pa_device_manager *dm, uint32_t id);

audio_device_t device_item_get_type(device_item *device);
audio_device_direction_t device_item_get_direction(device_item *device);
pa_sink* device_item_get_sink(device_item *device, audio_device_role_t role);
pa_source* device_item_get_source(device_item *device, audio_device_role_t role);
void device_item_set_state(pa_device_manager *dm, device_item *device, audio_device_status_t state);
audio_device_status_t device_item_get_state(pa_device_manager *dm, device_item *device);

int device_manager_load_sink(audio_device_t device_type, audio_device_role_t role, pa_device_manager *dm);
int device_manager_load_source(audio_device_t device_type, audio_device_role_t role, pa_device_manager *dm);
