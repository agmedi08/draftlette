#pragma once
/* ----- NiceMCU-C5E-DEV_2.8IPS -----
 *
 * XH-C5E_N16R8 module: ESP32-C5 (single-core RISC-V, 2.4 / 5 GHz
 * WiFi 6, BLE), 16 MB flash, 8 MB PSRAM. 2.8" 240x320 IPS panel with
 * an ST7789P3 controller on a write-only 4-wire SPI interface
 * (rendered landscape at 320x240 by the display backend), capacitive
 * touch on I2C, MicroSD slot on SPI, WS2812 LED, buzzer amplifier,
 * RESET and BOOT buttons, and two USB-C ports: one to a CH340C
 * USB-to-UART bridge (flashing + console on UART0), one to the
 * ESP32-C5's native USB (USB-Serial/JTAG on GPIO13 / GPIO14).
 *
 * Sources: the vendor's example sketch and README
 * (github.com/NiceMCU-01/NiceMCU-C5-2.8IPS, ESP32_C5_2.8IPS.ino) and
 * the schematic in its docs/ directory (NiceMCU-C5-2.8IPS_V1.0.pdf).
 * Tested on physical hardware.
 *
 * Included by main/app_config.h when
 * CONFIG_DRAFTLING_MODEL_NICEMCU_C5_28IPS is selected.
 */

#define BOARD_NAME          "NiceMCU-C5E-DEV_2.8IPS"

/* ST7789P3 panel, driven through the PARLIO peripheral in 1-bit SPI
 * mode (CONFIG_DRAFTLING_LCD_BUS_PARLIO): the ESP32-C5 has a single
 * general-purpose SPI controller and it is needed for the MicroSD
 * slot, which sits on different pins. No MISO (the panel is
 * write-only). The panel's RESET line is tied to the chip's EN net,
 * so it is reset together with the ESP32-C5; the backend also sends
 * SWRESET. The backlight is switched by an AO3400 MOSFET on GPIO25
 * (active HIGH, 10K pull-down on the gate, so it is off while the
 * chip is in reset or deep sleep).
 *
 * The SPI clock is 10 MHz, the default of ESP-IDF's PARLIO LCD
 * example: PARLIO raises the CS ("valid") line on the same clock edge
 * as the first data bit, so a fast clock leaves the ST7789 little
 * chip-select setup time before the first SCL edge (15 ns minimum).
 * A full-screen update takes about 125 ms at this rate. */
#define LCD_MOSI_PIN        7
#define LCD_MISO_PIN        -1
#define LCD_SCK_PIN         6
#define LCD_CS_PIN          5
#define LCD_DC_PIN          4
#define LCD_RST_PIN         -1
#define LCD_BL_PIN          25
#define LCD_IM0_PIN         -1
#define LCD_IM1_PIN         -1
#define LCD_SPI_CLOCK_HZ    (10 * 1000 * 1000)
#define LCD_BL_PWM_HZ       20000

/* MicroSD on the only GP-SPI controller (SPI2), on its own pins.
 * Chip-select is GPIO13, the ESP32-C5's native USB D- pin, as the
 * vendor's README and sketch say (the schematic draws it on the USB_D+
 * net, GPIO14, but on hardware the card only answers with CS on
 * GPIO13). The native USB-C port therefore does not work while the
 * firmware runs (the vendor notes that USB and MicroSD cannot be used
 * together); flash and monitor through the CH340 USB-C port instead.
 *
 * CS must be toggled by the sdspi driver: holding it LOW for the whole
 * session mounted the card and saved files, but formatting failed
 * ("write failed, card is locked"), as the card needs CS edges between
 * commands. */
#define SD_SPI_MISO_PIN     8
#define SD_SPI_MOSI_PIN     10
#define SD_SPI_SCK_PIN      9
#define SD_SPI_CS_PIN       13
#define SD_EN_PIN           -1
#define SD_SPI_HOST_NUM     SPI2_HOST

/* Touch controller on its own I2C bus (5.1K pull-ups on the board).
 * The vendor's README and sketch talk to a Hynitron CST816D at 0x15;
 * the product manual lists a FocalTech FT6336G (0x38). Both report
 * the first touch point at the same registers (finger count at 0x02,
 * X/Y at 0x03..0x06), so the FT6336U driver handles either:
 * touchscreen_init() tries TOUCH_I2C_ADDR first and falls back to
 * TOUCH_I2C_ADDR_ALT. The CST816D does not answer while it is in its
 * idle auto-sleep, so it stays the primary address -- when neither
 * address answers at boot, the driver keeps 0x15 and the chip wakes
 * on the first touch. RST is tied to the chip's EN net.
 *
 * The touch controller reports in the panel's native 240x320 portrait
 * frame, turned 180 degrees from the vendor example's upright portrait
 * rotation; swap + mirror Y map it to the landscape frame of the
 * display backend (MADCTL 0xA0). Verified on hardware (with swap +
 * mirror X, as on the Freenove FNK0104B, a tap in the lower-right
 * corner landed in the upper-left one). */
#define I2C_SDA_PIN         24
#define I2C_SCL_PIN         23
#define TOUCH_I2C_ADDR      0x15
#define TOUCH_I2C_ADDR_ALT  0x38
#define TOUCH_INT_PIN       1
#define TOUCH_RST_PIN       -1
#define TOUCH_NATIVE_W      240
#define TOUCH_NATIVE_H      320
#define TOUCH_SWAP_XY       1
#define TOUCH_MIRROR_X      0
#define TOUCH_MIRROR_Y      1

/* No battery monitor. */
#define BATT_ADC_PIN        -1
#define BATT_EN_PIN         -1
#define BATT_DIVIDER        1

/* On-board WS2812 LED (data on GPIO27). main.cpp switches it off at
 * boot (ws2812_off()). */
#define BOARD_WS2812_PIN    27

/* BOOT / user button (GPIO28, active LOW, external 10K pull-up).
 * Holding it 2 s forgets all paired BLE keyboards. It cannot wake the
 * chip from deep sleep (the ESP32-C5 wakes only on GPIO0-6); the
 * RESET button does. */
#define WAKEUP_GPIO_NUM     28
