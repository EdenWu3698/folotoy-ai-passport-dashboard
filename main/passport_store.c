#include "passport_store.h"

#include <stddef.h>
#include <string.h>

#include "nvs.h"

#define PASSPORT_STORE_MAGIC UINT32_C(0x50535054)
#define PASSPORT_STORE_VERSION UINT32_C(4)

typedef struct {
    uint32_t magic;
    uint32_t version;
    passport_data_t data;
} passport_store_blob_t;

static nvs_handle_t s_handle;
static bool s_initialized;
/* Passport snapshots are large enough to overflow the app task when they are
 * copied onto its stack during a sync.  Store the serialization scratch space
 * in static RAM; load happens at boot and save runs only on the app task. */
static passport_store_blob_t s_blob;

esp_err_t passport_store_init(void)
{
    esp_err_t error = nvs_open("passport", NVS_READWRITE, &s_handle);
    s_initialized = error == ESP_OK;
    return error;
}

esp_err_t passport_store_load(passport_data_t *data)
{
    size_t size = sizeof(s_blob);
    size_t header_size = offsetof(passport_store_blob_t, data);
    size_t version_3_size = header_size + offsetof(passport_data_t, cc_daily);
    esp_err_t error;

    if (!s_initialized || data == NULL) return ESP_ERR_INVALID_STATE;
    memset(data, 0, sizeof(*data));
    memset(&s_blob, 0, sizeof(s_blob));
    error = nvs_get_blob(s_handle, "data", &s_blob, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    if (s_blob.magic != PASSPORT_STORE_MAGIC) return ESP_ERR_INVALID_VERSION;
    if (s_blob.version == PASSPORT_STORE_VERSION && size == sizeof(s_blob)) {
        *data = s_blob.data;
        return ESP_OK;
    }
    /* Version 3 ends immediately before the appended daily Kimi statistics. */
    if (s_blob.version == 3U && size == version_3_size) {
        memcpy(data, &s_blob.data, size - header_size);
        return ESP_OK;
    }
    /* Version 2 used the same CC record footprint for daily token/cost data.
     * Preserve every other page but invalidate that record so it cannot be
     * mistaken for Kimi's window quotas before the next local sync. */
    if (s_blob.version == 2U && size == version_3_size) {
        memcpy(data, &s_blob.data, size - header_size);
        memset(&data->cc, 0, sizeof(data->cc));
        return ESP_OK;
    }
    /* Version 1 ended at stamps. Later fields were appended, so the serialized
     * prefix can be copied safely and both quota pages start empty. */
    if (s_blob.version == 1U && size > header_size && size < sizeof(s_blob)) {
        memcpy(data, &s_blob.data, size - header_size);
        return ESP_OK;
    }
    return ESP_ERR_INVALID_VERSION;
}

esp_err_t passport_store_save(const passport_data_t *data)
{
    esp_err_t error;

    if (!s_initialized || data == NULL) return ESP_ERR_INVALID_STATE;
    s_blob.magic = PASSPORT_STORE_MAGIC;
    s_blob.version = PASSPORT_STORE_VERSION;
    s_blob.data = *data;
    error = nvs_set_blob(s_handle, "data", &s_blob, sizeof(s_blob));
    return error == ESP_OK ? nvs_commit(s_handle) : error;
}

esp_err_t passport_store_reset(void)
{
    esp_err_t error;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    error = nvs_erase_all(s_handle);
    return error == ESP_OK ? nvs_commit(s_handle) : error;
}
