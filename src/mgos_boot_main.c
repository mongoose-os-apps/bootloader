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

#include "mgos_system.h"
#include "mgos_uart.h"
#include "mgos_utils.h"
#include "mgos_vfs_dev.h"

#include "mgos_boot.h"
#include "mgos_boot_cfg.h"
#include "mgos_boot_dbg.h"
#include "mgos_boot_devs.h"

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
IRAM void mgos_ints_disable(void) __attribute__((alias("mgos_ints_enable")));
void mgos_lock(void) __attribute__((alias("mgos_ints_enable")));
void mgos_unlock(void) __attribute__((alias("mgos_ints_enable")));

extern const struct mgos_vfs_dev_ops mgos_vfs_dev_encr_ops;
extern const struct mgos_vfs_dev_ops mgos_vfs_dev_w25xxx_ops;

static uint8_t io_buf[STM32_BOOT_IO_SIZE];

uint32_t mgos_boot_checksum(struct mgos_vfs_dev *src, size_t len) {
  bool res = false;
  uint32_t offset = 0, crc32 = 0;
  mgos_boot_dbg_printf("Checksum %s (%lu): ", src->name, (unsigned long) len);
  while (len > 0) {
    size_t io_len = sizeof(io_buf);
    size_t data_len = MIN(len, io_len);
    /* Always read in fixed size chunks. */
    enum mgos_vfs_dev_err r = mgos_vfs_dev_read(src, offset, io_len, io_buf);
    if (r != 0) {
      crc32 = 0;
      mgos_boot_dbg_printf("Read err %s @ %lu: %d\r\n", src->name,
                           (unsigned long) offset, r);
      goto out;
    }
    crc32 = cs_crc32(crc32, io_buf, data_len);
    mgos_wdt_feed();
    offset += data_len;
    len -= data_len;
    if (len % 65536 == 0) mgos_boot_dbg_putc('.');
  }
  res = true;
out:
  if (res) mgos_boot_dbg_printf(" 0x%08lx\r\n", (unsigned long) crc32);
  return crc32;
}

