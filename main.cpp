#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "./squarewave.pio.h"

extern "C"{ 
#include "ssd1306.h"
}

#include "CCJEncoder.h"

#define FR_PIN      8
#define ALARM_NUM   0
#define ALARM_IRQ   TIMER_IRQ_0
#define PIN_START   0
#define DELAYUS     10

#define ROTARY_A 2
#define ROTARY_B 3
#define BUTTON   6

#define I2C_SDA_PIN 4 // SDA
#define I2C_SCL_PIN 5 // SCL 

#define SHORT_PRESS_THRESHOLD 300000

/*

scilab:

round(sin((1:256)/256*6.28)*127+128)'

*/

void display_val(int val);

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

uint32_t rf_freq = (uint32_t) 6250000;
uint32_t mod_freq = (uint16_t) 2000;

//static volatile uint16_t fcw = 328; // 500Hz

static volatile uint16_t fcw = mod_freq * 65536/100000; // 2kHz

static volatile uint16_t accumulator = 0x0;

ssd1306_t disp;

CCJISREncoder<1> encoder1;

static void alarm_irq(void) {
    unsigned char amplitude;
    accumulator = accumulator + fcw ;

    uint16_t idx = accumulator >> 8 ;
    
    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
    amplitude = sintbl[idx] >> 2;
    pwm_set_gpio_level(128 + amplitude);
    pwm_set_gpio_level(PIN_START+1,128 - amplitude);

    timer_hw->alarm[ALARM_NUM]= timer_hw->timerawl + DELAYUS;
}

static void alarm_in_us() {
    // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
    // Set irq handler for alarm irq
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
    // Enable the alarm irq
    irq_set_enabled(ALARM_IRQ, true);
    // Enable interrupt in block and at processor

    // Alarm is only 32 bits so if trying to delay more
    // than that need to be careful and keep track of the upper
    // bits
    timer_hw->alarm[ALARM_NUM]= timer_hw->timerawl + DELAYUS;
}


void init_pwm() {
  gpio_set_function(PIN_START, GPIO_FUNC_PWM);
  gpio_set_drive_strength(PIN_START, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_function(PIN_START+1, GPIO_FUNC_PWM);
  gpio_set_drive_strength(PIN_START+1, GPIO_DRIVE_STRENGTH_12MA);  

  const uint16_t pwm_max = 255; // 8 bit pwm
  const int magnitude_pwm_slice = pwm_gpio_to_slice_num(PIN_START); // GPIO1
  pwm_config config = pwm_get_default_config();
  pwm_config_set_clkdiv(&config, 2.f); // 125MHz
  pwm_config_set_wrap(&config, pwm_max);
  pwm_config_set_output_polarity(&config, false, false);
  pwm_init(magnitude_pwm_slice, &config, true);
}

void display_init() {
  i2c_init(i2c0, 400000);
  gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA_PIN);
  gpio_pull_up(I2C_SCL_PIN);

  disp.external_vcc=false;
  ssd1306_init(&disp, 128, 32, 0x3C, i2c0);
  sleep_ms(1);
  ssd1306_clear(&disp);
}

void display_val(int val) {
  char frequency_string[15];
  ssd1306_clear(&disp);
  sprintf(frequency_string, "%d", val);
  ssd1306_draw_string(&disp, 0, 0, 1, frequency_string);
  ssd1306_show(&disp);
}

char *backMenuItem = (char*) "<---";
char *mainMenuItems[] = { (char*)"Generator type", (char*)"RF Freq", (char*)"Audio Freq"};

void hanle_mainMenu_generator_type() {
  ssd1306_clear(&disp);
  ssd1306_draw_string(&disp, 0, 0, 1, "Only AM transmitter");
  ssd1306_show(&disp);
  while(gpio_get(BUTTON));

  // Wait for button release
  while(!gpio_get(BUTTON));
}


