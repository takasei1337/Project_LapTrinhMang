#include "database.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int compare_strings(const void *a, const void *b)
{
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;
    return strcmp(sa, sb);
}

static char *read_file_to_string(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rb = fread(buf, 1, (size_t)size, f);
    buf[rb] = '\0';
    fclose(f);
    return buf;
}

static int table_exists(sqlite3 *db, const char *table)
{
    const char *sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, table, -1, SQLITE_STATIC);
    int exists = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return exists > 0;
}

static int column_exists(sqlite3 *db, const char *table, const char *column)
{
    char sql[256];
    snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name && strcmp(name, column) == 0) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

static int has_foreign_key(sqlite3 *db, const char *from_table, const char *from_col, const char *to_table, const char *to_col)
{
    char sql[256];
    snprintf(sql, sizeof(sql), "PRAGMA foreign_key_list(%s);", from_table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *target_table = (const char *)sqlite3_column_text(stmt, 2);
        const char *from = (const char *)sqlite3_column_text(stmt, 3);
        const char *to = (const char *)sqlite3_column_text(stmt, 4);

        if (target_table && strcasecmp(target_table, to_table) == 0 &&
            from && strcasecmp(from, from_col) == 0) {
            if (to_col == NULL || to_col[0] == '\0' ||
                (to && (to[0] == '\0' || strcasecmp(to, to_col) == 0))) {
                found = 1;
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

static int count_fk_violations(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    int violations = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        violations++;
    }
    sqlite3_finalize(stmt);
    return violations;
}

static int is_migration_applied(sqlite3 *db, const char *version)
{
    if (!table_exists(db, "schema_migrations")) {
        return 0;
    }
    const char *sql = "SELECT COUNT(*) FROM schema_migrations WHERE version = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, version, -1, SQLITE_STATIC);
    int applied = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        applied = (sqlite3_column_int(stmt, 0) > 0);
    }
    sqlite3_finalize(stmt);
    return applied;
}

static int has_any_classification_element(sqlite3 *db)
{
    if (table_exists(db, "subjects") == 1 ||
        table_exists(db, "chapters") == 1 ||
        table_exists(db, "lessons") == 1 ||
        table_exists(db, "topics") == 1) {
        return 1;
    }
    if (table_exists(db, "questions") == 1 && column_exists(db, "questions", "topic_id") == 1) {
        return 1;
    }
    return 0;
}

/*
 * Kiem tra xem database da mang day du toan ven toan bo schema cua 001_classification chua.
 * Yeu cau tat ca cac dieu kien sau phai dong thoi dung:
 * - Bang subjects, chapters, lessons, topics ton tai
 * - Cac cot subjects.code, chapters.subject_id, lessons.chapter_id, topics.lesson_id ton tai
 * - Cot questions.topic_id ton tai
 * - Cac quan he khoa ngoai FOREIGN KEY duoc khai bao day du qua PRAGMA foreign_key_list
 * - PRAGMA foreign_key_check khong co vi pham nao
 * Tuyet doi khong coi la da migrate neu chi co mot vai bang hoac cot le te,
 * hoac co cot nhung khong co dinh nghia rang buoc FOREIGN KEY.
 */
static int is_complete_classification_schema(sqlite3 *db)
{
    if (table_exists(db, "subjects") != 1 ||
        table_exists(db, "chapters") != 1 ||
        table_exists(db, "lessons") != 1 ||
        table_exists(db, "topics") != 1) {
        return 0;
    }

    /* Kiem tra cac cot dinh danh va khoa ngoai cot loi cua tung bang phan cap */
    if (column_exists(db, "subjects", "code") != 1 ||
        column_exists(db, "chapters", "subject_id") != 1 ||
        column_exists(db, "lessons", "chapter_id") != 1 ||
        column_exists(db, "topics", "lesson_id") != 1) {
        return 0;
    }

    if (column_exists(db, "questions", "topic_id") != 1) {
        return 0;
    }

    /* Kiem tra quan he khoa ngoai thuc te bang PRAGMA foreign_key_list */
    if (has_foreign_key(db, "chapters", "subject_id", "subjects", "id") != 1 ||
        has_foreign_key(db, "lessons", "chapter_id", "chapters", "id") != 1 ||
        has_foreign_key(db, "topics", "lesson_id", "lessons", "id") != 1 ||
        has_foreign_key(db, "questions", "topic_id", "topics", "id") != 1) {
        return 0;
    }

    if (count_fk_violations(db) != 0) {
        return 0;
    }

    return 1;
}

int database_apply_migrations(sqlite3 *db, const char *migrations_dir)
{
    /* 1. Kiem tra tham so */
    if (db == NULL || migrations_dir == NULL || migrations_dir[0] == '\0') {
        fprintf(stderr, "Migration error: Invalid parameter (db or migrations_dir is NULL/empty)\n");
        return DATABASE_MIGRATION_ERR_PARAM;
    }

    /* 2. Bao ve transaction cua caller (Requirement 6) */
    if (sqlite3_get_autocommit(db) == 0) {
        fprintf(stderr, "Migration error: Cannot run migrations within an active caller transaction\n");
        return DATABASE_MIGRATION_ERR_TRANSACTION;
    }

    /* 3. Kiem tra va mo thu muc migrations_dir (Requirement 11) */
    DIR *dir = opendir(migrations_dir);
    if (!dir) {
        fprintf(stderr, "Migration error: Cannot open directory '%s'\n", migrations_dir);
        return DATABASE_MIGRATION_ERR_DIR;
    }

    /* 4. Thu thap cac file .sql trong migrations_dir */
    size_t file_count = 0;
    size_t file_capacity = 16;
    char **file_list = (char **)malloc(sizeof(char *) * file_capacity);
    if (!file_list) {
        closedir(dir);
        return DATABASE_MIGRATION_ERR_MEMORY;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        size_t name_len = strlen(entry->d_name);
        if (name_len > 4 && strcmp(entry->d_name + name_len - 4, ".sql") == 0) {
            if (file_count >= file_capacity) {
                file_capacity *= 2;
                char **new_list = (char **)realloc(file_list, sizeof(char *) * file_capacity);
                if (!new_list) {
                    for (size_t i = 0; i < file_count; i++) {
                        free(file_list[i]);
                    }
                    free(file_list);
                    closedir(dir);
                    return DATABASE_MIGRATION_ERR_MEMORY;
                }
                file_list = new_list;
            }

            char *name_copy = (char *)malloc(name_len + 1);
            if (!name_copy) {
                for (size_t i = 0; i < file_count; i++) {
                    free(file_list[i]);
                }
                free(file_list);
                closedir(dir);
                return DATABASE_MIGRATION_ERR_MEMORY;
            }
            memcpy(name_copy, entry->d_name, name_len + 1);
            file_list[file_count++] = name_copy;
        }
    }
    closedir(dir);

    /* Neu khong co file .sql nao trong thu muc, bao loi DATABASE_MIGRATION_ERR_DIR */
    if (file_count == 0) {
        free(file_list);
        fprintf(stderr, "Migration error: No .sql migration files found in directory '%s'\n", migrations_dir);
        return DATABASE_MIGRATION_ERR_DIR;
    }

    /* 5. Sap xep file theo thu tu chu cai / phien ban (Requirement 3) */
    qsort(file_list, file_count, sizeof(char *), compare_strings);

    /* 6. Dam bao bang schema_migrations ton tai */
    const char *create_tbl_sql =
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "    version TEXT PRIMARY KEY,"
        "    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");";
    char *err_msg = NULL;
    if (sqlite3_exec(db, create_tbl_sql, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "Migration error creating schema_migrations: %s\n",
                err_msg ? err_msg : "Unknown");
        sqlite3_free(err_msg);
        for (size_t i = 0; i < file_count; i++) {
            free(file_list[i]);
        }
        free(file_list);
        return DATABASE_MIGRATION_ERR_DATABASE;
    }

    /* 7. Duyet tung file migration theo thu tu */
    int result_status = DATABASE_MIGRATION_SUCCESS;

    for (size_t i = 0; i < file_count; i++) {
        const char *filename = file_list[i];
        size_t fn_len = strlen(filename);
        size_t version_len = fn_len - 4; /* Loai bo phan mo rong .sql */

        char version[128];
        if (version_len >= sizeof(version)) {
            version_len = sizeof(version) - 1;
        }
        memcpy(version, filename, version_len);
        version[version_len] = '\0';

        /* Kiem tra migration da duoc ap dung chua (Requirement 4) */
        if (is_migration_applied(db, version)) {
            continue;
        }

        /*
         * Kiem tra dac biet: Truong hop DB da mang day du schema baseline (Requirement 7 & 8)
         * - Neu day la migration 001_classification va DB da co day du 4 bang + topic_id hop le,
         *   bao gom day du cac rang buoc FOREIGN KEY thuc te (PRAGMA foreign_key_list) va 0 vi pham FK:
         *   tu dong ghi nhan version vao schema_migrations va bo qua viec chay DDL gay xung dot.
         * - Neu DB da co mot so thanh phan (bang hoac cot) nhung thieu rang buoc FK hoac khong day du:
         *   day la schema khong tuong thich, tu choi migration ngay lap tuc (DATABASE_MIGRATION_ERR_INCOMPATIBLE).
         */
        if (strcmp(version, "001_classification") == 0) {
            if (is_complete_classification_schema(db)) {
                const char *ins_sql = "INSERT OR IGNORE INTO schema_migrations (version) VALUES ('001_classification');";
                if (sqlite3_exec(db, ins_sql, NULL, NULL, NULL) != SQLITE_OK) {
                    result_status = DATABASE_MIGRATION_ERR_DATABASE;
                    break;
                }
                continue;
            } else if (has_any_classification_element(db)) {
                fprintf(stderr, "Migration error: Incompatible classification schema detected for 001_classification\n");
                result_status = DATABASE_MIGRATION_ERR_INCOMPATIBLE;
                break;
            }
        }

        /* Xay dung duong dan file tuyet doi / tuong doi */
        char fullpath[1024];
        size_t dir_len = strlen(migrations_dir);
        if (dir_len > 0 && migrations_dir[dir_len - 1] == '/') {
            snprintf(fullpath, sizeof(fullpath), "%s%s", migrations_dir, filename);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", migrations_dir, filename);
        }

        /* Doc noi dung script */
        char *sql_content = read_file_to_string(fullpath);
        if (!sql_content) {
            fprintf(stderr, "Migration error: Cannot read migration file '%s'\n", fullpath);
            result_status = DATABASE_MIGRATION_ERR_FAILED;
            break;
        }

        /* Thuc thi migration script */
        char *exec_err = NULL;
        int rc = sqlite3_exec(db, sql_content, NULL, NULL, &exec_err);
        free(sql_content);

        if (rc != SQLITE_OK) {
            fprintf(stderr, "Migration error executing '%s': %s\n",
                    filename, exec_err ? exec_err : "Unknown error");
            if (exec_err) {
                sqlite3_free(exec_err);
            }

            /* Thu hoi transaction do runner khoi tao neu chua hoan tat (Requirement 5) */
            if (sqlite3_get_autocommit(db) == 0) {
                sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            }
            result_status = DATABASE_MIGRATION_ERR_FAILED;
            break;
        }

        /* Dam bao transaction da commit (autocommit == 1) */
        if (sqlite3_get_autocommit(db) == 0) {
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            result_status = DATABASE_MIGRATION_ERR_FAILED;
            break;
        }

        /* Kiem tra tinh toan ven foreign key sau khi migration hoan tat */
        if (count_fk_violations(db) > 0) {
            fprintf(stderr, "Migration error: Foreign key check failed after applying '%s'\n", filename);
            result_status = DATABASE_MIGRATION_ERR_INCOMPATIBLE;
            break;
        }
    }

    /* Giai phong danh sach file */
    for (size_t i = 0; i < file_count; i++) {
        free(file_list[i]);
    }
    free(file_list);

    return result_status;
}

