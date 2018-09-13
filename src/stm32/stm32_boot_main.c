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

#include "mgos_boot.h"

#include "mgos_boot_dbg.h"

#include "stm32_flash.h"
#include "stm32_system.h"

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
  mgos_boot_dbg_printf("!!! H 0x%lx C 0x%lx sp %p\r\n", hfsr, cfsr, &hfsr);
  while (1) {
  }
}

extern uint32_t _stack;
extern void stm32_entry(void);
extern void arm_exc_handler_top(void);

/* We don't use interrupts so we can get away with just two entries */
const __attribute__((section(".flash_int_vectors_boot"))) struct int_vectors
    boot_vectors = {
        .sp = &_stack,
        .reset = stm32_entry,
#if 1
        .nmi = arm_exc_handler_top,
        .hard_fault = arm_exc_handler_top,
        .mem_manage_fault = arm_exc_handler_top,
        .bus_fault = arm_exc_handler_top,
        .usage_fault = arm_exc_handler_top,
#endif
};

void mgos_boot_app(const struct mgos_boot_cfg *cfg, int slot) {
  const struct int_vectors *app_vectors =
      (const struct int_vectors *) cfg->slots[slot].cfg.app_map_addr;
  mgos_boot_dbg_printf("Booting slot %d (ma %p)\r\n", slot, app_vectors);
  if ((uintptr_t) app_vectors < FLASH_BASE ||
      (uintptr_t) app_vectors > FLASH_BASE + 4 * 1024 * 1024) {
    goto out;
  }
  uint32_t sp = (uint32_t) app_vectors->sp;
  uint32_t entry = (uint32_t) app_vectors->reset;
  mgos_boot_dbg_printf("SP %p, entry: %p\r\n\r\n", app_vectors->sp,
                       app_vectors->reset);
  if (sp < SRAM_BASE_ADDR || sp > SRAM_BASE_ADDR + 2 * 1024 * 1024 ||
      (uintptr_t) entry < (uintptr_t) app_vectors ||
      entry > FLASH_BASE + 4 * 1024 * 1024) {
    goto out;
  }
  SCB->VTOR = (uint32_t) app_vectors;
  __asm volatile(
      "mov  sp, %0 \n"
      "bx   %1     \n"
      : /* output */
      : /* input */ "r"(sp), "r"(entry)
      : /* scratch */);
/* Not reached */
out:
  mgos_boot_dbg_putl("Invalid!");
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

int main() {
  stm32_system_init();
  stm32_clock_config();
  SystemCoreClockUpdate();
  mgos_boot_main();
}
