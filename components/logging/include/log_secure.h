#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t log_secure_init();

esp_err_t log_secure(const char *tag, const char *fmt, ...);

void log_secure_uart_dump();
