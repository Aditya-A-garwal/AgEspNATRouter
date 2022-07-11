/** Maximum length of the administrator username */
#define MAX_USERNAME_LEN    (32)
/** Maximum length of the administrator password */
#define MAX_PASSWORD_LEN    (32)

#define AUTH_PARTITION          "nvs2"

/** Namespace which contains the user's information */
#define AUTH_INFO_NAMESPACE     "\x00"
/** Key for referring to the administrator username */
#define AUTH_USERNAME_KEY       "\x00"
/** Key for referring to the administrator password */
#define AUTH_PASSWORD_KEY       "\x01"

/** Default value of the administrator username */
#define AUTH_USERNAME_DEFAULT   "admin"
/** Default value of the administrator password */
#define AUTH_PASSWORD_DEFAULT   "admin"

httpd_handle_t              server;

char                        adminUsername[32];
char                        adminPassword[32];

nvs_handle_t                loginNvsHandle;

esp_err_t       index_handler (httpd_req_t *pReq);
esp_err_t       login_handler (httpd_req_t *pReq);
esp_err_t       changePassword_handler (httpd_req_t *pReq);
esp_err_t       updatePassword_handler (httpd_req_t *pReq);

void            start_httpserver ();
void            stop_httpserver ();

void            nvs_auth_info_init ();
