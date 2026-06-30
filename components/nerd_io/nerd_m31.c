#include "nerd_m31.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

#define M31_IP_ADDRESS      "192.168.0.90"
#define M31_TCP_PORT        502
#define M31_UNIT_ID         1

#define M31_QUEUE_LENGTH    16
#define M31_TASK_STACK      4096
#define M31_TASK_PRIORITY   5
#define M31_SOCKET_TIMEOUT_MS 1500
#define M31_RETRY_COUNT     3

typedef struct
{
    uint16_t coil_address;
    bool on;
} m31_command_t;

static const char * TAG = "NERD_M31";

static QueueHandle_t s_command_queue = NULL;
static TaskHandle_t s_worker_task = NULL;
static uint16_t s_transaction_id = 1;

static int m31_connect(void)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if(sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        return -1;
    }

    struct timeval timeout = {
        .tv_sec = M31_SOCKET_TIMEOUT_MS / 1000,
        .tv_usec = (M31_SOCKET_TIMEOUT_MS % 1000) * 1000,
    };

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in destination = {
        .sin_family = AF_INET,
        .sin_port = htons(M31_TCP_PORT),
    };

    if(inet_pton(AF_INET, M31_IP_ADDRESS, &destination.sin_addr) != 1) {
        ESP_LOGE(TAG, "Invalid M31 IP address");
        close(sock);
        return -1;
    }

    if(connect(sock, (struct sockaddr *)&destination, sizeof(destination)) != 0) {
        ESP_LOGW(TAG, "Connection to %s:%d failed: errno=%d",
                 M31_IP_ADDRESS, M31_TCP_PORT, errno);
        close(sock);
        return -1;
    }

    ESP_LOGI(TAG, "Connected to M31 at %s:%d", M31_IP_ADDRESS, M31_TCP_PORT);
    return sock;
}

static bool send_all(int sock, const uint8_t * data, size_t length)
{
    size_t sent_total = 0;

    while(sent_total < length) {
        int sent = send(sock, data + sent_total, length - sent_total, 0);

        if(sent <= 0) {
            ESP_LOGW(TAG, "send() failed: errno=%d", errno);
            return false;
        }

        sent_total += (size_t)sent;
    }

    return true;
}

static bool recv_all(int sock, uint8_t * data, size_t length)
{
    size_t received_total = 0;

    while(received_total < length) {
        int received = recv(sock, data + received_total,
                            length - received_total, 0);

        if(received <= 0) {
            ESP_LOGW(TAG, "recv() failed or connection closed: errno=%d", errno);
            return false;
        }

        received_total += (size_t)received;
    }

    return true;
}

static bool m31_write_single_coil(int sock, uint16_t coil_address, bool on)
{
    uint16_t transaction_id = s_transaction_id++;

    if(s_transaction_id == 0)
        s_transaction_id = 1;

    uint16_t coil_value = on ? 0xFF00 : 0x0000;

    uint8_t request[12] = {
        (uint8_t)(transaction_id >> 8),
        (uint8_t)(transaction_id & 0xFF),
        0x00, 0x00,             // Protocol ID = Modbus
        0x00, 0x06,             // Remaining bytes
        M31_UNIT_ID,
        0x05,                   // Write Single Coil
        (uint8_t)(coil_address >> 8),
        (uint8_t)(coil_address & 0xFF),
        (uint8_t)(coil_value >> 8),
        (uint8_t)(coil_value & 0xFF),
    };

    uint8_t response[12] = {0};

    if(!send_all(sock, request, sizeof(request)))
        return false;

    if(!recv_all(sock, response, sizeof(response)))
        return false;

    if(memcmp(request, response, sizeof(request)) != 0) {
        ESP_LOGE(TAG, "Invalid Modbus response for coil %u",
                 (unsigned)coil_address);
        return false;
    }

    ESP_LOGI(TAG, "DO%u / coil %u -> %s",
             (unsigned)(coil_address + 1),
             (unsigned)coil_address,
             on ? "ON" : "OFF");

    return true;
}

static void m31_worker_task(void * argument)
{
    (void)argument;

    int sock = -1;
    m31_command_t command;

    while(true) {
        if(xQueueReceive(s_command_queue, &command, portMAX_DELAY) != pdTRUE)
            continue;

        bool completed = false;

        for(int attempt = 1; attempt <= M31_RETRY_COUNT && !completed; attempt++) {
            if(sock < 0)
                sock = m31_connect();

            if(sock >= 0)
                completed = m31_write_single_coil(
                    sock,
                    command.coil_address,
                    command.on
                );

            if(!completed) {
                if(sock >= 0) {
                    close(sock);
                    sock = -1;
                }

                ESP_LOGW(TAG,
                         "Retry %d/%d for coil %u -> %s",
                         attempt,
                         M31_RETRY_COUNT,
                         (unsigned)command.coil_address,
                         command.on ? "ON" : "OFF");

                vTaskDelay(pdMS_TO_TICKS(300));
            }
        }

        if(!completed) {
            ESP_LOGE(TAG,
                     "Command failed: coil %u -> %s",
                     (unsigned)command.coil_address,
                     command.on ? "ON" : "OFF");
        }
    }
}

void nerd_m31_start(void)
{
    if(s_worker_task)
        return;

    s_command_queue = xQueueCreate(
        M31_QUEUE_LENGTH,
        sizeof(m31_command_t)
    );

    if(!s_command_queue) {
        ESP_LOGE(TAG, "Could not create command queue");
        return;
    }

    BaseType_t created = xTaskCreate(
        m31_worker_task,
        "m31_modbus",
        M31_TASK_STACK,
        NULL,
        M31_TASK_PRIORITY,
        &s_worker_task
    );

    if(created != pdPASS) {
        ESP_LOGE(TAG, "Could not create M31 worker task");
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        s_worker_task = NULL;
        return;
    }

    ESP_LOGI(TAG, "M31 Modbus TCP worker started");
}

bool nerd_m31_write_coil(uint16_t coil_address, bool on)
{
    if(!s_command_queue) {
        ESP_LOGE(TAG, "M31 client not started");
        return false;
    }

    m31_command_t command = {
        .coil_address = coil_address,
        .on = on,
    };

    if(xQueueSend(s_command_queue, &command, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Command queue full");
        return false;
    }

    return true;
}
