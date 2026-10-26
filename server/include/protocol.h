
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Cong TCP mac dinh cua server */
#define SERVER_PORT 8080

/* So byte chua do dai JSON o dau moi message */
#define MESSAGE_HEADER_SIZE 4

/* Kich thuoc JSON toi da: 1 MiB */
#define MAX_MESSAGE_SIZE (1024U * 1024U)

/*
 * Dinh dang message:
 * [4 byte do dai][JSON payload]
 *
 * Do dai duoc ma hoa theo network byte order.
 * JSON payload su dung UTF-8.
 *
 * Request:
 * {
 *   "type": "AUTH_LOGIN",
 *   "request_id": "req_001",
 *   "payload": {}
 * }
 *
 * Response:
 * {
 *   "type": "AUTH_LOGIN_RESULT",
 *   "request_id": "req_001",
 *   "success": true,
 *   "data": {},
 *   "error": null
 * }
 */


/* Authentication - The Anh */
#define REQ_AUTH_REGISTER     "AUTH_REGISTER"
#define REQ_AUTH_LOGIN        "AUTH_LOGIN"
#define REQ_AUTH_LOGOUT       "AUTH_LOGOUT"
#define REQ_AUTH_PROFILE      "AUTH_PROFILE"
#define REQ_AUTH_UPDATE       "AUTH_UPDATE"

/* Practice & Questions - The Anh */
#define REQ_PRACTICE_START    "PRACTICE_START"
#define REQ_PRACTICE_SUBMIT   "PRACTICE_SUBMIT"
#define REQ_QUESTION_LIST     "QUESTION_LIST"

/* Room - Tuan Anh */
#define REQ_ROOM_CREATE       "ROOM_CREATE"
#define REQ_ROOM_LIST         "ROOM_LIST"
#define REQ_ROOM_JOIN         "ROOM_JOIN"

/* Exam & Results - Cong Minh */
#define REQ_EXAM_START        "EXAM_START"
#define REQ_EXAM_ANSWER       "EXAM_ANSWER"
#define REQ_EXAM_SUBMIT       "EXAM_SUBMIT"
#define REQ_EXAM_RESULT       "EXAM_RESULT"
#define REQ_EXAM_HISTORY      "EXAM_HISTORY"

#endif
