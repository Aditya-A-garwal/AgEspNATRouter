#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_console.h"
#include "esp_http_server.h"

#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "nvs_flash.h"

#include "http_server.h"

#include "secrets/uplink.h"

/** SSID of the wifi access point produced */
#define AP_SSID             "ipnat"
/** Password of the wifi access point produced */
#define AP_PASS             "ipnat@1234"
/** Channel on which to broadcast the wifi */
#define AP_CHANNEL          (1)
/** Maximum number of connected devices */
#define AP_MAX_CON          (10)

/** Number of times to retry connecting to the Uplink before conclusively failing */
#define MAX_RETRY_CON_AMT   (16)
/** Mask to set in event group to signal a connection success */
#define WIFI_CON_BIT        BIT0
/** Mask to set in event group to signal a connection failure */
#define WIFI_FAIL_BIT       BIT1

/** Namespace which contains the user's information */
#define AUTH_INFO_NAMESPACE     "\x00"
/** Key for referring to the administrator username */
#define AUTH_USERNAME_KEY       "\x00"
/** Key for referring to the administrator password */
#define AUTH_PASSWORD_KEY       "\x0a"

/** Default value of the administrator username */
#define AUTH_USERNAME_DEFAULT   "admin"
/** Default value of the administrator password */
#define AUTH_PASSWORD_DEFAULT   "admin"

/** Event group to signal connection success/failure to uplink */
EventGroupHandle_t          wifiEventGroup;
/** Number of times the esp32 has tried connecting to the uplink */
int32_t                     retryCount;

/** Netif handles to manage the AP configuration of the esp32 */
esp_netif_t                 *netifAp;
/** Netif handles to manage the Station configuration of the esp32 */
esp_netif_t                 *netifSta;

nvs_handle_t                loginNvsHandle;


/**
 * @brief               Event Handler for wifi events (such as station connect/disconnect)
 *
 * @param pArgs         Pointer to user-defined arguments for handler
 * @param pEventBase    Structure for base information about the event (type of event)
 * @param pEventId      Identifier of the event
 * @param pEventData    Structure of Data about the event (irrespective of type)
 */
static void
wifi_ap_event_handler (void *pArgs, esp_event_base_t pEventBase, int32_t pEventId, void *pEventData)
{
    // check what kind of event occoured
    // if a connection or disconnection happened, then print information about the station
    // otherwise, report that the event is not known
    switch (pEventId) {

        case WIFI_EVENT_AP_STACONNECTED:
            printf ("Connect Station\t%X:%X:%X:%X:%X:%X\t\tAID\t%d\n",
                MAC2STR (((wifi_event_ap_staconnected_t *)pEventData)->mac),
                ((wifi_event_ap_staconnected_t *)pEventData)->aid);
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            printf ("Disconnect Station\t%X:%X:%X:%X:%X:%X\t\tAID\t%d\n",
                MAC2STR (((wifi_event_ap_stadisconnected_t *)pEventData)->mac),
                ((wifi_event_ap_stadisconnected_t *)pEventData)->aid);
            break;

        default:
            printf ("Unknown event occoured in AP mode\n");
    }
}

/**
 * @brief               Event handler for wifi events (such as uplink connect/disconnect)
 *
 * @param pArgs         Pointer to user-defined arguments for handler
 * @param pEventBase    Structure for base information about the event (type of event)
 * @param pEventId      Identifier of the event
 * @param pEventData    Structure of Data about the event (irrespective of type)
 */
static void
wifi_sta_con_event_handler (void *pArgs, esp_event_base_t pEventBase, int32_t pEventId, void *pEventData)
{
    switch (pEventId) {

        case WIFI_EVENT_STA_START:
            esp_wifi_connect ();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (retryCount++ < MAX_RETRY_CON_AMT) {
                esp_wifi_connect ();
                printf ("Retrying connection to %s\n", UPLINK_SSID);
            }
            else {
                xEventGroupSetBits (wifiEventGroup, WIFI_FAIL_BIT);
            }
            break;
    }
}

/**
 * @brief               Event handler for wifi events (such as uplink connect/disconnect)
 *
 * @param pArgs         Pointer to user-defined arguments for handler
 * @param pEventBase    Structure for base information about the event (type of event)
 * @param pEventId      Identifier of the event
 * @param pEventData    Structure of Data about the event (irrespective of type)
 */
static void
wifi_sta_ip_event_handler (void *pArgs, esp_event_base_t pEventBase, int32_t pEventId, void *pEventData)
{
    if (pEventId == IP_EVENT_STA_GOT_IP) {
        printf ("Recieved IP from uplink\n");
        printf ("%d.%d.%d.%d\n", IP2STR (&((ip_event_got_ip_t *)pEventData)->ip_info.ip));

        xEventGroupSetBits (wifiEventGroup, WIFI_CON_BIT);
        retryCount  = 0;
    }
}

/**
 * @brief           Initializes the ESP32 in Access Point and Station modes
 *
 */
