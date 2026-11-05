/*
 * Minimal clean-room register map for the GD32F350-class USBFS tablet.
 *
 * Only the peripherals used by this firmware are described. Bit positions
 * follow the public GD32F3x0 user manual (RCU, GPIO, USART, NVIC, SysTick).
 * No vendor source code is copied into this file.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef H420X_GD32F350_REGS_H
#define H420X_GD32F350_REGS_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

/* ---- system control ---------------------------------------------------- */
#define SCB_VTOR          REG32(0xE000ED08UL)
#define SCB_CPACR         REG32(0xE000ED88UL)
#define SCB_SHPR2         REG32(0xE000ED1CUL)
#define SCB_SHPR3         REG32(0xE000ED20UL)

#define NVIC_ISER         ((volatile uint32_t *)0xE000E100UL)
#define NVIC_ICER         ((volatile uint32_t *)0xE000E180UL)
#define NVIC_ICPR         ((volatile uint32_t *)0xE000E280UL)
#define NVIC_IPR          ((volatile uint8_t  *)0xE000E400UL)
#define SYST_CSR          REG32(0xE000E010UL)

#define USBFS_IRQN        67U
/* USBFS device status register (DWC2 DSTS). FNSOF, bits 21:8, is the frame
 * number of the last received SOF. */
#define USBFS_DSTS           (*(volatile uint32_t *)0x50000808UL)
#define USBFS_DSTS_FNSOF(v)  ((((uint32_t)(v)) >> 8) & 0x3FFFU)
/* USBFS IN endpoint 1 interrupt register (DWC2 DIEPINT1). Bit 0 (XFRC) is set
 * when the host has taken the packet queued on EP1 IN (the vendor report) and
 * stays set until the USB interrupt handler clears it. Read-only use here. */
#define USBFS_DIEPINT1       (*(volatile uint32_t *)0x50000928UL)
#define USBFS_DIEPINT_XFRC   0x01U
/* USBFS OUT endpoint 0 interrupt register (DWC2 DOEPINT0). Bit 3 (STUP) is set when
 * a SETUP stage is done; TinyUSB clears it in dcd_int_handler(). */
#define USBFS_DOEPINT0       (*(volatile uint32_t *)0x50000B08UL)
#define USBFS_DOEPINT_STUP   0x08U
#define IRQ_COUNT         68U

/* ---- reset and clock unit --------------------------------------------- */
#define RCU_BASE          0x40021000UL
#define RCU_CTL0          REG32(RCU_BASE + 0x00UL)
#define RCU_CFG0          REG32(RCU_BASE + 0x04UL)
#define RCU_AHBEN         REG32(RCU_BASE + 0x14UL)
#define RCU_APB2EN        REG32(RCU_BASE + 0x18UL)
#define RCU_AHBRST        REG32(RCU_BASE + 0x28UL)
#define RCU_CFG1          REG32(RCU_BASE + 0x2CUL)
#define RCU_CFG2          REG32(RCU_BASE + 0x30UL)
#define RCU_CTL1          REG32(RCU_BASE + 0x34UL)   /* bit 0 IRC28MEN */
#define RCU_ADDCTL        REG32(RCU_BASE + 0xC0UL)   /* bit 16 IRC48MEN */

#define RCU_CTL0_IRC8MEN  (1UL << 0)
#define RCU_CTL0_IRC8MSTB (1UL << 1)
#define RCU_CTL0_HXTALEN  (1UL << 16)
#define RCU_CTL0_HXTALSTB (1UL << 17)
#define RCU_CTL0_PLLEN    (1UL << 24)
#define RCU_CTL0_PLLSTB   (1UL << 25)

#define RCU_CFG0_SCS_MASK (3UL << 0)
#define RCU_CFG0_SCS_PLL  (2UL << 0)
#define RCU_CFG0_SCSS_MASK (3UL << 2)
#define RCU_CFG0_SCSS_PLL (2UL << 2)
#define RCU_CFG0_AHBPS_MASK (0xFUL << 4)
#define RCU_CFG0_APB1PS_MASK (7UL << 8)
#define RCU_CFG0_APB2PS_MASK (7UL << 11)
#define RCU_CFG0_PLLSEL   (1UL << 16)
#define RCU_CFG0_PLLPREDV (1UL << 17)
#define RCU_CFG0_PLLMF_MASK (0xFUL << 18)
#define RCU_CFG0_PLLMF4   (1UL << 27)
#define RCU_CFG0_PLLMF_MUL6 (4UL << 18)   /* multiplier = value + 2 */
#define RCU_CFG0_USBFSPS_MASK (3UL << 22)
#define RCU_CFG0_USBFSPS_DIV1_5 (0UL << 22)

#define RCU_CFG1_PREDV_MASK (0xFUL << 0)
#define RCU_CFG1_PLLPRESEL (1UL << 30)
#define RCU_CFG1_PLLMF5   (1UL << 31)   /* PLL multiplier bit 5 */

#define RCU_CFG2_USBFSPS2 (1UL << 30)