void ui_freq_change(uint32_t *freq, char* str, int maxpos, int pos, uint32_t maxvalue) {
  encoder1.setValue(0);
  while(1) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, str);
    char frequency_string[15];
    sprintf(frequency_string, "%8ld", *freq);
    ssd1306_draw_string(&disp, 0, 10, 1, frequency_string);
    uint32_t delta_freq = 1;
    int i;
    for (i = 0; i< (pos-1); i++) {
      delta_freq *= 10;
    }
    for (i = 0; i < (8-pos); i++) {
      frequency_string[i] =  ' '; 
    }
    frequency_string[i] = '^';
    frequency_string[i+1] = 0x0;
    ssd1306_draw_string(&disp, 0, 20, 1, frequency_string);
    ssd1306_show(&disp);
    
    while( gpio_get(BUTTON) && !encoder1.check()) { 
      // while the button is not pressed and encoder does not changed do nothing
    }
    if (!gpio_get(BUTTON)) {
      uint64_t press_start = time_us_64();

      // Wait for button release
      while(!gpio_get(BUTTON));

      uint64_t press_duration = time_us_64() - press_start;
      
      if (press_duration < SHORT_PRESS_THRESHOLD) {
        pos +=1;
        if(pos>maxpos) {
          pos = 1;
        }
      } else {
        // long press exit
        return;
      }
    }
    *freq+=(encoder1.value()*delta_freq);
    if( (*freq) > maxvalue) {
      *freq = maxvalue;
    }
    encoder1.setValue(0);
  }
}

void hanle_mainMenu_rf_freq() {
  ui_freq_change(&rf_freq, (char*)"RF Frequency:", 7, 4, 30000000);
}

void hanle_mainMenu_audio_freq() {
  ui_freq_change(&mod_freq, (char*)"Mod Frequency:", 4, 2, 5000);
}
void (*mainMenuActions[])() = { hanle_mainMenu_generator_type, hanle_mainMenu_rf_freq, hanle_mainMenu_audio_freq};

void updateMenu(int idx, char *menuStr) {
  char frequency_string[15];
  ssd1306_clear(&disp);
  ssd1306_draw_string(&disp, 0, 0, 1, menuStr);
  sprintf(frequency_string, "%d", idx);
  ssd1306_draw_string(&disp, 8, 24, 1, frequency_string);
  ssd1306_show(&disp);
}

void menu_handler(char *menuItems[], int menuSize, void (*menuActions[])()) {
  encoder1.setValue(0);
  bool isExit = false;
  int idx = 0;
  menuSize++; // back option should be always
  updateMenu(0, menuItems[idx]);
  while (1) {
    if(encoder1.check()) {
      idx+= encoder1.value();
      encoder1.setValue(0);
      if (idx<0) idx += menuSize;
      idx = idx % menuSize;
      updateMenu(idx, (idx == (menuSize-1)) ? backMenuItem : menuItems[idx]);
    }
    if(!gpio_get(BUTTON)) {
      // Wait for button release
      while(!gpio_get(BUTTON));

      if (idx == (menuSize-1)) {
        return;
      }
    
      menuActions[idx]();
      updateMenu(idx, (idx == (menuSize-1)) ? backMenuItem : menuItems[idx]);
    }
  }
}

int main() {
    stdio_init_all();
    init_pwm();
    display_init();
    ssd1306_draw_string(&disp, 8, 24, 1, "HELLO123");
    ssd1306_show(&disp);
    alarm_in_us();
    encoder1.init(ROTARY_A, ROTARY_B);
    gpio_init(BUTTON);
    gpio_set_dir(BUTTON, GPIO_IN);
	  gpio_pull_up(BUTTON);
    uint offset = pio_add_program(pio0, &squarewave_program);
    init_squarewave(pio0, 0, offset, FR_PIN, rf_freq << 1);
    sleep_ms(2000);
    while(1) {
      menu_handler(mainMenuItems, 3, mainMenuActions);
      fcw = mod_freq * 65536/100000; 
      init_squarewave(pio0, 0, offset, FR_PIN, rf_freq << 1);
      char dstr[55];
      ssd1306_clear(&disp);
      sprintf(dstr, "RF: %8ld", rf_freq);
      ssd1306_draw_string(&disp, 0, 0, 1, dstr);

      sprintf(dstr, "Mod: %4ld", mod_freq);
      ssd1306_draw_string(&disp, 0, 10, 1, dstr);

      ssd1306_show(&disp);
      while(gpio_get(BUTTON));
    }
}