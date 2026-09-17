idf_component_register(
    SRCS "lte_app.c"
    INCLUDE_DIRS "include"
    REQUIRES driver state_machine comms hardware lte_types lte_test_harness
)
