#include "ui.h"
#include "ui_logic.h"
#include <stdio.h>
#include "driver/temperature_sensor.h"
#include "nvs_flash.h"
#include "nvs.h"


static bool feed_pump_on = false;
static bool hp_pump_on = false;
static bool flushing_cycle_on = false;
static bool water_to_tank = true;

static int autom_time_minutes = 0;
static int autom_liters = 0;

static bool auto_production_on = false;
static bool start_auto_on = false;
static bool tank_2_selected = false;

static int pressure_preset_bar = 0;
static bool auto_pressure_on = false;
static int valve_manual_position = 0;

static float input_pressure_bar = 0.0f;
static bool input_pressure_valid = false;

static int input_tds_ppm = 0;
static bool input_tds_valid = false;

static float input_flow_lph = 0.0f;
static bool input_flow_valid = false;

static float temp_feed_water_c = 0.0f;
static float temp_hp_pump_c = 0.0f;
static float temp_feed_pump_c = 0.0f;
static float temp_hp_relay_c = 0.0f;
static float temp_relays_box_c = 0.0f;
static float temp_p4_cpu_c = 0.0f;

static bool temp_feed_water_valid = false;
static bool temp_hp_pump_valid = false;
static bool temp_feed_pump_valid = false;
static bool temp_hp_relay_valid = false;
static bool temp_relays_box_valid = false;
static bool temp_p4_cpu_valid = false;

static temperature_sensor_handle_t p4_temp_sensor = NULL;

static int settings_flush_runtime_index = 1;
static int settings_flow_fine_runtime_index = 3;
static int settings_flow_sensor_runtime_index = 0;
static int settings_step_speed_runtime_index = 0;

static int settings_flush_saved_index = 1;
static int settings_flow_fine_saved_index = 3;
static int settings_flow_sensor_saved_index = 0;
static int settings_step_speed_saved_index = 0;

static lv_timer_t * flushing_timer = NULL;
static int flushing_remaining_seconds = 0;


typedef enum
{
    DO_FEED_PUMP = 0,
    DO_HP_PUMP,
    DO_FLUSH_VALVE,
    DO_TANK_TEST_DIVERT,
    DO_HP_PUMP_FAN,
    DO_AUXILIARY_FAN,
    DO_TANK_1_2_DIVERT,
    DO_ALARM_RELAY,
    DO_COUNT
} machine_output_t;

static bool machine_outputs[DO_COUNT] = { false };


#define SETTINGS_FLUSH_MAX_INDEX       10
#define SETTINGS_FLOW_FINE_MAX_INDEX    6
#define SETTINGS_FLOW_SENSOR_MAX_INDEX  2
#define SETTINGS_STEP_SPEED_MAX_INDEX   2

static int clamp_int(int value, int min, int max)
{
    if(value < min)
        return min;

    if(value > max)
        return max;

    return value;
}

static void save_settings_to_nvs(void);
static void load_settings_from_nvs(void);
static void apply_saved_settings_to_rollers(void);


static void set_checked(lv_obj_t * btn, bool checked)
{
    if(!btn) return;

    if(checked)
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
}

static void update_machine_outputs(void)
{
    machine_outputs[DO_FEED_PUMP] = feed_pump_on;
    machine_outputs[DO_HP_PUMP] = hp_pump_on;
    machine_outputs[DO_FLUSH_VALVE] = flushing_cycle_on;
    machine_outputs[DO_TANK_TEST_DIVERT] = !water_to_tank;

    machine_outputs[DO_HP_PUMP_FAN] = false;       // Future: HP pump fan logic
    machine_outputs[DO_AUXILIARY_FAN] = false;     // Future: auxiliary fan logic
    machine_outputs[DO_TANK_1_2_DIVERT] = tank_2_selected;
    machine_outputs[DO_ALARM_RELAY] = false;       // Future: alarm logic

    // Future:
    // nerd_io_write_outputs(machine_outputs);
}

static void update_feed_pump_state(void)
{
    set_checked(ui_main_btn_feed_pump, feed_pump_on);
    set_checked(ui_settings_btn_feed_pump, feed_pump_on);
    set_checked(ui_autom_btn_feed_pump, feed_pump_on);
    set_checked(ui_log_btn_feed_pump, feed_pump_on);
    set_checked(ui_aux_btn_feed_pump, feed_pump_on);
}

