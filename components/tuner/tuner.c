
#include <tuner.h>
#include <settings.h>
#include <asserts.h>
#include <errno.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tuner, LOG_LEVEL_INF);

#include <zephyr/kernel.h>

#define MAX_CHANNELS    (32)

static struct tuner_instance
{
    tuner_state_t    state;
    tuner_t         *radio;

    uint16_t    cur_station;
    bool        rds_changed;
    const char  *rds_name;
    const char  *rds_text;

    uint32_t    cur_volume;
    uint32_t    pre_disco_freq;
    uint32_t    pre_disco_volume;
    uint32_t    disco_station;
    uint16_t    disco_chan;
    uint16_t    seek_threshold;
    uint32_t    min_freq;
    uint32_t    max_freq;
    uint16_t    max_chan;
    uint32_t    kHz_per_channel;

    struct station_info stations[MAX_CHANNELS];
}
m_tuner;

static uint32_t TunerChanToFreq(tuner_t *in_radio, const uint16_t in_chan)
{
    uint32_t freq_kHz = 0;

    freq_kHz = in_chan * m_tuner.kHz_per_channel + m_tuner.min_freq;
    return freq_kHz;
}

static uint16_t TunerFreqToChan(tuner_t *in_radio, const uint32_t in_freq_kHz)
{
    uint16_t chan = 0;

    require(m_tuner.kHz_per_channel, exit);
    chan = (in_freq_kHz - m_tuner.min_freq) / m_tuner.kHz_per_channel;
exit:
    return chan;
}

static const char *TunerBandName(const tuner_band_t in_band)
{
    const char *rs;

    switch (in_band)
    {
    case TUNER_SW:
        rs = "SW";
        break;
    case TUNER_MW:
        rs = "SW";
        break;
    case TUNER_AM:
        rs = "AM";
        break;
    case TUNER_FM:
        rs = "FM";
        break;
    default:
        rs = "??";
        break;
    }

    return rs;
}

static int TunerDiscoSlice(uint32_t *out_delay, bool *complete)
{
    int ret = -ENOENT;
    uint32_t freq_kHz;
    uint16_t chan;
    uint8_t rssi;
    bool stereo;
    bool afcrailed;

    require(m_tuner.radio, exit);

    if  (complete)
    {
        *complete = false;
    }

    if (m_tuner.disco_station >= MAX_CHANNELS)
    {
        m_tuner.disco_station = 0;
        m_tuner.disco_chan = 0;

        // clear out all channel info
        //
        for (chan = 0; chan < MAX_CHANNELS; chan++)
        {
            m_tuner.stations[m_tuner.disco_chan].channel = 0;
            m_tuner.stations[m_tuner.disco_chan].freq_kHz = 0;
            m_tuner.stations[m_tuner.disco_chan].rssi = 0;
            memset(m_tuner.stations[m_tuner.disco_chan].name, 0, sizeof(m_tuner.stations[m_tuner.disco_chan].name));
            memset(m_tuner.stations[m_tuner.disco_chan].text, 0, sizeof(m_tuner.stations[m_tuner.disco_chan].text));
        }

        // get band params from radio
        ret = m_tuner.radio->get_band_info(m_tuner.radio, TUNER_FM, &m_tuner.min_freq, &m_tuner.max_freq, &m_tuner.kHz_per_channel);
        require_noerr(ret, exit);

        m_tuner.max_chan = TunerFreqToChan(m_tuner.radio, m_tuner.max_freq);

        LOG_INF("Scanning for channels. band:%s  fMin=%5.1f fMax=%5.1f step=%ukHz",
                TunerBandName(TUNER_FM), (float)m_tuner.min_freq / 1000.0,
                (float)m_tuner.max_freq / 1000.0, m_tuner.kHz_per_channel);
    }

    // set current channel
    //
    ret = m_tuner.radio->set_tune(m_tuner.radio, TunerChanToFreq(m_tuner.radio, m_tuner.disco_chan));
    if (ret)
    {
        return ret;
    }

    // get tuned-to frequency and signal strength
    //
    k_msleep(10);

    ret = m_tuner.radio->get_rssi(m_tuner.radio, &rssi, &stereo, &afcrailed);
    require_noerr(ret, exit);

    ret = m_tuner.radio->get_tune(m_tuner.radio, &freq_kHz);
    require_noerr(ret, exit);

    chan = TunerFreqToChan(m_tuner.radio, freq_kHz);

    LOG_DBG("disco check chan %u at %u 0x%04X", m_tuner.disco_chan, chan, rssi);

    // for stations with signal, add to our database
    //
    if (rssi >= m_tuner.seek_threshold && !afcrailed)
    {
        LOG_INF("Disco add %d chan %d rssi 0x%04X", m_tuner.disco_station, chan, rssi);

        m_tuner.stations[m_tuner.disco_station].channel = chan;
        m_tuner.stations[m_tuner.disco_station].freq_kHz = TunerChanToFreq(m_tuner.radio, chan);
        m_tuner.stations[m_tuner.disco_station].rssi = rssi;

        m_tuner.disco_station++;
        if (m_tuner.disco_station >= MAX_CHANNELS)
        {
            // TODO evict weakest and move slots up?
            LOG_ERR("too many channels");
            if (complete)
            {
                *complete = true;
            }
        }

        ret = 0;
    }

    m_tuner.disco_chan = chan;

    if (afcrailed)
    {
        // if AFCRL was set in rssi, it means we are close to a strong station
        // so just bump up one channel
        //
        LOG_INF("Fudge at chan %u 0x%04X", chan, rssi);
        m_tuner.disco_chan = chan + 1;

        if (m_tuner.disco_chan > m_tuner.max_chan)
        {
            if (complete)
            {
                *complete = true;
            }
        }
    }
    else
    {
        // seek up to next chan
        //
        ret = m_tuner.radio->seek(m_tuner.radio, true, true);
        if (ret)
        {
            LOG_INF("seek fail, recheck from %u", m_tuner.disco_chan);
            m_tuner.disco_chan = chan + 1;
        }
        else
        {
            ret = m_tuner.radio->get_tune(m_tuner.radio, &freq_kHz);
            require_noerr(ret, exit);

            chan = TunerFreqToChan(m_tuner.radio, freq_kHz);

            LOG_INF("Seek up to chan %u 0x%04X", chan, rssi);
            if (chan < m_tuner.disco_chan)
            {
                //wrapped, all done
                if (complete)
                {
                    *complete = true;
                }
            }

            m_tuner.disco_chan = chan;
        }
    }
exit:
    return ret;
}

