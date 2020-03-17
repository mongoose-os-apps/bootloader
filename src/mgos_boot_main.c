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

#include <stdbool.h>
#include <stdlib.h>

#include "common/cs_crc32.h"
#include "common/str_util.h"

#include "mgos_hal.h"
#include "mgos_uart.h"
#include "mgos_utils.h"
#include "mgos_vfs_dev.h"

#include "mgos_boot_cfg.h"
#include "mgos_boot_dbg.h"
#include "mgos_boot_hal.h"

/* This size is chosen to suit AES alignment and size and also
 * to match NAND flash page size. */
#define STM32_BOOT_IO_SIZE 2048

extern const char *build_version, *build_id;

struct mgos_rlock_type *mgos_rlock_create(void) {
  return NULL;
}
void mgos_rlock(struct mgos_rlock_type *l) {
  (void) l;
}
void mgos_runlock(struct mgos_rlock_type *l)
    __attribute__((alias("mgos_rlock")));
void mgos_rlock_destroy(struct mgos_rlock_type *l)
    __attribute__((alias("mgos_rlock")));

IRAM void mgos_ints_enable(void) {
}
IRAM void mgos_ints_disable(void) {
}
void mgos_lock(void) __attribute__((alias("mgos_ints_enable")));
void mgos_unlock(void) __attribute__((alias("mgos_ints_enable")));

extern bool mgos_root_devtab_init(void);

static uint8_t io_buf[STM32_BOOT_IO_SIZE];

void mgos_usleep(uint32_t usecs) {
  (*mgos_nsleep100)(usecs * 10);
}

uint32_t mgos_boot_checksum(struct mgos_vfs_dev *src, size_t len) {
  bool res = false;
  size_t l = 0;
  uint32_t offset = 0, crc32 = 0;
  mgos_boot_dbg_printf("Checksum %s (%lu): ", src->name, (unsigned long) len);
  while (l < len) {
    size_t io_len = sizeof(io_buf);
    size_t data_len = MIN(len - l, io_len);
    /* Always read in fixed size chunks. */
    enum mgos_vfs_dev_err r = mgos_vfs_dev_read(src, offset, io_len, io_buf);
    if (r != 0) {
      crc32 = 0;
      mgos_boot_dbg_printf("Read err %s @ %lu: %d\n", src->name,
                           (unsigned long) offset, r);
      goto out;
    }
    crc32 = cs_crc32(crc32, io_buf, data_len);
    mgos_wdt_feed();
    offset += data_len;
    l += data_len;
    if (l % 65536 == 0) mgos_boot_dbg_putc('.');
  }
  res = true;
out:
  if (res) mgos_boot_dbg_printf(" 0x%08lx\n", (unsigned long) crc32);
  return crc32;
}

bool mgos_boot_copy_dev(struct mgos_vfs_dev *src, struct mgos_vfs_dev *dst,
                        size_t len) {
  bool res = false;
  size_t l = 0;
  uint32_t offset = 0, erased_until = 0;
  mgos_boot_dbg_printf("%s --> %s (%lu): ", src->name, dst->name,
                       (unsigned long) len);
  while (l < len) {
    enum mgos_vfs_dev_err r;
    size_t io_len = sizeof(io_buf);
    size_t data_len = MIN(len - l, io_len);
    /* Always read and write in fixed size chunks. */
    r = mgos_vfs_dev_read(src, offset, io_len, io_buf);
    if (r != 0) {
      mgos_boot_dbg_printf("Read err %s @ %lu: %d\n", src->name,
                           (unsigned long) offset, r);
      goto out;
    }
    if (offset + io_len > erased_until) {
      int i = 0, j = 0;
      size_t erase_sizes[MGOS_VFS_DEV_NUM_ERASE_SIZES];
      mgos_vfs_dev_get_erase_sizes(dst, erase_sizes);
      /*
       * Erase is complicated. Devices have different erase sizes and some
       * (STM32F flash) have non-uniform layout with varying sector size.
       */
      while (i < (int) ARRAY_SIZE(erase_sizes) && erase_sizes[i] > 0 &&
             erase_sizes[i] < len) {
        j = i++;
      }
      while (j >= 0) {
        size_t erase_size = erase_sizes[j];
        if (mgos_vfs_dev_erase(dst, offset, erase_size) == 0) {
          erased_until = offset + erase_size;
          break;
        }
        j--;
      }
    }
    r = mgos_vfs_dev_write(dst, offset, io_len, io_buf);
    if (r != 0) {
      mgos_boot_dbg_printf("Write err %s @ %lu: %d\n", dst->name,
                           (unsigned long) offset, r);
      goto out;
    }
    mgos_wdt_feed();
    offset += data_len;
    l += data_len;
    if (l % 65536 == 0) mgos_boot_dbg_putc('.');
  }
  res = true;
out:
  if (res) mgos_boot_dbg_putl(" ok");
  return res;
}

