#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// INMP441 microphone + MAX98357 amplifier use separate I2S peripherals.
#define AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

// On-board WS2812 status strip (7 LEDs). Verified against the vendor firmware,
// which initializes it as "WS2812 RGB LED (GPIO38) ... 7 LEDs".
#define BUILTIN_LED_GPIO GPIO_NUM_38
#define BUILTIN_LED_COUNT 7

#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

// The three-button panel is optional on the desktop version.
#define TOUCH_BUTTON_GPIO GPIO_NUM_NC

#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#define DISPLAY_WIDTH 128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT 32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT 64
#else
#error "OLED display type is not selected"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// Vendor lamp output (GPIO18). This hardware revision has no separate lamp:
// the 7-LED WS2812 strip is the only light. GPIO18 is still driven for
// compatibility with the vendor firmware.
#define LAMP_GPIO GPIO_NUM_18

// L298N dual H-bridge. Verified against the vendor firmware's InitMotorPwm,
// which configures LEDC channels 0-3 on GPIO 12, 13, 14 and 21 (IN1-IN4).
// The vendor's forward command drives channels 0+2 (IN1+IN3), backward drives
// 1+3 (IN2+IN4), which matches the channel mapping in compact_wifi_board.cc.
#define MOTOR_IN1_GPIO GPIO_NUM_12
#define MOTOR_IN2_GPIO GPIO_NUM_13
#define MOTOR_IN3_GPIO GPIO_NUM_14
#define MOTOR_IN4_GPIO GPIO_NUM_21

// The N20 motors do not overcome static friction below roughly 60 % duty on
// this 5 V/L298N rail. Map every non-zero speed command into
// [MOTOR_MIN_DRIVE_PERCENT, 100] so low speeds still move the car.
#define MOTOR_MIN_DRIVE_PERCENT 60

#endif  // _BOARD_CONFIG_H_
