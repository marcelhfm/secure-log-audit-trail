#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t log_plain(const void *data, size_t len);
