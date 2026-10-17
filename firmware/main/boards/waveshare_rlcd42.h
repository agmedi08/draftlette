#pragma once
/* ----- Waveshare ESP32-S3-RLCD-4.2 -----
 *
 * Pin definitions and board constants for the Waveshare
 * ESP32-S3-RLCD-4.2 reflective LCD board.  Included by
 * main/app_config.h when CONFIG_DRAFTLING_MODEL_WAVESHARE_RLCD42
 * is selected in menuconfig.
 */

#define BOARD_NAME      "Waveshare ESP32-S3-RLCD-4.2"

/* RLCD Display - SPI interface */
#define RLCD_MOSI_PIN   12
#define RLCD_SCK_PIN    11
#define RLCD_DC_PIN     5
#define RLCD_CS_PIN     40
#define RLCD_RST_PIN    41
#define RLCD_TE_PIN     6

/* SD Card - SDMMC 1-bit interface */
#define SD_CLK_PIN      38
#define SD_CMD_PIN      21
#define SD_D0_PIN       39

/* I2C Bus */
#define I2C_SDA_PIN     13
#define I2C_SCL_PIN     14

/* Battery voltage ADC (GPIO4, ADC1_CH3, 3:1 divider) */
#define BATT_ADC_PIN    4
#define BATT_EN_PIN     -1
#define BATT_DIVIDER    3

/* Deep-sleep wakeup on GPIO18 (EXT0, active-low) */
#define WAKEUP_GPIO_NUM 18

/* On-board peripherals Draftling does not use. They sit on the
 * always-on 3.3 V rail, so main.cpp puts them in their lowest-power
 * state at boot (rlcd42_quiet_peripherals()) and holds the amplifier
 * and audio lines low through deep sleep. Pins and addresses are from
 * Waveshare's factory firmware (github.com/waveshareteam/ESP32-S3-RLCD-4.2). */
#define RLCD42_AMP_EN_PIN     46   /* speaker amplifier enable, active-high */
#define RLCD42_I2S_MCLK_PIN   16
#define RLCD42_I2S_BCLK_PIN   9
#define RLCD42_I2S_DOUT_PIN   8    /* ESP32 -> ES8311 */
#define RLCD42_SHTC3_ADDR     0x70 /* temperature / humidity sensor */
#define RLCD42_ES8311_ADDR    0x18 /* speaker codec */
#define RLCD42_ES7210_ADDR    0x40 /* microphone ADC */
#define RLCD42_PCF85063_ADDR  0x51 /* RTC */
