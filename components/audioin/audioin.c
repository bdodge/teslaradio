/*
 * Copyright (c) 2024, Level Home Inc.
 *
 * All rights reserved.
 *
 * Proprietary and confidential. Unauthorized copying of this file,
 * via any medium is strictly prohibited.
 *
 */

#include "audioin.h"
#include "asserts.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(audioin, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/audio/codec.h>

#define I2S_RX_NODE DT_NODELABEL(i2s_rx)

// Frequency source of audio clock (xtal 32MHz)
#define AUDIO_CLOCK             (32000000)

// bytes per sample
#define AUDIO_IN_BYTES_PER_SAMPLE  (4) /* 24 bit stereo from codec */
#define AUDIO_OUT_BYTES_PER_SAMPLE (2) /* 16 bit stereo to reader */

// sector size we want
#define AUDIO_SECTOR_SIZE       (512)

// how many bytes per sample block.. easiest to provide one sectors worth per
#define AUDIO_BLOCK_SIZE        (AUDIO_SECTOR_SIZE * AUDIO_IN_BYTES_PER_SAMPLE / AUDIO_OUT_BYTES_PER_SAMPLE)

// blocks in flight total, need at least 3 for proper operation
#define AUDIO_BLOCK_COUNT       (6)

// size of output ring buffer (bytes)
#define AUDIO_RING_COUNT        (2 * AUDIO_SECTOR_SIZE)

// I2S interface requires a slab alloctor for audio blocks
K_MEM_SLAB_DEFINE_STATIC(mem_slab, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_COUNT, 4);

static struct playback_ctx
{
    const struct device *i2s_dev;
    struct k_sem        start_sem;
    struct k_mutex      block_lock;

    uint32_t            channels;
    uint32_t            block_size;
    uint32_t            bytes_per_sample;
    uint32_t            sample_rate;

    bool                active;
    uint32_t            rx_blocks;
    uint64_t            start_time_ms;
    uint64_t            duration_ms;

    uint32_t            data[AUDIO_RING_COUNT];
    int                 head;
    int                 tail;
    int                 count;
}
m_playback_state;

static struct
{
    uint8_t divisor;
    uint32_t mckfreq;
}
_s_clk_div[] =
{
    { 1,  0x00000000 }, // 32.0
    { 2,  0x80000000 }, // 16.0
    { 3,  0x50000000 }, // 10.7
    { 4,  0x40000000 }, // 8.0
    { 5,  0x30000000 }, // 6.4
    { 6,  0x28000000 }, // 5.3
    { 8,  0x20000000 }, // 4.0
    { 10, 0x18000000 }, // 3.2
    { 11, 0x16000000 }, // 2.9
    { 15, 0x11000000 }, // 2.13
    { 16, 0x10000000 }, // 2.0
    { 21, 0xC0000000 }, // 1.5
};

static int _SetupAudioClock(uint32_t sample_rate)
{
    int ret = -EINVAL;
    NRF_I2S_Type *p_reg = NRF_I2S0;
    uint32_t desired_mclk;
    uint32_t actual_freq;
    uint32_t min_diff;
    uint32_t diff;
    int min_dex;

    // adc clk wants to be 256 sample-rate, so 256*44100 = 11289600
    //
    desired_mclk = 256 * m_playback_state.sample_rate;
    if (desired_mclk > AUDIO_CLOCK)
    {
        LOG_ERR("bad clock");
        goto exit;
    }

    // See pages 230-246 of nRF5340_OPS_v-0.5.pdf

    // find divisor value closest to desired mclk frequency
    min_dex = 0;
    min_diff = AUDIO_CLOCK - desired_mclk;

    for (int i = 1; i < ARRAY_SIZE(_s_clk_div); i++)
    {
        actual_freq = AUDIO_CLOCK / _s_clk_div[i].divisor;
        if (actual_freq > desired_mclk)
        {
            diff = actual_freq - desired_mclk;
        }
        else
        {
            diff = desired_mclk - actual_freq;
        }

        if (diff < min_diff)
        {
            min_diff = diff;
            min_dex = i;
        }
    }

    //
    LOG_INF("freq_config=%08X actual_freq=%u",
            _s_clk_div[min_dex].mckfreq,
            AUDIO_CLOCK / _s_clk_div[min_dex].divisor);
    //
    if (min_dex == 0)
    {
        p_reg->CONFIG.CLKCONFIG |= ((uint32_t) 1 << I2S_CONFIG_CLKCONFIG_BYPASS_Pos);
    }
    else
    {
        p_reg->CONFIG.CLKCONFIG &= ~((uint32_t) 1 << I2S_CONFIG_CLKCONFIG_BYPASS_Pos);
        p_reg->CONFIG.MCKFREQ = _s_clk_div[min_dex].mckfreq;
        p_reg->CONFIG.RATIO = I2S_CONFIG_RATIO_RATIO_256X;  // Div by 256
    }

    p_reg->CONFIG.MCKEN = 1;
    ret = 0;
exit:
    return ret;
}


