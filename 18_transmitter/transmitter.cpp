//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2023
// filename: transmitter.cpp
// description: Ham Transmitter for Pi Pico
// License: MIT
//

#include "pico/stdlib.h"
#include <cmath>
#include <stdio.h>
#include <ctype.h>

#include "adc.h"
#include "modulator.h"
#include "nco.h"
#include "psu_mode.h"
#include "pwm.h"
#include "signal_generator.h"

void transmitter_start(tx_mode_t mode, double frequency_Hz,
                       const bool enable_serial_data = false) {
  const uint8_t mic_pin = 28;
  const uint8_t magnitude_pin = 6;
  const uint8_t rf_pin = 8;

  // Use ADC to capture MIC input
  adc mic_adc(mic_pin, 2);

  // Use PWM to output magnitude
  pwm magnitude_pwm(magnitude_pin);

  // Use PIO to output phase/frequency controlled oscillator
  nco rf_nco(rf_pin, frequency_Hz);
  const double sample_frequency_Hz = (mode == AM)    ? 12e3
                                     : (mode == FM)  ? 15e3
                                     : (mode == LSB) ? 10e3
                                                     : 10e3;
  const uint8_t waveforms_per_sample =
      rf_nco.get_waveforms_per_sample(sample_frequency_Hz);

  // create modulator
  modulator audio_modulator;

  // scale FM deviation
  const double fm_deviation_Hz = 2.5e3; // 2.5kHz
  const uint32_t fm_deviation_f15 =
      round(2 * 32768.0 * fm_deviation_Hz /
            rf_nco.get_sample_frequency_Hz(waveforms_per_sample));

  int32_t audio;
  uint16_t magnitude;
  int16_t phase;
  int16_t i; // not used in this design
  int16_t q; // not used in this design

  // use gpio for debug
  uint8_t debug_pin = 1;
  gpio_init(debug_pin);
  gpio_set_dir(debug_pin, GPIO_OUT);
  uint8_t debug_pin_2 = 2;
  gpio_init(debug_pin_2);
  gpio_set_dir(debug_pin_2, GPIO_OUT);

  while (1) {
    // get a sample to transmit
    if (enable_serial_data) {

      // timeout ends transmission
      audio = getchar_timeout_us(1000);
      if (audio == PICO_ERROR_TIMEOUT)
        return;

      // read audio from serial port
      audio <<= 8;

    } else {

      // read audio from mic
      audio = mic_adc.get_sample() * 96; // multiply by a gain value
    }

    // demodulate
    gpio_put(debug_pin, 1);
    audio_modulator.process_sample(mode, audio, i, q, magnitude, phase,
                                   fm_deviation_f15);
    gpio_put(debug_pin, 0);

    // output magnitude
    magnitude_pwm.output_sample(magnitude);

    // output phase
    rf_nco.output_sample(phase, waveforms_per_sample);
  }
}

// example application
int main() {
  stdio_init_all();
  disable_power_save();

  // chose default values
  uint32_t frequency = 14.175e6;
  tx_mode_t mode = USB;

  printf("Pi Pico Transmitter>\r\n");
  while (1) {
    int command = getchar_timeout_us(0);
    char frequency_string[100];
    if (command != PICO_ERROR_TIMEOUT) {
      printf("command: %c\r\n", command);
      switch (command) {
      // set frequency
      case 'f':
        frequency = 0;
        while(1)
        {
          char c = getchar();
          if(!isdigit(c)) break;
          frequency *= 10;
          frequency += (c - '0');
        }
        printf("terminate: %c\r\n");
        printf("frequency: %u Hz\r\n", frequency);
        break;

      // set mode
      case 'm':
        while (1) {
          char command = getchar();
          if (command == 'a') {
            printf("MODE=AM\r\n");
            mode = AM;
            break;
          } else if (command == 'f') {
            printf("MODE=FM\r\n");
            mode = FM;
            break;
          } else if (command == 'l') {
            printf("MODE=LSB\r\n");
            mode = LSB;
            break;
          } else if (command == 'u') {
            printf("MODE=USB\r\n");
            mode = USB;
            break;
          }
        }
        break;

      // transmit serial data
      case 's':
        printf("Starting transmitter\r\n");
        stdio_set_translate_crlf(&stdio_usb, false);
        transmitter_start(mode, frequency, true);
        stdio_set_translate_crlf(&stdio_usb, true);
        printf("Transmitter timed out\r\n");
        break;

      // help
      case '?':
        printf("\r\nSerial Interface Help\r\n");
        printf("=====================\r\n\r\n");
        printf("fxxxxxx, set frequency Hz\r\n");
        printf("mx, set mode, a=AM, f=FM, l=LSB, u=USB\r\n");
        printf("s, transmit serial data - (timeout terminates)\r\n");
        printf("?, Help (this message)\r\n");
        break;
      }
      printf("Pi Pico Transmitter>\r\n");
    }

    sleep_us(1000);
  }
}
