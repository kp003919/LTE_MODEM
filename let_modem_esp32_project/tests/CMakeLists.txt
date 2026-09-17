idf_component_register(
    SRCS "lte_test_harness.c"
    INCLUDE_DIRS "include"
    REQUIRES driver comms hardware lte_types
)
