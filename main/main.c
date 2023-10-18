#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"

#include "common.h"
#include "artnet.h"
#include "dmxtask.h"
#include "wifitask.h"
#include "servertask.h"

enum state_ state;

void app_main(void)
{
    state = STATE_STARTING;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    dmx_task_start();
    wifi_task_start();
    //server_task_start();
    artnet_task_start();

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
