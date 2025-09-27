#include <stdint.h>

/* --------- Base addresses --------- */
#define PERIPH_BASE        0x40000000UL
#define AHB1PERIPH_BASE    0x40020000UL
#define APB1PERIPH_BASE    PERIPH_BASE

#define GPIOA_BASE         (AHB1PERIPH_BASE + 0x0000)
#define RCC_BASE           0x40023800UL
#define USART2_BASE        (APB1PERIPH_BASE + 0x4400)

/* --------- RCC --------- */
#define RCC_AHB1ENR        (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define RCC_APB1ENR        (*(volatile uint32_t*)(RCC_BASE + 0x40))
#define RCC_AHB1ENR_GPIOAEN   (1U<<0)
#define RCC_APB1ENR_USART2EN  (1U<<17)

/* --------- GPIOA --------- */
#define GPIOA_MODER        (*(volatile uint32_t*)(GPIOA_BASE + 0x00))
#define GPIOA_OTYPER       (*(volatile uint32_t*)(GPIOA_BASE + 0x04))
#define GPIOA_OSPEEDR      (*(volatile uint32_t*)(GPIOA_BASE + 0x08))
#define GPIOA_PUPDR        (*(volatile uint32_t*)(GPIOA_BASE + 0x0C))
#define GPIOA_IDR          (*(volatile uint32_t*)(GPIOA_BASE + 0x10))
#define GPIOA_AFRL         (*(volatile uint32_t*)(GPIOA_BASE + 0x20))

/* --------- USART2 --------- */
#define USART2_SR          (*(volatile uint32_t*)(USART2_BASE + 0x00))
#define USART2_DR          (*(volatile uint32_t*)(USART2_BASE + 0x04))
#define USART2_BRR         (*(volatile uint32_t*)(USART2_BASE + 0x08))
#define USART2_CR1         (*(volatile uint32_t*)(USART2_BASE + 0x0C))
#define USART2_CR2         (*(volatile uint32_t*)(USART2_BASE + 0x10))
#define USART2_CR3         (*(volatile uint32_t*)(USART2_BASE + 0x14))

/* --------- Bits we use --------- */
#define USART_SR_TXE       (1U<<7)
#define USART_SR_TC        (1U<<6)
#define USART_CR1_UE       (1U<<13)
#define USART_CR1_TE       (1U<<3)
#define USART_CR1_RE       (1U<<2)

/* --------- User config --------- */
#define PCLK1_HZ  16000000UL   /* APB1 clock – keep 16MHz */
#define BAUD      115200UL

/* Baud helper: oversampling by 16, 8N1 */
static uint16_t usart_brr_from(uint32_t pclk, uint32_t baud)
{
    uint32_t div = 16U * baud;
    uint32_t mant = pclk / div;
    uint32_t rem  = pclk % div;
    uint32_t frac = (rem + baud/2U) / baud; /* round */
    if (frac >= 16U) { mant += 1U; frac = 0U; }
    return (uint16_t)((mant << 4) | (frac & 0xFU));
}

/* used for debounce */
static void delay_cycles(volatile uint32_t n) { while (n--) { __asm__ volatile ("nop"); } }

static void usart2_init(void)
{
    /* Clocks */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;

    /* GPIO: PA2 (TX), PA3 (RX) -> AF7 */
    GPIOA_MODER &= ~((3U<<(2*2)) | (3U<<(3*2)));     /* clear */
    GPIOA_MODER |=  ((2U<<(2*2)) | (2U<<(3*2)));     /* AF */
    GPIOA_OTYPER &= ~((1U<<2) | (1U<<3));            /* push-pull */
    GPIOA_OSPEEDR |= ((2U<<(2*2)) | (2U<<(3*2)));    /* high speed */
    GPIOA_PUPDR &= ~((3U<<(2*2)) | (3U<<(3*2)));     /* no pulls; RX has external line */
    GPIOA_AFRL  &= ~((0xFU<<(4*2)) | (0xFU<<(4*3)));
    GPIOA_AFRL  |=  ((7U  <<(4*2)) | (7U  <<(4*3))); /* AF7 */

    /* USART: 8N1, oversampling 16 */
    USART2_CR1 = 0;
    USART2_CR2 = 0;
    USART2_CR3 = 0;

    /* Baudrate */
    USART2_BRR = usart_brr_from(PCLK1_HZ, BAUD);    /* 16MHz & 115200 -> 0x008B */

    /* Enable TX/RX and USART */
    USART2_CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);
}

static void usart2_write_char(char c)
{
    while ((USART2_SR & USART_SR_TXE) == 0) { }
    USART2_DR = (uint8_t)c;
}

static void usart2_write_str(const char *s)
{
    while (*s) usart2_write_char(*s++);
    while ((USART2_SR & USART_SR_TC) == 0) { }  /* ensure last byte fully sent */
}

static void button_init_PA0_pullup(void)
{
    /* PA0 as input with internal pull-up */
    GPIOA_MODER  &= ~(3U << (0*2));        /* input mode */
    GPIOA_PUPDR  &= ~(3U << (0*2));
    GPIOA_PUPDR  |=  (1U << (0*2));        /* pull-up */
}

static int button_is_pressed(void)
{
    /* Active-low: pressed == 0 */
    return ((GPIOA_IDR & (1U<<0)) == 0);
}

/* --------- send line when PA0 pressed --------- */
int main(void)
{
    usart2_init();
    button_init_PA0_pullup();

    usart2_write_str("STM32F401 USART2 ready.\r\n");
    usart2_write_str("Press PA0 to send a line.\r\n");

    int last = 0;
    for (;;)
    {
        int now = button_is_pressed();
        if (now && !last) {
            /* crude debounce: tiny wait and confirm */
            delay_cycles(80000); /* ~5–10 ms @ 16 MHz, approximate */
            if (button_is_pressed()) {
                usart2_write_str("BTN: PA0 pressed\r\n");
                /* wait until released to avoid repeats */
                while (button_is_pressed()) { }
                delay_cycles(80000);
            }
        }
        last = now;
    }
}

