#include "ui.h"
#include "ui_logic.h"
#include <stdio.h>
#include "driver/temperature_sensor.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nerd_m31.h"


static bool feed_pump_on = false;
static bool hp_pump_on = false;
static bool flushing_cycle_on = false;
static bool water_to_tank = true;

/*
 * Tank/Test diverting valve mode.
 *
 * Default at boot:
 *   WATER TO TANK -> M31 DO4 / coil 3 OFF
 *
 * Button cycle:
 *   WATER TO TANK -> WATER TO TEST -> AUTO DIVERT -> WATER TO TANK
 */
typedef enum
{
    DIVERT_MODE_WATER_TO_TANK = 0,
    DIVERT_MODE_WATER_TO_TEST,
    DIVERT_MODE_AUTO
} divert_mode_t;

static divert_mode_t divert_mode = DIVERT_MODE_WATER_TO_TANK;
static lv_timer_t * divert_blink_timer = NULL;
static int divert_blink_elapsed_seconds = 0;
static bool divert_blink_on = false;

#define AUTO_DIVERT_TDS_TO_TANK_PPM  490
#define AUTO_DIVERT_TDS_TO_TEST_PPM  510
#define AUTO_DIVERT_SLOW_BLINK_SEC      3
#define AUTO_DIVERT_FAST_BLINK_SEC      1

static int autom_time_minutes = 0;
static int autom_liters = 0;

static bool auto_production_on = false;
static bool start_auto_on = false;

/*
 * AUTO PRODUCTION monitors production already in progress.
 * Reaching TIME or LITERS calls the common soft AUTO STOP.
 */
static lv_timer_t * auto_production_timer = NULL;
static int auto_time_remaining_seconds = 0;
static float auto_liters_produced = 0.0f;
static bool auto_running_liters_mode = false;

static bool tank_2_selected = false;

static int pressure_preset_bar = 0;
static bool auto_pressure_on = false;
static int valve_manual_position = 0;

static float input_pressure_bar = 0.0f;
static bool input_pressure_valid = false;

static int input_tds_ppm = 0;
static bool input_tds_valid = false;

static float input_flow_hz = 0.0f;
static bool input_flow_hz_valid = false;

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

/*
 * AUTO STOP - Stage 2
 *
 * Visual state machine only.
 * No pump output and no valve command is executed in this stage.
 */
typedef enum
{
    AUTO_STOP_VISUAL_IDLE = 0,
    AUTO_STOP_VISUAL_HOMING,
    AUTO_STOP_VISUAL_COUNTDOWN,
    AUTO_STOP_VISUAL_HP_OFF,
    AUTO_STOP_VISUAL_COMPLETE,
    AUTO_STOP_VISUAL_ABORT_DELAY,
    AUTO_STOP_VISUAL_ABORT_HOMING
} auto_stop_visual_state_t;

static auto_stop_visual_state_t auto_stop_visual_state = AUTO_STOP_VISUAL_IDLE;
static lv_timer_t * auto_stop_visual_timer = NULL;
static int auto_stop_visual_countdown_seconds = 0;
static int auto_stop_visual_phase_seconds = 0;

/*
 * Overpressure protection.
 *
 * Trigger condition: valid pressure strictly greater than 65.0 bar.
 * Both pumps are switched OFF immediately.
 * Valve homing is then simulated for now; later this will wait for S3.
 */
#define OVERPRESSURE_LIMIT_BAR 65.0f

typedef enum
{
    OVERPRESSURE_IDLE = 0,
    OVERPRESSURE_HOMING,
    OVERPRESSURE_COMPLETE
} overpressure_state_t;

static overpressure_state_t overpressure_state = OVERPRESSURE_IDLE;
static lv_timer_t * overpressure_timer = NULL;
static int overpressure_phase_seconds = 0;



/*
 * M31 base digital-output map.
 *
 * DO1 / coil 0 = Feed Pump
 * DO2 / coil 1 = HP Pump
 * DO3 / coil 2 = Auxiliary 1
 * DO4 / coil 3 = Auxiliary 2
 */
#define M31_COIL_FEED_PUMP   0
#define M31_COIL_HP_PUMP     1
#define M31_COIL_AUX_1       2
#define M31_COIL_TANK_TEST_DIVERT  3

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
/*
 * Fattori Hz -> litri/ora ricavati da test reali a 180 l/h.
 *
 * Sensor A: 312 Hz -> 180 l/h
 * Sensor B:  20 Hz -> 180 l/h
 * Sensor C:  40 Hz -> 180 l/h
 */
