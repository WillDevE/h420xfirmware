/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 H420X clean-room project
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack, adapted for the GD32F350.
 *
 * GD32F350 USBFS is a device-only Synopsys DWC2 derivative:
 *   - register block 0x50000000
 *   - 4 bidirectional endpoints (EP0 + 3 programmable)
 *   - 320 x 32-bit words of dedicated FIFO RAM
 *   - ARM Cortex-M4F NVIC interrupt 67
 *   - 48 MHz PHY clock derived from CK_PLL / 1.5
 *
 * Unlike the GD32VF103 port, this is an ARM core, so interrupts go through
 * the NVIC rather than the Nuclei ECLIC. Unlike the STM32 ports, the USBFS
 * clock is selected through RCU_CFG0.USBFSPSC by board code before USB init.
 */

#ifndef DWC2_GD32F350_H_
#define DWC2_GD32F350_H_

#ifdef __cplusplus
extern "C" {
#endif

#define DWC2_REG_BASE       0x50000000UL
#define DWC2_EP_MAX         4
#define DWC2_IRQNUM         67

/* GD32F3x0 USBFS_GCCFG (offset 0x38). The GD32 PHY must be powered on and
 * the VBUS comparers enabled even when VBUS sensing is ignored. Without
 * these bits the internal D+ pull-up never appears and the host sees no
 * device at all. */
#define GD32_GCCFG_PWRON    (1UL << 16)
#define GD32_GCCFG_VBUSACEN (1UL << 18)
#define GD32_GCCFG_VBUSBCEN (1UL << 19)
#define GD32_GCCFG_VBUSIG   (1UL << 21)

// Fixed description: the ID/HWCFG registers are not relied upon on this part.
static const dwc2_controller_t _dwc2_controller[] =
{
  { .reg_base = DWC2_REG_BASE, .irqnum = DWC2_IRQNUM, .ep_count = DWC2_EP_MAX,
    .ep_in_count = DWC2_EP_MAX, .otg_dfifo_depth = 320 }
};

extern uint32_t SystemCoreClock;

#define RCU_BASE            0x40021000UL
#define RCU_AHBEN           (*(volatile uint32_t *)(RCU_BASE + 0x14UL))
#define RCU_AHBRST          (*(volatile uint32_t *)(RCU_BASE + 0x28UL))
#define RCU_AHBEN_USBFS     (1UL << 12)

#define NVIC_ISER_BASE      0xE000E100UL
#define NVIC_ICER_BASE      0xE000E180UL

// Enable and reset the USBFS peripheral before the first register access.
TU_ATTR_ALWAYS_INLINE static inline void dwc2_clock_init(uint8_t rhport, tusb_role_t role) {
  (void) rhport;
  (void) role;
  RCU_AHBEN |= RCU_AHBEN_USBFS;
  RCU_AHBRST |= RCU_AHBEN_USBFS;
  RCU_AHBRST &= ~RCU_AHBEN_USBFS;

  // Let the USBFS block come out of reset before the first register access.
  for (volatile uint32_t i = 0; i < (SystemCoreClock / 100000U); i++) {
    __asm volatile ("nop");
  }
}

TU_ATTR_ALWAYS_INLINE static inline void dwc2_int_set(uint8_t rhport, tusb_role_t role, bool enabled) {
  (void) role;
  const uint32_t irq = _dwc2_controller[rhport].irqnum;
  if (enabled) {
    *(volatile uint32_t *)(NVIC_ISER_BASE + 4UL * (irq >> 5)) = 1UL << (irq & 31UL);
  } else {
    *(volatile uint32_t *)(NVIC_ICER_BASE + 4UL * (irq >> 5)) = 1UL << (irq & 31UL);
  }
}

static inline void dwc2_remote_wakeup_delay(void)
{
  // approximately 1 ms
  uint32_t count = SystemCoreClock / 1000UL;
  while (count--) {
    __asm volatile ("nop");
  }
}

// The GD32F350 has an internal full-speed PHY. PA11/PA12 are "additional
// function" pins claimed by the USBFS block, but the PHY power and D+ pull-up
// are controlled by GCCFG.
TU_ATTR_ALWAYS_INLINE static inline void dwc2_gd32f350_gccfg_init(dwc2_regs_t *dwc2) {
  dwc2->stm32_gccfg |= GD32_GCCFG_PWRON | GD32_GCCFG_VBUSACEN |
                       GD32_GCCFG_VBUSBCEN | GD32_GCCFG_VBUSIG;
}

static inline void dwc2_phy_init(dwc2_regs_t *dwc2, uint8_t hs_phy_type) {
  (void) hs_phy_type;
  dwc2_gd32f350_gccfg_init(dwc2);
}

static inline void dwc2_phy_deinit(dwc2_regs_t *dwc2, uint8_t hs_phy_type) {
  (void) hs_phy_type;
  dwc2->stm32_gccfg &= ~GD32_GCCFG_PWRON;
}

static inline void dwc2_phy_update(dwc2_regs_t *dwc2, uint8_t hs_phy_type) {
  (void) hs_phy_type;
  /* Core reset can clear GCCFG, so reapply it here (after reset) and give
   * the internal PHY time to power up before D+ is driven. */
  dwc2_gd32f350_gccfg_init(dwc2);
  dwc2_remote_wakeup_delay();
}

#ifdef __cplusplus
}
#endif

#endif /* DWC2_GD32F350_H_ */
