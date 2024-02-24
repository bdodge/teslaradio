
#pragma once

#define REG_DEVICEID    0x00
#define REG_CHIPID      0x01
#define REG_POWERCFG    0x02
#define REG_CHANNEL     0x03
#define REG_SYSCONFIG1  0x04
#define REG_SYSCONFIG2  0x05
#define REG_SYSCONFIG3  0x06
#define REG_TEST1       0x07
#define REG_TEST2       0x08
#define REG_BOOTCFG     0x09
#define REG_RSSI        0x0a
#define REG_READCHAN    0x0b
#define REG_RDSA        0x0c
#define REG_RDSB        0x0d
#define REG_RDSC        0x0e
#define REG_RDSD        0x0f

/* REG_POWERCFG */
#define DMUTE           0x4000
#define MUTE            0x0000
#define MONO            0x2000
#define SKMODE          0x0400
#define SEEKUP          0x0200
#define SEEKDOWN        0x0000
#define SEEK            0x0100
#define DISABLE         0x0040
#define ENABLE          0x0001
#define DSOFTMUTE       0x8000

/* REG_CHANNEL */
#define TUNE            0x8000

/* REG_SYSCONFIG1 */
#define RDSIEN          0x8000
#define STCIEN          0x4000
#define RDS             0x1000
#define GPIO2_INTERRUPT 0x0004

/* REG_SYSCONFIG2 */
#define RECOMMENDED_SEEKTH      0x0c00
#define BAND_USA                0x0000 /* 87.5-108MHz */
#define BAND_EURO               0x0000 /* 87.5-108MHz */
#define WIDEBAND_JAPAN          0x0040 /* 76-108MHZ */
#define BAND_JAPAN              0x0080 /* 76-90MHz */
#define SPACE_200KHZ            0x0000
#define SPACE_100KHZ            0x0010
#define SPACE_50KHZ             0x0020
#define VOLUME                  0x000F

/* REG_SYSCONFIG3 */
#define SEN_THRESHOLD           0x0040
#define DETECTION_THRESHOLD     0x0008

/* REG_RSSI */
#define STC                     0x4000
#define RSSI                    0x00FF
#define AFCRL                   0x1000
#define SFBL                    0x2000

/* go from frequency in units of 100kHz to units to MHz */
#define FREQ_MUL                100

#ifdef CONFIG_RADIO_FM_USA
#define BAND                    BAND_USA
#define SPACE                   SPACE_200KHZ
#define CHANNEL_SPACE           200 /* in kHZ */
#define FREQ_MIN                87500
#define FREQ_MAX                108000
#endif

#ifdef CONFIG_RADIO_FM_EURO
#define BAND                    BAND_EURO
#define SPACE                   SPACE_100KHZ
#define CHANNEL_SPACE           100 /* in kHZ */
#define FREQ_MIN                87500
#define FREQ_MAX                108000
#endif

#ifdef CONFIG_RADIO_FM_JAPAN_WIDEBAND
#define BAND                    WIDEBAND_JAPAN
#define SPACE                   SPACE_100KHZ
#define CHANNEL_SPACE           100 /* in kHZ */
#define FREQ_MIN                76000
#define FREQ_MAX                108000
#endif

#ifdef CONFIG_RADIO_FM_JAPAN
#define BAND                    BAND_JAPAN
#define SPACE                   SPACE_100KHZ
#define CHANNEL_SPACE           100 /* in kHZ */
#define FREQ_MIN                76000
#define FREQ_MAX                90000
#endif

