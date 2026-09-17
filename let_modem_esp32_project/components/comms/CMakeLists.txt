idf_component_register(
    SRCS "lte_tcp.c" "lte_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES lte_types hardware driver app
)
