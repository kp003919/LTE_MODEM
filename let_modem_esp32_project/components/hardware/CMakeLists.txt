idf_component_register(
    SRCS "lte_hw.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_uart esp_driver_gpio driver
)


