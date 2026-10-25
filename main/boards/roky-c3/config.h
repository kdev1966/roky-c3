#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Brochage releve dans le firmware d'origine de la carte WHA103 (xiaozhi 1.8.8).
// Identique a la Xmini-C3, sauf l'ampli (actif a l'etat bas) et GPIO2 (batterie).

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_10
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_6
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_8
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_7
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_5

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_11
#define AUDIO_CODEC_PA_INVERTED  true
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_3
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_4
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

#define BOOT_BUTTON_GPIO        GPIO_NUM_9

// Tension batterie via pont diviseur sur GPIO2 = ADC1 canal 2
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_2

#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
// Ecran tourne de 180 degres par rapport au firmware d'origine : le
// connecteur USB-C se retrouve en haut, comme dans le boitier prevu.
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false

#endif // _BOARD_CONFIG_H_
