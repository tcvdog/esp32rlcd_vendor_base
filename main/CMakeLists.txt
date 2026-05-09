idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "./"
    PRIV_REQUIRES port_bsp esp_driver_sdmmc fatfs)
