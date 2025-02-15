#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "./squarewave.pio.h"

#define FR_PIN           8
#define ALARM_NUM 0
#define PIN_START 6
#define TIMER_INTERVAL_US 10


/*
scilab:
round(sin((1:256)/256*6.28)*127+128)'
*/

static unsigned char sintbl[] = { 
  131, 134, 137, 140, 144, 147, 150, 153, 156, 159, 162, 165, 168, 171, 174, 177, 179, 
  182, 185, 188, 191, 193, 196, 199, 201, 204, 206, 209, 211, 213, 216, 218, 220, 222, 
  224, 226, 228, 230, 232, 234, 235, 237, 238, 240, 241, 243, 244, 245, 246, 248, 249, 
  250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255, 255, 255, 255, 255, 254, 
  254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 247, 245, 244, 243, 241, 240, 239, 
  237, 235, 234, 232, 230, 228, 226, 224, 222, 220, 218, 216, 213, 211, 209, 206, 204, 
  201, 199, 196, 193, 191, 188, 185, 182, 180, 177, 174, 171, 168, 165, 162, 159, 156, 
  153, 150, 147, 144, 141, 138, 134, 131, 128, 125, 122, 119, 116, 113, 110, 106, 103, 
  100,  97,  94,  91,  88,  85,  83,  80,  77,  74,  71,  68,  66,  63,  60,  58,  55,
   53,  50,  48,  45,  43,  41,  38,  36,  34,  32,  30,  28,  26,  24,  23,  21,  19,
   18,  16,  15,  13,  12,  11,  10,   9,   7,   7,   6,   5,   4,   3,   3,   2,   2,
    2,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   3,   3,   4,   5,   6,   6,
    7,   8,   9,  11,  12,  13,  14,  16,  17,  19,  21,  22,  24,  26,  28,  30,  32,
   34,  36,  38,  40,  42,  45,  47,  50,  52,  55,  57,  60,  62,  65,  68,  71,  73,
   76,  79,  82,  85,  88,  91,  94,  97, 100, 103, 106, 109, 112, 115, 118, 121, 124, 128 };

static volatile int idx = 0;

/*

https://en.wikipedia.org/wiki/Numerically_controlled_oscillator

Fclk = 1 / DELAYUS     # 100kHz
freq = Fclk / (2^N / fcw)    # 100000 / (65536/328) -> 500 Hz

fcw = f*2^N / Fclk    # 500*65536/100000 -> 327.68

fcw = f*2^N / Fclk    # 8200*65536/100000 -> 5373.952

*/

static volatile uint16_t fcw = 328;

static volatile uint16_t accumulator = 0x0;


bool timer_callback(struct repeating_timer *t) {
    unsigned char amplitude;
    accumulator = accumulator + fcw ;

    uint16_t idx = accumulator >> 8 ;
  
    amplitude = sintbl[idx] >> 2;
    //amplitude = accumulator >> 3;
    pwm_set_gpio_level(PIN_START,128 + amplitude);
    pwm_set_gpio_level(PIN_START+1,128 - amplitude);

    return true;
}

void main() {
  stdio_init_all();

  gpio_set_function(PIN_START, GPIO_FUNC_PWM);
  gpio_set_drive_strength(PIN_START, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_function(PIN_START+1, GPIO_FUNC_PWM);
  gpio_set_drive_strength(PIN_START+1, GPIO_DRIVE_STRENGTH_12MA);  

  const uint16_t pwm_max = 255; // 8 bit pwm
  const int magnitude_pwm_slice = pwm_gpio_to_slice_num(PIN_START);
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, 2.f); // 125MHz
  pwm_config_set_wrap(&config, pwm_max);
  pwm_config_set_output_polarity(&config, false, false);
  pwm_init(magnitude_pwm_slice, &config, true);
  pwm_set_gpio_level(PIN_START,55);
  pwm_set_gpio_level(PIN_START+1,200);

  struct repeating_timer timer;
  add_repeating_timer_us(-TIMER_INTERVAL_US, timer_callback, NULL, &timer);
  while(1) {

  }
}