static void
wifi_apsta_init ()
{
    esp_event_handler_instance_t    instanceAnyId;
    esp_event_handler_instance_t    instanceGotIp;

    EventBits_t         staInfo;

    // configuration for AP mode
    wifi_config_t       apConfig = {
        .ap     = {
            .ssid               = AP_SSID,
            .ssid_len           = strlen  (AP_SSID),
            .password           = AP_PASS,
            .authmode           = (strlen (AP_PASS) == 0) ? (WIFI_AUTH_OPEN) : (WIFI_AUTH_WPA_WPA2_PSK),        // only put wpa2 auth if the password is non-empty
            .channel            = AP_CHANNEL,
            .max_connection     = AP_MAX_CON,
            // .beacon_interval    = 1000
        }
    };

    // configuration for STA mode
    wifi_config_t       staConfig = {
        .sta    = {
            .ssid               = UPLINK_SSID,
            .password           = UPLINK_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK
        }
    };

    wifiEventGroup                  = xEventGroupCreate ();

    // initialize netif (abstraction over TCP/IP stack) and create the event loop
    ESP_ERROR_CHECK (esp_netif_init ());
    ESP_ERROR_CHECK (esp_event_loop_create_default ());

    // initialize access point and station modes
    netifAp                         = esp_netif_create_default_wifi_ap ();
    netifSta                        = esp_netif_create_default_wifi_sta ();

    // initialize the wifi drivers with a default config
    // this must be called before any wifi functionality can be used
    wifi_init_config_t  config      = WIFI_INIT_CONFIG_DEFAULT ();
    ESP_ERROR_CHECK (esp_wifi_init (&config));

    // add the wifi event handler(s) for Access Point Events
    ESP_ERROR_CHECK (
        esp_event_handler_instance_register (
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_ap_event_handler,
            NULL,
            NULL
        )
    );

    // add the wifi event handler(s) for Station Wifi Events
    ESP_ERROR_CHECK (esp_event_handler_instance_register (
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_sta_con_event_handler,
        NULL,
        &instanceAnyId
    ));

    // add the wifi event handler(s) for Station IP Events
    ESP_ERROR_CHECK (esp_event_handler_instance_register (
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_sta_ip_event_handler,
        NULL,
        &instanceGotIp
    ));

    // set to simulataneous AP and STA mode nad start the wifi
    ESP_ERROR_CHECK (esp_wifi_set_mode (WIFI_MODE_APSTA));
    ESP_ERROR_CHECK (esp_wifi_set_config (WIFI_IF_AP, &apConfig));
    ESP_ERROR_CHECK (esp_wifi_set_config (WIFI_IF_STA, &staConfig));
    ESP_ERROR_CHECK (esp_wifi_start ());

    printf ("Finished Wifi APSTA init\n");
    printf ("SSID:\t%s\nPASS:\t%s\nCHANNEL:\t%d\n", AP_SSID, AP_PASS, AP_CHANNEL);

    staInfo             = xEventGroupWaitBits (wifiEventGroup, WIFI_CON_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (staInfo & WIFI_CON_BIT) {
        printf ("Connected to Wifi Uplink\n");
    }
    else if (staInfo & WIFI_FAIL_BIT) {
        printf ("Failed to connect to Wifi Uplink\n");
        printf ("Restarting device\n");
        esp_restart ();
    }
    else {
        printf ("Unknown event occoured while trying to connect to uplink\n");
    }

    //! the case where it disconnects and reconnects/ip changes and stuff still needs to be handled
    ESP_ERROR_CHECK (esp_event_handler_unregister (IP_EVENT, IP_EVENT_STA_GOT_IP, instanceGotIp));
    ESP_ERROR_CHECK (esp_event_handler_unregister (WIFI_EVENT, ESP_EVENT_ANY_ID, instanceAnyId));
    vEventGroupDelete (wifiEventGroup);
}

void
nvs_auth_info_init ()
{
    esp_err_t   nvsStatus;
    esp_err_t   valStatus;
    size_t      bufLen;

    // initialize nvs on the "nvsUsr" partition (this is for user related data, not app or driver data/logs)
    // and open the namespace "login", which contains login related information, i.e. username and password
    nvsStatus   = nvs_flash_init_partition ("nvs2");
    if (nvsStatus == ESP_ERR_NVS_NO_FREE_PAGES || nvsStatus == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK (nvs_flash_erase_partition ("nvs2"));
        nvsStatus   = nvs_flash_init_partition ("nvs2");
    }
    ESP_ERROR_CHECK (nvsStatus);

    // open the auth information namespace, which contains the administrator username and password for logging in
    ESP_ERROR_CHECK (nvs_open_from_partition ("nvs2", AUTH_INFO_NAMESPACE, NVS_READWRITE, &loginNvsHandle));

    // check if there are any existing values of the username and password
    // if there are no default values, then write the default values
    bufLen      = MAX_USERNAME_LEN;
    valStatus   = nvs_get_str (loginNvsHandle, AUTH_USERNAME_KEY, adminUsername, &bufLen);

    if (valStatus == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_str (loginNvsHandle, AUTH_USERNAME_KEY, AUTH_USERNAME_DEFAULT);
    }

    bufLen      = MAX_PASSWORD_LEN;
    valStatus   = nvs_get_str (loginNvsHandle, AUTH_PASSWORD_KEY, adminPassword, &bufLen);

    if (valStatus == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_str (loginNvsHandle, AUTH_PASSWORD_KEY, AUTH_PASSWORD_DEFAULT);
    }

    nvs_commit (loginNvsHandle);

    // read the stored username and password and write into the arrays and close the nvs
    ESP_ERROR_CHECK (nvs_get_str (loginNvsHandle, AUTH_USERNAME_KEY, adminUsername, &bufLen));
    ESP_ERROR_CHECK (nvs_get_str (loginNvsHandle, AUTH_PASSWORD_KEY, adminPassword, &bufLen));

    nvs_close (loginNvsHandle);

    printf ("USERNAME %s PASSWORRD %s\n", adminUsername, adminPassword);
}

void
app_main ()
{
    esp_err_t   nvsStatus;

    nvsStatus   = nvs_flash_init ();
    if (nvsStatus == ESP_ERR_NVS_NO_FREE_PAGES || nvsStatus == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK (nvs_flash_erase ());
        nvsStatus   = nvs_flash_init ();
    }
    ESP_ERROR_CHECK (nvsStatus);

    nvs_auth_info_init ();

    wifi_apsta_init ();
    start_httpserver ();
}