int TunerGetTunedStationFreq(
                        uint32_t    *out_freq_kHz
                        )
{
    int ret = -ENOENT;

    require(m_tuner.radio, exit);
    require_action(m_tuner.state == TUNER_TUNED, exit, ret = -EAGAIN);

    ret = m_tuner.radio->get_tune(m_tuner.radio, out_freq_kHz);
exit:
    return ret;
}

int TunerGetTunedStationRSSI(
                        uint8_t     *out_rssi,
                        bool        *out_stereo
                        )
{
    int ret = -ENOENT;
    bool railed;

    require(m_tuner.radio, exit);
    require_action(m_tuner.state == TUNER_TUNED, exit, ret = -EAGAIN);

    ret = m_tuner.radio->get_rssi(m_tuner.radio, out_rssi, out_stereo, &railed);
exit:
    return ret;
}

int TunerGetTunedStationRDS(
                        const char  **rds_short,
                        const char  **rds_long
                        )
{
    int ret = -ENOENT;
    bool rds_changed;

    require(m_tuner.radio, exit);
    require_action(m_tuner.state == TUNER_TUNED, exit, ret = -EAGAIN);

    ret = m_tuner.radio->get_rds(m_tuner.radio, &rds_changed, rds_short, rds_long);
exit:
    return ret;
}

int TunerSetVolume(const uint32_t in_volume_percent)
{
    int ret = -ENOENT;

    require(m_tuner.radio, exit);

    if (m_tuner.state < TUNER_READY)
    {
        ret = -EAGAIN;
        goto exit;
    }

    ret = m_tuner.radio->set_volume(m_tuner.radio, in_volume_percent);
    require_noerr(ret, exit);

    m_tuner.cur_volume = in_volume_percent;
    ret = SettingsWriteUint32("Vol", 0, in_volume_percent);
    require_noerr(ret, exit);
exit:
    return ret;
}

int TunerTuneTo(uint32_t freq_kHz)
{
    int ret = -ENOENT;

    require(m_tuner.radio, exit);

    if (m_tuner.state < TUNER_READY)
    {
        ret = -EAGAIN;
        goto exit;
    }

    ret = m_tuner.radio->set_tune(m_tuner.radio, freq_kHz);
    require_noerr(ret, exit);

    m_tuner.state = TUNER_TUNED;

    ret = SettingsWriteUint32("Freq", 0, freq_kHz);
    require_noerr(ret, exit);

    m_tuner.cur_station = MAX_CHANNELS;

    for (uint16_t station = 0; station < MAX_CHANNELS; station++)
    {
        if (m_tuner.stations[station].rssi > 0)
        {
            if (m_tuner.stations[station].freq_kHz == freq_kHz)
            {
                m_tuner.cur_station = station;
                break;
            }
        }
    }

exit:
    return ret;
}

