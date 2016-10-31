/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Scott Shawcroft
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Machine is the HAL for low-level, hardware accelerated functions. It is not
// meant to simplify APIs, its only meant to unify them so that other modules
// do not require port specific logic.
//
// This file defines core data structures for machine classes. They are port
// specific and passed through the Python layer untouched.

#ifndef __MICROPY_INCLUDED_ATMEL_SAMD_API_MACHINE_TYPES_H__
#define __MICROPY_INCLUDED_ATMEL_SAMD_API_MACHINE_TYPES_H__

// Don't reorder these includes because they are dependencies of adc_feature.h.
// They should really be included by adc_feature.h.
#include "compiler.h"
#include "asf/sam0/drivers/system/clock/gclk.h"
#include "asf/sam0/utils/cmsis/samd21/include/component/adc.h"
#include "asf/sam0/drivers/adc/adc_sam_d_r/adc_feature.h"

#include "asf/sam0/drivers/sercom/i2c/i2c_master.h"
#include "asf/sam0/drivers/sercom/spi/spi.h"
#include "asf/sam0/drivers/sercom/usart/usart.h"

#include "py/obj.h"

typedef struct {
  Sercom *const sercom;
  uint8_t pad;
  uint32_t pinmux;
} pin_sercom_t;

typedef struct {
  Tc *const tc;
  Tcc *const tcc;
  uint8_t channel;
  uint8_t wave_output;
  uint32_t pin;
  uint32_t mux;
} pin_timer_t;

typedef struct {
  mp_obj_base_t base;
  qstr name;
  uint32_t pin;
  bool has_adc;
  enum adc_positive_input adc_input;
  pin_timer_t primary_timer;
  pin_timer_t secondary_timer;
  pin_sercom_t primary_sercom;
  pin_sercom_t secondary_sercom;
} pin_obj_t;

typedef struct _machine_i2c_obj_t {
  mp_obj_base_t base;
  struct i2c_master_module i2c_master_instance;
} machine_i2c_obj_t;

typedef struct _machine_spi_obj_t {
  mp_obj_base_t base;
  struct spi_module spi_master_instance;
} machine_spi_obj_t;

typedef struct _machine_uart_obj_t {
  mp_obj_base_t base;
  struct usart_module uart_instance;
} machine_uart_obj_t;

#endif // __MICROPY_INCLUDED_ATMEL_SAMD_API_MACHINE_TYPES_H__
