#pragma once
/* ----- Freenove FNK0104B -----
 *
 * 2.8" color LCD, ILI9341 controller, 240x320 native panel (rendered
 * landscape at 320x240 by the display backend). Standard 4-wire SPI
 * interface. Adds an FT6336U capacitive touch controller over I2C
 * compared to the FNK0104A. Pin assignments below come from
 * Freenove's own TFT_eSPI setup header
 * (FNK0104AB_2.8_240x320_ILI9341.h) and the Sketch_11.1_Touch /
 * Sketch_05.1_Battery_Voltage reference sketches bundled with the
 * board.
 * See https://github.com/Freenove/Freenove_ESP32_S3_Display
 *
 * Included by main/app_config.h when
 * CONFIG_DRAFTLING_MODEL_FREENOVE_FNK0104B is selected.
 */

#define BOARD_NAME          "Freenove FNK0104B"

/* SPI TFT panel (passed to display_ili9341.cpp through
 * display_spi_tft_config()). Same wiring on every FNK0104 SPI-TFT
 * SKU (A/B/S): no MISO, no discrete RST line (the backend sends
 * SWRESET), active-HIGH PWM backlight on GPIO45. */
#define LCD_MOSI_PIN        11
#define LCD_MISO_PIN        -1
#define LCD_SCK_PIN         12
#define LCD_CS_PIN          10
#define LCD_DC_PIN          46
#define LCD_RST_PIN         -1
#define LCD_BL_PIN          45
#define LCD_IM0_PIN         -1
#define LCD_IM1_PIN         -1
#define LCD_SPI_CLOCK_HZ    (40 * 1000 * 1000)
#define LCD_BL_PWM_HZ       50000

/* SD card on the on-chip SDMMC peripheral (1-bit mode: CLK/CMD/D0
 * only -- D1-D3 are left unconfigured, matching the existing
 * Waveshare RLCD-4.2 usage of sd_card_init()). */
#define SD_CLK_PIN          38
#define SD_CMD_PIN          40
#define SD_D0_PIN           39

/* I2C bus shared by the FT6336U touch controller. */
#define I2C_SDA_PIN         16
#define I2C_SCL_PIN         15

/* FocalTech FT6336U capacitive touch, default I2C address 0x38.
 * Native touch-panel resolution matches the LCD's native portrait
 * orientation (240x320); the touchscreen component rotates it into
 * the backend's landscape 320x240 logical frame. */
#define TOUCH_I2C_ADDR      0x38
#define TOUCH_INT_PIN       17
#define TOUCH_RST_PIN       18
#define TOUCH_NATIVE_W      240
#define TOUCH_NATIVE_H      320
#define TOUCH_SWAP_XY       1
#define TOUCH_MIRROR_X      1
#define TOUCH_MIRROR_Y      0

/* Battery voltage through a 1:2 divider (matches the M5Stack PaperS3
 * ADC backend usage: analogReadMilliVolts() * 2.0 in Freenove's
 * reference Sketch_05.1_Battery_Voltage). */
#define BATT_ADC_PIN        9
#define BATT_EN_PIN         -1
#define BATT_DIVIDER        2

/* BOOT button (KEY_PIN in Freenove's reference sketches) doubles as
 * the deep-sleep wakeup source. */
#define WAKEUP_GPIO_NUM     0