bool mgos_boot_copy_app(struct mgos_boot_cfg *cfg, int src, int dst) {
  bool res = false;
  const struct mgos_boot_slot_cfg *ssc = &cfg->slots[src].cfg;
  struct mgos_boot_slot_state *sss = &cfg->slots[src].state;
  const struct mgos_boot_slot_cfg *dsc = &cfg->slots[dst].cfg;
  struct mgos_boot_slot_state *dss = &cfg->slots[dst].state;
  struct mgos_vfs_dev *src_app_dev = NULL, *dst_app_dev = NULL;
  if (sss->app_len > 0) {
    src_app_dev = mgos_vfs_dev_open(ssc->app_dev);
    dst_app_dev = mgos_vfs_dev_open(dsc->app_dev);
    if (src_app_dev == NULL || dst_app_dev == NULL) {
      mgos_boot_dbg_printf("Error opening %s %s\n", ssc->app_dev, dsc->app_dev);
      goto out;
    }
    if (!mgos_boot_copy_dev(src_app_dev, dst_app_dev, sss->app_len)) goto out;
    uint32_t app_crc32 = mgos_boot_checksum(dst_app_dev, sss->app_len);
    if (sss->app_crc32 != 0 && sss->app_crc32 != app_crc32) goto out;
    dss->app_len = sss->app_len;
    dss->app_org = sss->app_org;
    dss->app_crc32 = app_crc32;
    dss->app_flags = sss->app_flags;
  }
  res = true;
out:
  if (!res) {
    dss->err_count++;
    mgos_boot_cfg_write(cfg, true /* dump */);
  }
  mgos_vfs_dev_close(src_app_dev);
  mgos_vfs_dev_close(dst_app_dev);
  return res;
}

void mgos_cd_putc(int c) {
  mgos_boot_dbg_putc(c);
}

static void swap_fs_devs(struct mgos_boot_cfg *cfg, int8_t a, int8_t b) {
  char temp_fs_dev[8];
  strcpy(temp_fs_dev, cfg->slots[a].cfg.fs_dev);
  strcpy(cfg->slots[a].cfg.fs_dev, cfg->slots[b].cfg.fs_dev);
  strcpy(cfg->slots[b].cfg.fs_dev, temp_fs_dev);
}

