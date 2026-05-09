idf_component_register(
    SRCS "user_app.cpp" "ui_manager.cpp" "vocab_loader.cpp" "sm2_scheduler.cpp"
         "sd_audio_player.cpp" "sd_setup.cpp" "sketch_player.cpp"
         "tts_player.cpp" "arithmetic.cpp" "socrates_content.cpp"
         "socrates_ui.cpp" "snake_game.cpp"
    INCLUDE_DIRS "./"
    PRIV_REQUIRES port_bsp app_bsp lvgl esp_system esp_codec_dev
    REQUIRES json
    PRIV_INCLUDE_DIRS ../port_bsp ../ExternLib/codec_board/include)
