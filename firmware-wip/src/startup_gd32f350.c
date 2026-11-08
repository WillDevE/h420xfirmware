/*
 * Clean-room Cortex-M4 startup for the H420X application slot.
 *
 * The stock bootloader jumps to 0x08004000 with a valid vector table. This
 * file rebuilds that contract: initial SP in SRAM, Thumb reset vector inside
 * the 32,000-byte application window, VTOR relocated before any C code runs.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>
#include "gd32f350_regs.h"

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

extern int main(void);
extern void USBFS_IRQHandler(void);

void Reset_Handler(void);
void reset_handler_c(void);
void Default_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

typedef void (*vector_fn)(void);

__attribute__((section(".isr_vector"), used))
static const vector_fn vector_table[16U + IRQ_COUNT] = {
    (vector_fn)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    [16U ... (16U + USBFS_IRQN - 1U)] = Default_Handler,
    [16U + USBFS_IRQN] = USBFS_IRQHandler,
};

/* The bootloader enters the application with a branch, not a reset, so the
 * hardware has not reloaded MSP from our vector table. Establish our stack
 * before executing any compiled C code. */
__attribute__((naked, noreturn)) void Reset_Handler(void) {
  __asm volatile (
      "cpsid i\n"
      "ldr r0, =_estack\n"
      "msr msp, r0\n"
      "dsb\n"
      "isb\n"
      "b reset_handler_c\n");
}

__attribute__((used, noreturn)) void reset_handler_c(void) {
  /* The bootloader may leave VTOR at the bootloader base. */
  SCB_VTOR = 0x08004000UL;

  /* Drop any bootloader interrupt state before the application enables its
   * own. SysTick is not used by M0; an inherited tick would vector into the
   * default handler and hang. */
  for (unsigned i = 0; i < 3U; i++) {
    NVIC_ICER[i] = 0xFFFFFFFFUL;
    NVIC_ICPR[i] = 0xFFFFFFFFUL;
  }
  SYST_CSR = 0U;

  uint32_t *src = &_sidata;
  uint32_t *dst = &_sdata;
  while (dst < &_edata) {
    *dst++ = *src++;
  }

  dst = &_sbss;
  while (dst < &_ebss) {
    *dst++ = 0U;
  }

  (void)main();
  for (;;) {
  }
}

void Default_Handler(void) {
  for (;;) {
  }
}

void NMI_Handler(void)        { for (;;) {} }
void HardFault_Handler(void)  { for (;;) {} }
void MemManage_Handler(void)  { for (;;) {} }
void BusFault_Handler(void)   { for (;;) {} }
void UsageFault_Handler(void) { for (;;) {} }
void SVC_Handler(void)        { for (;;) {} }
void DebugMon_Handler(void)   { for (;;) {} }
void PendSV_Handler(void)     { for (;;) {} }
void SysTick_Handler(void)    { for (;;) {} }
