idf_component_register(
    SRCS "lte_sm.c"
    INCLUDE_DIRS "include"
    REQUIRES driver comms hardware lte_types
)
