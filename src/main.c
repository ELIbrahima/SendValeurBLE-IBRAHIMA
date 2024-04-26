#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"

char *TAG = "BLE-Server";
uint8_t ble_addr_type;

#define SERVICE_UUID 0xA000    // UUID de service personnalisé
#define CHAR_READ_UUID 0xA001  // UUID de caractéristique de lecture
#define CHAR_WRITE_UUID 0xA002 // UUID de caractéristique d'écriture

// Function prototypes
void ble_app_advertise(void);
static int ble_write_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ble_read_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

// GATT service and characteristic definitions
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(SERVICE_UUID), // Example UUID for the service
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(CHAR_READ_UUID),
          .flags = BLE_GATT_CHR_F_READ,
          .access_cb = ble_read_handler},
         {.uuid = BLE_UUID16_DECLARE(CHAR_WRITE_UUID),
          .flags = BLE_GATT_CHR_F_WRITE,
          .access_cb = ble_write_handler},
         {0} // Termination entry
     }},
    {0} // Termination entry for the service list
};

// BLE write handler

static int ble_write_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t *data = ctxt->om->om_data;
    int data_len = ctxt->om->om_len;

    // Assurez-vous que les données sont terminées par un caractère nul pour éviter les débordements.
    char str[data_len + 1]; // +1 pour le caractère nul
    memcpy(str, data, data_len);
    str[data_len] = '\0'; // Ajouter le caractère nul à la fin

    ESP_LOGI(TAG, "Data received: %s", str);

    return 0;
}

// BLE read handler
static int ble_read_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *response = "Data from server";
    os_mbuf_append(ctxt->om, response, strlen(response));
    return 0;
}

// BLE event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        if (event->connect.status != 0)
        {
            ble_app_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE GAP EVENT DISCONNECTED");
        ble_app_advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertisement complete");
        ble_app_advertise();
        break;
    default:
        break;
    }
    return 0;
}

// Initialize and start BLE advertising

void ble_app_advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    const char *device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error starting advertisement; rc=%d", rc);
    }
    else
    {
        ESP_LOGI(TAG, "Advertisement started");
    }
}

void host_task(void *param)
{
    nimble_port_run(); // Runs until nimble_port_stop() is executed
}

void ble_app_on_sync(void)
{
    ESP_LOGI(TAG, "BLE sync callback called");
    ble_hs_id_infer_auto(0, &ble_addr_type);
    ble_app_advertise();
}

void app_main()
{
    esp_err_t ret;

    // Logging de démarrage
    ESP_LOGI(TAG, "Initialisation du système BLE");

    // Initialisation de NVS
    ESP_LOGI(TAG, "Initialisation de NVS");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGI(TAG, "NVS requires erasing; erasing now");
        ESP_ERROR_CHECK(nvs_flash_erase()); // Erase the NVS flash if required
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret); // Check if NVS init was successful

    // Initialisation de NimBLE
    ESP_LOGI(TAG, "Démarrage du port NimBLE");
    nimble_port_init();

    // Configuration des services GAP et GATT
    ESP_LOGI(TAG, "Configuration des services GAP et GATT");
    ble_svc_gap_device_name_set("BLE-Server");
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Configuration et ajout des services GATT
    ESP_LOGI(TAG, "Ajout des services GATT");
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    // Configuration du callback de synchronisation
    ESP_LOGI(TAG, "Configuration du callback de synchronisation");
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // Démarrage de la tâche NimBLE FreeRTOS
    ESP_LOGI(TAG, "Démarrage de la tâche NimBLE FreeRTOS");
    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "Configuration BLE terminée avec succès");
}
