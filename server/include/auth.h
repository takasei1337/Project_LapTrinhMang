
#ifndef AUTH_H
#define AUTH_H

#include <sqlite3.h>

/* Ma trang thai tra ve cua module Auth */
#define AUTH_SUCCESS                  0
#define AUTH_ERR_INVALID_PARAM       -1
#define AUTH_ERR_USER_EXISTS         -2
#define AUTH_ERR_HASH_FAILED         -3
#define AUTH_ERR_DATABASE            -4
#define AUTH_ERR_INVALID_CREDENTIALS -5

/* Dang ky tai khoan moi */
int auth_register(sqlite3 *db, const char *username,
                  const char *password);

/* Dang nhap va tra ve user_id neu thanh cong */
int auth_login(sqlite3 *db, const char *username,
               const char *password);

#endif