static void update_hp_pump_state(void)
{
    set_checked(ui_main_btn_hp_pump, hp_pump_on);
    set_checked(ui_settings_btn_hp_pump, hp_pump_on);
    set_checked(ui_autom_btn_hp_pump, hp_pump_on);
    set_checked(ui_log_btn_hp_pump, hp_pump_on);
    set_checked(ui_aux_btn_hp_pump, hp_pump_on);
}

static void update_flushing_cycle_state(void)
{
    set_checked(ui_main_btn_flushing_cycle, flushing_cycle_on);
    set_checked(ui_settings_btn_flushing_cycle, flushing_cycle_on);
    set_checked(ui_autom_btn_flushing_cycle, flushing_cycle_on);
    set_checked(ui_log_btn_flushing_cycle, flushing_cycle_on);
    set_checked(ui_aux_btn_flushing_cycle, flushing_cycle_on);

    if(ui_settings_screen_label_ttg_flushing_text) {
        if(flushing_cycle_on)
            lv_obj_set_style_text_color(ui_settings_screen_label_ttg_flushing_text, lv_color_hex(0x00C853), LV_PART_MAIN | LV_STATE_DEFAULT);
        else
            lv_obj_set_style_text_color(ui_settings_screen_label_ttg_flushing_text, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void update_water_labels(void)
{
    const char * txt = water_to_tank ? "WATER\nTO TANK" : "WATER\nTO TEST";
    bool checked = !water_to_tank;

    if(ui_main_label_tank_test) lv_label_set_text(ui_main_label_tank_test, txt);
    if(ui_settings_label_tank_test) lv_label_set_text(ui_settings_label_tank_test, txt);
    if(ui_autom_label_tank_test) lv_label_set_text(ui_autom_label_tank_test, txt);
    if(ui_log_label_tank_test) lv_label_set_text(ui_log_label_tank_test, txt);
    if(ui_aux_label_tank_test) lv_label_set_text(ui_aux_label_tank_test, txt);

    set_checked(ui_main_btn_tank_test, checked);
    set_checked(ui_settings_btn_tank_test, checked);
    set_checked(ui_autom_btn_tank_test, checked);
    set_checked(ui_log_btn_tank_test, checked);
    set_checked(ui_aux_btn_tank_test, checked);
}

static void feed_pump_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    feed_pump_on = !feed_pump_on;
    update_feed_pump_state();
    update_machine_outputs();
}

static void hp_pump_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    hp_pump_on = !hp_pump_on;
    update_hp_pump_state();
    update_machine_outputs();
}

static void start_flushing_countdown(void);
static void stop_flushing_countdown(void);

static void flushing_cycle_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    flushing_cycle_on = !flushing_cycle_on;

    if(flushing_cycle_on)
        start_flushing_countdown();
    else
        stop_flushing_countdown();

    update_flushing_cycle_state();
    update_machine_outputs();
}

static void tank_test_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    water_to_tank = !water_to_tank;
    update_water_labels();
    update_machine_outputs();
}

static void update_autom_target_set_label(void);

static void update_target_mode_label(void)
{
    if(!ui_autom_screen_roller_target_mode || !ui_autom_screen_label_target_mode_output)
        return;

    uint32_t sel = lv_roller_get_selected(ui_autom_screen_roller_target_mode);

    if(sel == 0)
        lv_label_set_text(ui_autom_screen_label_target_mode_output, "LITERS MODE");
    else
        lv_label_set_text(ui_autom_screen_label_target_mode_output, "TIME MODE");

    update_autom_target_set_label();
}

static void target_mode_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
        return;

    update_target_mode_label();
}

static bool autom_is_liters_mode(void)
{
    if(!ui_autom_screen_roller_target_mode)
        return false;

    return lv_roller_get_selected(ui_autom_screen_roller_target_mode) == 0;
}

static void update_autom_target_set_label(void)
{
    if(!ui_autom_screen_label_countdown_time_set)
        return;

    static char buf[32];

    if(autom_is_liters_mode()) {
        snprintf(buf, sizeof(buf), "%d LITERS", autom_liters);
    } else {
        int h = autom_time_minutes / 60;
        int m = autom_time_minutes % 60;

        if(h > 0 && m > 0)
            snprintf(buf, sizeof(buf), "%d H %d MIN", h, m);
        else if(h > 0)
            snprintf(buf, sizeof(buf), "%d HOURS", h);
        else
            snprintf(buf, sizeof(buf), "%d MIN", m);
    }

    lv_label_set_text(ui_autom_screen_label_countdown_time_set, buf);

    if(ui_autom_screen_countdown_ttg_ltg) {
        static char short_buf[16];

        if(autom_is_liters_mode()) {
            snprintf(short_buf, sizeof(short_buf), "%d L.", autom_liters);
        } else {
            int h = autom_time_minutes / 60;
            int m = autom_time_minutes % 60;
            snprintf(short_buf, sizeof(short_buf), "%d:%02d", h, m);
        }

        lv_label_set_text(ui_autom_screen_countdown_ttg_ltg, short_buf);
    }
}

