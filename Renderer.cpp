#include <cstdint>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include <FreeSansBold24pt7b.h>

class Renderer
{
    struct Theme
    {
        uint16_t background;
        uint16_t text;
    };

    struct SectorBounds
    {
        uint16_t x_start, x_end;
        uint16_t y_start, y_end;
    };

    const char *sector_state[9] = {"", "", "", "", "", "", "", "", ""};

    const SectorBounds sector_bounds[9] = {
        {x_start : 0, x_end : 11, y_start : 0, y_end : 2}, // UPPER_LEFT
        {6, 17, 0, 2},                                     // UPPER_CENTER
        {12, 23, 0, 2},                                    // UPPER_RIGHT

        {0, 11, 3, 3},  // CENTER_LEFT
        {6, 17, 3, 3},  // CENTER
        {12, 23, 3, 3}, // CENTER_RIGHT

        {0, 11, 4, 6},  // LOWER_LEFT
        {6, 17, 4, 6},  // LOWER_CENTER
        {12, 23, 4, 6}, // LOWER_RIGHT
    };

    const Theme color_schemes[2] = {{BLACK, WHITE}, {WHITE, BLACK}};

    /*
    Screen Layout:
            / x | x | x \
          / x | x | x | x \
        / x | x | x | x | x \
       |x | x | x | x | x | x|
        \ x | x | x | x | x /
          \ x | x | x | x /
            \ x | x | x /

    */

    // Row data — stack allocated, lives for program duration
    const uint8_t r0[3][2] = {{0, 5}, {6, 11}, {12, 17}};
    const uint8_t r1[4][2] = {{0, 5}, {6, 11}, {12, 17}, {18, 23}};
    const uint8_t r2[5][2] = {{0, 5}, {6, 11}, {12, 17}, {18, 23}, {24, 29}};
    const uint8_t r3[6][2] = {{0, 5}, {6, 11}, {12, 17}, {18, 23}, {24, 29}, {30, 35}};
    const uint8_t r4[5][2] = {{0, 5}, {6, 11}, {12, 17}, {18, 23}, {24, 29}};
    const uint8_t r5[4][2] = {{0, 5}, {6, 11}, {12, 17}, {18, 23}};
    const uint8_t r6[3][2] = {{0, 5}, {6, 11}, {12, 17}};

    const uint8_t (*grid[7])[2] = {r0, r1, r2, r3, r4, r5, r6};
    const uint8_t grid_cols[7] = {3, 4, 5, 6, 5, 4, 3};

    Arduino_DataBus *bus = new Arduino_ESP32QSPI(
        LCD_CS /* CS */, LCD_SCLK /* SCK */, LCD_SDIO0 /* SDIO0 */, LCD_SDIO1 /* SDIO1 */,
        LCD_SDIO2 /* SDIO2 */, LCD_SDIO3 /* SDIO3 */);

public:
    // public for now so I can test
    Arduino_GFX *gfx = new Arduino_CO5300(bus, LCD_RST /* RST */,
                                          0 /* rotation */, false /* IPS */, LCD_WIDTH, LCD_HEIGHT,
                                          6 /* col offset 1 */, 0 /* row offset 1 */, 0 /* col_offset2 */, 0 /* row_offset2 */);
    enum class Sector
    {
        upper_sector_left,
        upper_sector_center,
        upper_sector_right,

        center_sector_left,
        center_sector,
        center_sector_right,

        lower_sector_left,
        lower_sector_center,
        lower_sector_right,
    };

    enum class ColorTheme
    {
        dark,
        light
    };

    uint8_t _currentFontSize;
    Theme _currentTheme;

    void init(ColorTheme startTheme, uint8_t brightness, uint8_t defaultFontSize)
    {
        Theme theme = color_schemes[(int)startTheme];
        _currentFontSize = defaultFontSize;
        _currentTheme = theme;

        gfx->begin(80000000);
        gfx->fillScreen(theme.background);
        gfx->setTextColor(theme.text);
        gfx->Display_Brightness(brightness);
        gfx->setFont(&FreeSansBold24pt7b);
        gfx->setTextSize(defaultFontSize);
    }

    void draw(Sector sector, const char *text, uint8_t *fontSize = nullptr)
    {
        if (fontSize)
        {
            gfx->setTextSize(*fontSize);
        }

        auto writeLocation = sector_bounds[(int)sector];
        uint16_t textWidth, textHeight;
        int16_t textEndX, textEndY;

        gfx->getTextBounds(text, writeLocation.x_start, writeLocation.y_start, &textEndX, &textEndY, &textWidth, &textHeight);
        Serial.printf("x end: %d | y end: %d | width: %u | height: %u \n", textEndX, textEndY, textWidth, textHeight);

        // Wipe only location needed for writing

        gfx->setCursor(writeLocation.x_start, writeLocation.y_start);
        gfx->print(text);

        gfx->setTextSize(_currentFontSize);

        sector_state[(int)sector] = text;
    }

    void draw(Sector sector, uint value, uint8_t *fontSize = nullptr)
    {
        unsigned int digits = 1;
        unsigned int t = value;

        while (t >= 10)
        {
            t /= 10;
            digits++;
        }

        char buf[digits + 1];
        itoa(value, buf, 10);
        draw(sector, buf, fontSize);
    }

    void draw(int32_t x, int32_t y, uint8_t *fontSize = nullptr)
    {
        if (fontSize)
        {
            gfx->setTextSize(*fontSize);
        }

        gfx->setTextSize(_currentFontSize);
    }

    // Something is overflowing or doing something weird... Getting artifacts sometimes on screen
    void setTheme(ColorTheme colorTheme)
    {
        Theme theme = color_schemes[(int)colorTheme];
        _currentTheme = theme;

        gfx->fillScreen(theme.background);
        gfx->setTextColor(theme.text);

        for (size_t i = 0; i < 9; i++)
        {
            auto sector_value = sector_state[i];
            auto sector = (Sector)i;

            draw(sector, sector_value);
        }
    }

    void setFontSize(uint8_t fontSize)
    {
        _currentFontSize = fontSize;
    }

    void forceReRender()
    {
        gfx->fillScreen(_currentTheme.background);
        gfx->setTextColor(_currentTheme.text);
    }
};