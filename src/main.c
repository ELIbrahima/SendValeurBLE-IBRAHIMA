#include <stdio.h>                       // Inclut la bibliothèque standard d'entrée/sortie pour les opérations telles que printf.
#include "freertos/FreeRTOS.h"           // Inclut les définitions de base pour le système d'exploitation temps réel FreeRTOS.
#include "freertos/task.h"               // Fournit les fonctions de gestion des tâches dans FreeRTOS.
#include "freertos/event_groups.h"       // Permet la gestion des groupes d'événements dans FreeRTOS pour la synchronisation entre les tâches.
#include "esp_event.h"                   // Fournit les définitions et fonctions de gestion des événements spécifiques à l'ESP-IDF.
#include "nvs_flash.h"                   // Inclut les fonctions pour utiliser la mémoire non volatile (NVS) pour stocker des données qui doivent être conservées entre les redémarrages.
#include "esp_log.h"                     // Fournit les fonctions de journalisation pour le débogage et le suivi du code.
#include "esp_nimble_hci.h"              // Inclut l'interface HCI pour le Bluetooth NimBLE, permettant la communication entre la pile Bluetooth et le matériel de l'ESP32.
#include "nimble/nimble_port.h"          // Inclut les fonctions de portage de NimBLE nécessaires pour initialiser la pile NimBLE sur l'ESP32.
#include "nimble/nimble_port_freertos.h" // Adapte NimBLE pour fonctionner avec le système d'exploitation FreeRTOS.
#include "host/ble_hs.h"                 // Inclut les définitions du host Bluetooth LE (Low Energy) de NimBLE.
#include "services/gap/ble_svc_gap.h"    // Inclut les fonctions de gestion du Generic Access Profile (GAP) pour la configuration des aspects de découverte et de connexion Bluetooth.
#include "services/gatt/ble_svc_gatt.h"  // Fournit les fonctionnalités de gestion du Generic Attribute Profile (GATT) pour Bluetooth Low Energy.
#include "sdkconfig.h"                   // Fichier généré par l'ESP-IDF pour appliquer les configurations du SDK spécifiées via `make menuconfig` ou l'interface de configuration.

char *TAG = "Serveur-BLE-IBRAHIMA"; // Étiquette utilisée pour les logs système, identifie les messages provenant de ce serveur BLE.
uint8_t ble_addr_type;              // Type d'adresse BLE utilisé pour la publicité et la gestion des connexions.

#define SERVICE_UUID 0xA000    // UUID de service personnalisé : un seul service et plusieurs caracteristiques
#define CHAR_WRITE_UUID 0xA006 // UUID de caractéristique d'écriture : Ecrire et envoyer au serveur à partir d'un smarphone par exemple
#define CHAR_TEMP_UUID 0xA005  // UUID de caractéristique de lecture temperature par exemple
#define CHAR_HUM_UUID 0xA003   // UUID de caractéristique de lecture humidité par exemple
#define CHAR_LUX_UUID 0xA004   // UUID de caractéristique de lecture luminosité par exemple

// Function prototypes
void ble_app_advertise(void);
// Lance la procédure de publicité BLE, rendant le dispositif découvrable et connectable par d'autres dispositifs BLE.

static int ble_write_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
// Gère les requêtes d'écriture sur une caractéristique spécifique. Utilisé pour recevoir des données envoyées par un client BLE.

static int ble_temperature_read_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
// Gère les requêtes de lecture pour la caractéristique de la température, renvoyant la valeur de la température au client.

static int ble_humidity_read_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
// Gère les requêtes de lecture pour la caractéristique de l'humidité, renvoyant la valeur de l'humidité au client.

static int ble_lux_read_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
// Gère les requêtes de lecture pour la caractéristique de la luminosité, renvoyant la valeur de la luminosité au client.

