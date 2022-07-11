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

/** HTTPD handle to manage the http server configuration of the esp32 */
httpd_handle_t              server              = NULL;

char                        adminUsername[32]   = "admin";
char                        adminPassword[32]   = "admin";

extern const uint8_t        indexPage[] asm ("_binary_index_html_start");
extern const uint8_t        dashboardPage[] asm ("_binary_dashboard_html_start");
extern const uint8_t        changePasswordPage[] asm ("_binary_changePassword_html_start");

/** */
static const httpd_uri_t    indexUri = {
    .uri            = "/index",
    .method         = HTTP_GET,
    .handler        = index_handler,
    .user_ctx       = NULL
};

/** */
static const httpd_uri_t    loginUri = {
    .uri            = "/login",
    .method         = HTTP_GET,
    .handler        = login_handler,
    .user_ctx       = NULL
};

/** */
static const httpd_uri_t    changePasswordUri = {
    .uri            = "/changePassword",
    .method         = HTTP_GET,
    .handler        = changePassword_handler,
    .user_ctx       = NULL
};

/** */
static const httpd_uri_t    updatePasswordUri = {
    .uri            = "/updatePassword",
    .method         = HTTP_GET,
    .handler        = updatePassword_handler,
    .user_ctx       = NULL
};

/**
 * @brief
 *
 * @param pReq
 * @return esp_err_t
 */
esp_err_t
index_handler (httpd_req_t *pReq)
{
    // send the index page which has a login form
    httpd_resp_send (pReq, (const char *)indexPage, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief
 *
 * @param pReq
 * @return esp_err_t
 */
esp_err_t
login_handler (httpd_req_t *pReq)
{
    // get the uri variables (username and password) and check if their values are correct
    // if they are more than 32 characters long each, then they are wrong anyways

    size_t  bufLen;
    char    *buf;

    char    username[MAX_USERNAME_LEN];
    char    password[MAX_PASSWORD_LEN];

    bufLen  = httpd_req_get_url_query_len (pReq) + 1;

    //! send error page
    if (bufLen > 128) {
        printf ("URI too big\n");
        abort ();
    }

    buf     = (char *)malloc (bufLen);

    //! send error page
    if (buf == NULL) {
        printf ("Could not allocate buffer while processing request\n");
        abort ();
    }

    // get the variables from the header one by one and free the buffer afterwards
    if (httpd_req_get_url_query_str (pReq, buf, bufLen) == ESP_OK) {
        //! send error page
        if (httpd_query_key_value (buf, "username", username, sizeof (username)) != ESP_OK) {
            printf ("Request must contain \"username\" field\n");
            abort ();
        }
        //! send error page
        if (httpd_query_key_value (buf, "password", password, sizeof (password)) != ESP_OK) {
            printf ("Request must contain \"password\" field\n");
            abort ();
        }
    }
    free ((void *)buf);

    // printf ("Username:\t%s\nPassword:\t%s\n", username, password);
    // printf ("Admin Username:\t%s\nAdmin Password:\t%s\n", adminUsername, adminPassword);

    if (strncmp (username, adminUsername, MAX_USERNAME_LEN) == 0 &&
        strncmp (password, adminPassword, MAX_PASSWORD_LEN) == 0) {
        httpd_resp_send (pReq, (const char *)dashboardPage, HTTPD_RESP_USE_STRLEN);
    }
    else {
        httpd_resp_send (pReq, (const char *)indexPage, HTTPD_RESP_USE_STRLEN);
    }

    return ESP_OK;
}

/**
 * @brief
 *
 * @param pReq
 * @return esp_err_t
 */
esp_err_t
changePassword_handler (httpd_req_t *pReq)
{
    httpd_resp_send (pReq, (const char *)changePasswordPage, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/**
 * @brief
 *
 * @param pReq
 * @return esp_err_t
 */
esp_err_t
updatePassword_handler (httpd_req_t *pReq)
{
    size_t  bufLen;
    char    *buf;

    char    username[MAX_USERNAME_LEN];
    char    password[MAX_PASSWORD_LEN];

    bufLen  = httpd_req_get_url_query_len (pReq) + 1;

    if (bufLen > 128) {
        printf ("URI too big");
        abort ();
    }

    buf     = (char *)malloc (bufLen);

    if (httpd_req_get_url_query_str (pReq, buf, bufLen) == ESP_OK) {
        //! send error page
        if (httpd_query_key_value (buf, "username", username, sizeof (username)) != ESP_OK) {
            printf ("Request must contain \"username\" field\n");
            abort ();
        }
        //! send error page
        if (httpd_query_key_value (buf, "password", password, sizeof (password)) != ESP_OK) {
            printf ("Request must contain \"password\" field\n");
            abort ();
        }
    }
    free ((void *)buf);

    bufLen  = strnlen (username, MAX_USERNAME_LEN);
    if (bufLen != 0) {
        strncpy (adminUsername, username, bufLen + 1);
    }

    bufLen  = strnlen (password, MAX_PASSWORD_LEN);
    if (bufLen != 0) {
        strncpy (adminPassword, password, bufLen + 1);
    }

    // printf ("New Username:\t%s\nNew Password:\t%s\n", adminUsername, adminPassword);
    // printf ("Sent Username:\t%s\nSent Password:\t%s\n", adminUsername, adminPassword);

    httpd_resp_send (pReq, (const char *)indexPage, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief
 *
 */
void
start_httpserver ()
{
    httpd_config_t      httpConf    = HTTPD_DEFAULT_CONFIG ();

    httpConf.lru_purge_enable   = true;

    printf ("Starting HTTP server on port %d\n", httpConf.server_port);
    if (httpd_start (&server, &httpConf) == ESP_OK) {
        printf ("Registering URI handlers\n");
        httpd_register_uri_handler (server, &indexUri);
        httpd_register_uri_handler (server, &loginUri);
        httpd_register_uri_handler (server, &changePasswordUri);
        httpd_register_uri_handler (server, &updatePasswordUri);

        return;
    }

    printf ("Failed to start HTTP server\n");
}

// static void
// stop_httpserver ()
// {
//     if (server) {
//         httpd_stop (server);
//     }
// }
