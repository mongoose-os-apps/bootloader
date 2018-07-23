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

/* We don't use interrupts so we can get away with just two entries */
const __attribute__((section(".flash_int_vectors_boot"))) struct int_vectors
    boot_vectors = {
        .sp = &_stack,
        .reset = stm32_entry,
        .nmi = exc_handler,
        .hard_fault = exc_handler2,
        .mem_manage_fault = exc_handler,
        .bus_fault = exc_handler,
        .usage_fault = exc_handler,
};

void mgos_boot_app(const struct mgos_boot_cfg *cfg, int slot) {
  const struct int_vectors *app_vectors =
      (const struct int_vectors *) cfg->slots[slot].cfg.app_map_addr;
  mgos_boot_dbg_printf("Booting slot %d (ma %p)\r\n", slot, app_vectors);
  if ((uintptr_t) app_vectors < FLASH_BASE_ADDR ||
      (uintptr_t) app_vectors > FLASH_BASE_ADDR + 4 * 1024 * 1024) {
    goto out;
  }
  uint32_t sp = (uint32_t) app_vectors->sp;
  uint32_t entry = (uint32_t) app_vectors->reset;
  mgos_boot_dbg_printf("SP %p, entry: %p\r\n\r\n", app_vectors->sp,
                       app_vectors->reset);
  if (sp < SRAM_BASE_ADDR || sp > SRAM_BASE_ADDR + 2 * 1024 * 1024 ||
      (uintptr_t) entry < (uintptr_t) app_vectors ||
      entry > FLASH_BASE_ADDR + 4 * 1024 * 1024) {
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

int main() {
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
  SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2)); /* Enabled FPU */
#endif
  RCC->CIR = 0; /* Disable interrupts */
  mgos_boot_main();
}
