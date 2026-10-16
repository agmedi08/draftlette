#pragma once
/* ----- Viewe UEDX24320028E-WB-A + UEED035HV-RX40-L001 -----
 *
 * VIEWE UEDX24320028E-WB-A ESP32-S3 board (ESP32-S3-WROOM-1-N16R8:
 * 16 MB flash, 8 MB octal PSRAM) with the VIEWE UEED035HV-RX40-L001
 * display attached in place of the board's stock 2.8" GC9307 panel:
 * 3.5" 320x480 sunlight-readable transflective color LCD, ST7365
 * controller (ST7796-compatible), rendered landscape at 480x320 by the
 * display backend over 4-wire SPI. It uses Viewe's UEDX32480035E-WB-A
 * panel configuration (same pins, IM straps both HIGH, inversion on).
 * The panel stays readable in ambient light with the backlight fully
 * off, so the backlight setting goes down to 0 %
 * (CONFIG_DRAFTLING_BACKLIGHT_MIN_PCT). No battery.
 *
 * Display: https://viewedisplay.com/product/3-5-inch-320x480-sunlight-readable-transflective-with-super-low-power-tft-lcd-module/
 *
 * Pin assignments come from the vendor repository
 * https://github.com/VIEWESMART/UEDX24320028ESP32-2.8inch-Touch-Display
 * (README, V1.1 schematic, ESP-IDF example and the ESP32_Display_Panel
 * board files BOARD_VIEWE_UEDX24320028E_WB_A.h /
 * BOARD_VIEWE_UEDX32480035E_WB_A.h).
 *
 * Included by main/app_config.h when
 * CONFIG_DRAFTLING_MODEL_VIEWE_UEDX24320028_UEED035HV is selected.
 */

#define BOARD_NAME          "Viewe UEDX24320028E-WB-A + UEED035HV"

/* ST7365 SPI panel (passed to display_ili9341.cpp through
 * display_spi_tft_config()). RST comes from the board's V1.1
 * schematic. IM0/IM1 are the interface-mode straps (IM2 is tied HIGH
 * on the board); both are driven HIGH to select 4-wire 8-bit SPI.
 * The vendor runs this panel at 80 MHz, but these pins are not the
 * SPI2 IOMUX pins and go through the GPIO matrix; 40 MHz leaves
 * timing margin and still pushes a full 480x320 frame in ~60 ms.
 * The backlight is an LP3310 boost LED driver with the PWM on its
 * dimming input rather than on the LED string itself, so it uses a
 * conventional LED-driver dimming frequency (above the audible
 * range, well below what an internal soft-start filter could smooth
 * into a DC level). */
#define LCD_MOSI_PIN        45
#define LCD_MISO_PIN        -1
#define LCD_SCK_PIN         40
#define LCD_CS_PIN          42
#define LCD_DC_PIN          41
#define LCD_RST_PIN         39
#define LCD_BL_PIN          13
#define LCD_IM0_PIN         47
#define LCD_IM1_PIN         48
#define LCD_SPI_CLOCK_HZ    (40 * 1000 * 1000)
#define LCD_BL_PWM_HZ       20000

/* MicroSD on the on-chip SDMMC peripheral. The slot is wired for
 * 4-bit mode (D1=18, D2=15, D3=21); Draftling uses 1-bit mode. */
#define SD_CLK_PIN          14
#define SD_CMD_PIN          17
#define SD_D0_PIN           16

/* I2C bus carrying only the CHSC6540 touch controller. */
#define I2C_SDA_PIN         1
#define I2C_SCL_PIN         3

/* Capacitive touch at 0x2E, Chipsemi CHSC6540-compatible. Its
 * touch-point registers use the FocalTech layout (count at 0x02, first
 * point's X/Y at 0x03..0x06), so the FT6336U poll routine drives it. The
 * chip ignores the register address of a read and always answers from
 * 0x00, which is why that routine reads from 0x00.
 *
 * Native touch frame is the panel's portrait 320x480 in the vendor's
 * MADCTL 0x48 orientation (the vendor firmware applies no touch
 * transform). The backend's landscape MADCTL 0x28 maps logical
 * (x, y) to portrait (319 - y, x), so the inverse is mirror X, then
 * swap -- the same as the Freenove FNK0104B's ILI9341 + FT6336U. */
#define TOUCH_I2C_ADDR      0x2E
#define TOUCH_INT_PIN       CONFIG_DRAFTLING_TOUCH_INT_GPIO
#define TOUCH_RST_PIN       CONFIG_DRAFTLING_TOUCH_RST_GPIO
#define TOUCH_NATIVE_W      320
#define TOUCH_NATIVE_H      480
#define TOUCH_SWAP_XY       1
#define TOUCH_MIRROR_X      1
#define TOUCH_MIRROR_Y      0

/* No battery on this board (USB-C 5 V only). */
#define BATT_ADC_PIN        -1
#define BATT_EN_PIN         -1
#define BATT_DIVIDER        1

/* BOOT button on GPIO0 (external 10k pull-up) is the deep-sleep
 * wakeup source. GPIO0 also carries the WS2812B RGB LED's data line
 * through a diode; Draftling does not drive the LED. */
#define WAKEUP_GPIO_NUM     0