// Définitions des services et caractéristiques GATT
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,        // Déclare un service GATT primaire
     .uuid = BLE_UUID16_DECLARE(SERVICE_UUID), // UUID du service, déclaré dans une constante

     // Définition des caractéristiques du service
     .characteristics = (struct ble_gatt_chr_def[]){
         // Caractéristique pour l'écriture (ex. envoyer des données au serveur depuis le client)
         {.uuid = BLE_UUID16_DECLARE(CHAR_WRITE_UUID),
          .flags = BLE_GATT_CHR_F_WRITE,   // Indique que cette caractéristique est writable
          .access_cb = ble_write_handler}, // Fonction callback appelée lors d'une opération d'écriture

         // Caractéristique pour la lecture de la température
         {.uuid = BLE_UUID16_DECLARE(CHAR_TEMP_UUID),
          .flags = BLE_GATT_CHR_F_READ,               // Indique que cette caractéristique est readable
          .access_cb = ble_temperature_read_handler}, // Fonction callback pour lire la température

         // Caractéristique pour la lecture de l'humidité
         {.uuid = BLE_UUID16_DECLARE(CHAR_HUM_UUID),
          .flags = BLE_GATT_CHR_F_READ,            // Indique que cette caractéristique est readable
          .access_cb = ble_humidity_read_handler}, // Fonction callback pour lire l'humidité

         // Caractéristique pour la lecture de la luminosité
         {.uuid = BLE_UUID16_DECLARE(CHAR_LUX_UUID),
          .flags = BLE_GATT_CHR_F_READ,       // Indique que cette caractéristique est readable
          .access_cb = ble_lux_read_handler}, // Fonction callback pour lire la luminosité

         {0} // Entrée de terminaison pour les caractéristiques
     }},

    {0} // Entrée de terminaison pour la liste des services
};

// BLE Write handler

// Gestionnaire pour les écritures sur une caractéristique GATT
static int ble_write_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Accès aux données envoyées par le client
    uint8_t *data = ctxt->om->om_data;
    int data_len = ctxt->om->om_len;

    // Préparation d'un buffer pour stocker les données reçues avec un caractère nul à la fin pour la terminaison de la chaîne C
    char str[data_len + 1];      // +1 pour le caractère nul
    memcpy(str, data, data_len); // Copie les données dans le buffer
    str[data_len] = '\0';        // Assure la terminaison de la chaîne avec un caractère nul

    // Enregistre dans le log la valeur reçue du client
    ESP_LOGI(TAG, "Valeur venant du Client: %s", str);

    return 0; // Retourne 0 pour indiquer le succès du traitement
}

// BLE read handler

// Gestionnaire pour les demandes de lecture de la caractéristique de la température
static int ble_temperature_read_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Définit la valeur de la température à envoyer au client
    const char *temp_value = "Valeur temperature venant du serveur : Temperature = 45 deg C";
    // Ajoute la chaîne de caractères de la température au message buffer (mbuf) pour l'envoi
    os_mbuf_append(ctxt->om, temp_value, strlen(temp_value));
    // Retourne 0 pour indiquer le succès de la fonction
    return 0;
}

// Gestionnaire pour les demandes de lecture de la caractéristique de l'humidité
static int ble_humidity_read_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Définit la valeur de l'humidité à envoyer au client
    const char *hum_value = "Valeur humidite venant du serveur : Humidite = 45%";
    // Ajoute la chaîne de caractères de l'humidité au message buffer (mbuf) pour l'envoi
    os_mbuf_append(ctxt->om, hum_value, strlen(hum_value));
    // Retourne 0 pour indiquer le succès de la fonction
    return 0;
}

// Gestionnaire pour les demandes de lecture de la caractéristique de la luminosité
static int ble_lux_read_handler(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // Définit la valeur de la luminosité à envoyer au client
    const char *lux_value = "Valeur luminosite venant du serveur : Luminosite = 500 lux";
    // Ajoute la chaîne de caractères de la luminosité au message buffer (mbuf) pour l'envoi
    os_mbuf_append(ctxt->om, lux_value, strlen(lux_value));
    // Retourne 0 pour indiquer le succès de la fonction
    return 0;
}

// BLE event handler

// Gestionnaire d'événements pour les interactions GAP (Generic Access Profile)
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    // Traitement basé sur le type d'événement reçu
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT: // Événement de connexion
        ESP_LOGI(TAG, "Événement BLE GAP : CONNEXION %s", event->connect.status == 0 ? "RÉUSSIE" : "ÉCHOUÉE");
        if (event->connect.status == 0) // Si la connexion est réussie
        {
            // Initier la négociation MTU pour optimiser la taille des paquets de données
            ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
        }
        else // Si la connexion a échoué
        {
            // Relancer la publicité pour permettre de nouvelles tentatives de connexion
            ble_app_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT: // Événement de déconnexion
        ESP_LOGI(TAG, "Événement BLE GAP : DÉCONNECTÉ");
        // Relancer la publicité après une déconnexion
        ble_app_advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: // Événement de fin de publicité
        ESP_LOGI(TAG, "Publicité terminée");
        // Relancer la publicité pour rester découvrable par d'autres appareils
        ble_app_advertise();
        break;
    default:
        break;
    }
    return 0; // Retourner 0 indique que l'événement a été traité avec succès
}

