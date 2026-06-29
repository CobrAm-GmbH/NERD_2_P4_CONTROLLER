#include "nerd_sensor_client.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"

#include "cJSON.h"

#include "bsp/esp-bsp.h"

#include "ui_logic.h"


#define SENSOR_NODE_STATUS_URL       "http://192.168.0.81/status"
#define SENSOR_POLL_INTERVAL_MS      1000
#define SENSOR_HTTP_TIMEOUT_MS       1000
#define SENSOR_RESPONSE_BUFFER_SIZE  512
#define SENSOR_OFFLINE_THRESHOLD     3

#define TDS_MAX_VOLTAGE              2.3f
#define TDS_MAX_PPM                  999.0f

/*
 * Fattore di calibrazione finale.
 *
 * 1.000 = nessuna correzione
 * 0.950 = riduce la lettura del 5%
 * 1.050 = aumenta la lettura del 5%
 */
#define TDS_CORRECTION_FACTOR        1.000f


/*
 * Sensore pressione:
 * 0...5 V = 0...100 bar
 *
 * Partitore:
 * uscita sensore -> 10 kΩ -> GPIO
 * GPIO -> 22 kΩ -> GND
 */
#define PRESSURE_SENSOR_MAX_VOLTAGE      5.0f
#define PRESSURE_SENSOR_MAX_BAR        100.0f

#define PRESSURE_DIVIDER_TOP_OHM     10000.0f
#define PRESSURE_DIVIDER_BOTTOM_OHM  22000.0f

/*
 * Fattore di calibrazione finale.
 *
 * 1.000 = nessuna correzione
 * 0.950 = riduce la lettura del 5%
 * 1.050 = aumenta la lettura del 5%
 */
#define PRESSURE_CORRECTION_FACTOR       1.077f


static const char *TAG = "NERD_SENSOR_CLIENT";


typedef struct
{
    char data[SENSOR_RESPONSE_BUFFER_SIZE];
    size_t length;
} http_response_buffer_t;


/**
 * Conversione iniziale volutamente lineare:
 *
 * 0.0 V -> 0 ppm
 * 2.3 V -> 999 ppm
 */
static int tds_voltage_to_ppm(float voltage)
{
    if(voltage <= 0.0f)
        return 0;

    float ppm =
    (voltage / TDS_MAX_VOLTAGE)
    * TDS_MAX_PPM
    * TDS_CORRECTION_FACTOR;

    if(ppm < 0.0f)
        ppm = 0.0f;

    if(ppm > TDS_MAX_PPM)
        ppm = TDS_MAX_PPM;

    return (int)(ppm + 0.5f);
}

/**
 * Converte la tensione letta sul GPIO nella pressione reale.
 *
 * Il Sensor Node invia pressure_voltage, cioè la tensione
 * già ridotta dal partitore e presente sul GPIO.
 */
static float pressure_voltage_to_bar(float gpio_voltage)
{
    if(gpio_voltage <= 0.0f)
        return 0.0f;

    const float divider_ratio =
        PRESSURE_DIVIDER_BOTTOM_OHM /
        (PRESSURE_DIVIDER_TOP_OHM + PRESSURE_DIVIDER_BOTTOM_OHM);

    float sensor_voltage = gpio_voltage / divider_ratio;

    float pressure_bar =
        (sensor_voltage / PRESSURE_SENSOR_MAX_VOLTAGE)
        * PRESSURE_SENSOR_MAX_BAR
        * PRESSURE_CORRECTION_FACTOR;

    if(pressure_bar < 0.0f)
        pressure_bar = 0.0f;

    if(pressure_bar > PRESSURE_SENSOR_MAX_BAR)
        pressure_bar = PRESSURE_SENSOR_MAX_BAR;

    return pressure_bar;
}


static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_buffer_t *response =
        (http_response_buffer_t *)event->user_data;

    if(response == NULL)
        return ESP_OK;

    switch(event->event_id) {
        case HTTP_EVENT_ON_DATA: {
            if(event->data == NULL || event->data_len <= 0)
                break;

            size_t available =
                sizeof(response->data) - response->length - 1;

            size_t copy_length = (size_t)event->data_len;

            if(copy_length > available)
                copy_length = available;

            if(copy_length > 0) {
                memcpy(
                    response->data + response->length,
                    event->data,
                    copy_length
                );

                response->length += copy_length;
                response->data[response->length] = '\0';
            }

            break;
        }

        default:
            break;
    }

    return ESP_OK;
}