#define FLOW_FACTOR_SENSOR_A  0.577f
#define FLOW_FACTOR_SENSOR_B  9.0f
#define FLOW_FACTOR_SENSOR_C  4.5f

static const float flow_fine_tuning_factors[] = {
    0.85f,  // -15%
    0.90f,  // -10%
    0.95f,  //  -5%
    1.00f,  //   0%
    1.05f,  //  +5%
    1.10f,  // +10%
    1.15f   // +15%
};

static int clamp_int(int value, int min, int max)
{
    if(value < min)
        return min;

    if(value > max)
        return max;

    return value;
}


static float get_saved_flow_sensor_factor(void)
{
    switch(settings_flow_sensor_saved_index) {
        case 0:
            return FLOW_FACTOR_SENSOR_A;

        case 1:
            return FLOW_FACTOR_SENSOR_B;

        case 2:
            return FLOW_FACTOR_SENSOR_C;

        default:
            return FLOW_FACTOR_SENSOR_A;
    }
}

static float get_saved_flow_fine_tuning_factor(void)
{
    int index = clamp_int(
        settings_flow_fine_saved_index,
        0,
        SETTINGS_FLOW_FINE_MAX_INDEX
    );

    return flow_fine_tuning_factors[index];
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

static void auto_stop_visual_set_label(const char * text);
static bool auto_stop_visual_is_active(void);
static bool auto_stop_start(const char * source);
static void auto_stop_visual_emergency_abort(void);

static void auto_production_stop_tracking(const char * status_text);
static void update_auto_production_state(void);
static void update_start_auto_state(void);
static void update_autom_target_set_label(void);

static bool overpressure_is_active(void);
static void overpressure_trigger(void);

static void update_water_labels(void);
static void divert_evaluate_auto(void);
static void divert_stop_blink_timer(void);

static void divert_set_all_labels(const char * text)
{
    if(ui_main_label_tank_test)
        lv_label_set_text(ui_main_label_tank_test, text);

    if(ui_settings_label_tank_test)
        lv_label_set_text(ui_settings_label_tank_test, text);

    if(ui_autom_label_tank_test)
        lv_label_set_text(ui_autom_label_tank_test, text);

    if(ui_log_label_tank_test)
        lv_label_set_text(ui_log_label_tank_test, text);

    if(ui_aux_label_tank_test)
        lv_label_set_text(ui_aux_label_tank_test, text);
}

static void divert_set_all_checked(bool checked)
{
    set_checked(ui_main_btn_tank_test, checked);
    set_checked(ui_settings_btn_tank_test, checked);
    set_checked(ui_autom_btn_tank_test, checked);
    set_checked(ui_log_btn_tank_test, checked);
    set_checked(ui_aux_btn_tank_test, checked);
}

static void divert_apply_output(bool divert_to_test)
{
    bool new_water_to_tank = !divert_to_test;
    bool output_changed = water_to_tank != new_water_to_tank;

    water_to_tank = new_water_to_tank;
    update_machine_outputs();

    /*
     * Physical output:
     * M31 DO4 / coil 3 OFF = WATER TO TANK
     * M31 DO4 / coil 3 ON  = WATER TO TEST
     */
    if(output_changed) {
        nerd_m31_write_coil(
            M31_COIL_TANK_TEST_DIVERT,
            divert_to_test
        );

        printf(
            "TANK/TEST DIVERT: %s | DO4 coil 3 -> %s\n",
            divert_to_test ? "WATER TO TEST" : "WATER TO TANK",
            divert_to_test ? "ON" : "OFF"
        );
    }
}

static void update_water_labels(void)
{
    switch(divert_mode) {
        case DIVERT_MODE_WATER_TO_TANK:
            divert_set_all_labels("WATER\nTO TANK");
            divert_set_all_checked(false);
            break;

        case DIVERT_MODE_WATER_TO_TEST:
            divert_set_all_labels("WATER\nTO TEST");
            divert_set_all_checked(true);
            break;

        case DIVERT_MODE_AUTO:
            /*
             * In AUTO mode the physical route is represented by
             * water_to_tank, while the glow indicates that AUTO is active.
             */
            if(!input_tds_valid) {
                divert_set_all_labels(
                    divert_blink_on ? "TDS\nINVALID" : "AUTO\nDIVERT"
                );
            } else if(!water_to_tank) {
                divert_set_all_labels(
                    divert_blink_on ? "WATER\nTO TEST" : "AUTO\nDIVERT"
                );
            } else {
                divert_set_all_labels(
                    divert_blink_on ? "WATER\nTO TANK" : "AUTO\nDIVERT"
                );
            }

            divert_set_all_checked(divert_blink_on);
            break;

        default:
            divert_mode = DIVERT_MODE_WATER_TO_TANK;
            divert_set_all_labels("WATER\nTO TANK");
            divert_set_all_checked(false);
            break;
    }
}

static void divert_blink_timer_cb(lv_timer_t * timer)
{
    (void)timer;

    if(divert_mode != DIVERT_MODE_AUTO)
        return;

    divert_blink_elapsed_seconds++;

    /*
     * Slow blink while automatic routing is safely going to the tank.
     * Fast blink while diverted to test or while TDS is invalid.
     */
    int blink_period_seconds =
        (input_tds_valid && water_to_tank)
            ? AUTO_DIVERT_SLOW_BLINK_SEC
            : AUTO_DIVERT_FAST_BLINK_SEC;

    if(divert_blink_elapsed_seconds >= blink_period_seconds) {
        divert_blink_elapsed_seconds = 0;
        divert_blink_on = !divert_blink_on;
        update_water_labels();
    }
}

static void divert_start_blink_timer(void)
{
    if(divert_blink_timer) {
        lv_timer_del(divert_blink_timer);
        divert_blink_timer = NULL;
    }

    divert_blink_elapsed_seconds = 0;
    divert_blink_on = true;
    update_water_labels();

    divert_blink_timer =
        lv_timer_create(divert_blink_timer_cb, 1000, NULL);
}

static void divert_stop_blink_timer(void)
{
    if(divert_blink_timer) {
        lv_timer_del(divert_blink_timer);
        divert_blink_timer = NULL;
    }

    divert_blink_elapsed_seconds = 0;
    divert_blink_on = false;
}

static void divert_evaluate_auto(void)
{
    if(divert_mode != DIVERT_MODE_AUTO)
        return;

    bool divert_to_test = !water_to_tank;

    /*
     * Fail-safe:
     * invalid TDS always diverts product water to test/waste.
     */
    if(!input_tds_valid) {
        divert_to_test = true;
    } else if(input_tds_ppm > AUTO_DIVERT_TDS_TO_TEST_PPM) {
        divert_to_test = true;
    } else if(input_tds_ppm < AUTO_DIVERT_TDS_TO_TANK_PPM) {
        divert_to_test = false;
    }
    /*
     * 490...510 ppm:
     * keep the current physical route to provide hysteresis.
     */

    bool route_changed = water_to_tank == divert_to_test;

    divert_apply_output(divert_to_test);

    if(route_changed) {
        divert_blink_elapsed_seconds = 0;
        divert_blink_on = true;
    }

    update_water_labels();
}

static void feed_pump_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    bool requested_on = !feed_pump_on;

    /*
     * During AUTO STOP, an OFF request is an emergency override:
     * both pumps are switched OFF immediately and the normal sequence
     * is aborted. ON requests are ignored while AUTO STOP is active.
     */
    if(overpressure_is_active()) {
        printf("FEED PUMP command ignored - overpressure homing active\n");
        return;
    }

    if(auto_stop_visual_is_active()) {
        if(!requested_on)
            auto_stop_visual_emergency_abort();

        return;
    }

    /*
     * Feed Pump may always be started or stopped manually.
     *
     * If Feed Pump is stopped while HP Pump is running,
     * HP Pump is stopped immediately as a logical safety interlock.
     */
    if(!requested_on && hp_pump_on) {
        hp_pump_on = false;
        update_hp_pump_state();

        /*
         * Safety order: HP Pump OFF first, then Feed Pump OFF.
         */
        nerd_m31_write_coil(M31_COIL_HP_PUMP, false);

        printf("HP PUMP: OFF because FEED PUMP was stopped\n");
    }

    feed_pump_on = requested_on;

    update_feed_pump_state();
    update_machine_outputs();

    nerd_m31_write_coil(M31_COIL_FEED_PUMP, feed_pump_on);

    printf(
        "FEED PUMP: %s | HP PUMP: %s\n",
        feed_pump_on ? "ON" : "OFF",
        hp_pump_on ? "ON" : "OFF"
    );
}

