# N.E.R.D. Gen2 UI Merge Test Report

Merged 7 SquareLine single-screen exports into one LVGL UI package.

## Screens

- `ui_splash_screen.c`
- `ui_main_screen.c`
- `ui_settings_screen.c`
- `ui_autom_screen.c`
- `ui_log_screen.c`
- `ui_aux_screen.c`
- `ui_debug_screen.c`

## Deduplicated assets

- `ui_img_625624875.c`: 7 copies reduced to 1
- `ui_img_menu_bar_png.c`: 7 copies reduced to 1
- `ui_img_top_bar_png.c`: 7 copies reduced to 1
- `ui_img_btn_main_off_png.c`: 5 copies reduced to 1
- `ui_img_btn_main_on_png.c`: 5 copies reduced to 1
- `ui_img_roller_bg_png.c`: 3 copies reduced to 1
- `ui_font_Small_Font.c`: 7 copies reduced to 1

## Shared object variables namespaced

- `ui_btn_feed_pump` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_btn_feed_pump`
- `ui_btn_flushing_cycle` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_btn_flushing_cycle`
- `ui_btn_hp_pump` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_btn_hp_pump`
- `ui_btn_menu_autom` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_btn_menu_autom`
- `ui_btn_menu_aux` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_btn_menu_aux`
- `ui_btn_menu_debug` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_btn_menu_debug`
- `ui_btn_menu_home` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_btn_menu_home`
- `ui_btn_menu_log` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_btn_menu_log`
- `ui_btn_menu_main` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_btn_menu_main`
- `ui_btn_menu_settings` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_btn_menu_settings`
- `ui_btn_tank_test` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_btn_tank_test`
- `ui_buttons_container` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_buttons_container`
- `ui_image_ce` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_image_ce`
- `ui_label_feed_pump` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_label_feed_pump`
- `ui_label_flushing_cycle` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_label_flushing_cycle`
- `ui_label_hp_pump` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_label_hp_pump`
- `ui_label_tank_test` appeared in 5 screens; renamed internally per screen, e.g. `ui_main_label_tank_test`
- `ui_menu_bar` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_menu_bar`
- `ui_menu_container` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_menu_container`
- `ui_top_bar` appeared in 7 screens; renamed internally per screen, e.g. `ui_main_top_bar`

## Notes

- Original SquareLine exports are untouched.
- Shared buttons/containers can keep identical semantic names in SquareLine; merge script namespaces them for C.
- Splash screen is loaded by default in `ui_init()`.