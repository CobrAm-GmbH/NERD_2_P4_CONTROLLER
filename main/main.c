#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"

#include "lvgl.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

#include "ui.h"

#include "ethernet_manager.h"
#include "nerd_sensor_client.h"
#include "nerd_m31.h"



void app_main(void)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_90,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 1,
            .mirror_x = 1,
            .mirror_y = 0,
        },
    };

    /*
     * Display DSI, touch, LVGL task e relativo mutex.
     */
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    /*
     * Ethernet nativa P4 con IP statico.
     */
    ethernet_manager_start();

    /*
     * Client Modbus TCP asincrono per il modulo M31.
     */
    nerd_m31_start();

    /*
     * Creazione UI sotto lock LVGL.
     */
    bsp_display_lock(-1);

    ui_init();
	


    bsp_display_unlock();
	
	

    /*
     * Polling HTTP del Sensor Node.
     */
    nerd_sensor_client_start();

    while(true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}