static void autom_target_plus_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    if(autom_is_liters_mode()) {
        if(autom_liters < 1005)
            autom_liters += 15;

        if(autom_liters > 1005)
            autom_liters = 1005;
    } else {
        if(autom_time_minutes < 300)
            autom_time_minutes += 15;

        if(autom_time_minutes > 300)
            autom_time_minutes = 300;
    }

    update_autom_target_set_label();
}

static void autom_target_minus_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    if(autom_is_liters_mode()) {
        if(autom_liters >= 15)
            autom_liters -= 15;
        else
            autom_liters = 0;
    } else {
        if(autom_time_minutes >= 15)
            autom_time_minutes -= 15;
        else
            autom_time_minutes = 0;
    }

    update_autom_target_set_label();
}

static void update_auto_production_state(void)
{
    if(ui_autom_screen_label_auto_prod_set) {
        if(auto_production_on) {
            lv_label_set_text(ui_autom_screen_label_auto_prod_set, "ENABLED");
            lv_obj_set_style_text_color(ui_autom_screen_label_auto_prod_set, lv_color_hex(0x00C853), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_label_set_text(ui_autom_screen_label_auto_prod_set, "DISABLED");
            lv_obj_set_style_text_color(ui_autom_screen_label_auto_prod_set, lv_color_hex(0x1D47A5), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    set_checked(ui_autom_screen_btn_on_off, auto_production_on);
}

static void auto_production_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    auto_production_on = !auto_production_on;
    update_auto_production_state();
}

static void update_start_auto_state(void)
{
    if(ui_autom_screen_label_start_auto) {
        if(start_auto_on)
            lv_label_set_text(ui_autom_screen_label_start_auto, "STOP AUTO");
        else
            lv_label_set_text(ui_autom_screen_label_start_auto, "START AUTO");
    }

    set_checked(ui_autom_screen_btn_start_auto, start_auto_on);
}

static void start_auto_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    start_auto_on = !start_auto_on;
    update_start_auto_state();
}

static void update_tank_diverting_state(void)
{
    if(ui_aux_screen_label__tank_diverting) {
        if(tank_2_selected)
            lv_label_set_text(ui_aux_screen_label__tank_diverting, "TANK 2");
        else
            lv_label_set_text(ui_aux_screen_label__tank_diverting, "TANK 1");
    }

    set_checked(ui_aux_screen_btn_tank_diverting, tank_2_selected);
}

static void tank_diverting_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    tank_2_selected = !tank_2_selected;
    update_tank_diverting_state();
    update_machine_outputs();
}

static void update_settings_flushing_ttg(void)
{
    if(!ui_settings_screen_roller_flush_preset || !ui_settings_screen_label_ttg_time)
        return;

    uint32_t sel = lv_roller_get_selected(ui_settings_screen_roller_flush_preset);

    const char * values[] = {
        "2 MIN",
        "3 MIN",
        "4 MIN",
        "5 MIN",
        "6 MIN",
        "7 MIN",
        "8 MIN",
        "9 MIN",
        "10 MIN",
        "15 MIN",
        "20 MIN"
    };

    if(sel < 11)
        lv_label_set_text(ui_settings_screen_label_ttg_time, values[sel]);
}

static void update_settings_flushing_text_color(void)
{
    if(!ui_settings_screen_label_ttg_flushing_text)
        return;

    if(flushing_cycle_on)
        lv_obj_set_style_text_color(ui_settings_screen_label_ttg_flushing_text, lv_color_hex(0x00C853), LV_PART_MAIN | LV_STATE_DEFAULT);
    else
        lv_obj_set_style_text_color(ui_settings_screen_label_ttg_flushing_text, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void update_pressure_preset_label(void)
{
    if(ui_main_screen_label_bar_preset) {
        static char bar_buf[16];
        snprintf(bar_buf, sizeof(bar_buf), "%d", pressure_preset_bar);
        lv_label_set_text(ui_main_screen_label_bar_preset, bar_buf);
    }

    if(ui_main_screen_label_psi_preset) {
        static char psi_buf[16];
        int psi = (int)(pressure_preset_bar * 14.5f);
        snprintf(psi_buf, sizeof(psi_buf), "- %d", psi);
        lv_label_set_text(ui_main_screen_label_psi_preset, psi_buf);
    }
}

static void pressure_plus_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    if(auto_pressure_on) {
        if(pressure_preset_bar < 60)
            pressure_preset_bar++;

        update_pressure_preset_label();
    } else {
        valve_manual_position++;

        // Future: send_stepper_close()
        // For now only the simulated position changes.
    }
}

static void pressure_minus_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    if(auto_pressure_on) {
        if(pressure_preset_bar > 0)
            pressure_preset_bar--;

        update_pressure_preset_label();
    } else {
        valve_manual_position--;

        // Future: send_stepper_open()
        // For now only the simulated position changes.
    }
}

static void update_auto_pressure_state(void)
{
    set_checked(ui_main_screen_switch_auto_pressure, auto_pressure_on);
}

static void auto_pressure_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
        return;

    auto_pressure_on = lv_obj_has_state(ui_main_screen_switch_auto_pressure, LV_STATE_CHECKED);
    update_auto_pressure_state();
}