static void hp_pump_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    bool requested_on = !hp_pump_on;

    /*
     * During AUTO STOP, an OFF request is an emergency override.
     * ON requests are ignored until the sequence has finished.
     */
    if(overpressure_is_active()) {
        printf("HP PUMP command ignored - overpressure homing active\n");
        return;
    }

    if(auto_stop_visual_is_active()) {
        if(!requested_on)
            auto_stop_visual_emergency_abort();

        return;
    }

    /*
     * HP Pump start permissive:
     * it may start only when Feed Pump is already ON.
     *
     * HP Pump may always be stopped manually.
     */
    if(requested_on && !feed_pump_on) {
        hp_pump_on = false;
        update_hp_pump_state();
        update_machine_outputs();

        printf("HP PUMP: START REJECTED - FEED PUMP is OFF\n");
        return;
    }

    hp_pump_on = requested_on;

    update_hp_pump_state();
    update_machine_outputs();

    nerd_m31_write_coil(M31_COIL_HP_PUMP, hp_pump_on);

    printf(
        "HP PUMP: %s | FEED PUMP: %s\n",
        hp_pump_on ? "ON" : "OFF",
        feed_pump_on ? "ON" : "OFF"
    );
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

    switch(divert_mode) {
        case DIVERT_MODE_WATER_TO_TANK:
            /*
             * First click:
             * WATER TO TEST, fixed glow, relay ON.
             */
            divert_stop_blink_timer();
            divert_mode = DIVERT_MODE_WATER_TO_TEST;
            divert_apply_output(true);
            update_water_labels();
            break;

        case DIVERT_MODE_WATER_TO_TEST:
            /*
             * Second click:
             * AUTO DIVERT. Evaluate the current TDS immediately.
             */
            divert_mode = DIVERT_MODE_AUTO;
            divert_evaluate_auto();
            divert_start_blink_timer();
            break;

        case DIVERT_MODE_AUTO:
        default:
            /*
             * Third click:
             * return to default WATER TO TANK, relay OFF, glow OFF.
             */
            divert_stop_blink_timer();
            divert_mode = DIVERT_MODE_WATER_TO_TANK;
            divert_apply_output(false);
            update_water_labels();
            break;
    }
}

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

    if(start_auto_on) {
        lv_roller_set_selected(
            ui_autom_screen_roller_target_mode,
            auto_running_liters_mode ? 0 : 1,
            LV_ANIM_OFF
        );
        return;
    }

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

    if(start_auto_on)
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

    if(start_auto_on)
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
        if(start_auto_on) {
            lv_label_set_text(ui_autom_screen_label_auto_prod_set, "RUNNING");
            lv_obj_set_style_text_color(
                ui_autom_screen_label_auto_prod_set,
                lv_color_hex(0x00C853),
                LV_PART_MAIN | LV_STATE_DEFAULT
            );
        } else if(auto_production_on) {
            lv_label_set_text(ui_autom_screen_label_auto_prod_set, "ENABLED");
            lv_obj_set_style_text_color(
                ui_autom_screen_label_auto_prod_set,
                lv_color_hex(0x00C853),
                LV_PART_MAIN | LV_STATE_DEFAULT
            );
        } else {
            lv_label_set_text(ui_autom_screen_label_auto_prod_set, "DISABLED");
            lv_obj_set_style_text_color(
                ui_autom_screen_label_auto_prod_set,
                lv_color_hex(0x1D47A5),
                LV_PART_MAIN | LV_STATE_DEFAULT
            );
        }
    }

    set_checked(ui_autom_screen_btn_on_off, auto_production_on);
}

