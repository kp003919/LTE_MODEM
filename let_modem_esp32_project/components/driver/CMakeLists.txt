idf_component_register(
    SRCS "lte_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES lte_types state_machine hardware
)
