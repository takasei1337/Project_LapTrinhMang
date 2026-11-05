#include "rbac.h"
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sqlite3.h>

/*
 * ============================================================================
 * Ma tran phan quyen nghiep vu (RBAC Matrix)
 * ============================================================================
 */
int rbac_check_role_action(const char *role, rbac_action_t action)
{
    if (role == NULL) {
        return RBAC_ERR_FORBIDDEN;
    }

    /* Vai tro ADMIN: Co toan quyen quan tri va thuc hien cac hanh dong cong khai */
    if (strcmp(role, "ADMIN") == 0) {
        switch (action) {
            case RBAC_ACTION_CLASSIFICATION_READ:
            case RBAC_ACTION_CLASSIFICATION_MANAGE:
            case RBAC_ACTION_QUESTION_READ_PUBLIC:
            case RBAC_ACTION_QUESTION_READ_DETAIL_ADMIN:
            case RBAC_ACTION_QUESTION_MANAGE:
            case RBAC_ACTION_QUESTION_ASSIGN_TOPIC:
            case RBAC_ACTION_QUESTION_VIEW_UNASSIGNED:
            case RBAC_ACTION_PRACTICE_START:
            case RBAC_ACTION_PRACTICE_SUBMIT:
                return RBAC_PERMITTED;
            default:
                return RBAC_ERR_FORBIDDEN;
        }
    }

    /* Vai tro USER: Chi duoc xem cong khai va thuc hien bai luyen tap cua chinh minh */
    if (strcmp(role, "USER") == 0) {
        switch (action) {
            case RBAC_ACTION_CLASSIFICATION_READ:
            case RBAC_ACTION_QUESTION_READ_PUBLIC:
            case RBAC_ACTION_PRACTICE_START:
            case RBAC_ACTION_PRACTICE_SUBMIT:
                return RBAC_PERMITTED;

            /* Cam USER thuc hien cac hanh dong quan tri hoac xem dap an dung */
            case RBAC_ACTION_CLASSIFICATION_MANAGE:
            case RBAC_ACTION_QUESTION_READ_DETAIL_ADMIN:
            case RBAC_ACTION_QUESTION_MANAGE:
            case RBAC_ACTION_QUESTION_ASSIGN_TOPIC:
            case RBAC_ACTION_QUESTION_VIEW_UNASSIGNED:
                return RBAC_ERR_FORBIDDEN;

            default:
                return RBAC_ERR_FORBIDDEN;
        }
    }

    /* Deny by default: Tu choi bat ky role nao khong xac dinh (ke ca TEACHER neu chua ho tro) */
    return RBAC_ERR_FORBIDDEN;
}

int rbac_authorize(sqlite3 *db,
                   const char *token,
                   rbac_action_t action,
                   int *out_user_id)
{
    if (out_user_id != NULL) {
        *out_user_id = 0;
    }

    if (db == NULL) {
        return RBAC_ERR_INVALID_PARAM;
    }

    if (token == NULL || token[0] == '\0') {
        return RBAC_ERR_UNAUTHORIZED;
    }

    int session_uid = 0;
    char role[32] = {0};
    int s_rc = session_validate(db, token, &session_uid, role, sizeof(role));
    if (s_rc != SESSION_SUCCESS) {
        if (s_rc == SESSION_ERR_DATABASE) {
            return RBAC_ERR_DATABASE;
        }
        /* SESSION_ERR_INVALID_PARAM, NOT_FOUND, EXPIRED, REVOKED -> UNAUTHORIZED */
        return RBAC_ERR_UNAUTHORIZED;
    }

    int perm = rbac_check_role_action(role, action);
    if (perm == RBAC_PERMITTED) {
        if (out_user_id != NULL) {
            *out_user_id = session_uid;
        }
        return RBAC_PERMITTED;
    }

    return RBAC_ERR_FORBIDDEN;
}

int rbac_check_attempt_ownership(sqlite3 *db,
                                 const char *token,
                                 int attempt_id,
                                 int *out_user_id)
{
    if (out_user_id != NULL) {
        *out_user_id = 0;
    }

    if (db == NULL || attempt_id <= 0) {
        return RBAC_ERR_INVALID_PARAM;
    }

    if (token == NULL || token[0] == '\0') {
        return RBAC_ERR_UNAUTHORIZED;
    }

    int session_uid = 0;
    char role[32] = {0};
    int s_rc = session_validate(db, token, &session_uid, role, sizeof(role));
    if (s_rc != SESSION_SUCCESS) {
        if (s_rc == SESSION_ERR_DATABASE) {
            return RBAC_ERR_DATABASE;
        }
        return RBAC_ERR_UNAUTHORIZED;
    }

    /* Kiem tra exam_attempts xem attempt co ton tai va thuoc ve ai */
    const char *sql = "SELECT user_id FROM exam_attempts WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return RBAC_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt, 1, attempt_id);
    int step = sqlite3_step(stmt);

    if (step == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return RBAC_ERR_NOT_FOUND;
    }

    if (step != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return RBAC_ERR_DATABASE;
    }

    int owner_uid = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    /*
     * Ma tran quyen: Submit attempt cua nguoi khac -> Deny mac dinh
     * Bat ke role la USER hay ADMIN, chi co nguoi so huu moi duoc submit attempt.
     */
    if (owner_uid != session_uid) {
        return RBAC_ERR_FORBIDDEN;
    }

    if (out_user_id != NULL) {
        *out_user_id = session_uid;
    }

    return RBAC_PERMITTED;
}

int rbac_can_view_question_detail(sqlite3 *db,
                                  const char *token,
                                  int *out_user_id)
{
    return rbac_authorize(db, token, RBAC_ACTION_QUESTION_READ_DETAIL_ADMIN, out_user_id);
}

const char *rbac_action_to_string(rbac_action_t action)
{
    switch (action) {
        case RBAC_ACTION_CLASSIFICATION_READ:
            return "CLASSIFICATION_READ";
        case RBAC_ACTION_CLASSIFICATION_MANAGE:
            return "CLASSIFICATION_MANAGE";
        case RBAC_ACTION_QUESTION_READ_PUBLIC:
            return "QUESTION_READ_PUBLIC";
        case RBAC_ACTION_QUESTION_READ_DETAIL_ADMIN:
            return "QUESTION_READ_DETAIL_ADMIN";
        case RBAC_ACTION_QUESTION_MANAGE:
            return "QUESTION_MANAGE";
        case RBAC_ACTION_QUESTION_ASSIGN_TOPIC:
            return "QUESTION_ASSIGN_TOPIC";
        case RBAC_ACTION_QUESTION_VIEW_UNASSIGNED:
            return "QUESTION_VIEW_UNASSIGNED";
        case RBAC_ACTION_PRACTICE_START:
            return "PRACTICE_START";
        case RBAC_ACTION_PRACTICE_SUBMIT:
            return "PRACTICE_SUBMIT";
        default:
            return "UNKNOWN";
    }
}

