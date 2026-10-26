
#ifndef ROOM_H
#define ROOM_H

#include <sqlite3.h>

/*
 * Module Room - Tuan Anh
 *
 * Quan ly tao phong, danh sach phong va tham gia phong.
 * Khong xu ly TCP truc tiep trong module nay.
 */

/* Tao phong thi moi. Tra ve room_id hoac -1 neu loi. */
int room_create(sqlite3 *db,
                int owner_id,
                const char *room_name,
                int question_count,
                int duration_minutes);

/* Tham gia phong thi. Tra ve 0 neu thanh cong. */
int room_join(sqlite3 *db,
              int room_id,
              int user_id);

/*
 * Lay danh sach phong thi duoi dang JSON.
 * Tra ve chuoi duoc cap phat dong.
 * Ben goi phai free() chuoi sau khi su dung.
 */
char *room_list(sqlite3 *db);

#endif
