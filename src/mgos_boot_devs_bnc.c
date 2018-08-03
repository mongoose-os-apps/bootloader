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

#include "mgos_gpio.h"
#include "mgos_vfs_dev_encr.h"
#include "mgos_vfs_dev_part.h"
#include "mgos_vfs_dev_w25xxx.h"

#include "mgos_boot_cfg.h"
#include "mgos_boot_dbg.h"

#include "stm32_vfs_dev_flash.h"

#define STM32_BOOT_APP_DEV_0 "app0"
#define STM32_BOOT_APP_DEV_1 "app1"
#define STM32_BOOT_APP_DEV_2 "app2"
#define STM32_BOOT_FS_DEV_0 "fs0"
#define STM32_BOOT_FS_DEV_1 "fs1"
#define STM32_BOOT_FS_DEV_2 "fs2"

extern bool mgos_vfs_dev_encr_init(void);
extern bool mgos_vfs_dev_spi_flash_init(void);
extern bool mgos_vfs_dev_w25xxx_init(void);

static struct mgos_spi *spi_dev;
static struct mgos_vfs_dev *w25_dev, *w25e_dev, *app0_dev;
static struct mgos_vfs_dev *app0_dev, *app1_dev, *app2_dev;
static struct mgos_vfs_dev *bcfg0_dev, *bcfg1_dev;

bool mgos_boot_devs_init(void) {
  bool res = false;

  stm32_vfs_dev_flash_register_type();
  mgos_vfs_dev_w25xxx_init();
  mgos_vfs_dev_encr_init();
  mgos_vfs_dev_part_init();

  struct mgos_config_spi spi_cfg = {
      .enable = true,
      .unit_no = 1,
      .miso_gpio = 0x50006,
      .mosi_gpio = 0x50007,
      .sclk_gpio = 0x50005,
      .cs0_gpio = 0x4,
      .cs1_gpio = -1,
      .cs2_gpio = -1,
  };
  spi_dev = mgos_spi_create(&spi_cfg);
  if (spi_dev == NULL) {
    mgos_boot_dbg_putl("SPI!");
    goto out;
  }
  w25_dev = mgos_vfs_dev_create(MGOS_VFS_DEV_TYPE_W25XXX, NULL);
  if (w25xxx_dev_init(w25_dev, spi_dev, 0, 48000000, 0, 24, true) != 0) {
    mgos_boot_dbg_putl("w25xxx");
    goto out;
  }
  w25e_dev = mgos_vfs_dev_create(MGOS_VFS_DEV_TYPE_ENCR, NULL);
  if (encr_dev_init(w25e_dev, w25_dev, (uint8_t *) FLASH_OTP_BASE, NULL, 16,
                    true) != 0) {
    goto out;
  }

  uint32_t app_size = (STM32_FLASH_SIZE - APP0_OFFSET);
  app0_dev = mgos_vfs_dev_create(MGOS_VFS_DEV_TYPE_STM32_FLASH, NULL);
  if (stm32_flash_dev_init(app0_dev, APP0_OFFSET, app_size, false /* ese */) !=
          0 ||
      !mgos_vfs_dev_register(app0_dev, STM32_BOOT_APP_DEV_0)) {
    mgos_boot_dbg_putl("app0");
    goto out;
  }

  app1_dev = mgos_vfs_dev_create(MGOS_VFS_DEV_TYPE_PART, NULL);
  if (part_dev_init(app1_dev, w25e_dev, 12582912, 1048576) != 0 ||
      !mgos_vfs_dev_register(app1_dev, STM32_BOOT_APP_DEV_1)) {
    mgos_boot_dbg_putl("app1");
    goto out;
  }

  app2_dev = mgos_vfs_dev_create(MGOS_VFS_DEV_TYPE_PART, NULL);
  if (part_dev_init(app2_dev, w25e_dev, 13631488, 1048576) != 0 ||
      !mgos_vfs_dev_register(app2_dev, STM32_BOOT_APP_DEV_2)) {
    mgos_boot_dbg_putl("app2");
    goto out;
  }

  bcfg0_dev = mgos_vfs_dev_create(MGOS_VFS_DEV_TYPE_STM32_FLASH, NULL);
  if (stm32_flash_dev_init(bcfg0_dev, 32768, 16384,
                           false /* emulate_small_erase */) != 0 ||
      !mgos_vfs_dev_register(bcfg0_dev, MGOS_BOOT_CFG_DEV_0)) {
    mgos_boot_dbg_putl("bcfg0");
    goto out;
  }
  bcfg1_dev = mgos_vfs_dev_create(MGOS_VFS_DEV_TYPE_STM32_FLASH, NULL);
  if (stm32_flash_dev_init(bcfg1_dev, 49152, 16384,
                           false /* emulate_small_erase */) != 0 ||
      !mgos_vfs_dev_register(bcfg1_dev, MGOS_BOOT_CFG_DEV_1)) {
    mgos_boot_dbg_putl("bcfg1");
    goto out;
  }
  res = true;
out:
  return res;
}

