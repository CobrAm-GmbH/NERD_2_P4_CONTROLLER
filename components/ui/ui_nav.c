#include "ui_nav.h"
#include "ui.h"
#include "ui_helpers.h"


static void nav_home(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
        _ui_screen_change(&ui_splash_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_splash_screen_screen_init);
}

static void nav_main(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
        _ui_screen_change(&ui_main_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_main_screen_screen_init);
}

static void nav_settings(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
        _ui_screen_change(&ui_settings_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_settings_screen_screen_init);
}

static void nav_autom(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
        _ui_screen_change(&ui_autom_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_autom_screen_screen_init);
}

static void nav_log(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
        _ui_screen_change(&ui_log_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_log_screen_screen_init);
}

static void nav_aux(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
        _ui_screen_change(&ui_aux_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_aux_screen_screen_init);
}

static void nav_debug(lv_event_t * e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED)
        _ui_screen_change(&ui_debug_screen, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_debug_screen_screen_init);
}

#define ADD(btn, cb) do { if(btn) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL); } while(0)

static void add_menu(lv_obj_t *home, lv_obj_t *main, lv_obj_t *settings, lv_obj_t *autom,
                     lv_obj_t *log, lv_obj_t *aux, lv_obj_t *debug)
{
    ADD(home, nav_home);
    ADD(main, nav_main);
    ADD(settings, nav_settings);
    ADD(autom, nav_autom);
    ADD(log, nav_log);
    ADD(aux, nav_aux);
    ADD(debug, nav_debug);
}

void ui_nav_init_all(void)
{
	
    
	add_menu(ui_splash_btn_menu_home, ui_splash_btn_menu_main, ui_splash_btn_menu_settings,
             ui_splash_btn_menu_autom, ui_splash_btn_menu_log, ui_splash_btn_menu_aux,
             ui_splash_btn_menu_debug);
			 

    add_menu(ui_main_btn_menu_home, ui_main_btn_menu_main, ui_main_btn_menu_settings,
             ui_main_btn_menu_autom, ui_main_btn_menu_log, ui_main_btn_menu_aux,
             ui_main_btn_menu_debug);

    add_menu(ui_settings_btn_menu_home, ui_settings_btn_menu_main, ui_settings_btn_menu_settings,
             ui_settings_btn_menu_autom, ui_settings_btn_menu_log, ui_settings_btn_menu_aux,
             ui_settings_btn_menu_debug);

    add_menu(ui_autom_btn_menu_home, ui_autom_btn_menu_main, ui_autom_btn_menu_settings,
             ui_autom_btn_menu_autom, ui_autom_btn_menu_log, ui_autom_btn_menu_aux,
             ui_autom_btn_menu_debug);

    add_menu(ui_log_btn_menu_home, ui_log_btn_menu_main, ui_log_btn_menu_settings,
             ui_log_btn_menu_autom, ui_log_btn_menu_log, ui_log_btn_menu_aux,
             ui_log_btn_menu_debug);

    add_menu(ui_aux_btn_menu_home, ui_aux_btn_menu_main, ui_aux_btn_menu_settings,
             ui_aux_btn_menu_autom, ui_aux_btn_menu_log, ui_aux_btn_menu_aux,
             ui_aux_btn_menu_debug);

    add_menu(ui_debug_btn_menu_home, ui_debug_btn_menu_main, ui_debug_btn_menu_settings,
             ui_debug_btn_menu_autom, ui_debug_btn_menu_log, ui_debug_btn_menu_aux,
             ui_debug_btn_menu_debug);
}
