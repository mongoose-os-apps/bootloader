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

/* Interface to platform-specific bits the loader. */

#pragma once

#include "mgos_boot_cfg.h"
#include "mgos_boot_dbg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platform code should initialize .bss, .data and heap and
 * invoke mgos_boot_main. Do not touch any peripherals.
 */
void mgos_boot_main(void);

/*
 * mgos_boot_pre_init is a very early init hook, before next_app_org.
 */
void mgos_boot_early_init(void);

/*
 * mgos_boot_init should perform basic CPU init, set decent clock speed.
 */
void mgos_boot_init(void);

/*
 * mgos_boot_init should perform sanity check on app we are about to boot
 * and print basic info about it.
 */
bool mgos_boot_print_app_info(uintptr_t app_org);

/*
 * mgos_boot_system_restart should restart the system.
 */
void mgos_boot_system_restart(void) __attribute__((noreturn));

/*
 * mgos_boot_app should perform platform-specific actions to boot
 * the specified app.
 */
void mgos_boot_app(uintptr_t app_org) __attribute__((noreturn));

/*
 * mgos_boot_devs_init should initialize the storage device drivers.
 */
bool mgos_boot_devs_init(void);

/*
 * mgos_boot_cfg_set_default_slots should fill config with reasonable defaults.
 */
void mgos_boot_cfg_set_default_slots(struct mgos_boot_cfg *cfg);

/*
 * mgos_boot_dbg_setup should set up debugging output,
 * such that mgos_boot_dbg_putc works.
 */
bool mgos_boot_dbg_setup(void);

/*
 * mgos_boot_dbg_putc should output one character of debugging output,
 * typically to UART. Note: make sure character is really sent, i.e.
 * flush buffers and FIFO (if any) before returning.
 */
void mgos_boot_dbg_putc(char c);

#ifdef __cplusplus
}
#endif