void mgos_boot_devs_deinit(void) {
  mgos_vfs_dev_unregister(MGOS_BOOT_CFG_DEV_1);
  mgos_vfs_dev_unregister(MGOS_BOOT_CFG_DEV_0);
  mgos_vfs_dev_unregister(STM32_BOOT_APP_DEV_2);
  mgos_vfs_dev_unregister(STM32_BOOT_APP_DEV_1);
  mgos_vfs_dev_unregister(STM32_BOOT_APP_DEV_0);
  mgos_vfs_dev_close(bcfg1_dev);
  mgos_vfs_dev_close(bcfg0_dev);
  mgos_vfs_dev_close(app2_dev);
  mgos_vfs_dev_close(app1_dev);
  mgos_vfs_dev_close(app0_dev);
  mgos_vfs_dev_close(w25e_dev);
  if (w25_dev->refs != 1) {
    mgos_boot_dbg_printf("REFS %d!\r\n", w25_dev->refs);
  }
  mgos_vfs_dev_close(w25_dev);
  mgos_spi_close(spi_dev);
}

void mgos_boot_cfg_set_default_slots(struct mgos_boot_cfg *cfg) {
  struct mgos_boot_slot_cfg *sc;
  struct mgos_boot_slot_state *ss;
  /* Create app0 in a committed state. */
  cfg->num_slots = 3;
  /* Slot 0 */
  sc = &cfg->slots[0].cfg;
  ss = &cfg->slots[0].state;
  strcpy(sc->app_dev, STM32_BOOT_APP_DEV_0);
  strcpy(sc->fs_dev, STM32_BOOT_FS_DEV_0);
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
  sc->app_map_addr = FLASH_BASE + APP0_OFFSET;
  ss->app_org = sc->app_map_addr; /* Directly bootable */
  /* Note: we don't know the actual length of the FW. */
  ss->app_len = mgos_vfs_dev_get_size(app0_dev);
  /* Slot 1 - on the ext flash, not mappable. */
  sc = &cfg->slots[1].cfg;
  ss = &cfg->slots[1].state;
  strcpy(sc->app_dev, STM32_BOOT_APP_DEV_1);
  strcpy(sc->fs_dev, STM32_BOOT_FS_DEV_1);
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
  /* Slot 2 - on the ext flash, not mappable. */
  sc = &cfg->slots[2].cfg;
  ss = &cfg->slots[2].state;
  strcpy(sc->app_dev, STM32_BOOT_APP_DEV_2);
  strcpy(sc->fs_dev, STM32_BOOT_FS_DEV_2);
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
/* XXXX */
#if 0
  cfg->active_slot = 1;
  cfg->slots[1].cfg.flags = MGOS_BOOT_SLOT_F_VALID; // r/o
  cfg->slots[1].state.app_org = FLASH_BASE_ADDR + APP_OFFSET;
  cfg->slots[1].state.app_len = mgos_vfs_dev_get_size(app0_dev);
#endif
}