int TunerDiscoverStations(void)
{
    int ret = -ENOENT;

    require(m_tuner.radio, exit);

    m_tuner.pre_disco_volume = m_tuner.cur_volume;
    TunerGetTunedStationFreq(&m_tuner.pre_disco_freq);

    if (m_tuner.state < TUNER_READY)
    {
        ret = -EAGAIN;
    }
    else
    {
        m_tuner.disco_station = MAX_CHANNELS;
        m_tuner.state = TUNER_DISCOVERY;
        ret = 0;
    }

exit:
    return ret;
}

int TunerGetStations(struct station_info **stations, uint32_t *num_stations)
{
    int ret = -ENOENT;
    uint32_t nstations;

    if (stations)
    {
        *stations = m_tuner.stations;
    }

    if (num_stations)
    {
        *num_stations = 0;
    }

    if (m_tuner.state < TUNER_READY)
    {
        ret = -EAGAIN;
        goto exit;
    }

    for (nstations = 0; nstations < MAX_CHANNELS; nstations++)
    {
        if (m_tuner.stations[nstations].rssi == 0)
        {
            break;
        }
    }

    if (num_stations)
    {
        *num_stations = nstations;
    }

    ret = 0;
exit:
    return ret;
}

int TunerSlice(uint32_t *delay, tuner_state_t *state)
{
    bool complete;
    int ret = -ENOENT;

    require(m_tuner.radio, exit);

    switch (m_tuner.state)
    {
    case TUNER_INIT:
        m_tuner.state = TUNER_DISCOVERY;
        break;

    case TUNER_DISCOVERY:
        ret = TunerDiscoSlice(delay, &complete);
        if (ret || complete)
        {
            m_tuner.state = TUNER_READY;
            TunerTuneTo(m_tuner.pre_disco_freq);
            TunerSetVolume(m_tuner.pre_disco_volume);
            break;
        }

        *delay = 20;
        break;

    case TUNER_READY:
        *delay = 100;
        break;

    case TUNER_TUNED:
        ret = TunerGetTunedStationRDS(&m_tuner.rds_name, &m_tuner.rds_text);
        *delay = 20;
        break;
    }

    ret = 0;

    if (state)
    {
        *state = m_tuner.state;
    }

exit:
    return ret;
}

int TunerInit(tuner_t *radio, const tuner_seek_threshold_t seek_threshold)
{
    int ret = -EINVAL;
    uint32_t freq;
    uint32_t vol;

    require(radio, exit);
    m_tuner.state = TUNER_INIT;
    m_tuner.radio = radio;
    ret = m_tuner.radio->init(m_tuner.radio, seek_threshold);
    require_noerr(ret, exit);
    m_tuner.state = TUNER_READY;

    ret = SettingsReadUint32("Freq", 0, &freq);
    if (!ret)
    {
        TunerTuneTo(freq);
    }

    ret = SettingsReadUint32("Vol", 0, &vol);
    if (ret == -ENOENT)
    {
        vol = 50;
    }

    ret = TunerSetVolume(vol);
    require_noerr(ret, exit);

exit:
    return ret;
}

#if CONFIG_SHELL

#include <zephyr/shell/shell.h>
#include <stdlib.h>

static int _CommandSetVol(const struct shell *s, size_t argc, char **argv)
{
    uint32_t vol = 80;

    if (argc > 1)
    {
        vol = strtoul(argv[1], NULL, 0);
    }

    if (vol > 100)
    {
        vol = 100;
    }

    TunerSetVolume(vol);
    return 0;
}

static int _CommandSetFreq(const struct shell *s, size_t argc, char **argv)
{
    uint32_t freq = 89700;

    if (argc > 1)
    {
        freq = strtoul(argv[1], NULL, 0);
    }

    TunerTuneTo(freq);
    return 0;
}

static int _CommandSetStation(const struct shell *s, size_t argc, char **argv)
{
    uint32_t station = 0;

    if (argc > 1)
    {
        station = strtoul(argv[1], NULL, 0);
    }

    if (station >= MAX_CHANNELS)
    {
        shell_print(s, "station should be < %u", MAX_CHANNELS);
        return 0;
    }

    if (m_tuner.stations[station].rssi == 0)
    {
        shell_print(s, "station %u wasn't discovered", station);
        return 0;
    }

    if (m_tuner.radio)
    {
        m_tuner.radio->set_tune(m_tuner.radio, m_tuner.stations[station].freq_kHz);
    }
    return 0;
}

static int _CommandSeekUp(const struct shell *s, size_t argc, char **argv)
{
    if (m_tuner.radio)
    {
        m_tuner.radio->seek(m_tuner.radio, true, true);
    }

    return 0;
}

static int _CommandSeekDown(const struct shell *s, size_t argc, char **argv)
{
    if (m_tuner.radio)
    {
        m_tuner.radio->seek(m_tuner.radio, false, true);
    }

    return 0;
}

