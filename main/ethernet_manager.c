#include "ethernet_manager.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_eth.h"

#include "lwip/ip4_addr.h"

#include "ethernet_init.h"

static const char *TAG = "NERD_ETH";

static bool s_eth_ready = false;
static esp_eth_handle_t *s_eth_handles = NULL;
static uint8_t s_eth_count = 0;
static esp_netif_t *s_eth_netif = NULL;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if(event_base == ETH_EVENT) {
        switch(event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet link connected");
                break;

            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet link disconnected");
                s_eth_ready = false;
                break;

            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet started");
                break;

            case ETHERNET_EVENT_STOP:
                ESP_LOGW(TAG, "Ethernet stopped");
                s_eth_ready = false;
                break;

            default:
                break;
        }
    }
}

static void set_static_ip(esp_netif_t *netif)
{
    esp_netif_ip_info_t ip_info;

    IP4_ADDR(&ip_info.ip, 192, 168, 0, 80);
    IP4_ADDR(&ip_info.gw, 192, 168, 0, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));

    ESP_LOGI(TAG, "Static IP configured: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
}

void ethernet_manager_start(void)
{
    ESP_LOGI(TAG, "Starting Ethernet manager");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(
        ETH_EVENT,
        ESP_EVENT_ANY_ID,
        eth_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(example_eth_init(&s_eth_handles, &s_eth_count));

    ESP_LOGI(TAG, "Ethernet interfaces initialized: %d", s_eth_count);

    if(s_eth_count < 1) {
        ESP_LOGE(TAG, "No Ethernet interface found");
        return;
    }

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(s_eth_netif == NULL ? ESP_FAIL : ESP_OK);

    ESP_ERROR_CHECK(esp_netif_attach(
        s_eth_netif,
        esp_eth_new_netif_glue(s_eth_handles[0])
    ));

    set_static_ip(s_eth_netif);

    ESP_ERROR_CHECK(esp_eth_start(s_eth_handles[0]));

    s_eth_ready = true;

    ESP_LOGI(TAG, "Ethernet manager ready");
}

bool ethernet_manager_is_ready(void)
{
    return s_eth_ready;
}