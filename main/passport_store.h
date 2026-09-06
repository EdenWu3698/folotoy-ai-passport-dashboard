#pragma once

#include "esp_err.h"
#include "passport_data.h"

esp_err_t passport_store_init(void);
esp_err_t passport_store_load(passport_data_t *data);
esp_err_t passport_store_save(const passport_data_t *data);
esp_err_t passport_store_reset(void);