static float convert_4_20ma_to_pressure_bar(float ma)
{
    if(ma < 3.5f || ma > 21.0f)
        return 0.0f;

    if(ma < 4.0f)
        ma = 4.0f;

    if(ma > 20.0f)
        ma = 20.0f;

    return (ma - 4.0f) * (100.0f / 16.0f);
}

static void update_pressure_input(float pressure_bar, bool valid)
{
    input_pressure_bar = pressure_bar;
    input_pressure_valid = valid;

    if(ui_main_screen_label_bar_gauge_bar) {
        static char bar_buf[24];

        if(input_pressure_valid)
            snprintf(bar_buf, sizeof(bar_buf), "%.1f bar", input_pressure_bar);
        else
            snprintf(bar_buf, sizeof(bar_buf), "-- bar");

        lv_label_set_text(ui_main_screen_label_bar_gauge_bar, bar_buf);
    }

    if(ui_main_screen_label_bar_gauge_psi) {
        static char psi_buf[24];

        if(input_pressure_valid)
            snprintf(psi_buf, sizeof(psi_buf), "%.0f psi", input_pressure_bar * 14.5038f);
        else
            snprintf(psi_buf, sizeof(psi_buf), "-- psi");

        lv_label_set_text(ui_main_screen_label_bar_gauge_psi, psi_buf);
    }
}

