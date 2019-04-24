/*
 * Copyright (c) 2014-2019 Cesanta Software Limited
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

#include "mgos_boot_hal.h"

#include "common/str_util.h"

#include "mgos_boot_cfg.h"
#include "mgos_boot_dbg.h"
#include "mgos_hal.h"
#include "mgos_uart.h"
#include "mgos_vfs_dev_part.h"

#include "rs14100_sdk.h"
#include "rs14100_uart.h"

#include "rs14100_vfs_dev_qspi_flash.h"

#define FLASH_BASE QSPI_AUTOM_CHIP0_ADDRESS

extern uint32_t mgos_boot_checksum(struct mgos_vfs_dev *src, size_t len);

bool mgos_boot_print_app_info(uintptr_t app_org) {
  if (app_org < FLASH_BASE || app_org > FLASH_BASE + 4 * 1024 * 1024) {
    mgos_boot_dbg_printf("Invalid app address\n");
    return false;
  }
  const uint32_t *app_vectors = (const uint32_t *) app_org;
  uint32_t sp = app_vectors[0];
  uint32_t entry = app_vectors[48];
  uint32_t magic = app_vectors[59];
  mgos_boot_dbg_printf("SP 0x%lx, entry: 0x%lx\r\n", sp, entry);
  if (magic != 0x10ad10ad || sp < SRAM_BASE ||
      sp > SRAM_BASE + 1 * 1024 * 1024 ||
      (uintptr_t) entry < (uintptr_t) app_vectors ||
      entry > FLASH_BASE + 4 * 1024 * 1024) {
    return false;
  }
  return true;
}

bool mgos_boot_cfg_should_write_default(void) {
  return false;
}

void mgos_boot_app(uintptr_t app_org) {
  SCB->VTOR = app_org;
  const uint32_t *app_vectors = (const uint32_t *) app_org;
  uint32_t sp = app_vectors[0];
  uint32_t entry = app_vectors[48];
  __asm volatile(
      "mov  sp, %0 \n"
      "bx   %1     \n"
      : /* output */
      : /* input */ "r"(sp), "r"(entry)
      : /* scratch */);
  while (true) {
  }
}

extern void rs14100_clock_config(uint32_t cpu_freq);
extern void (*mgos_nsleep100)(uint32_t n);
extern void mgos_nsleep100_impl(uint32_t n);
extern uint32_t mgos_nsleep100_loop_count;

void mgos_boot_init(void) {
  SystemInit();
  rs14100_clock_config(180000000);
  mgos_nsleep100 = mgos_nsleep100_impl;
  mgos_nsleep100_loop_count = 18;
}

bool mgos_boot_devs_init(void) {
  return (rs14100_vfs_dev_qspi_flash_register_type() &&
          mgos_vfs_dev_part_init());
}

void mgos_boot_cfg_set_default_slots(struct mgos_boot_cfg *cfg) {
  struct mgos_boot_slot_cfg *sc;
  struct mgos_boot_slot_state *ss;
  struct mgos_vfs_dev *app0_dev = mgos_vfs_dev_open("app0");
  /* Create app0 in a committed state. */
  cfg->num_slots = 3;
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
  /* Slot 1. */
  sc = &cfg->slots[1].cfg;
  ss = &cfg->slots[1].state;
  strcpy(sc->app_dev, "app1");
  strcpy(sc->fs_dev, "fs1");
  sc->flags = MGOS_BOOT_SLOT_F_VALID | MGOS_BOOT_SLOT_F_WRITEABLE;
  sc->app_map_addr = FLASH_BASE + MGOS_BOOT_APP1_OFFSET;
  /* Slot 2 - factory reset slot. Not writeable, not bootable. */
  sc = &cfg->slots[2].cfg;
  ss = &cfg->slots[2].state;
  strcpy(sc->app_dev, "appF");
  strcpy(sc->fs_dev, "fsF");
  sc->flags = MGOS_BOOT_SLOT_F_VALID;
  sc->app_map_addr = 0;
}

bool mgos_boot_dbg_setup(void) {
  struct mgos_uart_config cfg;
  mgos_uart_config_set_defaults(MGOS_DEBUG_UART, &cfg);
  cfg.baud_rate = MGOS_DEBUG_UART_BAUD_RATE;
  return mgos_uart_configure(MGOS_DEBUG_UART, &cfg);
}

void mgos_boot_dbg_putc(char c) {
  rs14100_uart_putc(MGOS_DEBUG_UART, c);
}

/* Lower RAM addresses get scribbled on by (presumably) ROM so we need
 * a location to stash it during final reboot.
 * mgos_boot_system_restart stashes it and
 * mgos_boot_early_init retrieves it. */
extern struct mgos_boot_state g_boot_state;
#define BOOT_STATE_STASH_LOCATION ((void *) 0x21f000)

void mgos_boot_system_restart(void) {
  memcpy(BOOT_STATE_STASH_LOCATION, &g_boot_state, sizeof(g_boot_state));
  mgos_dev_system_restart();
}

void mgos_boot_early_init(void) {
  memcpy(&g_boot_state, BOOT_STATE_STASH_LOCATION, sizeof(g_boot_state));
  memset(BOOT_STATE_STASH_LOCATION, 0, sizeof(g_boot_state));
}

int main(void) {
  mgos_boot_main();
  return 0;
}
