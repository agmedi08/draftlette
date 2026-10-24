#pragma once
/* ----- Elecrow CrowPanel ESP32 HMI 5.0-inch Display -----
 *
 * 5" TN color LCD, 800 x 480 native landscape, ILI6122 / ILI5960
 * drivers on a 16-bit parallel RGB565 interface driven by the
 * ESP32-S3 LCD RGB peripheral (esp_lcd_new_rgb_panel). The module is
 * an ESP32-S3-WROOM-1-N4R8 (4 MB flash, 8 MB octal PSRAM). All panel
 * data / control GPIOs are owned by the RGB display backend
 * (components/display/display_rgb.cpp, CONFIG_DRAFTLING_RGB_BOARD_
 * CROWPANEL_50 block) and are NOT redefined here:
 *
 *   hsync=39, vsync=41, de=40, pclk=0, backlight=GPIO2,
 *   data[16] B0-4, G0-5, R0-4 = {8,3,46,9,1, 5,6,7,15,16,4,
 *   45,48,47,21,14} (the Sunton ESP32-8048S043C's data wiring),
 *   timings: pclk 15 MHz, hsync pw=4 bp=43 fp=8,
 *            vsync pw=4 bp=12 fp=8, pclk_active_neg=1.
 *
 * Values from Elecrow's own example for this board (LovyanGFX config
 * in CrowPanel-5.0-HMI-ESP32-Display-800x480, Hardware Version 3.0)
 * and the Elecrow wiki. Not yet tested on physical hardware.
 *
 * GPIO0 (the BOOT button) is the panel's PCLK while Draftling runs,
 * so it cannot serve as a wake / forget-keyboards button:
 * WAKEUP_GPIO_NUM is -1 and the device wakes from deep sleep by
 * RESET only.
 *
 * Included by main/app_config.h when
 * CONFIG_DRAFTLING_MODEL_ELECROW_CROWPANEL_50 is selected.
 */

#define BOARD_NAME      "Elecrow CrowPanel 5.0\""

/* MicroSD slot on its own SPI bus: CS=10, MOSI=11, CLK=12, MISO=13. */
#define SD_SPI_MOSI_PIN 11
#define SD_SPI_MISO_PIN 13
#define SD_SPI_SCK_PIN  12
#define SD_SPI_CS_PIN   10
#define SD_EN_PIN       -1

/* I2C bus (also on the I2C connector): GT911 touch and, on V3.0
 * boards, the PCA9557 IO expander that drives the GT911's RST (IO0)
 * and INT (IO1) lines. */
#define I2C_SDA_PIN     19
#define I2C_SCL_PIN     20

/* PCA9557 on V3.0 boards (A0-A2 tied low). main.cpp runs Elecrow's
 * touch reset sequence through it before touchscreen_init(); on
 * earlier board versions nothing answers there and the step is
 * skipped. */
#define CROWPANEL50_PCA9557_ADDR 0x18

/* GT911 capacitive touch. RST and INT are not ESP32 GPIOs (on V3.0
 * they are PCA9557 pins), so the touch driver polls. The reset
 * sequence holds INT low, which selects address 0x5D; earlier board
 * versions without the expander may come up at 0x14 instead. The
 * panel is natively landscape 800x480: no swap or mirror expected
 * (Elecrow's example maps the raw coordinates 1:1). */
#define TOUCH_I2C_ADDR      0x5D
#define TOUCH_I2C_ADDR_ALT  0x14
#define TOUCH_INT_PIN       CONFIG_DRAFTLING_TOUCH_INT_GPIO
#define TOUCH_RST_PIN       CONFIG_DRAFTLING_TOUCH_RST_GPIO
#define TOUCH_NATIVE_W      800
#define TOUCH_NATIVE_H      480
#define TOUCH_SWAP_XY       0
#define TOUCH_MIRROR_X      0
#define TOUCH_MIRROR_Y      0

/* The BAT connector (PH2.0-2P, 3.7-4.2 V) has a charging circuit but
 * no voltage sense wired to the ESP32. */
#define BATT_ADC_PIN    -1
#define BATT_EN_PIN     -1
#define BATT_DIVIDER    1

/* No wake button: GPIO0 is the panel PCLK (see above). */
#define WAKEUP_GPIO_NUM -1
