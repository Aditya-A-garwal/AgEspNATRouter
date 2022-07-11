/** Maximum length of the administrator username */
#define MAX_USERNAME_LEN    (32)
/** Maximum length of the administrator password */
#define MAX_PASSWORD_LEN    (32)

httpd_handle_t              server;

char                        adminUsername[32];
char                        adminPassword[32];

esp_err_t       index_handler (httpd_req_t *pReq);
esp_err_t       login_handler (httpd_req_t *pReq);
esp_err_t       changePassword_handler (httpd_req_t *pReq);
esp_err_t       updatePassword_handler (httpd_req_t *pReq);

void            start_httpserver ();
// void            stop_httpserver ();
