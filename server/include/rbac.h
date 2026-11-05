#ifndef RBAC_H
#define RBAC_H

#include <sqlite3.h>
#include <stddef.h>

/*
 * ============================================================================
 * Ma trang thai va ma loi cua module RBAC (Role-Based Access Control)
 * ============================================================================
 */
#define RBAC_PERMITTED               0  /* Duoc phep thuc hien hanh dong (ALLOW) */
#define RBAC_ERR_INVALID_PARAM      -1  /* Tham so dau vao khong hop le (db NULL, action khong xac dinh) */
#define RBAC_ERR_UNAUTHORIZED       -2  /* Chua xac thuc: token khong hop le, het han hoac da bi thu hoi */
#define RBAC_ERR_FORBIDDEN          -3  /* Khong co quyen (DENY): role khong du tham quyen hoac khong phai chu so huu */
#define RBAC_ERR_NOT_FOUND          -4  /* Tai nguyen kiem tra (attempt_id) khong ton tai */
#define RBAC_ERR_DATABASE           -5  /* Loi truy van SQLite */

/*
 * ============================================================================
 * Danh sach hanh dong nghiep vu (RBAC Actions)
 * ============================================================================
 */
typedef enum {
    RBAC_ACTION_UNKNOWN = 0,

    /* Danh muc (Classification) */
    RBAC_ACTION_CLASSIFICATION_READ,        /* Xem danh muc Subject/Chapter/Lesson/Topic */
    RBAC_ACTION_CLASSIFICATION_MANAGE,      /* Tao/sua/xoa danh muc Subject/Chapter/Lesson/Topic */

    /* Ngan hang cau hoi (Question) */
    RBAC_ACTION_QUESTION_READ_PUBLIC,       /* Xem danh sach cau hoi cong khai (question_list) */
    RBAC_ACTION_QUESTION_READ_DETAIL_ADMIN, /* Xem chi tiet kem dap an dung (question_get_detail) */
    RBAC_ACTION_QUESTION_MANAGE,            /* Tao/sua/xoa cau hoi (question_create/update/delete) */
    RBAC_ACTION_QUESTION_ASSIGN_TOPIC,      /* Gan/go Topic cho cau hoi (assign/unassign) */
    RBAC_ACTION_QUESTION_VIEW_UNASSIGNED,   /* Xem danh sach cau hoi chua phan loai */

    /* Luyen tap (Practice) */
    RBAC_ACTION_PRACTICE_START,             /* Bat dau luot luyen tap moi */
    RBAC_ACTION_PRACTICE_SUBMIT,            /* Nop bai luyen tap cua chinh minh */
} rbac_action_t;

/*
 * ============================================================================
 * API: rbac_check_role_action
 * ============================================================================
 *
 * Kiem tra ma tran phan quyen thuan tuy giua role va action (Deny by default).
 *
 * Tham so:
 * - role: Chuoi vai tro ("USER", "ADMIN").
 * - action: Hanh dong can thuc hien.
 *
 * Tra ve:
 * - RBAC_PERMITTED (0) neu duoc phep.
 * - RBAC_ERR_FORBIDDEN (-3) neu bi tu choi hoac role/action khong hop le.
 */
int rbac_check_role_action(const char *role, rbac_action_t action);

/*
 * ============================================================================
 * API: rbac_authorize
 * ============================================================================
 *
 * Xac thuc Session token va kiem tra quyen han cua nguoi dung doi voi hanh dong:
 * - Xac thuc token qua session_validate (kiem tra ton tai, het han, thu hoi).
 * - Lay user_id va role thuc te tu CSDL (tuyet doi khong tin tuong role client gui).
 * - Kiem tra role voi hanh dong theo ma tran RBAC (Deny by default).
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo.
 * - token: Chuoi session token 64 ky tu hex.
 * - action: Hanh dong can thuc hien.
 * - out_user_id: Con tro nhan user_id sau khi xac thuc (co the NULL neu khong can).
 *
 * Tra ve:
 * - RBAC_PERMITTED (0) neu hop le va du tham quyen.
 * - RBAC_ERR_UNAUTHORIZED (-2) neu token sai, session het han hoac da revoke.
 * - RBAC_ERR_FORBIDDEN (-3) neu role khong co quyen thuc hien hanh dong nay.
 * - RBAC_ERR_DATABASE (-5) neu gap loi SQLite.
 * - RBAC_ERR_INVALID_PARAM (-1) neu db == NULL.
 */
int rbac_authorize(sqlite3 *db,
                   const char *token,
                   rbac_action_t action,
                   int *out_user_id);

/*
 * ============================================================================
 * API: rbac_check_attempt_ownership
 * ============================================================================
 *
 * Kiem tra quyen so huu cua luot luyen tap / thi (exam_attempts):
 * - Xac thuc session token de lay user_id hop le.
 * - Truy van bang exam_attempts de lay user_id chu so huu cua attempt_id.
 * - Tu choi neu attempt khong ton tai (RBAC_ERR_NOT_FOUND).
 * - Tu choi neu attempt khong thuoc ve user_id cua session (RBAC_ERR_FORBIDDEN).
 *   (Ap dung quy tac: "Submit attempt cua nguoi khac: Deny mac dinh").
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo.
 * - token: Chuoi session token 64 ky tu hex.
 * - attempt_id: ID luot luyen tap (> 0).
 * - out_user_id: Con tro nhan user_id (co the NULL).
 *
 * Tra ve:
 * - RBAC_PERMITTED (0) neu attempt ton tai va thuoc ve dung user_id cua session.
 * - RBAC_ERR_NOT_FOUND (-4) neu attempt_id khong ton tai trong CSDL.
 * - RBAC_ERR_FORBIDDEN (-3) neu attempt thuoc ve user khac.
 * - RBAC_ERR_UNAUTHORIZED (-2) neu session token khong hop le/het han/revoke.
 * - RBAC_ERR_DATABASE (-5) neu gap loi SQLite.
 * - RBAC_ERR_INVALID_PARAM (-1) neu tham so dau vao sai (db NULL, attempt_id <= 0).
 */
int rbac_check_attempt_ownership(sqlite3 *db,
                                 const char *token,
                                 int attempt_id,
                                 int *out_user_id);

/*
 * ============================================================================
 * API: rbac_can_view_question_detail
 * ============================================================================
 *
 * Kiem tra quyen xem thong tin chi tiet cau hoi co chua dap an dung (is_correct):
 * - Chi ADMIN moi duoc phep truy cap du lieu dap an dung qua question_get_detail().
 * - USER thi sinh luon bi tu choi (RBAC_ERR_FORBIDDEN).
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo.
 * - token: Chuoi session token.
 * - out_user_id: Con tro nhan user_id (co the NULL).
 *
 * Tra ve:
 * - RBAC_PERMITTED (0) neu nguoi dung la ADMIN.
 * - RBAC_ERR_FORBIDDEN (-3) neu la USER hoac role khac.
 * - RBAC_ERR_UNAUTHORIZED (-2) neu token khong hop le.
 */
int rbac_can_view_question_detail(sqlite3 *db,
                                  const char *token,
                                  int *out_user_id);

/*
 * ============================================================================
 * Helper API: rbac_action_to_string
 * ============================================================================
 *
 * Chuyen ma hanh dong rbac_action_t thanh chuoi mo ta de ghi log / bao loi.
 */
const char *rbac_action_to_string(rbac_action_t action);

#endif /* RBAC_H */

