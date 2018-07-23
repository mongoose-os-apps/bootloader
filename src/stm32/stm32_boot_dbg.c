/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mgos_gpio.h"

#include "stm32_uart_internal.h"

bool mgos_boot_dbg_setup(void) {
  struct mgos_uart_config cfg;
  mgos_uart_config_set_defaults(MGOS_DEBUG_UART, &cfg);
  cfg.baud_rate = MGOS_DEBUG_UART_BAUD_RATE;
  cfg.dev.pins.tx = STM32_PIN_DEF('C', 6, 8);
  cfg.dev.pins.rx = STM32_PIN_DEF('C', 7, 8);
  return mgos_uart_configure(MGOS_DEBUG_UART, &cfg);
}

void mgos_boot_dbg_putc(char c) {
  stm32_uart_putc(MGOS_DEBUG_UART, c);
}
