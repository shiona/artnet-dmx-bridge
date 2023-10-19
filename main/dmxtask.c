#include <string.h>

#include "esp_log.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/semphr.h"

static const char *TAG = "DMX task";

#define DMX_SERIAL_INPUT_PIN    GPIO_NUM_16 // pin for dmx rx
#define DMX_SERIAL_OUTPUT_PIN   GPIO_NUM_17 // pin for dmx tx
#define DMX_SERIAL_IO_PIN       GPIO_NUM_4  // pin for dmx rx/tx change

#define DMX_UART_NUM            UART_NUM_2  // dmx uart

#define DMX_DEBUG_PIN           GPIO_NUM_23

#define BUF_SIZE                1024        //  buffer size for rx events

#define DMX_UPDATE_SPEED (50)

static void setup_uart(void)
{
    uart_config_t uart_config = {
        // from https://github.com/luksal/ESP32-DMX/blob/master/src/dmx.cpp
        .baud_rate = 250000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(DMX_UART_NUM, &uart_config));

    // Setup UART buffered IO with event queue
    const int uart_buffer_size = (1024 * 2);
    QueueHandle_t uart_queue;
    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, uart_buffer_size, \
                                            uart_buffer_size, 10, &uart_queue, 0));

    // Set pins for UART
    uart_set_pin(DMX_UART_NUM, DMX_SERIAL_OUTPUT_PIN, DMX_SERIAL_INPUT_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // install queue
    //uart_driver_install(DMX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &dmx_rx_queue, 0);
    //uart_driver_install(DMX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, NULL, 0);

    // create mutex for syncronisation
    //sync_dmx = xSemaphoreCreateMutex();

    // set gpio for direction
    gpio_pad_select_gpio(DMX_SERIAL_IO_PIN);
    gpio_set_direction(DMX_SERIAL_IO_PIN, GPIO_MODE_OUTPUT);

    gpio_set_level(DMX_SERIAL_IO_PIN, 1);

    gpio_set_direction(DMX_DEBUG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DMX_DEBUG_PIN, 0);
}

static uint8_t dmx_buffer[2][513] = { 0 }; // first byte has to always be 0,
                                           // rest are the 512 channels 1-512
static size_t dmx_transmit_size = 513;
static size_t dmx_visible_layer = 0;
static bool   dmx_swap_request = false;

static SemaphoreHandle_t dmx_update_semaphore = NULL;
static StaticSemaphore_t dmx_update_mutex_buffer;

static portMUX_TYPE dmx_transmit_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void dmx_worker(void *bogus_param)
{
    setup_uart();
    dmx_update_semaphore = xSemaphoreCreateMutexStatic( &dmx_update_mutex_buffer );

    xSemaphoreGive(dmx_update_semaphore);

    while(true) {
        //dmx_buffer[101]++; // Overeflow ok
        //uart_write_bytes_with_break(DMX_UART_NUM, dmx_buffer, dmx_transmit_size , 100);

        //ESP_LOGI(TAG, "sending: %d", dmx_buffer[101]);
        //    uint8_t start_code = 0x00;

        if (xSemaphoreTake(dmx_update_semaphore, portMAX_DELAY) != pdTRUE)
        {
            ESP_LOGW(TAG, "Unable to take semaphore at buffer swap");
            //continue;
        }

        if(dmx_swap_request)
        {
            dmx_visible_layer ^= 0x01;
            dmx_swap_request = false;
        }
        xSemaphoreGive(dmx_update_semaphore);

        // wait till uart is ready
        if (uart_wait_tx_done(DMX_UART_NUM, 1000) != ESP_OK) {
            ESP_LOGW(TAG, "TX not done yet");
            continue;
        }
        // set line to inverse, creates break signal
        dmx_buffer[dmx_visible_layer][0] = 0x00;

        taskENTER_CRITICAL(&dmx_transmit_spinlock);
        uart_set_line_inverse(DMX_UART_NUM, UART_SIGNAL_TXD_INV);
        // wait break time
        //ets_delay_us(104);
        ets_delay_us(184);
        // disable break signal
        uart_set_line_inverse(DMX_UART_NUM,  0);
        // wait mark after break
        // TODO: Use freertos wait
        //ets_delay_us(4);
        ets_delay_us(24);
        // write start code
        //uart_write_bytes(DMX_UART_NUM, (const char*) &start_code, 1);
        // transmit the dmx data
        //uart_write_bytes(DMX_UART_NUM, (const char*) dmx_buffer[dmx_visible_layer]+1, 512);
//critical section
        //gpio_set_level(DMX_DEBUG_PIN, 1);
        uart_write_bytes(DMX_UART_NUM,  dmx_buffer[dmx_visible_layer], dmx_transmit_size);
        taskEXIT_CRITICAL(&dmx_transmit_spinlock);
        //gpio_set_level(DMX_DEBUG_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(DMX_UPDATE_SPEED));
    }
}

void dmx_write(size_t channel, uint8_t value)
{
    if (channel < 1 || channel > 512)
    {
        ESP_LOGW(TAG, "Write request to invalid DMX channel %d", channel);
        return;
    }
    if (xSemaphoreTake(dmx_update_semaphore, portMAX_DELAY) == pdTRUE)
    {
        dmx_buffer[0x01 ^ dmx_visible_layer][channel] = value;
        dmx_swap_request = true;
        xSemaphoreGive(dmx_update_semaphore);
    }
    else
    {
        ESP_LOGW(TAG, "Unable to take semaphore at dmx_write");
    }

}

void dmx_write_multiple(size_t first, uint8_t* values, size_t count)
{
    assert(first + count <= 513);
    if (xSemaphoreTake(dmx_update_semaphore, portMAX_DELAY) == pdTRUE)
    {
        memcpy(&dmx_buffer[0x01 ^dmx_visible_layer][first], values, count);
        dmx_swap_request = true;
        xSemaphoreGive(dmx_update_semaphore);
    }
    else
    {
        ESP_LOGW(TAG, "Unable to take semaphore at dmx_write_multiple");
    }
}

// TODO: Write multiple channels in succession making sure data is not being sent in between

#define STACK_SIZE 2000
static StaticTask_t xTaskBuffer;
static StackType_t xStack[ STACK_SIZE ];
static TaskHandle_t task_handle = NULL;

void dmx_task_start(void)
{
    task_handle = xTaskCreateStaticPinnedToCore(
                  dmx_worker,
                  "DMX worker",
                  STACK_SIZE,
                  (void *) 0,
                  //tskIDLE_PRIORITY,
                  //4,
                  configMAX_PRIORITIES - 1,
                  xStack,
                  &xTaskBuffer,
                  1
                  );
}
