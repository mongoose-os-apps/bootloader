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

uint32_t mgos_boot_checksum(struct mgos_vfs_dev *src, size_t len);

void mgos_boot_cfg_set_default_slots(struct mgos_boot_cfg *cfg) {
  struct mgos_boot_slot_cfg *sc;
  struct mgos_boot_slot_state *ss;
  struct mgos_vfs_dev *app0_dev = mgos_vfs_dev_open("app0");
  /* Create app0 in a committed state. */
  cfg->num_slots = 4;
  /* Slot 0 */
  sc = &cfg->slots[0].cfg;
  ss = &cfg->slots[0].state;
  strcpy(sc->app_dev, "app0");
  strcpy(sc->fs_dev, "fs0");
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
  sc->app_map_addr = FLASH_BASE + MGOS_BOOT_APP0_OFFSET;
  ss->app_org = sc->app_map_addr; /* Directly bootable */
  /* Note: we don't know the actual length of the FW. */
  ss->app_len = mgos_vfs_dev_get_size(app0_dev);
  ss->app_crc32 = mgos_boot_checksum(app0_dev, ss->app_len);
  /* Slot 1 - on SPI flash, not mappable. */
  sc = &cfg->slots[1].cfg;
  ss = &cfg->slots[1].state;
  strcpy(sc->app_dev, "app1");
  strcpy(sc->fs_dev, "fs1");
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
  /* Slot 2 - on SPI flash, not mappable, no fs.
   * Only serves as a temp slot for swaps. */
  sc = &cfg->slots[2].cfg;
  ss = &cfg->slots[2].state;
  strcpy(sc->app_dev, "appT");
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
  /* Slot 3 - factory reset slot on SPI flash. Not writeable. */
  sc = &cfg->slots[3].cfg;
  ss = &cfg->slots[3].state;
  strcpy(sc->app_dev, "appF");
  strcpy(sc->fs_dev, "fsF");
  sc->flags = MGOS_BOOT_SLOT_F_VALID;
#if 0
  /* XXX - testing */
  cfg->flags = MGOS_BOOT_F_FIRST_BOOT_A | MGOS_BOOT_F_FIRST_BOOT_B | MGOS_BOOT_F_MERGE_FS;
  cfg->slots[1].state.app_len = 524288;
  cfg->slots[1].state.app_org = 0x8010000;
  struct mgos_vfs_dev *app1 = mgos_vfs_dev_open(cfg->slots[1].cfg.app_dev);
  cfg->slots[1].state.app_crc32 = mgos_boot_checksum(app1, cfg->slots[1].state.app_len);
  mgos_vfs_dev_close(app1);
  cfg->active_slot = 1;
  cfg->revert_slot = 0;
#endif
}
