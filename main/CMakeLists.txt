set(LV_DEMO_DIR ../managed_components/lvgl__lvgl/demos)
file(GLOB_RECURSE LV_DEMOS_SOURCES ${LV_DEMO_DIR}/*.c)

idf_component_register(
    SRCS
        main.c
        ethernet_manager.c
        nerd_sensor_client.c
        ${LV_DEMOS_SOURCES}

    INCLUDE_DIRS
        .
        ${LV_DEMO_DIR}

    REQUIRES
        lvgl
        esp_driver_tsens
        nvs_flash
        waveshare__esp32_p4_platform
        bsp_extra
        ui
        ethernet_init
        esp_netif
        esp_event
        esp_eth
        esp_http_client
        json
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)

target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)