#define RCU_AHBEN_USBFS   (1UL << 12)
#define RCU_AHBEN_GPIOA   (1UL << 17)
#define RCU_AHBEN_GPIOB   (1UL << 18)

#define RCU_APB2EN_ADC    (1UL << 9)
#define RCU_APB2EN_USART0 (1UL << 14)

#define RCU_APB1EN        REG32(RCU_BASE + 0x1CUL)
#define RCU_APB1EN_TIMER1 (1UL << 0)

#define RCU_CFG0_ADCPSC_MASK (3UL << 14)
#define RCU_CFG2_ADCSEL   (1UL << 8)
#define RCU_CFG2_ADCPSC2  (1UL << 31)

/* ---- GPIO -------------------------------------------------------------- */
#define GPIOA_BASE        0x48000000UL
#define GPIOB_BASE        0x48000400UL

/* GD32F3x0 GPIO register map (not the GD32F1 CTL0/CTL1 layout):
 *   CTL 0x00, OMODE 0x04, OSPD0 0x08, PUD 0x0C, ISTAT 0x10,
 *   OCTL 0x14, BOP 0x18, LOCK 0x1C, AFSEL0 0x20, AFSEL1 0x24,
 *   BC 0x28, TG 0x2C, OSPD1 0x3C. */
#define GPIO_CTL(base)    REG32((base) + 0x00UL)
#define GPIO_OMODE(base)  REG32((base) + 0x04UL)
#define GPIO_OSPD0(base)  REG32((base) + 0x08UL)
#define GPIO_PUD(base)    REG32((base) + 0x0CUL)
#define GPIO_ISTAT(base)  REG32((base) + 0x10UL)
#define GPIO_OCTL(base)   REG32((base) + 0x14UL)
#define GPIO_BOP(base)    REG32((base) + 0x18UL)
#define GPIO_LOCK(base)   REG32((base) + 0x1CUL)
#define GPIO_AFSEL0(base) REG32((base) + 0x20UL)
#define GPIO_AFSEL1(base) REG32((base) + 0x24UL)
#define GPIO_BC(base)     REG32((base) + 0x28UL)
#define GPIO_TG(base)     REG32((base) + 0x2CUL)
#define GPIO_OSPD1(base)  REG32((base) + 0x3CUL)

#define GPIOA_OCTL        GPIO_OCTL(GPIOA_BASE)
#define GPIOA_BOP         GPIO_BOP(GPIOA_BASE)
#define GPIOA_BC          GPIO_BC(GPIOA_BASE)
#define GPIOB_OCTL        GPIO_OCTL(GPIOB_BASE)
#define GPIOB_BOP         GPIO_BOP(GPIOB_BASE)
#define GPIOB_BC          GPIO_BC(GPIOB_BASE)

#define GPIO_MODE_INPUT   0UL
#define GPIO_MODE_OUTPUT  1UL
#define GPIO_MODE_AF      2UL
#define GPIO_MODE_ANALOG  3UL

#define GPIO_PULL_NONE    0UL
#define GPIO_PULL_UP      1UL
#define GPIO_PULL_DOWN    2UL

/* Set a 2-bit pin field in the single CTL register. */
static inline void gpio_mode(uint32_t base, unsigned pin, uint32_t mode) {
  volatile uint32_t *reg = &GPIO_CTL(base);
  unsigned shift = pin * 2U;
  *reg = (*reg & ~(3UL << shift)) | ((mode & 3UL) << shift);
}

static inline void gpio_af(uint32_t base, unsigned pin, unsigned af) {
  volatile uint32_t *reg = (pin < 8U) ? &GPIO_AFSEL0(base) : &GPIO_AFSEL1(base);
  unsigned shift = (pin & 7U) * 4U;
  *reg = (*reg & ~(0xFUL << shift)) | ((af & 0xFUL) << shift);
}

/* 0.6.1: this is the 0.5.1 helper, kept as is because 0.5.1 is the only
 * clean-room build that tracked on hardware. Pins 8..15 land in OSPD1; the
 * "stock-correct" OSPD0 version (0.6.0) read nothing but the floor. */
static inline void gpio_speed(uint32_t base, unsigned pin, unsigned speed) {
  volatile uint32_t *reg = (pin < 8U) ? &GPIO_OSPD0(base) : &GPIO_OSPD1(base);
  unsigned shift = (pin & 7U) * 2U;
  *reg = (*reg & ~(3UL << shift)) | ((speed & 3UL) << shift);
}

static inline void gpio_pull(uint32_t base, unsigned pin, unsigned pull) {
  volatile uint32_t *reg = &GPIO_PUD(base);
  unsigned shift = pin * 2U;
  *reg = (*reg & ~(3UL << shift)) | ((pull & 3UL) << shift);
}

/* ---- USART0 ------------------------------------------------------------ */
#define USART0_BASE       0x40013800UL
#define USART_CTL0(base)  REG32((base) + 0x00UL)
#define USART_BAUD(base)  REG32((base) + 0x0CUL)
#define USART_STAT(base)  REG32((base) + 0x1CUL)
#define USART_TDATA(base) REG32((base) + 0x28UL)

