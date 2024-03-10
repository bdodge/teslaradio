
#include <display.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>

#define DISPLAY_DRIVER      DT_NODELABEL(display)

#define DEFAULT_FONT_INDEX  (4)
#define MAX_FONTS           (5)

static struct display
{
    const struct device *dev;
    uint16_t rows;
    uint16_t cols;
    uint8_t ppt;
    uint8_t num_fonts;
    uint8_t font;
    uint8_t font_width[MAX_FONTS];
    uint8_t font_height[MAX_FONTS];
}
m_chip;

void display_play(void)
{
    uint8_t x_offset = 0;
    uint8_t y_offset;

    while (1)
    {
        for (int i = 0; i < m_chip.rows; i++)
        {
            y_offset = i * m_chip.ppt;

            switch (i)
            {
            case 0:
                cfb_print(m_chip.dev, " average", x_offset, y_offset);
                break;
            case 1:
                cfb_print(m_chip.dev, " good",    x_offset, y_offset);
                break;
            case 2:
                cfb_print(m_chip.dev, " better",  x_offset, y_offset);
                break;
            case 3:
                cfb_print(m_chip.dev, " best",    x_offset, y_offset);
                break;
            default:
                break;
            }

            cfb_framebuffer_finalize(m_chip.dev);

            k_sleep(K_MSEC(300));
        }

        cfb_framebuffer_clear(m_chip.dev, false);

        if (x_offset > 50)
        {
            x_offset = 0;
        }
        else
        {
            x_offset += 5;
        }
    }
}

int DisplaySetFontIndex(int index)
{
    int ret;

    if (index < 0)
    {
        index = 0;
    }
    else if (index > m_chip.num_fonts)
    {
        index = m_chip.num_fonts - 1;
    }

    ret = cfb_framebuffer_set_font(m_chip.dev, index);
    m_chip.ppt  = cfb_get_display_parameter(m_chip.dev, CFB_DISPLAY_PPT);
    LOG_DBG("Selected font index[%d]", index);
    return ret;
}

int DisplaySetFont(int height)
{
    int ret = -EINVAL;
    uint32_t diff;
    uint32_t mindiff;
    int mindex;

    mindex = 0;
    mindiff = 255;

    for (int index = 0; index < m_chip.num_fonts; index++)
    {
        if (height >= m_chip.font_height[index])
        {
            diff = height - m_chip.font_height[index];
        }
        else
        {
            diff = m_chip.font_height[index] - height;
        }

        if (diff < mindiff)
        {
            mindiff = diff;
            mindex = index;
        }
    }

    ret = DisplaySetFontIndex(mindex);
    return ret;
}

int DisplayText(int x, int y, const char *text)
{
    cfb_print(m_chip.dev, text, x, y);
    cfb_framebuffer_finalize(m_chip.dev);
    return 0;
}

int DisplayInit(void)
{
    m_chip.dev = DEVICE_DT_GET(DISPLAY_DRIVER);
    if (m_chip.dev == NULL)
    {
        return -ENODEV;
    }

    if (display_set_pixel_format(m_chip.dev, PIXEL_FORMAT_MONO10) != 0)
    {
        LOG_ERR("Failed to set required pixel format");
        return -ENODEV;
    }

    if (cfb_framebuffer_init(m_chip.dev))
    {
        LOG_ERR("Framebuffer initialization failed!");
        return -ENODEV;
    }

    cfb_framebuffer_clear(m_chip.dev, true);
    display_blanking_off(m_chip.dev);
    cfb_framebuffer_invert(m_chip.dev);  // white on black

    m_chip.rows = cfb_get_display_parameter(m_chip.dev, CFB_DISPLAY_ROWS);
    m_chip.cols = cfb_get_display_parameter(m_chip.dev, CFB_DISPLAY_COLS);

    m_chip.num_fonts = cfb_get_numof_fonts(m_chip.dev);

    for (int idx = 0; idx < m_chip.num_fonts; idx++)
    {
        cfb_get_font_size(m_chip.dev, idx, &m_chip.font_width[idx], &m_chip.font_height[idx]);
        LOG_INF("font[%d] font dimensions %2dx%d", idx, m_chip.font_width[idx], m_chip.font_height[idx]);
    }

    DisplaySetFontIndex(DEFAULT_FONT_INDEX);

    LOG_INF("x_res %d, y_res %d, ppt %d, rows %d, cols %d",
            cfb_get_display_parameter(m_chip.dev, CFB_DISPLAY_WIDTH),
            cfb_get_display_parameter(m_chip.dev, CFB_DISPLAY_HEIGH),
            m_chip.ppt,
            m_chip.rows,
            m_chip.cols);

    DisplaySetFontIndex(DEFAULT_FONT_INDEX);
    DisplayText(20, 0, "Booting");
    DisplaySetFont(20);
    DisplayText(24, 12, "Hello");

    cfb_framebuffer_finalize(m_chip.dev);
    return 0;
}

#if CONFIG_SHELL

#include <zephyr/shell/shell.h>
#include <stdlib.h>

static int _CommandText(const struct shell *s, size_t argc, char **argv)
{
    const char *text = "Hello";
    uint8_t font = DEFAULT_FONT_INDEX;

    if (argc > 1)
    {
        text = argv[1];
    }

    if (argc > 2)
    {
        font = strtoul(argv[2], NULL, 0);
    }

    cfb_framebuffer_clear(m_chip.dev, false);
    DisplaySetFontIndex(DEFAULT_FONT_INDEX);
    DisplayText(10, 0, text);
    DisplaySetFont(20);
    DisplayText(10, 8, text);

    return 0;
}

static int _CommandTest(const struct shell *s, size_t argc, char **argv)
{
    DisplayInit();
    display_play();
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(_sub_display_commands,
       SHELL_CMD_ARG(text,  NULL,  "Text [text (Hello)][Font [2)]", _CommandText, 1, 3),
       SHELL_CMD(test,  NULL,  "Test", _CommandTest),
       SHELL_SUBCMD_SET_END );

SHELL_CMD_REGISTER(display, &_sub_display_commands, "Display commands", NULL);

#endif