static void update_start_auto_state(void)
{
    if(ui_autom_screen_label_start_auto) {
        lv_label_set_text(
            ui_autom_screen_label_start_auto,
            start_auto_on ? "STOP AUTO" : "START AUTO"
        );
    }

    set_checked(ui_autom_screen_btn_start_auto, start_auto_on);
}

static void auto_production_update_runtime_label(void)
{
    if(!ui_autom_screen_countdown_ttg_ltg)
        return;

    static char buf[24];

    if(!start_auto_on) {
        update_autom_target_set_label();
        return;
    }

    if(auto_running_liters_mode) {
        float remaining = (float)autom_liters - auto_liters_produced;

        if(remaining < 0.0f)
            remaining = 0.0f;

        snprintf(buf, sizeof(buf), "%.1f L", remaining);
    } else {
        int hours = auto_time_remaining_seconds / 3600;
        int minutes = (auto_time_remaining_seconds % 3600) / 60;
        int seconds = auto_time_remaining_seconds % 60;

        if(hours > 0)
            snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, minutes, seconds);
        else
            snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
    }

    lv_label_set_text(ui_autom_screen_countdown_ttg_ltg, buf);
}

static void auto_production_stop_tracking(const char * status_text)
{
    if(auto_production_timer) {
        lv_timer_del(auto_production_timer);
        auto_production_timer = NULL;
    }

    start_auto_on = false;
    auto_time_remaining_seconds = 0;
    auto_liters_produced = 0.0f;

    update_start_auto_state();
    update_auto_production_state();
    update_autom_target_set_label();

    if(status_text && ui_autom_screen_label_auto_prod_set)
        lv_label_set_text(ui_autom_screen_label_auto_prod_set, status_text);
}

