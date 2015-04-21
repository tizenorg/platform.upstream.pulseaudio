#ifndef foopulseextpolicyhfoo
#define foopulseextpolicyhfoo

/***
  This file is part of PulseAudio.

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

#include <pulse/context.h>
#include <pulse/version.h>

/** \file
 *
 * Routines for controlling module-policy
 */

PA_C_DECL_BEGIN

typedef enum {
    PA_TIZEN_SESSION_MEDIA,
    PA_TIZEN_SESSION_VOICECALL,
    PA_TIZEN_SESSION_VIDEOCALL,
    PA_TIZEN_SESSION_VOIP,
    PA_TIZEN_SESSION_FMRADIO,
    PA_TIZEN_SESSION_CAMCORDER,
    PA_TIZEN_SESSION_NOTIFICATION,
    PA_TIZEN_SESSION_ALARM,
    PA_TIZEN_SESSION_EMERGENCY,
    PA_TIZEN_SESSION_VOICE_RECOGNITION,
    PA_TIZEN_SESSION_MAX
} pa_tizen_session_t;

typedef enum {
    PA_TIZEN_SUBSESSION_NONE,
    PA_TIZEN_SUBSESSION_VOICE,
    PA_TIZEN_SUBSESSION_RINGTONE,
    PA_TIZEN_SUBSESSION_MEDIA,
    PA_TIZEN_SUBSESSION_VR_INIT,
    PA_TIZEN_SUBSESSION_VR_NORMAL,
    PA_TIZEN_SUBSESSION_VR_DRIVE,
    PA_TIZEN_SUBSESSION_STEREO_REC,
    PA_TIZEN_SUBSESSION_MONO_REC,
    PA_TIZEN_SUBSESSION_MAX
} pa_tizen_subsession_t;

typedef enum {
    PA_TIZEN_DEVICE_IN_NONE,
    PA_TIZEN_DEVICE_IN_MIC,                 /**< Device builtin mic. */
    PA_TIZEN_DEVICE_IN_WIRED_ACCESSORY,     /**< Wired input devices */
    PA_TIZEN_DEVICE_IN_BT_SCO,              /**< Bluetooth SCO device */
} pa_tizen_device_in_t;

typedef enum pa_tizen_device_out {
    PA_TIZEN_DEVICE_OUT_NONE,
    PA_TIZEN_DEVICE_OUT_SPEAKER,            /**< Device builtin speaker */
    PA_TIZEN_DEVICE_OUT_RECEIVER,           /**< Device builtin receiver */
    PA_TIZEN_DEVICE_OUT_WIRED_ACCESSORY,    /**< Wired output devices such as headphone, headset, and so on. */
    PA_TIZEN_DEVICE_OUT_BT_SCO,             /**< Bluetooth SCO device */
    PA_TIZEN_DEVICE_OUT_BT_A2DP,            /**< Bluetooth A2DP device */
    PA_TIZEN_DEVICE_OUT_DOCK,               /**< DOCK device */
    PA_TIZEN_DEVICE_OUT_HDMI,               /**< HDMI device */
    PA_TIZEN_DEVICE_OUT_MIRRORING,          /**< MIRRORING device */
    PA_TIZEN_DEVICE_OUT_USB_AUDIO,          /**< USB Audio device */
    PA_TIZEN_DEVICE_OUT_MULTIMEDIA_DOCK,    /**< Multimedia DOCK device */
} pa_tizen_device_out_t;

typedef enum pa_tizen_volume_type {
    PA_TIZEN_VOLUME_TYPE_SYSTEM,            /**< System volume type */
    PA_TIZEN_VOLUME_TYPE_NOTIFICATION,      /**< Notification volume type */
    PA_TIZEN_VOLUME_TYPE_ALARM,             /**< Alarm volume type */
    PA_TIZEN_VOLUME_TYPE_RINGTONE,          /**< Ringtone volume type */
    PA_TIZEN_VOLUME_TYPE_MEDIA,             /**< Media volume type */
    PA_TIZEN_VOLUME_TYPE_CALL,              /**< Call volume type */
    PA_TIZEN_VOLUME_TYPE_VOIP,              /**< VOIP volume type */
    PA_TIZEN_VOLUME_TYPE_VOICE,             /**< VOICE volume type */
    PA_TIZEN_VOLUME_TYPE_FIXED,             /**< Volume type for fixed acoustic level */
    PA_TIZEN_VOLUME_TYPE_MAX,               /**< Volume type count */
    PA_TIZEN_VOLUME_TYPE_DEFAULT = PA_TIZEN_VOLUME_TYPE_SYSTEM,
} pa_tizen_volume_type_t;

typedef enum pa_tizen_gain_type {
    PA_TIZEN_GAIN_TYPE_DEFAULT,
    PA_TIZEN_GAIN_TYPE_DIALER,
    PA_TIZEN_GAIN_TYPE_TOUCH,
    PA_TIZEN_GAIN_TYPE_AF,
    PA_TIZEN_GAIN_TYPE_SHUTTER1,
    PA_TIZEN_GAIN_TYPE_SHUTTER2,
    PA_TIZEN_GAIN_TYPE_CAMCODING,
    PA_TIZEN_GAIN_TYPE_MIDI,
    PA_TIZEN_GAIN_TYPE_BOOTING,
    PA_TIZEN_GAIN_TYPE_VIDEO,
    PA_TIZEN_GAIN_TYPE_MAX,
} pa_tizen_gain_type_t;