bool mgos_boot_copy_dev(struct mgos_vfs_dev *src, struct mgos_vfs_dev *dst,
                        size_t len) {
  bool res = false;
  uint32_t offset = 0;
  mgos_boot_dbg_printf("%s --> %s (%lu): ", src->name, dst->name,
                       (unsigned long) len);
  while (len > 0) {
    enum mgos_vfs_dev_err r;
    size_t io_len = sizeof(io_buf);
    size_t data_len = MIN(len, io_len);
    /* Always read and write in fixed size chunks. */
    r = mgos_vfs_dev_read(src, offset, io_len, io_buf);
    if (r != 0) {
      mgos_boot_dbg_printf("Read err %s @ %lu: %d\r\n", src->name,
                           (unsigned long) offset, r);
      goto out;
    }
    /*
     * Erase is complicated. Devices have different erase sizes and some
     * (STM32F flash) have non-uniform layout with varying sector size.
     * Here we just try every erase size from 2K to 256K.
     * Note that none may succeed on every iteration if offset is not aligned.
     */
    for (size_t erase_len = io_len; erase_len <= 256 * 1024; erase_len *= 2) {
      if (mgos_vfs_dev_erase(dst, offset, erase_len) == 0) break;
    }
    r = mgos_vfs_dev_write(dst, offset, io_len, io_buf);
    if (r != 0) {
      mgos_boot_dbg_printf("Write err %s @ %lu: %d\r\n", dst->name,
                           (unsigned long) offset, r);
      goto out;
    }
    mgos_wdt_feed();
    offset += data_len;
    len -= data_len;
    if (len % 65536 == 0) mgos_boot_dbg_putc('.');
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
      mgos_boot_dbg_printf("Error opening %s %s\r\n", ssc->app_dev,
                           dsc->app_dev);
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

enum mgos_boot_slot_swap_phase {
  MGOS_BOOT_SLOT_SWAP_IDLE = 0,
  MGOS_BOOT_SLOT_SWAP_INIT = 1,
  MGOS_BOOT_SLOT_SWAP_COPY_AT = 2,
  MGOS_BOOT_SLOT_SWAP_COPY_BA = 3,
  MGOS_BOOT_SLOT_SWAP_COPY_TB = 4,
  MGOS_BOOT_SLOT_SWAP_COMMIT = 5,
};

bool mgos_boot_slot_swap_step(struct mgos_boot_cfg *cfg) {
  bool res = false;
  struct mgos_boot_swap_state *sws = &cfg->swap;
  int8_t a = sws->a, b = sws->b;
  struct mgos_boot_slot *sa = &cfg->slots[a];
  struct mgos_boot_slot *sb = &cfg->slots[b];
  mgos_boot_dbg_printf("Swap %d <-> %d ph %d t %d\r\n", a, b, sws->phase,
                       sws->t);
  switch (sws->phase) {
    case MGOS_BOOT_SLOT_SWAP_INIT: {
      /* Find a temp slot. We don't need FS for the swap. */
      sws->t = mgos_boot_cfg_find_slot(cfg, 0 /* map_addr */,
                                       false /* want_fs */, a, b);
      if (sws->t < 0) {
        mgos_boot_dbg_printf("No suitable temp slot!\r\n");
        break;
      }
      sws->phase = MGOS_BOOT_SLOT_SWAP_COPY_AT;
      res = true;
      break;
    }
    /* A swap with a read-only slot is effectively a copy. */
    case MGOS_BOOT_SLOT_SWAP_COPY_AT: {
      res = mgos_boot_copy_app(cfg, a, sws->t);
      if (res) sws->phase = MGOS_BOOT_SLOT_SWAP_COPY_BA;
      break;
    }
    case MGOS_BOOT_SLOT_SWAP_COPY_BA: {
      if ((sa->cfg.flags & MGOS_BOOT_SLOT_F_WRITEABLE)) {
        res = mgos_boot_copy_app(cfg, b, sws->a);
      } else {
        res = true;
      }
      if (res) sws->phase = MGOS_BOOT_SLOT_SWAP_COPY_TB;
      break;
    }
    case MGOS_BOOT_SLOT_SWAP_COPY_TB: {
      if ((sb->cfg.flags & MGOS_BOOT_SLOT_F_WRITEABLE)) {
        res = mgos_boot_copy_app(cfg, sws->t, b);
      } else {
        res = true;
      }
      if (res) sws->phase = MGOS_BOOT_SLOT_SWAP_COMMIT;
      break;
    }
    case MGOS_BOOT_SLOT_SWAP_COMMIT: {
      char fs_dev_t[8];
      strcpy(fs_dev_t, cfg->slots[a].cfg.fs_dev);
      if ((sa->cfg.flags & MGOS_BOOT_SLOT_F_WRITEABLE)) {
        strcpy(cfg->slots[a].cfg.fs_dev, cfg->slots[b].cfg.fs_dev);
      }
      if ((sb->cfg.flags & MGOS_BOOT_SLOT_F_WRITEABLE)) {
        strcpy(cfg->slots[b].cfg.fs_dev, fs_dev_t);
      }

      if (cfg->active_slot == a) {
        cfg->active_slot = b;
      } else if (cfg->active_slot == b) {
        cfg->active_slot = a;
      }
      if (cfg->revert_slot == a) {
        cfg->revert_slot = b;
      } else if (cfg->revert_slot == b) {
        cfg->revert_slot = a;
      }
      memset(sws, 0, sizeof(*sws));
      sws->phase = MGOS_BOOT_SLOT_SWAP_IDLE;
      res = true;
      break;
    }
    default:
      break;
  }
  return res;
}

bool mgos_boot_slot_swap_run(struct mgos_boot_cfg *cfg) {
  while (cfg->swap.phase != MGOS_BOOT_SLOT_SWAP_IDLE) {
    if (!mgos_boot_slot_swap_step(cfg)) return false;
    if (!mgos_boot_cfg_write(cfg, false /* dump */)) return false;
  }
  return true;
}

bool mgos_boot_slot_swap(struct mgos_boot_cfg *cfg, int a, int b) {
  cfg->swap.a = a;
  cfg->swap.b = b;
  cfg->swap.t = -1;
  cfg->swap.phase = MGOS_BOOT_SLOT_SWAP_INIT;
  mgos_boot_dbg_printf("Swapping %d <-> %d\r\n", a, b);
  return mgos_boot_slot_swap_run(cfg);
}

void mgos_cd_putc(int c) {
  mgos_boot_dbg_putc(c);
}

void mgos_boot_main(void) {
  struct mgos_boot_cfg *cfg;
  mgos_wdt_enable();
  mgos_wdt_set_timeout(5 /* seconds */);
  mgos_boot_dbg_setup();
  mgos_boot_dbg_printf("\r\n\r\nmOS loader %s (%s)\r\n", build_version,
                       build_id);

  if (!mgos_boot_devs_init()) {
    mgos_boot_dbg_printf("%s init failed\r\n", "dev");
    goto out;
  }
  if (!mgos_boot_cfg_init()) {
    mgos_boot_dbg_printf("%s init failed\r\n", "cfg");
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

  /* But first, finish a swap if one was underway. */
  if (!mgos_boot_slot_swap_run(cfg)) goto out;

  if (!(cfg->flags & MGOS_BOOT_F_COMMITTED)) {
    if (!(cfg->flags & MGOS_BOOT_F_FIRST_BOOT_B)) {
      mgos_boot_dbg_printf("Reboot without commit - reverting to %d\r\n",
                           cfg->revert_slot);
      cfg->active_slot = cfg->revert_slot;
      cfg->revert_slot = -1;
      cfg->flags |= MGOS_BOOT_F_COMMITTED;
      cfg->flags &= ~(MGOS_BOOT_F_FIRST_BOOT_A | MGOS_BOOT_F_MERGE_FS);
    } else {
      /* This is first reboot after update, flip our flag. */
      cfg->flags &= ~MGOS_BOOT_F_FIRST_BOOT_B;
      mgos_boot_dbg_printf("First boot of slot %d\r\n", cfg->active_slot);
    }
    if (!mgos_boot_cfg_write(cfg, true /* dump */)) goto out;
  }

  /*
   * We have decided which slot to boot.
   * It it is not directly bootable, a swap is required.
   */
  struct mgos_boot_slot *as = &cfg->slots[cfg->active_slot];
  if (as->cfg.app_map_addr != as->state.app_org) {
    int bootable_slot = mgos_boot_cfg_find_slot(cfg, as->state.app_org,
                                                true /* want_fs */, -1, -1);
    if (bootable_slot < 0) {
      mgos_boot_dbg_printf("No slot available @ 0x%lx!\r\n",
                           (unsigned long) as->state.app_org);
      goto out;
    }
    if (!mgos_boot_slot_swap(cfg, cfg->active_slot, bootable_slot)) goto out;
  }

  mgos_boot_cfg_deinit();
  mgos_boot_devs_deinit();
  mgos_boot_app(cfg, cfg->active_slot);
out:
  mgos_boot_dbg_printf("FAIL\r\n");
  while (1) {
  }
}
