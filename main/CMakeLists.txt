idf_component_register(SRCS "main.cpp"
                             "bsp/bsp_i2c.cpp"
                             "bsp/bsp_pca9557.cpp"
                             "bsp/bsp_lcd.cpp"
                             "bsp/bsp_touch.cpp"
                             "bsp/bsp_qmi8658.cpp"
                             "lvgl/lvgl_init.cpp"
                             "app/pet_state.cpp"
                             "app/pet_save.cpp"
                             "app/pet_idle_events.cpp"
                             "app/ble_pet.cpp"
                             # Gacha UI bytes (no KOS wrapper needed — AppGacha
                             # delegates to these functions).
                             "app/pet_game_gacha.cpp"
                             # KOS core.
                             "kos/kos.cpp"
                             "kos/kos_app_registry.cpp"
                             "kos/kos_display.cpp"
                             "kos/kos_input.cpp"
                             "kos/kos_launcher.cpp"
                             # KOS Apps.
                             "kos/kos_app_pet.cpp"
                             "kos/kos_app_gacha.cpp"
                             "kos/kos_app_settings.cpp"
                    PRIV_REQUIRES driver esp_driver_gpio esp_driver_spi esp_driver_ledc esp_driver_i2c esp_lcd espressif__esp_lcd_touch espressif__esp_lvgl_port lvgl__lvgl heap esp_timer nvs_flash bt
                    INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-deprecated-enum-enum-conversion)
