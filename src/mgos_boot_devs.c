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

#include "mgos_boot_devs.h"

#include <stdbool.h>
#include <stdlib.h>

#include "common/str_util.h"

#include "mgos_gpio.h"
#include "mgos_spi.h"
#include "mgos_vfs_dev_encr.h"
#include "mgos_vfs_dev_part.h"
#include "mgos_vfs_dev_ram.h"
#include "mgos_vfs_dev_spi_flash.h"

#include "mgos_boot_cfg.h"
#include "mgos_boot_dbg.h"

#include "stm32_vfs_dev_flash.h"

#define STM32_BOOT_APP_DEV_0 "app0"
#define STM32_BOOT_APP_DEV_1 "app1"
#define STM32_BOOT_APP_DEV_2 "app2"
#define STM32_BOOT_FS_DEV_0 "fs0"
#define STM32_BOOT_FS_DEV_1 "fs1"

extern bool mgos_vfs_dev_encr_init(void);
extern bool mgos_vfs_dev_spi_flash_init(void);
extern bool mgos_vfs_dev_ram_init(void);
extern bool mgos_root_devtab_init(void);

bool mgos_boot_devs_init(void) {
  bool res = false;

  stm32_vfs_dev_flash_register_type();
  mgos_vfs_dev_encr_init();
  mgos_vfs_dev_part_init();
  mgos_vfs_dev_ram_init();
  mgos_vfs_dev_spi_flash_init();

  res = mgos_root_devtab_init();

  return res;
}

void mgos_boot_devs_deinit(void) {
  mgos_vfs_dev_unregister_all();
}

void mgos_boot_cfg_set_default_slots(struct mgos_boot_cfg *cfg) {
  struct mgos_boot_slot_cfg *sc;
  struct mgos_boot_slot_state *ss;
  struct mgos_vfs_dev *app0_dev = mgos_vfs_dev_open(STM32_BOOT_APP_DEV_0);
  /* Create app0 in a committed state. */
  cfg->num_slots = 3;
  /* Slot 0 */
  sc = &cfg->slots[0].cfg;
  ss = &cfg->slots[0].state;
  strcpy(sc->app_dev, STM32_BOOT_APP_DEV_0);
  strcpy(sc->fs_dev, STM32_BOOT_FS_DEV_0);
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
  sc->app_map_addr = FLASH_BASE + MGOS_BOOT_APP0_OFFSET;
  ss->app_org = sc->app_map_addr; /* Directly bootable */
  /* Note: we don't know the actual length of the FW. */
  ss->app_len = mgos_vfs_dev_get_size(app0_dev);
  /* Slot 1 - on the ext flash, not mappable. */
  sc = &cfg->slots[1].cfg;
  ss = &cfg->slots[1].state;
  strcpy(sc->app_dev, STM32_BOOT_APP_DEV_1);
  strcpy(sc->fs_dev, STM32_BOOT_FS_DEV_1);
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
  /* Slot 2 - on the ext flash, not mappable, no fs.
   * Only serves as a temp slot for swaps. */
  sc = &cfg->slots[2].cfg;
  ss = &cfg->slots[2].state;
  strcpy(sc->app_dev, STM32_BOOT_APP_DEV_2);
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
}
