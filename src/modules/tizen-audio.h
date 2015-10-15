#ifndef foopulsetizenaudiofoo
#define foopulsetizenaudiofoo

/***
  This file is part of PulseAudio.

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


/* Error code */
#define AUDIO_IS_ERROR(ret)             (ret < 0)
typedef enum audio_return {
    AUDIO_RET_OK                        = 0,
    AUDIO_ERR_UNDEFINED                 = (int32_t)0x80001000,
    AUDIO_ERR_RESOURCE                  = (int32_t)0x80001001,
    AUDIO_ERR_PARAMETER                 = (int32_t)0x80001002,
    AUDIO_ERR_IOCTL                     = (int32_t)0x80001003,
    AUDIO_ERR_NOT_IMPLEMENTED           = (int32_t)0x80001004,
} audio_return_t ;

/* Direction */
typedef enum audio_direction {
    AUDIO_DIRECTION_IN,                 /**< Capture */
    AUDIO_DIRECTION_OUT,                /**< Playback */
} audio_direction_t;

typedef struct device_info {
    char *type;
    uint32_t direction;
    uint32_t id;
} device_info_t;

typedef struct audio_volume_info {
    const char *type;
    const char *gain;
    uint32_t direction;
} audio_volume_info_t ;

typedef struct audio_route_info {
    const char *role;
    device_info_t *device_infos;
    uint32_t num_of_devices;
} audio_route_info_t;

typedef struct audio_route_option {
    const char *role;
    const char *name;
    int32_t value;
} audio_route_option_t;

typedef struct audio_stream_info {
    const char *role;
    uint32_t direction;
    uint32_t idx;
} audio_stream_info_t ;

/* Overall */
typedef struct audio_interface {
    audio_return_t (*init)(void **userdata, void *platform_data);
    audio_return_t (*deinit)(void **userdata);
    audio_return_t (*get_volume_level_max)(void *userdata, audio_volume_info_t *info, uint32_t *level);
    audio_return_t (*get_volume_level)(void *userdata, audio_volume_info_t *info, uint32_t *level);
    audio_return_t (*set_volume_level)(void *userdata, audio_volume_info_t *info, uint32_t level);
    audio_return_t (*get_volume_value)(void *userdata, audio_volume_info_t *info, uint32_t level, double *value);
    audio_return_t (*get_volume_mute)(void *userdata, audio_volume_info_t *info, uint32_t *mute);
    audio_return_t (*set_volume_mute)(void *userdata, audio_volume_info_t *info, uint32_t mute);
    audio_return_t (*do_route)(void *userdata, audio_route_info_t *info);
    audio_return_t (*update_route_option)(void *userdata, audio_route_option_t *option);
    audio_return_t (*update_stream_connection_info) (void *userdata, audio_stream_info_t *info, uint32_t is_connected);
    audio_return_t (*alsa_pcm_open)(void *userdata, void **pcm_handle, char *device_name, uint32_t direction, int mode);
    audio_return_t (*alsa_pcm_close)(void *userdata, void *pcm_handle);

    /* Interface of PCM device */
    audio_return_t (*pcm_open)(void *userdata, void **pcm_handle, void *sample_spec, uint32_t direction);
    audio_return_t (*pcm_start)(void *userdata, void *pcm_handle);
    audio_return_t (*pcm_stop)(void *userdata, void *pcm_handle);
    audio_return_t (*pcm_close)(void *userdata, void *pcm_handle);
    audio_return_t (*pcm_avail)(void *userdata, void *pcm_handle, unsigned int *avail);
    audio_return_t (*pcm_write)(void *userdata, void *pcm_handle, const void *buffer, uint32_t frames);
    audio_return_t (*pcm_read)(void *userdata, void *pcm_handle, void *buffer, uint32_t frames);

    audio_return_t (*get_buffer_attr)(void *userdata, uint32_t direction, const char *latency, uint32_t samplerate, int format, uint32_t channels, uint32_t *maxlength, uint32_t *tlength, uint32_t *prebuf, uint32_t* minreq, uint32_t *fragsize);
    audio_return_t (*set_route)(void *userdata, uint32_t session, uint32_t subsession, uint32_t device_in, uint32_t device_out, uint32_t route_flag);
} audio_interface_t;

audio_return_t audio_init (void **userdata, void *platform_data);
audio_return_t audio_deinit (void **userdata);
audio_return_t audio_get_volume_level_max (void *userdata, audio_volume_info_t *info, uint32_t *level);
audio_return_t audio_get_volume_level (void *userdata, audio_volume_info_t *info, uint32_t *level);
audio_return_t audio_set_volume_level (void *userdata, audio_volume_info_t *info, uint32_t level);
audio_return_t audio_get_volume_value (void *userdata, audio_volume_info_t *info, uint32_t level, double *value);
audio_return_t audio_get_volume_mute (void *userdata, audio_volume_info_t *info, uint32_t *mute);
audio_return_t audio_set_volume_mute (void *userdata, audio_volume_info_t *info, uint32_t mute);
audio_return_t audio_do_route (void *userdata, audio_route_info_t *info);
audio_return_t audio_update_route_option (void *userdata, audio_route_option_t *option);
audio_return_t audio_update_stream_connection_info (void *userdata, audio_stream_info_t *info, uint32_t is_connected);
audio_return_t audio_alsa_pcm_open (void *userdata, void **pcm_handle, char *device_name, uint32_t direction, int mode);
audio_return_t audio_alsa_pcm_close (void *userdata, void *pcm_handle);
audio_return_t audio_pcm_open (void *userdata, void **pcm_handle, void *sample_spec, uint32_t direction);
audio_return_t audio_pcm_start (void *userdata, void *pcm_handle);
audio_return_t audio_pcm_stop (void *userdata, void *pcm_handle);
audio_return_t audio_pcm_close (void *userdata, void *pcm_handle);
audio_return_t audio_pcm_avail (void *userdata, void *pcm_handle, unsigned int *avail);
audio_return_t audio_pcm_write (void *userdata, void *pcm_handle, const void *buffer, uint32_t frames);
audio_return_t audio_pcm_read (void *userdata, void *pcm_handle, void *buffer, uint32_t frames);
audio_return_t audio_get_buffer_attr (void *userdata, uint32_t direction, const char *latency, uint32_t samplerate, int format, uint32_t channels, uint32_t *maxlength, uint32_t *tlength, uint32_t *prebuf, uint32_t* minreq, uint32_t *fragsize);
audio_return_t audio_set_route (void *userdata, uint32_t session, uint32_t subsession, uint32_t device_in, uint32_t device_out, uint32_t route_flag);
#endif