static void update_tds_input(int tds_ppm, bool valid)
{
    input_tds_ppm = tds_ppm;
    input_tds_valid = valid;

    static char center_buf[16];
    static char upper_buf[16];
    static char lower_buf[16];

    if(!input_tds_valid) {
        snprintf(center_buf, sizeof(center_buf), "--");
        snprintf(upper_buf, sizeof(upper_buf), "--");
        snprintf(lower_buf, sizeof(lower_buf), "--");
    } else {
        snprintf(center_buf, sizeof(center_buf), "%d", input_tds_ppm);
        snprintf(upper_buf, sizeof(upper_buf), "%d", input_tds_ppm - 1);
        snprintf(lower_buf, sizeof(lower_buf), "%d", input_tds_ppm + 1);
    }

    if(ui_main_screen_label_tds_gauge_center)
        lv_label_set_text(ui_main_screen_label_tds_gauge_center, center_buf);

    if(ui_main_screen_label_tds_gauge_upper)
        lv_label_set_text(ui_main_screen_label_tds_gauge_upper, upper_buf);

    if(ui_main_screen_label_tds_gauge_lower)
        lv_label_set_text(ui_main_screen_label_tds_gauge_lower, lower_buf);

    if(ui_main_screen_label_tds_gauge_center) {
        if(!input_tds_valid) {
            lv_obj_set_style_text_color(ui_main_screen_label_tds_gauge_center, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else if(input_tds_ppm <= 500) {
            lv_obj_set_style_text_color(ui_main_screen_label_tds_gauge_center, lv_color_hex(0x00C853), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else if(input_tds_ppm <= 750) {
            lv_obj_set_style_text_color(ui_main_screen_label_tds_gauge_center, lv_color_hex(0xFF9800), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_set_style_text_color(ui_main_screen_label_tds_gauge_center, lv_color_hex(0xFF1744), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

static void update_flow_input(float flow_lph, bool valid)
{
    input_flow_lph = flow_lph;
    input_flow_valid = valid;

    static char lph_buf[24];
    static char gpd_buf[24];

    if(!input_flow_valid) {
        snprintf(lph_buf, sizeof(lph_buf), "-- l/h");
        snprintf(gpd_buf, sizeof(gpd_buf), "-- gpd");
    } else {
        float gpd = input_flow_lph * 6.34013f;

        snprintf(lph_buf, sizeof(lph_buf), "%.0f l/h", input_flow_lph);
        snprintf(gpd_buf, sizeof(gpd_buf), "%.0f gpd", gpd);
    }

    if(ui_main_screen_label_lph_gauge_lph)
        lv_label_set_text(ui_main_screen_label_lph_gauge_lph, lph_buf);

    if(ui_main_screen_label_lph_gauge_gpd)
        lv_label_set_text(ui_main_screen_label_lph_gauge_gpd, gpd_buf);
}

static void update_aux_temperature_labels(void)
{
    if(ui_aux_screen_label_input_4_value)
        lv_label_set_text(ui_aux_screen_label_input_4_value, "--- °C");

    if(ui_aux_screen_label_input_5_value)
        lv_label_set_text(ui_aux_screen_label_input_5_value, "--- °C");

    if(ui_aux_screen_label_input_6_value)
        lv_label_set_text(ui_aux_screen_label_input_6_value, "--- °C");

    if(ui_aux_screen_label_input_7_value)
        lv_label_set_text(ui_aux_screen_label_input_7_value, "--- °C");

    if(ui_aux_screen_label_input_8_value)
        lv_label_set_text(ui_aux_screen_label_input_8_value, "--- °C");

    if(ui_aux_screen_label_input_9_value) {
        static char buf[24];

        if(temp_p4_cpu_valid)
            snprintf(buf, sizeof(buf), "%.1f °C", temp_p4_cpu_c);
        else
            snprintf(buf, sizeof(buf), "--- °C");

        lv_label_set_text(ui_aux_screen_label_input_9_value, buf);
    }
}

static void update_debug_screen_text(void)
{
    if(ui_debug_screen_textarea_1) {
        lv_textarea_set_text(ui_debug_screen_textarea_1,
            "NERD 2 P4\n"
            "UI: OK\n"
            "LVGL: 9.2.2\n"
            "IDF: 5.5.4\n"
            "CPU TEMP: active\n"
            "NVS: active\n"
            "LAN: n.a.\n"
            "MODBUS: n.a.\n"
            "S3 VALVE: n.a."
        );
    }

    
}


static void init_p4_cpu_temperature_sensor(void)
{
    temperature_sensor_config_t temp_sensor_config = {
        .range_min = -10,
        .range_max = 80,
    };

    if(temperature_sensor_install(&temp_sensor_config, &p4_temp_sensor) == ESP_OK) {
        temperature_sensor_enable(p4_temp_sensor);
    }
}

static void update_p4_cpu_temperature(void)
{
    if(!p4_temp_sensor) {
        temp_p4_cpu_valid = false;
        return;
    }

    float temp_c = 0.0f;

    if(temperature_sensor_get_celsius(p4_temp_sensor, &temp_c) == ESP_OK) {
        temp_p4_cpu_c = temp_c;
        temp_p4_cpu_valid = true;
    } else {
        temp_p4_cpu_valid = false;
    }
}

static void p4_cpu_temperature_timer_cb(lv_timer_t * timer)
{
    (void) timer;

    update_p4_cpu_temperature();
    update_aux_temperature_labels();
}

static void settings_save_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    settings_flush_saved_index = settings_flush_runtime_index;
    settings_flow_fine_saved_index = settings_flow_fine_runtime_index;
    settings_flow_sensor_saved_index = settings_flow_sensor_runtime_index;
    settings_step_speed_saved_index = settings_step_speed_runtime_index;

    save_settings_to_nvs();

    printf("Settings saved: flush=%d fine=%d sensor=%d speed=%d\n",
           settings_flush_saved_index,
           settings_flow_fine_saved_index,
           settings_flow_sensor_saved_index,
           settings_step_speed_saved_index);
}

static void settings_flush_preset_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
        return;

    settings_flush_runtime_index = clamp_int(
        (int)lv_roller_get_selected(ui_settings_screen_roller_flush_preset),
        0,
        SETTINGS_FLUSH_MAX_INDEX
    );

    update_settings_flushing_ttg();
}

static void settings_flow_fine_tuning_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
        return;

    settings_flow_fine_runtime_index = clamp_int(
        (int)lv_roller_get_selected(ui_settings_screen_roller_flow_fine_tuning),
        0,
        SETTINGS_FLOW_FINE_MAX_INDEX
    );
}

static void settings_flow_sensor_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
        return;

    settings_flow_sensor_runtime_index = clamp_int(
        (int)lv_roller_get_selected(ui_settings_screen_roller_flow_sens_selector),
        0,
        SETTINGS_FLOW_SENSOR_MAX_INDEX
    );
}

static void settings_step_speed_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
        return;

    settings_step_speed_runtime_index = clamp_int(
        (int)lv_roller_get_selected(ui_settings_screen_roller_step_speed),
        0,
        SETTINGS_STEP_SPEED_MAX_INDEX
    );
}

static void save_settings_to_nvs(void)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open("nerd_settings", NVS_READWRITE, &nvs);
    if(err != ESP_OK)
        return;

    nvs_set_i32(nvs, "flush", settings_flush_saved_index);
    nvs_set_i32(nvs, "flowfine", settings_flow_fine_saved_index);
    nvs_set_i32(nvs, "flowsens", settings_flow_sensor_saved_index);
    nvs_set_i32(nvs, "stepspeed", settings_step_speed_saved_index);

    nvs_commit(nvs);
    nvs_close(nvs);
}

static void load_settings_from_nvs(void)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open("nerd_settings", NVS_READONLY, &nvs);
    if(err != ESP_OK)
        return;

    int32_t value = 0;

    if(nvs_get_i32(nvs, "flush", &value) == ESP_OK)
        settings_flush_saved_index = clamp_int((int)value, 0, SETTINGS_FLUSH_MAX_INDEX);

    if(nvs_get_i32(nvs, "flowfine", &value) == ESP_OK)
        settings_flow_fine_saved_index = clamp_int((int)value, 0, SETTINGS_FLOW_FINE_MAX_INDEX);

    if(nvs_get_i32(nvs, "flowsens", &value) == ESP_OK)
        settings_flow_sensor_saved_index = clamp_int((int)value, 0, SETTINGS_FLOW_SENSOR_MAX_INDEX);

    if(nvs_get_i32(nvs, "stepspeed", &value) == ESP_OK)
        settings_step_speed_saved_index = clamp_int((int)value, 0, SETTINGS_STEP_SPEED_MAX_INDEX);

    nvs_close(nvs);
}

static void apply_saved_settings_to_rollers(void)
{
    settings_flush_runtime_index = settings_flush_saved_index;
    settings_flow_fine_runtime_index = settings_flow_fine_saved_index;
    settings_flow_sensor_runtime_index = settings_flow_sensor_saved_index;
    settings_step_speed_runtime_index = settings_step_speed_saved_index;

    if(ui_settings_screen_roller_flush_preset)
        lv_roller_set_selected(ui_settings_screen_roller_flush_preset, settings_flush_runtime_index, LV_ANIM_OFF);

    if(ui_settings_screen_roller_flow_fine_tuning)
        lv_roller_set_selected(ui_settings_screen_roller_flow_fine_tuning, settings_flow_fine_runtime_index, LV_ANIM_OFF);

    if(ui_settings_screen_roller_flow_sens_selector)
        lv_roller_set_selected(ui_settings_screen_roller_flow_sens_selector, settings_flow_sensor_runtime_index, LV_ANIM_OFF);

    if(ui_settings_screen_roller_step_speed)
        lv_roller_set_selected(ui_settings_screen_roller_step_speed, settings_step_speed_runtime_index, LV_ANIM_OFF);

    update_settings_flushing_ttg();
}

static int get_flush_seconds_from_index(int index)
{
    const int values_min[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20};

    if(index < 0 || index >= 11)
        index = 1; // default 3 min

    return values_min[index] * 60;
}

static void update_flushing_countdown_label(void)
{
    if(!ui_settings_screen_label_ttg_time)
        return;

    static char buf[16];

    int min = flushing_remaining_seconds / 60;
    int sec = flushing_remaining_seconds % 60;

    snprintf(buf, sizeof(buf), "%d:%02d", min, sec);
    lv_label_set_text(ui_settings_screen_label_ttg_time, buf);
}

static void flushing_timer_cb(lv_timer_t * timer)
{
    (void) timer;

    if(flushing_remaining_seconds > 0)
        flushing_remaining_seconds--;

    update_flushing_countdown_label();

    if(flushing_remaining_seconds <= 0) {
        flushing_cycle_on = false;

        if(flushing_timer) {
            lv_timer_del(flushing_timer);
            flushing_timer = NULL;
        }

        update_flushing_cycle_state();
        update_machine_outputs();
        update_settings_flushing_ttg();
    }
}

static void start_flushing_countdown(void)
{
    flushing_remaining_seconds = get_flush_seconds_from_index(settings_flush_runtime_index);

    update_flushing_countdown_label();

    if(flushing_timer) {
        lv_timer_del(flushing_timer);
        flushing_timer = NULL;
    }

    flushing_timer = lv_timer_create(flushing_timer_cb, 1000, NULL);
}

static void stop_flushing_countdown(void)
{
    if(flushing_timer) {
        lv_timer_del(flushing_timer);
        flushing_timer = NULL;
    }

    flushing_remaining_seconds = 0;
    update_settings_flushing_ttg();
}

void ui_logic_init(void)
{
    // Persistent settings
    esp_err_t nvs_err = nvs_flash_init();
    if(nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    load_settings_from_nvs();
    apply_saved_settings_to_rollers();

    // Initial UI state sync
    update_water_labels();
    update_feed_pump_state();
    update_hp_pump_state();
    update_flushing_cycle_state();
    update_settings_flushing_ttg();
    update_settings_flushing_text_color();
    update_pressure_preset_label();
    update_autom_target_set_label();
    update_auto_production_state();
    update_start_auto_state();
    update_auto_pressure_state();
    update_tank_diverting_state();
    update_machine_outputs();

    // Temporary simulated values. Replace with real LAN/Modbus values later.
    // update_pressure_input(0.0f, false);
    update_pressure_input(55.4f, true);

    // update_tds_input(0, false);
    update_tds_input(154, true);

    // update_flow_input(0.0f, false);
    update_flow_input(100.0f, true);

	
	init_p4_cpu_temperature_sensor();
	update_p4_cpu_temperature();

    update_aux_temperature_labels();
	
	lv_timer_create(p4_cpu_temperature_timer_cb, 5000, NULL);
	
    update_debug_screen_text();

    // Global buttons
    if(ui_main_btn_tank_test) lv_obj_add_event_cb(ui_main_btn_tank_test, tank_test_event, LV_EVENT_CLICKED, NULL);
    if(ui_settings_btn_tank_test) lv_obj_add_event_cb(ui_settings_btn_tank_test, tank_test_event, LV_EVENT_CLICKED, NULL);
    if(ui_autom_btn_tank_test) lv_obj_add_event_cb(ui_autom_btn_tank_test, tank_test_event, LV_EVENT_CLICKED, NULL);
    if(ui_log_btn_tank_test) lv_obj_add_event_cb(ui_log_btn_tank_test, tank_test_event, LV_EVENT_CLICKED, NULL);
    if(ui_aux_btn_tank_test) lv_obj_add_event_cb(ui_aux_btn_tank_test, tank_test_event, LV_EVENT_CLICKED, NULL);

    if(ui_main_btn_feed_pump) lv_obj_add_event_cb(ui_main_btn_feed_pump, feed_pump_event, LV_EVENT_CLICKED, NULL);
    if(ui_settings_btn_feed_pump) lv_obj_add_event_cb(ui_settings_btn_feed_pump, feed_pump_event, LV_EVENT_CLICKED, NULL);
    if(ui_autom_btn_feed_pump) lv_obj_add_event_cb(ui_autom_btn_feed_pump, feed_pump_event, LV_EVENT_CLICKED, NULL);
    if(ui_log_btn_feed_pump) lv_obj_add_event_cb(ui_log_btn_feed_pump, feed_pump_event, LV_EVENT_CLICKED, NULL);
    if(ui_aux_btn_feed_pump) lv_obj_add_event_cb(ui_aux_btn_feed_pump, feed_pump_event, LV_EVENT_CLICKED, NULL);

    if(ui_main_btn_hp_pump) lv_obj_add_event_cb(ui_main_btn_hp_pump, hp_pump_event, LV_EVENT_CLICKED, NULL);
    if(ui_settings_btn_hp_pump) lv_obj_add_event_cb(ui_settings_btn_hp_pump, hp_pump_event, LV_EVENT_CLICKED, NULL);
    if(ui_autom_btn_hp_pump) lv_obj_add_event_cb(ui_autom_btn_hp_pump, hp_pump_event, LV_EVENT_CLICKED, NULL);
    if(ui_log_btn_hp_pump) lv_obj_add_event_cb(ui_log_btn_hp_pump, hp_pump_event, LV_EVENT_CLICKED, NULL);
    if(ui_aux_btn_hp_pump) lv_obj_add_event_cb(ui_aux_btn_hp_pump, hp_pump_event, LV_EVENT_CLICKED, NULL);

    if(ui_main_btn_flushing_cycle) lv_obj_add_event_cb(ui_main_btn_flushing_cycle, flushing_cycle_event, LV_EVENT_CLICKED, NULL);
    if(ui_settings_btn_flushing_cycle) lv_obj_add_event_cb(ui_settings_btn_flushing_cycle, flushing_cycle_event, LV_EVENT_CLICKED, NULL);
    if(ui_autom_btn_flushing_cycle) lv_obj_add_event_cb(ui_autom_btn_flushing_cycle, flushing_cycle_event, LV_EVENT_CLICKED, NULL);
    if(ui_log_btn_flushing_cycle) lv_obj_add_event_cb(ui_log_btn_flushing_cycle, flushing_cycle_event, LV_EVENT_CLICKED, NULL);
    if(ui_aux_btn_flushing_cycle) lv_obj_add_event_cb(ui_aux_btn_flushing_cycle, flushing_cycle_event, LV_EVENT_CLICKED, NULL);

    // AUTOM screen
    if(ui_autom_screen_roller_target_mode) {
        lv_obj_add_event_cb(
            ui_autom_screen_roller_target_mode,
            target_mode_event,
            LV_EVENT_VALUE_CHANGED,
            NULL
        );

        update_target_mode_label();
    }

    if(ui_autom_screen_btn_countdown_plus) {
        lv_obj_add_event_cb(ui_autom_screen_btn_countdown_plus, autom_target_plus_event, LV_EVENT_CLICKED, NULL);
    }

    if(ui_autom_screen_btn_countdown_minus) {
        lv_obj_add_event_cb(ui_autom_screen_btn_countdown_minus, autom_target_minus_event, LV_EVENT_CLICKED, NULL);
    }

    if(ui_autom_screen_btn_on_off) {
        lv_obj_add_event_cb(ui_autom_screen_btn_on_off, auto_production_event, LV_EVENT_CLICKED, NULL);
    }

    if(ui_autom_screen_btn_start_auto) {
        lv_obj_add_event_cb(ui_autom_screen_btn_start_auto, start_auto_event, LV_EVENT_CLICKED, NULL);
    }

    // AUX screen
    if(ui_aux_screen_btn_tank_diverting) {
        lv_obj_add_event_cb(ui_aux_screen_btn_tank_diverting, tank_diverting_event, LV_EVENT_CLICKED, NULL);
    }

    // SETTINGS screen
    if(ui_settings_screen_roller_flush_preset) {
        lv_obj_add_event_cb(ui_settings_screen_roller_flush_preset, settings_flush_preset_event, LV_EVENT_VALUE_CHANGED, NULL);
    }

    if(ui_settings_screen_roller_flow_fine_tuning) {
        lv_obj_add_event_cb(ui_settings_screen_roller_flow_fine_tuning, settings_flow_fine_tuning_event, LV_EVENT_VALUE_CHANGED, NULL);
    }

    if(ui_settings_screen_roller_flow_sens_selector) {
        lv_obj_add_event_cb(ui_settings_screen_roller_flow_sens_selector, settings_flow_sensor_event, LV_EVENT_VALUE_CHANGED, NULL);
    }

    if(ui_settings_screen_roller_step_speed) {
        lv_obj_add_event_cb(ui_settings_screen_roller_step_speed, settings_step_speed_event, LV_EVENT_VALUE_CHANGED, NULL);
    }

    if(ui_settings_screen_btn_save) {
        lv_obj_add_event_cb(ui_settings_screen_btn_save, settings_save_event, LV_EVENT_CLICKED, NULL);
    }

    // MAIN screen
    if(ui_main_screen_glow_3) {
        lv_obj_add_event_cb(ui_main_screen_glow_3, pressure_plus_event, LV_EVENT_CLICKED, NULL);
    }

    if(ui_main_screen_glow_4) {
        lv_obj_add_event_cb(ui_main_screen_glow_4, pressure_minus_event, LV_EVENT_CLICKED, NULL);
    }

    if(ui_main_screen_switch_auto_pressure) {
        lv_obj_add_event_cb(ui_main_screen_switch_auto_pressure, auto_pressure_event, LV_EVENT_VALUE_CHANGED, NULL);
    }
}
