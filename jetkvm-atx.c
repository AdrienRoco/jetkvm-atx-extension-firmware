#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#define UART_ID uart0
#define BAUD_RATE 115200

#define UART_TX_PIN 16
#define UART_RX_PIN 17

// Outputs to motherboard
#define MB_BTN_RST_PIN 18
#define MB_BTN_PWR_PIN 19

// Inputs from motherboard LEDs
#define MB_LED_HDD_PIN 20
#define MB_LED_PWR_PIN 21

// Inputs from case buttons
#define CASE_BTN_RST_PIN 22
#define CASE_BTN_PWR_PIN 26

// Outputs to case LEDs
#define CASE_LED_HDD_PIN 27
#define CASE_LED_PWR_PIN 28

// Built-in LED
#define BUILTIN_LED_PIN 25

#define UART_BUF_SIZE 128
static char uart_buf[UART_BUF_SIZE];
static int uart_buf_pos = 0;

// Button state tracking for debouncing
static bool case_btn_rst_last = false;
static bool case_btn_pwr_last = false;
static uint64_t case_btn_rst_last_change = 0;
static uint64_t case_btn_pwr_last_change = 0;
#define DEBOUNCE_TIME_US 50000 // 50ms debounce

void on_uart_line(const char *line)
{
    printf("UART LINE: %s\n", line);
    if (strcmp(line, "BTN_RST_ON\n") == 0)
    {
        gpio_put(MB_BTN_RST_PIN, 1);
    }
    else if (strcmp(line, "BTN_RST_OFF\n") == 0)
    {
        gpio_put(MB_BTN_RST_PIN, 0);
    }
    else if (strcmp(line, "BTN_PWR_ON\n") == 0)
    {
        gpio_put(MB_BTN_PWR_PIN, 1);
        gpio_put(BUILTIN_LED_PIN, 1); // Turn on LED when power button pressed via UART
    }
    else if (strcmp(line, "BTN_PWR_OFF\n") == 0)
    {
        gpio_put(MB_BTN_PWR_PIN, 0);
        gpio_put(BUILTIN_LED_PIN, 0); // Turn off LED when power button released via UART
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

    // Setup UART
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

    // Setup motherboard button outputs
    gpio_init(MB_BTN_RST_PIN);
    gpio_set_dir(MB_BTN_RST_PIN, GPIO_OUT);
    gpio_put(MB_BTN_RST_PIN, 0);

    gpio_init(MB_BTN_PWR_PIN);
    gpio_set_dir(MB_BTN_PWR_PIN, GPIO_OUT);
    gpio_put(MB_BTN_PWR_PIN, 0);

    // Setup motherboard LED inputs
    gpio_init(MB_LED_HDD_PIN);
    gpio_pull_up(MB_LED_HDD_PIN);
    gpio_set_dir(MB_LED_HDD_PIN, GPIO_IN);

    gpio_init(MB_LED_PWR_PIN);
    gpio_pull_up(MB_LED_PWR_PIN);
    gpio_set_dir(MB_LED_PWR_PIN, GPIO_IN);

    // Setup case button inputs
    gpio_init(CASE_BTN_RST_PIN);
    gpio_pull_up(CASE_BTN_RST_PIN);
    gpio_set_dir(CASE_BTN_RST_PIN, GPIO_IN);

    gpio_init(CASE_BTN_PWR_PIN);
    gpio_pull_up(CASE_BTN_PWR_PIN);
    gpio_set_dir(CASE_BTN_PWR_PIN, GPIO_IN);

    // Setup case LED outputs
    gpio_init(CASE_LED_HDD_PIN);
    gpio_set_dir(CASE_LED_HDD_PIN, GPIO_OUT);
    gpio_put(CASE_LED_HDD_PIN, 0);

    gpio_init(CASE_LED_PWR_PIN);
    gpio_set_dir(CASE_LED_PWR_PIN, GPIO_OUT);
    gpio_put(CASE_LED_PWR_PIN, 0);

    // Setup built-in LED
    gpio_init(BUILTIN_LED_PIN);
    gpio_set_dir(BUILTIN_LED_PIN, GPIO_OUT);
    gpio_put(BUILTIN_LED_PIN, 1); // Turn on LED at startup

    printf("Built-in LED turned on for 5 seconds...\n");
    sleep_ms(5000);               // Keep LED on for 5 seconds
    gpio_put(BUILTIN_LED_PIN, 0); // Turn off LED
    printf("Built-in LED turned off\n");

    // State tracking
    bool btn_rst_state = false;
    bool btn_pwr_state = false;
    bool led_hdd_state = false;
    bool led_pwr_state = false;
    uint64_t last_update_sent = 0;
    uint64_t last_watchdog_reset = 0;
    char message[6];

    while (true)
    {
        uint64_t now = time_us_64();

        // Read motherboard LED states
        bool new_led_hdd_state = !gpio_get(MB_LED_HDD_PIN); // Inverted because of pull-up
        bool new_led_pwr_state = !gpio_get(MB_LED_PWR_PIN); // Inverted because of pull-up
        bool new_btn_rst_state = gpio_get(MB_BTN_RST_PIN);
        bool new_btn_pwr_state = gpio_get(MB_BTN_PWR_PIN);

        // Pass-through LEDs to case
        gpio_put(CASE_LED_HDD_PIN, new_led_hdd_state);
        gpio_put(CASE_LED_PWR_PIN, new_led_pwr_state);

        // Read case button states with debouncing
        bool case_btn_rst_current = !gpio_get(CASE_BTN_RST_PIN); // Inverted because of pull-up
        bool case_btn_pwr_current = !gpio_get(CASE_BTN_PWR_PIN); // Inverted because of pull-up

        // Handle reset button
        if (case_btn_rst_current != case_btn_rst_last)
        {
            case_btn_rst_last_change = now;
            case_btn_rst_last = case_btn_rst_current;
        }
        else if (now - case_btn_rst_last_change > DEBOUNCE_TIME_US)
        {
            // Button state is stable, pass it through
            gpio_put(MB_BTN_RST_PIN, case_btn_rst_current);
        }

        // Handle power button
        if (case_btn_pwr_current != case_btn_pwr_last)
        {
            case_btn_pwr_last_change = now;
            case_btn_pwr_last = case_btn_pwr_current;
        }
        else if (now - case_btn_pwr_last_change > DEBOUNCE_TIME_US)
        {
            // Button state is stable, pass it through
            gpio_put(MB_BTN_PWR_PIN, case_btn_pwr_current);
            // Turn LED on/off based on power button state
            gpio_put(BUILTIN_LED_PIN, case_btn_pwr_current);
        }

        bool states_changed = new_led_hdd_state != led_hdd_state ||
                              new_led_pwr_state != led_pwr_state ||
                              new_btn_rst_state != btn_rst_state ||
                              new_btn_pwr_state != btn_pwr_state;

        // Send updates to KVM (if states changed or 1000ms passed)
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

        // Reset watchdog
        if (now - last_watchdog_reset > 1000000)
        {
            watchdog_update();
            last_watchdog_reset = now;
        }

        sleep_ms(10);
    }
}