#define USART_CTL0_UEN    (1UL << 0)
#define USART_CTL0_TEN    (1UL << 3)
#define USART_STAT_TBE    (1UL << 7)

/* ---- ADC --------------------------------------------------------------- */
#define ADC_BASE          0x40012400UL
#define ADC_STAT          REG32(ADC_BASE + 0x00UL)
#define ADC_CTL0          REG32(ADC_BASE + 0x04UL)
#define ADC_CTL1          REG32(ADC_BASE + 0x08UL)
#define ADC_SAMPT0        REG32(ADC_BASE + 0x0CUL)
#define ADC_SAMPT1        REG32(ADC_BASE + 0x10UL)
#define ADC_RSQ0          REG32(ADC_BASE + 0x2CUL)
#define ADC_RSQ1          REG32(ADC_BASE + 0x30UL)
#define ADC_RSQ2          REG32(ADC_BASE + 0x34UL)
#define ADC_RDATA         REG32(ADC_BASE + 0x4CUL)

#define ADC_STAT_EOC      (1UL << 1)
#define ADC_CTL0_SM       (1UL << 8)
#define ADC_CTL0_EOCIE    (1UL << 5)
#define ADC_CTL1_ADCON    (1UL << 0)
#define ADC_CTL1_CTN      (1UL << 1)
#define ADC_CTL1_CLB      (1UL << 2)
#define ADC_CTL1_RSTCLB   (1UL << 3)
#define ADC_CTL1_DMA      (1UL << 8)
#define ADC_CTL1_ETSRC_MASK     (7UL << 17)
#define ADC_CTL1_ETSRC_SOFTWARE (7UL << 17)
#define ADC_CTL1_ETERC    (1UL << 20)
#define ADC_CTL1_SWRCST   (1UL << 22)

#define ADC_SAMPT_1_5     0UL
#define ADC_SAMPT_7_5     1UL
#define ADC_SAMPT_13_5    2UL
#define ADC_SAMPT_28_5    3UL
#define ADC_SAMPT_41_5    4UL
#define ADC_SAMPT_55_5    5UL
#define ADC_SAMPT_71_5    6UL
#define ADC_SAMPT_239_5   7UL

/* PA4 = ADC_IN4. */
#define ADC_PIN_CHANNEL   4U

/* ---- TIMER1 (PA1, 100 Hz stock control output) ------------------------- */
#define TIMER1_BASE       0x40000000UL
#define TIMER1_CTL0       REG32(TIMER1_BASE + 0x00UL)
#define TIMER1_SWEVG      REG32(TIMER1_BASE + 0x14UL)
#define TIMER1_CHCTL0     REG32(TIMER1_BASE + 0x18UL)
#define TIMER1_CHCTL2     REG32(TIMER1_BASE + 0x20UL)
#define TIMER1_PSC        REG32(TIMER1_BASE + 0x28UL)
#define TIMER1_CAR        REG32(TIMER1_BASE + 0x2CUL)
#define TIMER1_CH1CV      REG32(TIMER1_BASE + 0x38UL)

#define TIMER1_CTL0_CEN   (1UL << 0)
#define TIMER1_CTL0_ARSE  (1UL << 7)
#define TIMER1_SWEVG_UPG  (1UL << 0)
#define TIMER1_CHCTL0_CH1_PWM0 (0x60UL << 8)
#define TIMER1_CHCTL2_CH1EN (1UL << 4)

/* ---- DWT cycle counter ------------------------------------------------- */
#define DEMCR             REG32(0xE000EDFCUL)
#define DWT_CTRL          REG32(0xE0001000UL)
#define DWT_CYCCNT        REG32(0xE0001004UL)
#define DEMCR_TRCENA      (1UL << 24)
#define DWT_CTRL_CYCCNTENA (1UL << 0)

/* ---- misc -------------------------------------------------------------- */
#define CPU_HZ            72000000UL
#define USB_PHY_HZ        48000000UL

static inline void delay_cycles(volatile uint32_t n) {
  while (n--) {
    __asm volatile ("nop");
  }
}

static inline void dwt_init(void) {
  DEMCR |= DEMCR_TRCENA;
  DWT_CYCCNT = 0U;
  DWT_CTRL |= DWT_CTRL_CYCCNTENA;
}

static inline void delay_cycles_exact(uint32_t cycles) {
  uint32_t start = DWT_CYCCNT;
  while ((uint32_t)(DWT_CYCCNT - start) < cycles) {
  }
}

/* Requires dwt_init(). At 72 MHz, one microsecond is 72 cycles. */
static inline void delay_us(uint32_t us) {
  if (us != 0U) {
    delay_cycles_exact(us * (CPU_HZ / 1000000UL));
  }
}

static inline void delay_ms(uint32_t ms) {
  while (ms--) {
    delay_cycles(CPU_HZ / 1000UL);
  }
}

#endif /* H420X_GD32F350_REGS_H */