// Initialize and start BLE advertising

// Fonction pour démarrer la publicité BLE
void ble_app_advertise(void)
{
    // Définition des champs de publicité
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));                  // Initialisation de la structure à zéro
    const char *device_name = ble_svc_gap_device_name(); // Récupération du nom de l'appareil configuré dans le service GAP
    fields.name = (uint8_t *)device_name;                // Attribution du nom de l'appareil
    fields.name_len = strlen(device_name);               // Définition de la longueur du nom
    fields.name_is_complete = 1;                         // Indicateur que le nom est complet

    // Définition des champs de la publicité BLE
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Erreur lors de la configuration des données de publicité; rc=%d", rc);
        return;
    }

    // Définition des paramètres de publicité
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));   // Initialisation des paramètres à zéro
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Mode de connexion indéfini (connectable)
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // Mode de découverte général

    // Démarrage de la publicité BLE
    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Erreur lors du démarrage de la publicité; rc=%d", rc);
    }
    else
    {
        ESP_LOGI(TAG, "Publicité commencée");
    }
}

// Tâche principale pour exécuter le stack NimBLE
void host_task(void *param)
{
    nimble_port_run(); // Exécute le stack NimBLE jusqu'à ce que nimble_port_stop() soit appelé
}

// Fonction de callback appelée lors de la synchronisation avec le stack BLE
void ble_app_on_sync(void)
{
    ESP_LOGI(TAG, "Callback de synchronisation BLE appelé");
    ble_hs_id_infer_auto(0, &ble_addr_type); // Infère automatiquement le type d'adresse BLE le plus approprié
    ble_app_advertise();                     // Démarre le processus de publicité BLE
}

// Fonction principale du programme
void app_main()
{
    esp_err_t ret;

    // Journalisation du démarrage de l'initialisation du système BLE
    ESP_LOGI(TAG, "Initialisation du système BLE");

    // Initialisation du stockage non volatile (NVS) pour la gestion des données de configuration
    ESP_LOGI(TAG, "Initialisation de NVS");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGI(TAG, "NVS nécessite un effacement; effacement en cours");
        ESP_ERROR_CHECK(nvs_flash_erase()); // Efface le flash NVS si nécessaire
        ret = nvs_flash_init();             // Réinitialise le NVS après l'effacement
    }
    ESP_ERROR_CHECK(ret); // Vérifie si l'initialisation de NVS a réussi

    // Initialisation du port NimBLE, la bibliothèque Bluetooth pour ESP32
    ESP_LOGI(TAG, "Démarrage du port NimBLE");
    nimble_port_init();

    // Configuration des services GAP (Generic Access Profile) et GATT (Generic Attribute Profile)
    ESP_LOGI(TAG, "Configuration des services GAP et GATT");
    ble_svc_gap_device_name_set("Serveur-BLE-IBRAHIMA"); // Définit le nom du dispositif BLE
    ble_svc_gap_init();                                  // Initialise le service GAP
    ble_svc_gatt_init();                                 // Initialise le service GATT

    // Configuration et ajout des services GATT définis par l'utilisateur
    ESP_LOGI(TAG, "Ajout des services GATT");
    ble_gatts_count_cfg(gatt_svcs); // Compte et configure les services GATT
    ble_gatts_add_svcs(gatt_svcs);  // Ajoute les services GATT au dispositif BLE

    // Configuration du callback de synchronisation utilisé pour gérer les événements de connexion BLE
    ESP_LOGI(TAG, "Configuration du callback de synchronisation");
    ble_hs_cfg.sync_cb = ble_app_on_sync;

    // Démarrage de la tâche FreeRTOS pour NimBLE qui gère le stack BLE
    ESP_LOGI(TAG, "Démarrage de la tâche NimBLE FreeRTOS");
    nimble_port_freertos_init(host_task); // Initialise la tâche FreeRTOS pour NimBLE

    ESP_LOGI(TAG, "Configuration BLE terminée avec succès"); // Confirmation que la configuration BLE est terminée
}
