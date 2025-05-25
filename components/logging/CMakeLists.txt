idf_component_register(SRCS "log_secure.c" "log_plain.c" "ringbuf_flash.c"
                    INCLUDE_DIRS "include" REQUIRES esp_partition nvs_flash)


