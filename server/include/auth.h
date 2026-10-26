
#ifndef AUTH_H
#define AUTH_H

#include <sqlite3.h>

/* Dang ky tai khoan moi */
int auth_register(sqlite3 *db, const char *username,
                  const char *password);

/* Dang nhap va tra ve user_id neu thanh cong */
int auth_login(sqlite3 *db, const char *username,
               const char *password);

#endif
