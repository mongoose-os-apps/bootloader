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

#include "mgos_boot_hal.h"

#include <stdbool.h>
#include <stdlib.h>

#include "common/str_util.h"

#include "mgos_boot_cfg.h"
#include "mgos_boot_dbg.h"
#include "mgos_hal.h"
#include "mgos_vfs_dev_part.h"
#include "mgos_vfs_dev_spi_flash.h"

#include "stm32_flash.h"
#include "stm32_system.h"
#include "stm32_uart_internal.h"
#include "stm32_vfs_dev_flash.h"

bool mgos_boot_dbg_setup(void) {
  struct mgos_uart_config cfg;
  mgos_uart_config_set_defaults(MGOS_DEBUG_UART, &cfg);
  cfg.baud_rate = MGOS_DEBUG_UART_BAUD_RATE;
  return mgos_uart_configure(MGOS_DEBUG_UART, &cfg);
}

void mgos_boot_dbg_putc(char c) {
  stm32_uart_putc(MGOS_DEBUG_UART, c);
}

bool mgos_boot_devs_init(void) {
  return (stm32_vfs_dev_flash_register_type() && mgos_vfs_dev_part_init() &&
          mgos_vfs_dev_spi_flash_init());
}

extern uint32_t mgos_boot_checksum(struct mgos_vfs_dev *src, size_t len);

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
/* Test settings */
#if 0
  cfg->flags = MGOS_BOOT_F_FIRST_BOOT_A | MGOS_BOOT_F_FIRST_BOOT_B | MGOS_BOOT_F_MERGE_FS;
  cfg->slots[1].state.app_len = 524288;
  cfg->slots[1].state.app_org = 0x8010000;
  struct mgos_vfs_dev *app1 = mgos_vfs_dev_open(cfg->slots[1].cfg.app_dev);
  cfg->slots[1].state.app_crc32 = mgos_boot_checksum(app1, cfg->slots[1].state.app_len);
  mgos_vfs_dev_close(app1);
  cfg->active_slot = 1;
  cfg->revert_slot = 0;
#elif 0
  cfg->slots[2].state.app_len = 983040;
  cfg->slots[1].state.app_org = 0x8010000;
#endif
}

struct int_vectors {
  void *sp;
  void (*reset)(void);
  void (*nmi)(void);
  void (*hard_fault)(void);
  void (*mem_manage_fault)(void);
  void (*bus_fault)(void);
  void (*usage_fault)(void);
};

void exc_handler(void) {
  mgos_boot_dbg_putc('X');
  while (1) {
  }
}

void exc_handler2(void) {
  uint32_t hfsr = SCB->HFSR, cfsr = SCB->CFSR;
  mgos_boot_dbg_printf("!!! H 0x%lx C 0x%lx sp %p\n", hfsr, cfsr, &hfsr);
  while (1) {
  }
}

extern uint32_t _stack;
extern void stm32_entry(void);
extern void arm_exc_handler_top(void);

/* We don't use interrupts so we can get away with just two entries */
const __attribute__((
    section(".flash_int_vectors_boot"))) struct int_vectors boot_vectors = {
    .sp = &_stack,
    .reset = stm32_entry,
    .nmi = arm_exc_handler_top,
    .hard_fault = arm_exc_handler_top,
    .mem_manage_fault = arm_exc_handler_top,
    .bus_fault = arm_exc_handler_top,
    .usage_fault = arm_exc_handler_top,
};

bool mgos_boot_print_app_info(uintptr_t app_org) {
  if (app_org < FLASH_BASE || app_org > FLASH_BASE + 4 * 1024 * 1024) {
    mgos_boot_dbg_printf("Invalid app address\n");
    return false;
  }
  const struct int_vectors *app_vectors = (const struct int_vectors *) app_org;
  uint32_t sp = (uint32_t) app_vectors->sp;
  uint32_t entry = (uint32_t) app_vectors->reset;
  mgos_boot_dbg_printf("SP %p, entry: %p\r\n", app_vectors->sp,
                       app_vectors->reset);
  if (sp < STM32_SRAM_BASE_ADDR ||
      sp > STM32_SRAM_BASE_ADDR + 2 * 1024 * 1024 ||
      (uintptr_t) entry < (uintptr_t) app_vectors ||
      entry > FLASH_BASE + 4 * 1024 * 1024) {
    return false;
  }
  return true;
}

void mgos_boot_app(uintptr_t app_org) {
  SCB->VTOR = app_org;
  const struct int_vectors *app_vectors = (const struct int_vectors *) app_org;
  uint32_t sp = (uint32_t) app_vectors->sp;
  uint32_t entry = (uint32_t) app_vectors->reset;
  __asm volatile(
      "mov  sp, %0 \n"
      "bx   %1     \n"
      : /* output */
      : /* input */ "r"(sp), "r"(entry)
      : /* scratch */);
  /* Not reached. This loop is only to satisfy the compiler. */
  while (true) {
  }
}

/*
 * Use last 8 bytes of the boot loader area to keep the "already inited" flag.
 * 8 bytes to satisfy STM32L4 flash write 64-bit alignment requirement.
 */
bool mgos_boot_cfg_should_write_default(void) {
#ifdef STM32L4
  /* Because of ECC, on L4 we can only really overwrite with 0 reliably.
   * It actually depends on whther 0xffffffff was actually overwritten
   * or remains from last erase, in which case ECC bits are untouched
   * and any value can be written. ECC value for 0 is 0, so writing 0 is ok. */
  uint32_t buf[2] = {0, 0};
#else
  uint32_t buf[2] = {MGOS_BOOT_CFG_MAGIC, MGOS_BOOT_CFG_MAGIC};
#endif
  const uint32_t *pf = (uint32_t *) (FLASH_BASE + STM32_FLASH_BL_SIZE - 8);
  if (memcmp(pf, buf, sizeof(buf)) == 0) return false;
  stm32_flash_write_region(STM32_FLASH_BL_SIZE - 8, 8, buf);
  return true;
}

void mgos_boot_early_init(void) {
}

void mgos_boot_system_restart(void) {
  mgos_dev_system_restart();
}

extern struct mgos_boot_state g_boot_state;

void mgos_boot_init(void) {
#ifdef PWR_CSR_SBF  // F2, F4
  g_boot_state.pwr_sr1 = PWR->CSR;
  g_boot_state.pwr_sr2 = 0;
#endif
#ifdef PWR_CSR1_SBF  // F7
  g_boot_state.pwr_sr1 = PWR->CSR1;
  g_boot_state.pwr_sr2 = PWR->CSR2;
#endif
#ifdef PWR_SR1_SBF  // L4
  g_boot_state.pwr_sr1 = PWR->SR1;
  g_boot_state.pwr_sr2 = PWR->SR2;
#endif
  stm32_system_init();
  stm32_clock_config();
  SystemCoreClockUpdate();
}

int main(void) {
  mgos_boot_main();
  return 0;
}