static int _ReceiveBlock(struct playback_ctx *playback)
{
    int ret;
    void *mem_block;
    uint32_t *src;
    uint32_t block_size;
    int count;

    ret = i2s_read(playback->i2s_dev, &mem_block, &block_size);

    if (ret < 0)
    {
        LOG_ERR("Failed to read i2s: %d at block %u %u\n", ret, playback->rx_blocks, playback->count);
        playback->active = 0;
    }
    else
    {
        k_mutex_lock(&playback->block_lock, K_FOREVER);

        verify(block_size == AUDIO_BLOCK_SIZE);

        if (playback->count >= (AUDIO_RING_COUNT - (AUDIO_SECTOR_SIZE / 4)))
        {
            // overflow, drop the oldest chunk
            playback->count-= (AUDIO_SECTOR_SIZE / 4);
            playback->tail+= (AUDIO_SECTOR_SIZE / 4);
            playback->tail%= AUDIO_RING_COUNT;
        }
        // copy raw data, dropping some bits.  the format is 24 bit data
        // padded into 32 bits of data right justified and sign-extended
        //
        src = (uint32_t*)mem_block;
        for (count = 0; count < AUDIO_SECTOR_SIZE; count += 4, src+= 2)
        {
#if 1
            // scale preserving sign
            int32_t left  = (int32_t)src[0];
            int32_t right = (int32_t)src[1];

            left <<= 8;
            right >>= 8;

            playback->data[playback->head] = (uint32_t)(left & 0xFFFF0000) | (uint32_t)(right & 0xFFFF);
#else
            // take upper 16 bits of each channel
            playback->data[playback->head] = (src[0] & 0xFFFF0000) | (src[1] >> 16);
#endif
            playback->count++;
            playback->head++;
            if (playback->head >= AUDIO_RING_COUNT)
            {
                playback->head = 0;
            }
        }

        k_mutex_unlock(&playback->block_lock);

        playback->rx_blocks++;
        k_mem_slab_free(&mem_slab, mem_block);
    }

    return ret;
}

