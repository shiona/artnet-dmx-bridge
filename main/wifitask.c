
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "common.h"

static const char *TAG = "wifi task";

#define WIFI_SSID                CONFIG_ESP_WIFI_SSID
#define WIFI_PASS                CONFIG_ESP_WIFI_PASSWORD
#define WIFI_CONN_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1


static int s_retry_num = 0;

extern enum state_ state;

static bool ap = false; // AP or STA
static bool connected = false;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ESP_LOGI(TAG, "WIFI EVENT");
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (ap) {
            ESP_LOGI(TAG, "AP started");
        } else {
            ESP_LOGI(TAG, "connecting");
            state = STATE_CONNECTING;
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED
            && connected) {
        if (s_retry_num < WIFI_CONN_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            connected = false;
            state = STATE_DISCONNECTED;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"failed to connect");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "connected, fetching ip");
        connected = true;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        state = STATE_IDLE;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_restart(void);


static void set_static_ip(esp_netif_t *sta)
{
    esp_netif_dhcpc_stop(sta);

    esp_netif_ip_info_t ip_info;

    // 192.168.50.229
    IP4_ADDR(&ip_info.ip, 192, 168, 50, 229);
    IP4_ADDR(&ip_info.gw, 192, 168, 50, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    esp_netif_set_ip_info(sta, &ip_info);
}


static void wifi_start(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    //esp_netif_create_default_wifi_sta();
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();

    set_static_ip(sta);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    //wifi_restart();
}

void wifi_restart(void)
{
    ESP_LOGI(TAG, "Stopping wifi\n");
    ESP_ERROR_CHECK(esp_wifi_stop());

    esp_netif_t* wifiAP = esp_netif_create_default_wifi_ap();

    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192,168,1,1);
    IP4_ADDR(&ipInfo.gw, 0,0,0,0); // do not advertise as a gateway router
    IP4_ADDR(&ipInfo.netmask, 255,255,255,0);
    esp_netif_dhcps_stop(wifiAP);
    esp_netif_set_ip_info(wifiAP, &ipInfo);
    esp_netif_dhcps_start(wifiAP);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "esp32artnet",
            .ssid_len = 11,
            .password = "MattiJaTeppo",
            .channel = 6,
            .max_connection = 3,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_LOGI(TAG, "Starting wifi as AP\n");
    ESP_ERROR_CHECK(esp_wifi_start());
}


void wifi_worker(void * bogus_param)
{
    while(true) {
        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
         * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "connected to ap SSID: %s", WIFI_SSID);
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to SSID: %s", WIFI_SSID);
            state = STATE_ERROR;
        } else {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
        }
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    /* The worker will never quit, so the event handlers are are not deregistered */
}

#define STACK_SIZE 2000
static StaticTask_t xTaskBuffer;
static StackType_t xStack[ STACK_SIZE ];
static TaskHandle_t task_handle = NULL;

void wifi_task_start(void)
{
    /* Start the connection forming */
    wifi_start();

    task_handle = xTaskCreateStatic(
                  wifi_worker,
                  "WiFi worker",
                  STACK_SIZE,
                  ( void * ) 0,
                  tskIDLE_PRIORITY,
                  xStack,
                  &xTaskBuffer
                  );
}

