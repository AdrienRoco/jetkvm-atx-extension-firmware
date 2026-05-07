#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#define UART_ID uart0
#define BAUD_RATE 115200

#define UART_TX_PIN      0   // physical pin 1
#define UART_RX_PIN      1   // physical pin 2
#define MB_BTN_RST_PIN   2   // physical pin 4
#define MB_BTN_PWR_PIN   3   // physical pin 5
#define MB_LED_HDD_PIN   4   // physical pin 6
#define MB_LED_PWR_PIN   5   // physical pin 7
#define CASE_BTN_RST_PIN 6   // physical pin 9
#define CASE_BTN_PWR_PIN 7   // physical pin 10
#define CASE_LED_HDD_PIN 8   // physical pin 11
#define CASE_LED_PWR_PIN 10  // physical pin 14

#define UART_BUF_SIZE 128
static char uart_buf[UART_BUF_SIZE];
static int uart_buf_pos = 0;
static volatile bool btn_command_received = false;
static volatile bool kvm_rst_pressed = false;
static volatile bool kvm_pwr_pressed = false;

void on_uart_line(const char *line)
{
    printf("UART LINE: %s\n", line);
    if (strcmp(line, "BTN_RST_ON\n") == 0)
    {
        kvm_rst_pressed = true;
        btn_command_received = true;
    }
    else if (strcmp(line, "BTN_RST_OFF\n") == 0)
    {
        kvm_rst_pressed = false;
        btn_command_received = true;
    }
    else if (strcmp(line, "BTN_PWR_ON\n") == 0)
    {
        kvm_pwr_pressed = true;
        btn_command_received = true;
    }
    else if (strcmp(line, "BTN_PWR_OFF\n") == 0)
    {
        kvm_pwr_pressed = false;
        btn_command_received = true;
    }
}

void on_uart_rx()
{
    while (uart_is_readable(UART_ID))
    {
        uint8_t ch = uart_getc(UART_ID);

        if (uart_buf_pos < UART_BUF_SIZE - 1)
        {
            uart_buf[uart_buf_pos++] = ch;
        }

        if (ch == '\n' || uart_buf_pos >= UART_BUF_SIZE - 1)
        {
            uart_buf[uart_buf_pos] = '\0';
            on_uart_line(uart_buf);
            uart_buf_pos = 0;
        }
    }
}

int main()
{
    stdio_init_all();

    if (watchdog_caused_reboot())
    {
        printf("Rebooted by Watchdog!\n");
    }

    watchdog_enable(8388, true);

    gpio_set_function(UART_TX_PIN, UART_FUNCSEL_NUM(UART_ID, UART_TX_PIN));
    gpio_set_function(UART_RX_PIN, UART_FUNCSEL_NUM(UART_ID, UART_RX_PIN));

    uart_init(UART_ID, BAUD_RATE);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    gpio_init(MB_BTN_RST_PIN);
    gpio_set_dir(MB_BTN_RST_PIN, GPIO_OUT);
    gpio_put(MB_BTN_RST_PIN, 1);

    gpio_init(MB_BTN_PWR_PIN);
    gpio_set_dir(MB_BTN_PWR_PIN, GPIO_OUT);
    gpio_put(MB_BTN_PWR_PIN, 1);

    gpio_init(MB_LED_HDD_PIN);
    gpio_pull_up(MB_LED_HDD_PIN);
    gpio_set_dir(MB_LED_HDD_PIN, GPIO_IN);

    gpio_init(MB_LED_PWR_PIN);
    gpio_pull_up(MB_LED_PWR_PIN);
    gpio_set_dir(MB_LED_PWR_PIN, GPIO_IN);

    gpio_init(CASE_BTN_RST_PIN);
    gpio_pull_up(CASE_BTN_RST_PIN);
    gpio_set_dir(CASE_BTN_RST_PIN, GPIO_IN);

    gpio_init(CASE_BTN_PWR_PIN);
    gpio_pull_up(CASE_BTN_PWR_PIN);
    gpio_set_dir(CASE_BTN_PWR_PIN, GPIO_IN);

    gpio_init(CASE_LED_HDD_PIN);
    gpio_set_dir(CASE_LED_HDD_PIN, GPIO_OUT);
    gpio_put(CASE_LED_HDD_PIN, 1);

    gpio_init(CASE_LED_PWR_PIN);
    gpio_set_dir(CASE_LED_PWR_PIN, GPIO_OUT);
    gpio_put(CASE_LED_PWR_PIN, 1);

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif

    bool btn_rst_state = false;
    bool btn_pwr_state = false;
    bool led_hdd_state = false;
    bool led_pwr_state = false;
    uint64_t last_update_sent = 0;
    uint64_t last_watchdog_reset = 0;
    uint64_t builtin_led_off_at = 0;
    char message[6];
    while (true)
    {
        uint64_t now = time_us_64();

#ifdef PICO_DEFAULT_LED_PIN
        if (btn_command_received)
        {
            btn_command_received = false;
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            builtin_led_off_at = now + 200000;
        }

        if (builtin_led_off_at != 0 && now >= builtin_led_off_at)
        {
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            builtin_led_off_at = 0;
        }
#endif

        bool mb_led_hdd_level = gpio_get(MB_LED_HDD_PIN);
        bool mb_led_pwr_level = gpio_get(MB_LED_PWR_PIN);
        bool case_rst_pressed = !gpio_get(CASE_BTN_RST_PIN);
        bool case_pwr_pressed = !gpio_get(CASE_BTN_PWR_PIN);

        bool mb_rst_pressed = kvm_rst_pressed || case_rst_pressed;
        bool mb_pwr_pressed = kvm_pwr_pressed || case_pwr_pressed;

        gpio_put(MB_BTN_RST_PIN, mb_rst_pressed ? 0 : 1);
        gpio_put(MB_BTN_PWR_PIN, mb_pwr_pressed ? 0 : 1);

        gpio_put(CASE_LED_HDD_PIN, mb_led_hdd_level);
        gpio_put(CASE_LED_PWR_PIN, mb_led_pwr_level);

        bool new_led_hdd_state = !mb_led_hdd_level;
        bool new_led_pwr_state = !mb_led_pwr_level;
        bool new_btn_rst_state = gpio_get(MB_BTN_RST_PIN);
        bool new_btn_pwr_state = gpio_get(MB_BTN_PWR_PIN);

        // printf("LED_HDD_PIN: %d\n", new_led_hdd_state);
        // printf("LED_PWR_PIN: %d\n", new_led_pwr_state);

        bool states_changed = new_led_hdd_state != led_hdd_state ||
                              new_led_pwr_state != led_pwr_state ||
                              new_btn_rst_state != btn_rst_state ||
                              new_btn_pwr_state != btn_pwr_state;

        // if states changed or 1000ms passed since last update
        if (states_changed || (now - last_update_sent > 1000000))
        {
            snprintf(message, 6, "%d%d%d%d\n", new_led_hdd_state, new_led_pwr_state, new_btn_rst_state, new_btn_pwr_state);
            uart_puts(UART_ID, message);
            last_update_sent = now;
            printf("Sent at %llu: %s", now, message);

            led_hdd_state = new_led_hdd_state;
            led_pwr_state = new_led_pwr_state;
            btn_rst_state = new_btn_rst_state;
            btn_pwr_state = new_btn_pwr_state;
        }

        if (now - last_watchdog_reset > 1000000)
        {
            watchdog_update();
            last_watchdog_reset = now;
        }

        sleep_ms(10);

        // uart_puts(UART_ID, "Hello, UART!\n");
    }
}