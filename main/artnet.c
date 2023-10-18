
// Assumes a full UDP message is passed as one
//

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include "esp_log.h"
#include "common.h"
#include "dmxtask.h"

#include "driver/gpio.h"

#define ARTNET_MAGIC_HEADER "Art-Net\0"
#define ARTNET_MAGIC_HEADER_LEN 8

#define PORT 6454

#define ARTNET_DEBUG_PIN           GPIO_NUM_22

// Nice to have, sync packet latches new data
//static boolean synchronous = false;

static const char *TAG = "ART-NET";

extern enum state_ state;

static uint8_t *lights = NULL;

void init(uint8_t *light_data_buf)
{
    lights = light_data_buf;
    gpio_set_direction(ARTNET_DEBUG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(ARTNET_DEBUG_PIN, 0);
}

// TODO: respond to poll
bool handle_artnet(uint8_t *artnet_buf, size_t artnet_buf_len)
{
    if (artnet_buf_len < 13)
    {
        ESP_LOGW(TAG, "packet too short");
        // Shortest packet is probably ArtPoll
        return false;
    }

    if (memcmp(artnet_buf, ARTNET_MAGIC_HEADER, ARTNET_MAGIC_HEADER_LEN) != 0)
    {
        ESP_LOGW(TAG, "incorrect magic value");
        // Not an artnet packet
        return false;
    }

    uint16_t opcode = artnet_buf[8] | artnet_buf[9] << 8;
    uint16_t protver = artnet_buf[10] << 8 | artnet_buf[11];

    if (protver != 14)
    {
        ESP_LOGW(TAG, "Protocol version is not 14");
        return false;
    }

    if (opcode == 0x5000)
    {
        //uint8_t seq = (uint8_t)artnet_buf[12];
        //uint8_t phys = (uint8_t)artnet_buf[13];
        uint16_t universe = 0x7fff & (artnet_buf[14] | artnet_buf[15] << 8);
        uint16_t datalen = artnet_buf[16] << 8 | artnet_buf[17];

        if (datalen != artnet_buf_len - 18)
        {
            // Header is 18 bytes
            ESP_LOGW(TAG, "packet content length does not match header data");
            return false;
        }

        // TODO: Configurable universe
        if (universe == 0)
        {
            //ESP_LOGI(TAG, "1-1: %d", artnet_buf[18]);

            dmx_write_multiple(1, &artnet_buf[18], datalen);
            //memcpy(get_dmx_buf(), &artnet_buf[18], datalen);
        }
    }
    else
    {
        ESP_LOGD(TAG, "Unknown packet opcode %04x", opcode);
    }
    return true;
};

static void artnet_worker(void *pvParameters)
{
    // IPv4 isn't too long for this
    char addr_str[32];
    uint8_t rx_buffer[2048];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    struct sockaddr_storage dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);
    ip_protocol = IPPROTO_IP;

    int listen_sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        state = STATE_ERROR;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        state = STATE_ERROR;
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    while (1) {

        //ESP_LOGI(TAG, "Waiting for UDP data");

        assert(state == STATE_IDLE);

        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);

        int recv_len = recvfrom(listen_sock, rx_buffer, sizeof(rx_buffer) -1, 0,
                (struct sockaddr*) &source_addr, &addr_len);

        gpio_set_level(ARTNET_DEBUG_PIN, 1);
        if (recv_len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        }


        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGD(TAG, "data from address: %s", addr_str);

        handle_artnet(rx_buffer, recv_len);
        gpio_set_level(ARTNET_DEBUG_PIN, 0);

        //shutdown(listen_sock, 0);
        //close(listen_sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

#define STACK_SIZE 6000
static StaticTask_t xTaskBuffer;
static StackType_t xStack[ STACK_SIZE ];
static TaskHandle_t task_handle = NULL;

void artnet_task_start(void)
{
    init(NULL);
    while(state != STATE_IDLE) {
        /* Wait for wifi task to connect to a network */
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    task_handle = xTaskCreateStaticPinnedToCore(
            artnet_worker,
            "arnet",
            STACK_SIZE,
            (void*) 0,
            tskIDLE_PRIORITY,
            xStack,
            &xTaskBuffer,
            0);
}
