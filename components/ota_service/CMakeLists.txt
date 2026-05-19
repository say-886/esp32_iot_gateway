idf_component_register(
    SRCS
        "ota_service.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_https_ota
)