void mgos_boot_main(void) {
  struct mgos_boot_cfg *cfg;
  mgos_wdt_enable();
  mgos_wdt_set_timeout(10 /* seconds */);

  mgos_boot_early_init();

  uintptr_t next_app_org = mgos_boot_get_next_app_org();
  if (next_app_org != 0) {
    mgos_boot_set_next_app_org(0);
    mgos_boot_app(next_app_org);
    // Not reached.
    goto out;
  }

  mgos_boot_init();
  mgos_boot_dbg_setup();
  mgos_boot_dbg_printf("\n\nMongoose OS loader %s (%s)\n", build_version,
                       build_id);

  if (!mgos_boot_devs_init()) {
    mgos_boot_dbg_printf("%s init failed\n", "dev");
    goto out;
  }
  if (!mgos_root_devtab_init()) {
    mgos_boot_dbg_printf("%s init failed\n", "devtab");
    goto out;
  }
  if (!mgos_boot_cfg_init()) {
    mgos_boot_dbg_printf("%s init failed\n", "cfg");
    goto out;
  }
  cfg = mgos_boot_cfg_get();
  mgos_boot_cfg_dump(cfg);
  mgos_wdt_feed();

  /*
   * Before booting, we need to:
   * 1) Decide which slot to boot
   * 2) Make sure the desired boot slot is bootable
   */

  if (!(cfg->flags & MGOS_BOOT_F_COMMITTED)) {
    if (!(cfg->flags & MGOS_BOOT_F_FIRST_BOOT_B)) {
      mgos_boot_dbg_printf("Reboot without commit - reverting to %d\n",
                           cfg->revert_slot);
      cfg->active_slot = cfg->revert_slot;
      cfg->revert_slot = -1;
      cfg->flags |= MGOS_BOOT_F_COMMITTED;
      cfg->flags &= ~(MGOS_BOOT_F_FIRST_BOOT_A | MGOS_BOOT_F_MERGE_FS);
    } else {
      /* This is the first reboot after update, flip our flag. */
      cfg->flags &= ~MGOS_BOOT_F_FIRST_BOOT_B;
      mgos_boot_dbg_printf("First boot of slot %d\n", cfg->active_slot);
    }
    if (!mgos_boot_cfg_write(cfg, false /* dump */)) goto out;
  }

  /*
   * We have decided which slot to boot.
   * It it is not directly bootable, a swap is required.
   */
  struct mgos_boot_slot *as = &cfg->slots[cfg->active_slot];
  if (as->cfg.app_map_addr != as->state.app_org) {
    int bootable_slot = mgos_boot_cfg_find_slot(cfg, as->state.app_org,
                                                false /* want_fs */, -1, -1);
    mgos_boot_dbg_printf("Slot %d is not bootable, will use %d\n",
                         cfg->active_slot, bootable_slot);
    if (bootable_slot < 0) {
      mgos_boot_dbg_printf("No slot available @ 0x%lx!\n",
                           (unsigned long) as->state.app_org);
      goto out;
    }
    /* We found a bootable slot. If it is the revert slot, it is valuable
     * and we need to make a backup of it. */
    if (bootable_slot == cfg->revert_slot) {
      /* Find a temp slot. We don't need FS for the swap. */
      int8_t temp_slot =
          mgos_boot_cfg_find_slot(cfg, 0 /* map_addr */, false /* want_fs */,
                                  bootable_slot, cfg->revert_slot);
      mgos_boot_dbg_printf(
          "Slot %d contains useful data, "
          "will make a backup of it in slot %d\n",
          bootable_slot, temp_slot);
      if (temp_slot < 0) {
        mgos_boot_dbg_printf("No suitable temp slot!\n");
        goto out;
      }
      if (!mgos_boot_copy_app(cfg, bootable_slot, temp_slot)) goto out;
      cfg->revert_slot = temp_slot;
      swap_fs_devs(cfg, temp_slot, bootable_slot);
      /* Commit this config. This is a stable configuration and we need to
       * preserve it in case the subsequent copy is interrupted. */
      if (!mgos_boot_cfg_write(cfg, false /* dump */)) goto out;
    }
    if (!mgos_boot_copy_app(cfg, cfg->active_slot, bootable_slot)) goto out;
    swap_fs_devs(cfg, cfg->active_slot, bootable_slot);
    cfg->active_slot = bootable_slot;
    if (!mgos_boot_cfg_write(cfg, true /* dump */)) goto out;
  }

  /* cfg->active_slot may have changed. */
  as = &cfg->slots[cfg->active_slot];

  /* Verify app checksum. */
  struct mgos_vfs_dev *app_dev = mgos_vfs_dev_open(as->cfg.app_dev);
  uint32_t app_crc32 = mgos_boot_checksum(app_dev, as->state.app_len);
  mgos_vfs_dev_close(app_dev);
  if (app_crc32 != as->state.app_crc32) {
    mgos_boot_dbg_printf("App CRC mismatch!\n");
    goto out;
  }

  mgos_boot_cfg_deinit();
  uintptr_t app_org = cfg->slots[cfg->active_slot].state.app_org;
  mgos_boot_dbg_printf("Booting slot %d (%p)\r\n", cfg->active_slot,
                       (void *) app_org);
  mgos_boot_print_app_info(app_org);
  mgos_boot_set_next_app_org(app_org);
  next_app_org = mgos_boot_get_next_app_org();
  mgos_boot_system_restart();
  // Not reached.

out:
  mgos_boot_dbg_printf("FAIL\n");
  while (1) {
  }
}