static void _AudioTaskMain(void * parameter)
{
    int ret = -1;
    struct audio_codec_cfg codec_cfg;

    m_playback_state.i2s_dev = DEVICE_DT_GET(I2S_RX_NODE);

    if (!device_is_ready(m_playback_state.i2s_dev))
    {
        LOG_ERR("i2s not ready");
    }

    k_sem_init(&m_playback_state.start_sem, 0, 1);

    while(true)
    {
        // wait for signal to start
        ret = k_sem_take(&m_playback_state.start_sem, K_FOREVER);
        if (!ret)
        {
            m_playback_state.head = 0;
            m_playback_state.tail = 0;
            m_playback_state.count = 0;

            codec_cfg.dai_cfg.i2s.word_size         = m_playback_state.bytes_per_sample * 8;
            codec_cfg.dai_cfg.i2s.channels          = m_playback_state.channels;
            codec_cfg.dai_cfg.i2s.format            = I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED;
            codec_cfg.dai_cfg.i2s.options           = I2S_OPT_BIT_CLK_SLAVE | I2S_OPT_FRAME_CLK_SLAVE;
           // codec_cfg.dai_cfg.i2s.options           = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
            codec_cfg.dai_cfg.i2s.frame_clk_freq    = m_playback_state.sample_rate;
            codec_cfg.dai_cfg.i2s.mem_slab          = &mem_slab;
            codec_cfg.dai_cfg.i2s.block_size        = m_playback_state.block_size;
            codec_cfg.dai_cfg.i2s.timeout           = 1000;

            // configure i2s rx
            ret = i2s_configure(m_playback_state.i2s_dev, I2S_DIR_RX, &codec_cfg.dai_cfg.i2s);
            if (ret)
            {
                LOG_ERR("Can't config i2s");
                goto failed_start;
            }

            // stop any previous i2s activity
            i2s_trigger(m_playback_state.i2s_dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
            i2s_trigger(m_playback_state.i2s_dev, I2S_DIR_RX, I2S_TRIGGER_PREPARE);

            m_playback_state.start_time_ms = k_uptime_get();

            // start i2s
            ret = i2s_trigger(m_playback_state.i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START);
            if (ret)
            {
                LOG_ERR("Can't start i2s");
                goto failed_start;
            }

            m_playback_state.active = true;

            // The Zephyr I2S interface is too "smart" about picking an MCLK to match
            // the data-rate and source clock to get minimum bit-rate error. Unfortunately
            // audio codec needs an mclk 256* the sample rate, so this code hand-sets the
            // i2s register divisors
            //
            ret = _SetupAudioClock(m_playback_state.sample_rate);

            while (m_playback_state.active)
            {
                ret = _ReceiveBlock(&m_playback_state);

                if (m_playback_state.active && m_playback_state.duration_ms > 0)
                {
                    if ((m_playback_state.start_time_ms + m_playback_state.duration_ms) < k_uptime_get())
                    {
                        m_playback_state.active = false;
                    }
                }
            }

failed_start:
            i2s_trigger(m_playback_state.i2s_dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
        }
    }

    LOG_ERR("Audio Init failed");
    return;
}

#define AUDIO_STACKSIZE (1024)
#define AUDIO_PRIORITY  (-4)

K_THREAD_DEFINE(s_audio_thread_id, AUDIO_STACKSIZE, _AudioTaskMain, NULL, NULL, NULL, AUDIO_PRIORITY, 0, 0);

int AudioGetSamples(void **out_sample_block, size_t *out_sample_bytes)
{
    int ret = -ENOENT;

    *out_sample_block = NULL;
    *out_sample_bytes = 0;

    if (m_playback_state.count >= AUDIO_SECTOR_SIZE)
    {
        k_mutex_lock(&m_playback_state.block_lock, K_FOREVER);

        *out_sample_block = m_playback_state.data + m_playback_state.tail;
        *out_sample_bytes = AUDIO_SECTOR_SIZE;

        m_playback_state.count-= (AUDIO_SECTOR_SIZE / 4);
        m_playback_state.tail+= (AUDIO_SECTOR_SIZE / 4);
        m_playback_state.tail%= AUDIO_RING_COUNT;
        k_mutex_unlock(&m_playback_state.block_lock);
        ret = 0;
    }

    return ret;
}

bool AudioActive(void)
{
    return m_playback_state.active;
}

int AudioStart(void)
{
    m_playback_state.channels           = 2;
    m_playback_state.bytes_per_sample   = 24 / 8;
    m_playback_state.sample_rate        = 44100;
    m_playback_state.block_size         = AUDIO_BLOCK_SIZE;

    m_playback_state.rx_blocks          = 0;
    m_playback_state.duration_ms        = 0;

    k_sem_give(&m_playback_state.start_sem);
    return 0;
}

int AudioStop(void)
{
    m_playback_state.active = false;
    return 0;
}

int AudioInit(void)
{
    k_mutex_init(&m_playback_state.block_lock);
    return 0;
}

#if CONFIG_SHELL

#include <zephyr/shell/shell.h>
#include <stdlib.h>

static void _CmdAudioTest(const struct shell *shell, size_t argc, char **argv)
{
    int sample_bits = 24;
    int sample_rate = 44100;

    m_playback_state.channels           = 2;
    m_playback_state.bytes_per_sample   = sample_bits / 8;
    m_playback_state.sample_rate        = sample_rate;
    m_playback_state.block_size         = AUDIO_BLOCK_SIZE;

    m_playback_state.rx_blocks          = 0;
    m_playback_state.duration_ms        = 2000;

    k_sem_give(&m_playback_state.start_sem);

    do
    {
        k_msleep(1000);
        shell_print(shell, "%u blocks", m_playback_state.rx_blocks);
    }
    while (m_playback_state.active);
}

static void _CmdAudioStart(const struct shell *shell, size_t argc, char **argv)
{
    AudioStart();
}

SHELL_STATIC_SUBCMD_SET_CREATE(m_sub_audio,
    SHELL_CMD_ARG(test,  NULL, "Test audio", _CmdAudioTest, 1, 3),
    SHELL_CMD(start,  NULL, "Start audio", _CmdAudioStart),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(audio, &m_sub_audio, "Audio", NULL);

#endif

