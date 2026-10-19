#ifndef CONFIG_H
#define CONFIG_H

/* Network configuration */
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080

/* Server limits */
#define MAX_CLIENTS 50
#define MAX_REQUEST_SIZE 4096
#define MAX_RESPONSE_SIZE 8192

/* Field limits */
#define MAX_USERNAME_LEN 32
#define MAX_PASSWORD_LEN 128
#define MAX_NAME_LEN 100
#define MAX_DETAILS_LEN 512
#define MAX_FEEDBACK_LEN 1000
#define MAX_REASON_LEN 500

/* Banking configuration */
#define CURRENCY_CODE "INR"
#define CURRENCY_SYMBOL "\xE2\x82\xB9"

/* Money limits: values are in paise */
#define MAX_TRANSACTION_PAISE 10000000000LL

/* Storage directory */
#define STORAGE_DIR "storage"

#endif

