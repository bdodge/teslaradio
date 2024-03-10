
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	TUNER_INIT,
	TUNER_RESET,
	TUNER_CONFIG,
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

struct station_info
{
    uint32_t 	freq_kHz;
	uint8_t		channel;
    uint8_t 	rssi;
    char 		name[9];
    char 		text[65];
};

int SI4703TuneTo(uint32_t freq_kHz);
int SI4703DiscoverStations(void);
int SI4703GetTunedStation(
                        uint32_t *out_freq_kHz,
                        uint8_t *out_rssi,
                        bool *out_stereo,
                        const char **rds_short,
                        const char **rds_long
                        );
int SI4703GetStations(struct station_info **stations, uint32_t *num_stations);
int SI4703Slice(uint32_t *delay, tuner_state_t *state);
int SI4703Init(uint8_t tuner_seek_threshold_t);

int RequestTuneTo(uint32_t freq_kHz);