static void auto_production_timer_cb(lv_timer_t * timer)
{
    (void)timer;

    if(!start_auto_on)
        return;

    bool target_reached = false;

    if(auto_running_liters_mode) {
        /*
         * Integrate current production once per second:
         * liters added = l/h / 3600.
         */
        if(input_flow_valid && input_flow_lph > 0.0f)
            auto_liters_produced += input_flow_lph / 3600.0f;

        if(auto_liters_produced >= (float)autom_liters)
            target_reached = true;
    } else {
        if(auto_time_remaining_seconds > 0)
            auto_time_remaining_seconds--;

        if(auto_time_remaining_seconds <= 0)
            target_reached = true;
    }

    auto_production_update_runtime_label();

    if(target_reached) {
        const char * source =
            auto_running_liters_mode ? "LITERS TARGET" : "TIME TARGET";

        auto_production_stop_tracking("TARGET REACHED");
        auto_stop_start(source);
    }
}

static bool auto_production_start_tracking(void)
{
    if(start_auto_on)
        return false;

    if(!auto_production_on) {
        if(ui_autom_screen_label_auto_prod_set)
            lv_label_set_text(ui_autom_screen_label_auto_prod_set, "ENABLE FIRST");
        return false;
    }

    if(auto_stop_visual_is_active() || overpressure_is_active()) {
        if(ui_autom_screen_label_auto_prod_set)
            lv_label_set_text(ui_autom_screen_label_auto_prod_set, "STOP ACTIVE");
        return false;
    }

    auto_running_liters_mode = autom_is_liters_mode();

    if(auto_running_liters_mode) {
        if(autom_liters <= 0) {
            if(ui_autom_screen_label_auto_prod_set)
                lv_label_set_text(ui_autom_screen_label_auto_prod_set, "SET TARGET");
            return false;
        }

        auto_liters_produced = 0.0f;
    } else {
        if(autom_time_minutes <= 0) {
            if(ui_autom_screen_label_auto_prod_set)
                lv_label_set_text(ui_autom_screen_label_auto_prod_set, "SET TARGET");
            return false;
        }

        auto_time_remaining_seconds = autom_time_minutes * 60;
    }

    start_auto_on = true;

    if(auto_production_timer) {
        lv_timer_del(auto_production_timer);
        auto_production_timer = NULL;
    }

    auto_production_timer =
        lv_timer_create(auto_production_timer_cb, 1000, NULL);

    update_start_auto_state();
    update_auto_production_state();
    auto_production_update_runtime_label();

    printf(
        "AUTO PRODUCTION: started in %s mode\n",
        auto_running_liters_mode ? "LITERS" : "TIME"
    );

    return true;
}

static void auto_production_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    /*
     * Disabling cancels only target monitoring, not the pumps.
     */
    if(auto_production_on && start_auto_on)
        auto_production_stop_tracking(NULL);

    auto_production_on = !auto_production_on;
    update_auto_production_state();
}

static void start_auto_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    if(start_auto_on) {
        /*
         * STOP AUTO cancels only target monitoring.
         * Production keeps running.
         */
        auto_production_stop_tracking("CANCELLED");
        printf("AUTO PRODUCTION: target monitoring cancelled\n");
        return;
    }

    auto_production_start_tracking();
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

    if(input_pressure_valid &&
       input_pressure_bar > OVERPRESSURE_LIMIT_BAR &&
       !overpressure_is_active()) {
        overpressure_trigger();
    }

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

