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

#pragma once

#include "mgos_boot_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platform code should initialize .bss, .data and heap and
 * invoke mgos_boot_main. Do not touch any peripherals.
 */
void mgos_boot_main(void);
/*
 * mgos_boot_init should perform basic CPU init, set decent clock speed.
 */
void mgos_boot_init(void);

bool mgos_boot_print_app_info(uintptr_t app_org);

/*
 * mgos_boot_app should perform platform-specific actions to boot
 * the specified app.
 */
void mgos_boot_app(uintptr_t app_org) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