#define PA_TIZEN_VOLUME_TYPE_LEVEL_MAX           15

/** Callback prototype for pa_ext_policy_test(). \since 0.9.21 */
typedef void (*pa_ext_policy_test_cb_t)(
        pa_context *c,
        uint32_t version,
        void *userdata);

/** Test if this extension module is available in the server. \since 0.9.21 */
pa_operation *pa_ext_policy_test(
        pa_context *c,
        pa_ext_policy_test_cb_t cb,
        void *userdata);

/** Enable the mono mode. \since 0.9.21 */
pa_operation *pa_ext_policy_set_mono (
        pa_context *c,
        int enable,
        pa_context_success_cb_t cb,
        void *userdata);

/** Enable the balance mode. \since 0.9.21 */
pa_operation *pa_ext_policy_set_balance (
        pa_context *c,
        double *balance,
        pa_context_success_cb_t cb,
        void *userdata);

/** Enable the muteall mode. \since 0.9.21 */
pa_operation *pa_ext_policy_set_muteall (
        pa_context *c,
        int enable,
        pa_context_success_cb_t cb,
        void *userdata);

pa_operation *pa_ext_policy_set_use_case (
        pa_context *c,
        const char *verb,
        const char *devices[],
        const int num_devices,
        const char *modifiers[],
        const int num_modifiers,
        pa_context_success_cb_t cb,
        void *userdata);

pa_operation *pa_ext_policy_set_session (
        pa_context *c,
        uint32_t session,
        uint32_t start,
        pa_context_success_cb_t cb,
        void *userdata);

pa_operation *pa_ext_policy_set_subsession (
        pa_context *c,
        uint32_t subsession,
        uint32_t subsession_opt,
        pa_context_success_cb_t cb,
        void *userdata);

typedef void (*pa_ext_policy_set_active_device_cb_t)(
        pa_context *c,
        int success,
        uint32_t need_update,
        void *userdata);

pa_operation *pa_ext_policy_set_active_device (
        pa_context *c,
        uint32_t device_in,
        uint32_t device_out,
        pa_ext_policy_set_active_device_cb_t cb,
        void *userdata);

pa_operation *pa_ext_policy_reset (
        pa_context *c,
        pa_context_success_cb_t cb,
        void *userdata);

/** Callback prototype for pa_ext_policy_get_volume_level_max(). \since 0.9.21 */
typedef void (*pa_ext_policy_get_volume_level_max_cb_t)(
        pa_context *c,
        uint32_t volume_level,
        void *userdata);

pa_operation *pa_ext_policy_get_volume_level_max (
        pa_context *c,
        uint32_t volume_type,
        pa_ext_policy_get_volume_level_max_cb_t cb,
        void *userdata);

/** Callback prototype for pa_ext_policy_get_volume_level(). \since 0.9.21 */
typedef void (*pa_ext_policy_get_volume_level_cb_t)(
        pa_context *c,
        uint32_t volume_level,
        void *userdata);

pa_operation *pa_ext_policy_get_volume_level (
        pa_context *c,
        uint32_t stream_idx,
        uint32_t volume_type,
        pa_ext_policy_get_volume_level_max_cb_t cb,
        void *userdata);

pa_operation *pa_ext_policy_set_volume_level (
        pa_context *c,
        uint32_t stream_idx,
        uint32_t volume_type,
        uint32_t volume_level,
        pa_context_success_cb_t cb,
        void *userdata);

/** Callback prototype for pa_ext_policy_get_mute(). \since 0.9.21 */
typedef void (*pa_ext_policy_get_mute_cb_t)(
        pa_context *c,
        uint32_t mute,
        void *userdata);

pa_operation *pa_ext_policy_get_mute (
        pa_context *c,
        uint32_t stream_idx,
        uint32_t volume_type,
        uint32_t direction,
        pa_ext_policy_get_mute_cb_t cb,
        void *userdata);

pa_operation *pa_ext_policy_set_mute (
        pa_context *c,
        uint32_t stream_idx,
        uint32_t volume_type,
        uint32_t direction,
        uint32_t mute,
        pa_context_success_cb_t cb,
        void *userdata);

/** Callback prototype for pa_ext_policy_get_mute(). \since 0.9.21 */
typedef void (*pa_ext_policy_is_available_high_latency_cb_t)(
        pa_context *c,
        uint32_t available,
        void *userdata);

pa_operation *pa_ext_policy_is_available_high_latency (
        pa_context *c,
        pa_ext_policy_is_available_high_latency_cb_t cb,
        void *userdata);

pa_operation *pa_ext_policy_unload_hdmi (
        pa_context *c,
        pa_context_success_cb_t cb,
        void *userdata);

PA_C_DECL_END

#endif
