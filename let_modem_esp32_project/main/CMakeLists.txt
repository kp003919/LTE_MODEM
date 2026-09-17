idf_component_register(
    SRCS "lte_main.c"
    INCLUDE_DIRS "."
    REQUIRES driver state_machine hardware comms app
)