static int _CommandTuneUp(const struct shell *s, size_t argc, char **argv)
{
    if (m_tuner.radio)
    {
        m_tuner.radio->tune(m_tuner.radio, true, true);
    }

    return 0;
}

static int _CommandTuneDown(const struct shell *s, size_t argc, char **argv)
{
    if (m_tuner.radio)
    {
        m_tuner.radio->tune(m_tuner.radio, false, true);
    }

    return 0;
}

static int _CommandPrint(const struct shell *s, size_t argc, char **argv)
{
    shell_print(s, "Station List:");

    for (int station = 0; station < MAX_CHANNELS; station++)
    {
        if (m_tuner.stations[station].rssi == 0)
        {
            break;
        }

        shell_print(s, "%2d %4.1f  rssi:%u  S:%8s T:%s",
                station,
                (float)(m_tuner.stations[station].freq_kHz) / 1000.0,
                m_tuner.stations[station].rssi,
                m_tuner.stations[station].name,
                m_tuner.stations[station].text);
    }

    uint32_t freq_kHz;
    uint8_t rssi;
    bool stereo;
    int ret;

    ret = m_tuner.radio->get_tune(m_tuner.radio, &freq_kHz);
    ret = m_tuner.radio->get_rssi(m_tuner.radio, &rssi, &stereo, NULL);

    shell_print(s, "Current tune: %5.1f  rssi: %u %s",
            (float)freq_kHz / 1000.0, rssi, stereo ? "st" : "mono");

    return 0;
}

static int _CommandPoll(const struct shell *s, size_t argc, char **argv)
{
    int timeout = 30;

    if (argc > 1)
    {
        timeout = strtol(argv[1], NULL, 0);
    }
#if 0
    timeout *= 1000;

    while (timeout > 0)
    {
        k_msleep(2);
        timeout -= 2;

        // wait for rds ready
        rssi = si4703_get_rssi(&m_tuner);
        if (rssi & 0x8000)
        {
            si4703_decode_rds(&m_tuner);

            if (m_tuner.rds_changed)
            {
                LOG_INF("S:%8s  T:%s", m_tuner.name, m_tuner.text);
            }
        }
    }
#endif
    return 0;
}

static int _CommandDisco(const struct shell *s, size_t argc, char **argv)
{
    int timeout = 30;
    bool complete = false;
    uint32_t delay;
    tuner_state_t state;

    if (argc > 1)
    {
        timeout = strtol(argv[1], NULL, 0);
    }

    timeout *= 1000;

    // setup for discovery
    m_tuner.disco_station = MAX_CHANNELS;
    m_tuner.state = TUNER_DISCOVERY;
    TunerSlice(&delay, &state);

    // drive to completion
    do
    {
        delay = 20;
        TunerDiscoSlice(&delay, &complete);
        k_msleep(delay);
    }
    while(!complete && timeout-- > 0);
    m_tuner.state = TUNER_READY;

    LOG_INF("Stations:");
    for (uint16_t curchan = 0; curchan < MAX_CHANNELS; curchan++)
    {
        if (m_tuner.stations[curchan].rssi == 0)
        {
            break;
        }

        LOG_INF("%d %4.1f  rssi:%u  S:%8s T:%s",
                curchan,
                (float)m_tuner.stations[curchan].freq_kHz / 1000.0,
                m_tuner.stations[curchan].rssi,
                m_tuner.stations[curchan].name,
                m_tuner.stations[curchan].text);
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(_sub_tuner_commands,
       SHELL_CMD_ARG(vol,       NULL,  "Set Volume [vol  0-100% (80)]", _CommandSetVol, 1, 2),
       SHELL_CMD_ARG(freq,      NULL,  "Set Frequencey [kHz (89700)]", _CommandSetFreq, 1, 2),
       SHELL_CMD_ARG(station,   NULL,  "Set Station [0-max (0)]", _CommandSetStation, 1, 2),
       SHELL_CMD(seeku,         NULL,  "Seek Up", _CommandSeekUp),
       SHELL_CMD(seekd,         NULL,  "Seek Down", _CommandSeekDown),
       SHELL_CMD(tuneu,         NULL,  "Tune Up", _CommandTuneUp),
       SHELL_CMD(tuned,         NULL,  "Tune Down", _CommandTuneDown),
       SHELL_CMD(print,         NULL,  "Print out info", _CommandPrint),
       SHELL_CMD_ARG(poll,      NULL,  "Poll staton rds [seconds (30)]", _CommandPoll, 1, 2),
       SHELL_CMD_ARG(disco,     NULL,  "Discover Stations [seconds (30)]", _CommandDisco, 1, 2),
       SHELL_SUBCMD_SET_END );

SHELL_CMD_REGISTER(tuner, &_sub_tuner_commands, "Tuner commands", NULL);

#endif

