
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    TUNER_INIT,
    TUNER_DISCOVERY,
    TUNER_READY,
    TUNER_TUNED
}
tuner_state_t;

typedef enum
{
    TUNER_SEEK_ALL,
    TUNER_SEEK_MOST,
    TUNER_SEEK_BETTER,
    TUNER_SEEK_BEST,
    TUNER_SEEK_DEFAULT
}
tuner_seek_threshold_t;

typedef enum
{
    TUNER_SW,
    TUNER_MW,
    TUNER_AM,
    TUNER_FM,
}
tuner_band_t;

struct station_info
{
    uint32_t    freq_kHz;
    uint8_t     channel;
    uint8_t     rssi;
    char        name[9];
    char        text[65];
};

typedef struct _tuner
{
    int (*init)(struct _tuner *in_tuner, tuner_seek_threshold_t in_threshold);
    int (*get_band_info)(
                        struct _tuner *in_tuner,
                        const tuner_band_t in_band,
                        uint32_t *out_min_freq,
                        uint32_t *out_max_freq,
                        uint32_t *out_kHz_per_channel
                    );
    int (*set_volume)(struct _tuner *in_tuner, uint32_t in_volume_percent);
    int (*set_tune)(struct _tuner *in_tuner, uint32_t in_freq_kHz);
    int (*get_tune)(
                        struct _tuner *in_tuner,
                        uint32_t    *out_freq_kHz
                    );
    int (*get_rssi)(
                        struct _tuner *in_tuner,
                        uint8_t     *out_rssi,
                        bool        *out_stereo,
                        bool        *out_afc_railed
                    );
    int (*get_rds)(
                        struct _tuner *in_tuner,
                        bool        *out_rds_changed,
                        const char  **out_rds_short,
                        const char  **out_rds_long
                    );
    int (*tune)(struct _tuner *in_tuner, bool in_up, bool in_wrap);
    int (*seek)(struct _tuner *in_tuner, bool in_up, bool in_wrap);

    void *priv;
}
tuner_t;

int TunerRequestTuneTo(uint32_t freq_kHz);

int TunerSetVolume(uint32_t in_volume_percent);
int TunerTuneTo(uint32_t in_freq_kHz);
int TunerDiscoverStations(void);
int TunerGetTunedStationFreq(
                        uint32_t        *out_freq_kHz
                        );
int TunerGetTunedStationRSSI(
                        uint8_t         *out_rssi,
                        bool            *out_stereo
                        );
int TunerGetTunedStationRDS(
                        const char      **out_rds_short,
                        const char      **out_rds_long
                        );
int TunerGetStations(struct station_info **out_stations, uint32_t *out_num_stations);
int TunerSlice(uint32_t *out_delay, tuner_state_t *out_state);
int TunerInit(tuner_t *in_radio, const tuner_seek_threshold_t in_seek_threshold);

