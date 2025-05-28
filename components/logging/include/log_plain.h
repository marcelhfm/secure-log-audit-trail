#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t log_plain_init();

esp_err_t log_plain(const char *tag, const char *fmt, ...);

void log_plain_uart_dump();
