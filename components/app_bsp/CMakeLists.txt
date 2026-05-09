idf_component_register(
    SRCS "lvgl_bsp.cpp" "font_cn_16.c" "font_cn_24.c"
    INCLUDE_DIRS "./"
    PRIV_REQUIRES lvgl port_bsp)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
