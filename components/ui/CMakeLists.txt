idf_component_register(
    SRCS
        "screens/ui_splash_screen.c"
        "screens/ui_main_screen.c"
        "screens/ui_settings_screen.c"
        "screens/ui_autom_screen.c"
        "screens/ui_log_screen.c"
        "screens/ui_aux_screen.c"
        "screens/ui_debug_screen.c"

        "ui.c"
        "ui_nav.c"
        "ui_logic.c"
        "components/ui_comp_hook.c"
        "ui_helpers.c"

        "images/ui_img_1530556444.c"
        "images/ui_img_625624875.c"
        "images/ui_img_arrow_down_png.c"
        "images/ui_img_arrow_up_png.c"
        "images/ui_img_btn_autom_png.c"
        "images/ui_img_btn_aux_png.c"
        "images/ui_img_btn_debug_png.c"
        "images/ui_img_btn_home_png.c"
        "images/ui_img_btn_log_png.c"
        "images/ui_img_btn_main_off_png.c"
        "images/ui_img_btn_main_on_png.c"
        "images/ui_img_btn_main_png.c"
        "images/ui_img_btn_settings_png.c"
        "images/ui_img_btn_z_top_png.c"
        "images/ui_img_icn_head_png.c"
        "images/ui_img_icn_time_1_png.c"
        "images/ui_img_icn_time_2_png.c"
        "images/ui_img_menu_bar_png.c"
        "images/ui_img_roller_bg_png.c"
        "images/ui_img_top_bar_png.c"
        "images/ui_img_logo_negativo_traspsrente_130_px_png.c"
        "images/ui_img_btn_z_botton_png.c"

        "fonts/ui_font_Small_Font.c"

    INCLUDE_DIRS
        "."
        "screens"
        "images"
        "fonts"
        "components"

    REQUIRES
        lvgl
        esp_driver_tsens
		nvs_flash
        nerd_io
)