static bool parse_sensor_values(
    const char *json_text,
    float *tds_voltage,
    float *pressure_voltage,
    float *flow_hz
)
{
    if(json_text == NULL ||
	   tds_voltage == NULL ||
	   pressure_voltage == NULL ||
	   flow_hz == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_text);

    if(root == NULL) {
        ESP_LOGW(TAG, "Invalid JSON received");
        return false;
    }

    bool valid = false;

    const cJSON *node =
        cJSON_GetObjectItemCaseSensitive(root, "node");

    const cJSON *ok =
        cJSON_GetObjectItemCaseSensitive(root, "ok");

    const cJSON *eth =
        cJSON_GetObjectItemCaseSensitive(root, "eth");

    const cJSON *tds =
        cJSON_GetObjectItemCaseSensitive(root, "tds_voltage");

    const cJSON *pressure =
        cJSON_GetObjectItemCaseSensitive(root, "pressure_voltage");
		
	const cJSON *flow =
		cJSON_GetObjectItemCaseSensitive(root, "flow_hz");

    bool correct_node =
        cJSON_IsString(node) &&
        node->valuestring != NULL &&
        strcmp(node->valuestring, "sensor") == 0;

    bool node_ok =
        cJSON_IsBool(ok) &&
        cJSON_IsTrue(ok);

    bool ethernet_ok =
        cJSON_IsBool(eth) &&
        cJSON_IsTrue(eth);

    if(correct_node &&
	   node_ok &&
	   ethernet_ok &&
	   cJSON_IsNumber(tds) &&
	   cJSON_IsNumber(pressure) &&
	   cJSON_IsNumber(flow)) {

        *tds_voltage = (float)tds->valuedouble;
        *pressure_voltage = (float)pressure->valuedouble;
		*flow_hz = (float)flow->valuedouble;

        valid = true;
    }

    cJSON_Delete(root);

    return valid;
}


static void update_sensor_ui(
    int tds_ppm,
    float pressure_bar,
	float flow_hz,
    bool valid
)
{
    /*
     * LVGL non è thread-safe.
     * Aggiorniamo TDS e pressione sotto un unico lock.
     */
    esp_err_t err = bsp_display_lock(1000);

    if(err == ESP_OK) {
        ui_logic_set_tds(tds_ppm, valid);
        ui_logic_set_pressure(pressure_bar, valid);
		ui_logic_set_flow_hz(flow_hz, valid);

        bsp_display_unlock();
    } else {
        ESP_LOGW(
            TAG,
            "Could not obtain display lock: %s",
            esp_err_to_name(err)
        );
    }
}


static void sensor_client_task(void *arg)
{
    (void)arg;

    http_response_buffer_t response = {0};

    esp_http_client_config_t config = {
        .url = SENSOR_NODE_STATUS_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = SENSOR_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &response,
        .disable_auto_redirect = true,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if(client == NULL) {
		ESP_LOGE(TAG, "Could not create HTTP client");
		update_sensor_ui(0, 0.0f, 0.0f, false);
		vTaskDelete(NULL);
		return;
	}

    int consecutive_errors = 0;

    ESP_LOGI(
        TAG,
        "Sensor client started: %s",
        SENSOR_NODE_STATUS_URL
    );

    while(true) {
        response.length = 0;
        response.data[0] = '\0';

        esp_err_t err = esp_http_client_perform(client);

        if(err == ESP_OK) {
            int status_code =
                esp_http_client_get_status_code(client);

            if(status_code == 200) {
float tds_voltage = 0.0f;
float pressure_voltage = 0.0f;
float flow_hz = 0.0f;

if(parse_sensor_values(
    response.data,
    &tds_voltage,
    &pressure_voltage,
    &flow_hz
)) {
    int tds_ppm =
        tds_voltage_to_ppm(tds_voltage);

    float pressure_bar =
        pressure_voltage_to_bar(pressure_voltage);

    consecutive_errors = 0;

    ESP_LOGI(
    TAG,
    "FLOW=%.2f Hz | "
    "TDS raw=%.3f V -> %d ppm | "
    "PRESS raw=%.3f V -> %.1f bar",
    flow_hz,
    tds_voltage,
    tds_ppm,
    pressure_voltage,
    pressure_bar
);

update_sensor_ui(
    tds_ppm,
    pressure_bar,
    flow_hz,
    true
);

} else {
                    consecutive_errors++;

                    ESP_LOGW(
                        TAG,
                        "Sensor JSON missing or invalid: %s",
                        response.data
                    );
                }
            } else {
                consecutive_errors++;

                ESP_LOGW(
                    TAG,
                    "Sensor HTTP status: %d",
                    status_code
                );
            }
        } else {
            consecutive_errors++;

            ESP_LOGW(
                TAG,
                "Sensor request failed: %s",
                esp_err_to_name(err)
            );
        }

        /*
         * Non cancelliamo il valore per un singolo pacchetto perso.
         * Dopo tre errori consecutivi mostriamo "--".
         */
        if(consecutive_errors >= SENSOR_OFFLINE_THRESHOLD) {
			update_sensor_ui(0, 0.0f, 0.0f, false);
		}

        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_INTERVAL_MS));
    }
}


void nerd_sensor_client_start(void)
{
    BaseType_t result = xTaskCreate(
        sensor_client_task,
        "nerd_sensor_client",
        6144,
        NULL,
        5,
        NULL
    );

    if(result != pdPASS) {
        ESP_LOGE(TAG, "Could not create sensor client task");
    }
}