void ui_logic_set_pressure(float bar, bool valid)
{
    if(bar < 0.0f)
        bar = 0.0f;

    if(bar > 100.0f)
        bar = 100.0f;

    update_pressure_input(bar, valid);
}

static void update_tds_input(int tds_ppm, bool valid)
{
    input_tds_ppm = tds_ppm;
    input_tds_valid = valid;

    /*
     * AUTO DIVERT reacts immediately to each new TDS sample.
     */
    divert_evaluate_auto();

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

void ui_logic_set_tds(int ppm, bool valid)
{
    if(ppm < 0)
        ppm = 0;

    if(ppm > 999)
        ppm = 999;

    update_tds_input(ppm, valid);
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

void ui_logic_set_flow_hz(float hz, bool valid)
{
    input_flow_hz = hz;
    input_flow_hz_valid = valid;

    if(!valid) {
        update_flow_input(0.0f, false);
        return;
    }

    if(hz < 0.0f)
        hz = 0.0f;

    float sensor_factor =
        get_saved_flow_sensor_factor();

    float fine_tuning_factor =
        get_saved_flow_fine_tuning_factor();

    float flow_lph =
        hz *
        sensor_factor *
        fine_tuning_factor;

    update_flow_input(flow_lph, true);
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
	
	ui_logic_set_flow_hz(
    input_flow_hz,
    input_flow_hz_valid
);

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



static bool overpressure_is_active(void)
{
    return overpressure_state != OVERPRESSURE_IDLE;
}

static void overpressure_reset(void)
{
    if(overpressure_timer) {
        lv_timer_del(overpressure_timer);
        overpressure_timer = NULL;
    }

    overpressure_state = OVERPRESSURE_IDLE;
    overpressure_phase_seconds = 0;

    /*
     * Pumps remain OFF.
     * After homing completes, a new start requires a fresh manual command.
     */
    set_checked(ui_main_screen_btn_auto_stop, false);
    auto_stop_visual_set_label("AUTO\nSTOP");
}

static void overpressure_timer_cb(lv_timer_t * timer)
{
    (void) timer;

    switch(overpressure_state) {
        case OVERPRESSURE_HOMING:
            /*
             * Temporary simulation only.
             * Later this state will wait for the S3
             * VALVE FULLY OPEN confirmation.
             */
            if(overpressure_phase_seconds > 0)
                overpressure_phase_seconds--;

            if(overpressure_phase_seconds <= 0) {
                overpressure_state = OVERPRESSURE_COMPLETE;
                overpressure_phase_seconds = 2;
                auto_stop_visual_set_label("COMPLETE");

                printf("OVERPRESSURE: simulated valve homing complete\n");
            }
            break;

        case OVERPRESSURE_COMPLETE:
            if(overpressure_phase_seconds > 0)
                overpressure_phase_seconds--;

            if(overpressure_phase_seconds <= 0)
                overpressure_reset();
            break;

        case OVERPRESSURE_IDLE:
        default:
            overpressure_reset();
            break;
    }
}

static void overpressure_trigger(void)
{
    if(overpressure_is_active())
        return;

    if(start_auto_on)
        auto_production_stop_tracking("OVERPRESSURE");

    /*
     * Overpressure has priority over AUTO STOP.
     * Stop any AUTO STOP timer/sequence before proceeding.
     */
    if(auto_stop_visual_timer) {
        lv_timer_del(auto_stop_visual_timer);
        auto_stop_visual_timer = NULL;
    }

    auto_stop_visual_state = AUTO_STOP_VISUAL_IDLE;
    auto_stop_visual_countdown_seconds = 0;
    auto_stop_visual_phase_seconds = 0;

    /*
     * Immediate physical shutdown:
     * HP Pump OFF first, then Feed Pump OFF.
     */
    hp_pump_on = false;
    feed_pump_on = false;

    update_hp_pump_state();
    update_feed_pump_state();
    update_machine_outputs();

    nerd_m31_write_coil(M31_COIL_HP_PUMP, false);
    nerd_m31_write_coil(M31_COIL_FEED_PUMP, false);

    overpressure_state = OVERPRESSURE_HOMING;
    overpressure_phase_seconds = 2;

    set_checked(ui_main_screen_btn_auto_stop, true);
    auto_stop_visual_set_label("OVERPRESSURE");

    if(overpressure_timer) {
        lv_timer_del(overpressure_timer);
        overpressure_timer = NULL;
    }

    overpressure_timer =
        lv_timer_create(overpressure_timer_cb, 1000, NULL);

    printf(
        "OVERPRESSURE: %.2f bar > %.2f bar - both pumps OFF immediately\n",
        input_pressure_bar,
        OVERPRESSURE_LIMIT_BAR
    );
}

static void auto_stop_visual_set_label(const char * text)
{
    if(ui_main_screen_label_auto_stop)
        lv_label_set_text(ui_main_screen_label_auto_stop, text);
}

static bool auto_stop_visual_is_active(void)
{
    return auto_stop_visual_state != AUTO_STOP_VISUAL_IDLE;
}

static void auto_stop_visual_reset(void)
{
    if(auto_stop_visual_timer) {
        lv_timer_del(auto_stop_visual_timer);
        auto_stop_visual_timer = NULL;
    }

    auto_stop_visual_state = AUTO_STOP_VISUAL_IDLE;
    auto_stop_visual_countdown_seconds = 0;
    auto_stop_visual_phase_seconds = 0;

    set_checked(ui_main_screen_btn_auto_stop, false);
    auto_stop_visual_set_label("AUTO\nSTOP");

    update_start_auto_state();
    update_auto_production_state();
}

static void auto_stop_visual_emergency_abort(void)
{
    if(!auto_stop_visual_is_active())
        return;

    /*
     * Highest-priority manual override:
     * HP Pump OFF first, then Feed Pump OFF.
     */
    hp_pump_on = false;
    feed_pump_on = false;

    update_hp_pump_state();
    update_feed_pump_state();
    update_machine_outputs();

    nerd_m31_write_coil(M31_COIL_HP_PUMP, false);
    nerd_m31_write_coil(M31_COIL_FEED_PUMP, false);

    auto_stop_visual_state = AUTO_STOP_VISUAL_ABORT_DELAY;
    auto_stop_visual_phase_seconds = 2;

    set_checked(ui_main_screen_btn_auto_stop, true);
    auto_stop_visual_set_label("EMERGENCY\nSTOP");

    printf("AUTO STOP: emergency manual abort - both pumps OFF\n");
}

static void auto_stop_visual_timer_cb(lv_timer_t * timer)
{
    (void) timer;

    static char countdown_buf[24];

    switch(auto_stop_visual_state) {
        case AUTO_STOP_VISUAL_HOMING:
            /*
             * Temporary simulation only.
             * Later this transition will wait for the S3
             * VALVE FULLY OPEN confirmation.
             */
            if(auto_stop_visual_phase_seconds > 0)
                auto_stop_visual_phase_seconds--;

            if(auto_stop_visual_phase_seconds <= 0) {
                auto_stop_visual_state = AUTO_STOP_VISUAL_COUNTDOWN;
                auto_stop_visual_countdown_seconds = 30;

                snprintf(
                    countdown_buf,
                    sizeof(countdown_buf),
                    "STOP IN\n%d s",
                    auto_stop_visual_countdown_seconds
                );

                auto_stop_visual_set_label(countdown_buf);
            }
            break;

        case AUTO_STOP_VISUAL_COUNTDOWN:
            if(auto_stop_visual_countdown_seconds > 0)
                auto_stop_visual_countdown_seconds--;

            if(auto_stop_visual_countdown_seconds > 0) {
                snprintf(
                    countdown_buf,
                    sizeof(countdown_buf),
                    "STOP IN\n%d s",
                    auto_stop_visual_countdown_seconds
                );

                auto_stop_visual_set_label(countdown_buf);
            } else {
                /*
                 * Normal AUTO STOP:
                 * HP Pump OFF first.
                 */
                hp_pump_on = false;
                update_hp_pump_state();
                update_machine_outputs();
                nerd_m31_write_coil(M31_COIL_HP_PUMP, false);

                auto_stop_visual_state = AUTO_STOP_VISUAL_HP_OFF;
                auto_stop_visual_phase_seconds = 2;
                auto_stop_visual_set_label("HP PUMP\nOFF");

                printf("AUTO STOP: HP Pump OFF\n");
            }
            break;

        case AUTO_STOP_VISUAL_HP_OFF:
            if(auto_stop_visual_phase_seconds > 0)
                auto_stop_visual_phase_seconds--;

            if(auto_stop_visual_phase_seconds <= 0) {
                /*
                 * Two seconds after HP Pump OFF:
                 * Feed Pump OFF and sequence complete.
                 */
                feed_pump_on = false;
                update_feed_pump_state();
                update_machine_outputs();
                nerd_m31_write_coil(M31_COIL_FEED_PUMP, false);

                auto_stop_visual_state = AUTO_STOP_VISUAL_COMPLETE;
                auto_stop_visual_phase_seconds = 2;
                auto_stop_visual_set_label("COMPLETE");

                printf("AUTO STOP: Feed Pump OFF - sequence complete\n");
            }
            break;

        case AUTO_STOP_VISUAL_ABORT_DELAY:
            /*
             * Both pumps are already OFF.
             * Wait approximately two seconds before simulated homing.
             */
            if(auto_stop_visual_phase_seconds > 0)
                auto_stop_visual_phase_seconds--;

            if(auto_stop_visual_phase_seconds <= 0) {
                auto_stop_visual_state = AUTO_STOP_VISUAL_ABORT_HOMING;
                auto_stop_visual_phase_seconds = 2;
                auto_stop_visual_set_label("HOMING");

                printf("AUTO STOP abort: simulated valve homing\n");
            }
            break;

        case AUTO_STOP_VISUAL_ABORT_HOMING:
            if(auto_stop_visual_phase_seconds > 0)
                auto_stop_visual_phase_seconds--;

            if(auto_stop_visual_phase_seconds <= 0) {
                auto_stop_visual_state = AUTO_STOP_VISUAL_COMPLETE;
                auto_stop_visual_phase_seconds = 2;
                auto_stop_visual_set_label("COMPLETE");
            }
            break;

        case AUTO_STOP_VISUAL_COMPLETE:
            if(auto_stop_visual_phase_seconds > 0)
                auto_stop_visual_phase_seconds--;

            if(auto_stop_visual_phase_seconds <= 0)
                auto_stop_visual_reset();
            break;

        case AUTO_STOP_VISUAL_IDLE:
        default:
            auto_stop_visual_reset();
            break;
    }
}

static bool auto_stop_start(const char * source)
{
    /*
     * Single soft-stop entry point:
     * Main Screen, TIME target and LITERS target all call this.
     */
    if(overpressure_is_active() || auto_stop_visual_is_active())
        return false;

    if(start_auto_on)
        auto_production_stop_tracking(NULL);

    auto_stop_visual_state = AUTO_STOP_VISUAL_HOMING;
    auto_stop_visual_phase_seconds = 2;

    set_checked(ui_main_screen_btn_auto_stop, true);
    auto_stop_visual_set_label("HOMING");

    if(auto_stop_visual_timer) {
        lv_timer_del(auto_stop_visual_timer);
        auto_stop_visual_timer = NULL;
    }

    auto_stop_visual_timer =
        lv_timer_create(auto_stop_visual_timer_cb, 1000, NULL);

    printf(
        "AUTO STOP: started by %s - simulated valve homing\n",
        source ? source : "UNKNOWN"
    );

    return true;
}

static void auto_stop_visual_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    auto_stop_start("MAIN SCREEN");
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

    /*
     * Safe startup state on the physical M31.
     * HP Pump is switched OFF before Feed Pump.
     */
    nerd_m31_write_coil(M31_COIL_HP_PUMP, false);
    nerd_m31_write_coil(M31_COIL_FEED_PUMP, false);
    nerd_m31_write_coil(M31_COIL_TANK_TEST_DIVERT, false);

    // Temporary simulated values. Replace with real LAN/Modbus values later.
    update_pressure_input(0.0f, false);
    // update_pressure_input(55.4f, true);

    update_tds_input(0, false);
    // update_tds_input(154, true);

    update_flow_input(0.0f, false);
    // update_flow_input(100.0f, true);

	
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

    if(ui_main_screen_btn_auto_stop) {
        lv_obj_add_event_cb(
            ui_main_screen_btn_auto_stop,
            auto_stop_visual_event,
            LV_EVENT_CLICKED,
            NULL
        );
    }

    if(ui_main_screen_switch_auto_pressure) {
        lv_obj_add_event_cb(ui_main_screen_switch_auto_pressure, auto_pressure_event, LV_EVENT_VALUE_CHANGED, NULL);
    }
}
