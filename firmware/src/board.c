/*
 * Board support: 72 MHz clock tree, 921600 baud debug UART, non-blocking TX.
 *
 * Clock plan (matches the stock board's crystal and the USB requirement):
 *   HXTAL 12 MHz -> PLL x6 -> SYSCLK 72 MHz
 *   HCLK 72 MHz, APB1 36 MHz, APB2 72 MHz
 *   USBFS clock = CK_PLL / 1.5 = 48 MHz
 *   Final RCU_CFG0 = 0x0011840A and RCU_CFG2 = 0x00000100 once sensing_init() has set the ADC prescaler (the register values of the stock image).
 *   Flash wait states stay at the reset value (0), as on the stock image.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "board.h"
#include "gd32f350_regs.h"

uint32_t SystemCoreClock = 12000000UL;

static void clock_init(void) {
#if H420X_SYSINIT_PARITY
  /* 0.6.3 option: the stock image's reset-time clock cleanup also switches
   * the 28 MHz and 48 MHz internal oscillators off and never starts them
   * again. The bootloader may have left them on; this build matches stock. */
  RCU_CTL1 &= ~1UL;
  RCU_ADDCTL &= ~(1UL << 16);
#endif
  /* Start the 12 MHz crystal. */
  RCU_CTL0 |= RCU_CTL0_HXTALEN;
  while ((RCU_CTL0 & RCU_CTL0_HXTALSTB) == 0U) {
  }

  /* The bootloader may have left SYSCLK on the PLL. Never change the PLL
   * configuration while it is the system clock: make sure IRC8M is running,
   * then drop to it first. */
  RCU_CTL0 |= RCU_CTL0_IRC8MEN;
  while ((RCU_CTL0 & RCU_CTL0_IRC8MSTB) == 0U) {
  }
  uint32_t cfg = RCU_CFG0 & ~RCU_CFG0_SCS_MASK;
  RCU_CFG0 = cfg;
  while ((RCU_CFG0 & RCU_CFG0_SCSS_MASK) != 0U) {
  }

  /* The PLL may still be running even after the SYSCLK switch. Disable it
   * before changing PLLMF/PREDV, then reconfigure and re-enable. */
  RCU_CTL0 &= ~RCU_CTL0_PLLEN;
  while ((RCU_CTL0 & RCU_CTL0_PLLSTB) != 0U) {
  }

  /* PLL source HXTAL /1 (not IRC48M), multiply x6. Clear both extra
   * multiplier bits (CFG0 bit 27 = bit 4, CFG1 bit 31 = bit 5). */
  RCU_CFG1 &= ~(RCU_CFG1_PREDV_MASK | RCU_CFG1_PLLPRESEL | RCU_CFG1_PLLMF5);
  RCU_CFG2 &= ~RCU_CFG2_USBFSPS2;   /* USBFSPSC bit 2 = 0 -> /1.5 */

  /* APB1 divided by 2, APB2 undivided; USB clock = PLL/1.5 = 48 MHz. */
  cfg = RCU_CFG0;
  cfg &= ~(RCU_CFG0_PLLMF_MASK | RCU_CFG0_PLLMF4 | RCU_CFG0_PLLSEL |
           RCU_CFG0_PLLPREDV | RCU_CFG0_USBFSPS_MASK |
           RCU_CFG0_APB1PS_MASK | RCU_CFG0_APB2PS_MASK |
           RCU_CFG0_AHBPS_MASK);
  cfg |= RCU_CFG0_PLLMF_MUL6 | RCU_CFG0_PLLSEL | RCU_CFG0_USBFSPS_DIV1_5;
  cfg |= (4UL << 8);   /* APB1 / 2 */
  RCU_CFG0 = cfg;

  RCU_CTL0 |= RCU_CTL0_PLLEN;
  while ((RCU_CTL0 & RCU_CTL0_PLLSTB) == 0U) {
  }

  RCU_CFG0 = (RCU_CFG0 & ~RCU_CFG0_SCS_MASK) | RCU_CFG0_SCS_PLL;
  while ((RCU_CFG0 & RCU_CFG0_SCSS_MASK) != RCU_CFG0_SCSS_PLL) {
  }

  SystemCoreClock = CPU_HZ;
}

/* ------------------------------------------------------------------------- */
/* UART0: PA9 TX, PA10 RX, 921600 8N1.                                       */
/* ------------------------------------------------------------------------- */

#define UART_RING 256U
#define APB2_HZ   (CPU_HZ)
#define UART_BAUD_HZ 921600U
static volatile uint8_t uart_buf[UART_RING];
static volatile uint16_t uart_head;
static volatile uint16_t uart_tail;

void uart_init(void) {
  RCU_AHBEN |= RCU_AHBEN_GPIOA;
  RCU_APB2EN |= RCU_APB2EN_USART0;

  /* PA9/PA10 alternate function AF1 = USART0_TX/RX on GD32F3x0
   * (datasheet port-A alternate-function table and vendor examples). */
  gpio_mode(GPIOA_BASE, 9U, GPIO_MODE_AF);
  gpio_mode(GPIOA_BASE, 10U, GPIO_MODE_AF);
  gpio_pull(GPIOA_BASE, 10U, GPIO_PULL_UP);
  gpio_speed(GPIOA_BASE, 9U, 2U);
  gpio_speed(GPIOA_BASE, 10U, 2U);
  gpio_af(GPIOA_BASE, 9U, 1U);
  gpio_af(GPIOA_BASE, 10U, 1U);

  /* USART0 is clocked from APB2 (72 MHz). USARTDIV = 72e6/(16*921600) = 4.883; the rounded 16x divider is 78 = 0x4E (923,077 baud, +0.16%). */
  USART_BAUD(USART0_BASE) = (APB2_HZ + (UART_BAUD_HZ / 2U)) / UART_BAUD_HZ;
  USART_CTL0(USART0_BASE) = USART_CTL0_UEN | USART_CTL0_TEN;

  uart_head = uart_tail = 0U;
}

void uart_write(const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  while (len--) {
    uint16_t next = (uint16_t)((uart_head + 1U) % UART_RING);
    if (next == uart_tail) {
      return; /* ring full: drop rather than block the control loop */
    }
    uart_buf[uart_head] = *p++;
    uart_head = next;
  }
}

void uart_puts(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') {
    n++;
  }
  uart_write(s, n);
}

void uart_u32(uint32_t value) {
  char tmp[10];
  unsigned n = 0;
  if (value == 0U) {
    uart_write("0", 1);
    return;
  }
  while (value != 0U && n < sizeof(tmp)) {
    tmp[n++] = (char)('0' + (value % 10U));
    value /= 10U;
  }
  while (n != 0U) {
    uart_write(&tmp[--n], 1);
  }
}

void uart_hex32(uint32_t value) {
  static const char hex[] = "0123456789ABCDEF";
  char tmp[8];
  for (unsigned i = 0; i < 8U; i++) {
    tmp[7U - i] = hex[value & 0xFU];
    value >>= 4;
  }
  uart_write(tmp, sizeof(tmp));
}

void uart_poll(void) {
  while (uart_tail != uart_head) {
    if ((USART_STAT(USART0_BASE) & USART_STAT_TBE) == 0U) {
      return;
    }
    USART_TDATA(USART0_BASE) = uart_buf[uart_tail];
    uart_tail = (uint16_t)((uart_tail + 1U) % UART_RING);
  }
}

void board_init(void) {
  clock_init();
  uart_init();
}
