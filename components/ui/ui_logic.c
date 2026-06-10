#include "ui.h"
#include "ui_logic.h"
#include <stdio.h>


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

static void set_checked(lv_obj_t * btn, bool checked)
{
    if(!btn) return;

    if(checked)
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(btn, LV_STATE_CHECKED);
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
}

static void hp_pump_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    hp_pump_on = !hp_pump_on;
    update_hp_pump_state();
}

static void flushing_cycle_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    flushing_cycle_on = !flushing_cycle_on;
    update_flushing_cycle_state();
}

static void tank_test_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    water_to_tank = !water_to_tank;
    update_water_labels();
}

static void update_autom_target_set_label(void);

static void update_target_mode_label(void)
{
    if(!ui_autom_screen_roller_target_mode || !ui_autom_screen_label_target_mode_output)
        return;

    uint32_t sel = lv_roller_get_selected(ui_autom_screen_roller_target_mode);

    if(sel == 0) {
        lv_label_set_text(ui_autom_screen_label_target_mode_output, "LITERS MODE");
    } else {
        lv_label_set_text(ui_autom_screen_label_target_mode_output, "TIME MODE");
    }

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
	
	if(ui_autom_screen_countdown_ttg_ltg)
    lv_label_set_text(ui_autom_screen_countdown_ttg_ltg, buf);
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

static void settings_flush_preset_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
        return;

    update_settings_flushing_ttg();
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
        int psi = pressure_preset_bar * 14.5;
        snprintf(psi_buf, sizeof(psi_buf), "- %d", psi);
        lv_label_set_text(ui_main_screen_label_psi_preset, psi_buf);
    }
}

static void pressure_plus_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    if(pressure_preset_bar < 60)
        pressure_preset_bar++;

    update_pressure_preset_label();
}

static void pressure_minus_event(lv_event_t * e)
{
    if(lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    if(pressure_preset_bar > 0)
        pressure_preset_bar--;

    update_pressure_preset_label();
}

void ui_logic_init(void)
{
    update_water_labels();
    update_feed_pump_state();
	update_hp_pump_state();
	update_flushing_cycle_state();
	update_settings_flushing_ttg();
	update_settings_flushing_text_color();
	update_pressure_preset_label();

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
	
	if(ui_autom_screen_roller_target_mode)
	{
    lv_obj_add_event_cb(
        ui_autom_screen_roller_target_mode,
        target_mode_event,
        LV_EVENT_VALUE_CHANGED,
        NULL);

    
	}
	
	update_autom_target_set_label();

	if(ui_autom_screen_btn_countdown_plus)
		lv_obj_add_event_cb(ui_autom_screen_btn_countdown_plus, autom_target_plus_event, LV_EVENT_CLICKED, NULL);

	if(ui_autom_screen_btn_countdown_minus)
		lv_obj_add_event_cb(ui_autom_screen_btn_countdown_minus, autom_target_minus_event, LV_EVENT_CLICKED, NULL);
	
	update_auto_production_state();
	update_start_auto_state();

	if(ui_autom_screen_btn_on_off)
		lv_obj_add_event_cb(ui_autom_screen_btn_on_off, auto_production_event, LV_EVENT_CLICKED, NULL);

	if(ui_autom_screen_btn_start_auto)
		lv_obj_add_event_cb(ui_autom_screen_btn_start_auto, start_auto_event, LV_EVENT_CLICKED, NULL);
	
	update_tank_diverting_state();

	if(ui_aux_screen_btn_tank_diverting)
		lv_obj_add_event_cb(ui_aux_screen_btn_tank_diverting, tank_diverting_event, LV_EVENT_CLICKED, NULL);
	
	
	if(ui_settings_screen_roller_flush_preset)
    lv_obj_add_event_cb(ui_settings_screen_roller_flush_preset, settings_flush_preset_event, LV_EVENT_VALUE_CHANGED, NULL);

	if(ui_main_screen_glow_3)
		lv_obj_add_event_cb(ui_main_screen_glow_3, pressure_plus_event, LV_EVENT_CLICKED, NULL);

	if(ui_main_screen_glow_4)
		lv_obj_add_event_cb(ui_main_screen_glow_4, pressure_minus_event, LV_EVENT_CLICKED, NULL);

}