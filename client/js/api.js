/*
 * api.js - Lop giao tiep giua giao dien va server.
 *
 * Moi request deu theo dinh dang trong docs/protocol.md:
 *   { type, request_id, payload }
 * va response:
 *   { type, request_id, success, data, error }
 *
 * Hai che do:
 *   - "mock": dung MockServer (mock-server.js) de dung giao dien khi chua co backend.
 *   - "ws"  : gui JSON qua WebSocket gateway cua Tuan Anh.
 * Doi che do bang cach sua Api.config.mode ben duoi.
 */
var Api = (function () {
  var config = {
    mode: 'mock',                 // 'mock' hoac 'ws'
    wsUrl: 'ws://localhost:8081', // dia chi gateway, thong nhat lai voi Tuan Anh
    timeoutMs: 10000
  };

  var counter = 0;
  function nextRequestId() {
    counter += 1;
    return 'req_' + String(counter).padStart(3, '0');
  }

  /* ---------- Transport: mock ---------- */
  function sendMock(req) {
    return MockServer.handle(req);
  }

  /* ---------- Transport: WebSocket gateway ---------- */
  var ws = null;
  var wsReady = null;
  var pending = {}; // request_id -> { resolve, reject, timer }

  function connectWs() {
    if (wsReady) return wsReady;
    wsReady = new Promise(function (resolve, reject) {
      ws = new WebSocket(config.wsUrl);
      ws.onopen = function () { resolve(ws); };
      ws.onerror = function () {
        wsReady = null;
        reject(makeError('NETWORK_ERROR', 'Không kết nối được tới gateway'));
      };
      ws.onclose = function () {
        wsReady = null;
        Object.keys(pending).forEach(function (id) {
          clearTimeout(pending[id].timer);
          pending[id].reject(makeError('NETWORK_ERROR', 'Mất kết nối tới server'));
          delete pending[id];
        });
      };
      ws.onmessage = function (ev) {
        var res;
        try { res = JSON.parse(ev.data); } catch (e) { return; }
        var p = res && pending[res.request_id];
        if (!p) return; // co the la thong bao server tu day xuong, bo qua
        clearTimeout(p.timer);
        delete pending[res.request_id];
        p.resolve(res);
      };
    });
    return wsReady;
  }

  function sendWs(req) {
    return connectWs().then(function (sock) {
      return new Promise(function (resolve, reject) {
        var timer = setTimeout(function () {
          delete pending[req.request_id];
          reject(makeError('TIMEOUT', 'Server không phản hồi'));
        }, config.timeoutMs);
        pending[req.request_id] = { resolve: resolve, reject: reject, timer: timer };
        sock.send(JSON.stringify(req));
      });
    });
  }

  /* ---------- Ham chung ---------- */
  function makeError(code, message) {
    var e = new Error(message);
    e.code = code;
    return e;
  }

  // Gui request, tra ve data neu success, nem loi { code, message } neu that bai
  function request(type, payload) {
    var req = { type: type, request_id: nextRequestId(), payload: payload || {} };
    var send = config.mode === 'ws' ? sendWs : sendMock;
    return send(req).then(function (res) {
      if (!res || res.request_id !== req.request_id) {
        throw makeError('INVALID_RESPONSE', 'Phản hồi không hợp lệ từ server');
      }
      if (!res.success) {
        var err = res.error || {};
        throw makeError(err.code || 'INTERNAL_ERROR', err.message || 'Có lỗi xảy ra');
      }
      return res.data;
    });
  }

  return {
    config: config,
    request: request,

    // Tai khoan (The Anh)
    register: function (username, password) { return request('AUTH_REGISTER', { username: username, password: password }); },
    login: function (username, password) { return request('AUTH_LOGIN', { username: username, password: password }); },
    logout: function () { return request('AUTH_LOGOUT', {}); },

    // Phong thi (Tuan Anh)
    roomCreate: function (roomName, questionCount, durationMinutes) {
      return request('ROOM_CREATE', { room_name: roomName, question_count: questionCount, duration_minutes: durationMinutes });
    },
    roomList: function () { return request('ROOM_LIST', {}); },
    roomJoin: function (roomId) { return request('ROOM_JOIN', { room_id: roomId }); },

    // Bai thi (Cong Minh)
    examStart: function (roomId) { return request('EXAM_START', { room_id: roomId }); },
    examAnswer: function (attemptId, questionId, answerId) {
      return request('EXAM_ANSWER', { attempt_id: attemptId, question_id: questionId, answer_id: answerId });
    },
    examSubmit: function (attemptId) { return request('EXAM_SUBMIT', { attempt_id: attemptId }); },
    examResult: function (attemptId) { return request('EXAM_RESULT', { attempt_id: attemptId }); },
    examHistory: function () { return request('EXAM_HISTORY', {}); },

    // De xuat bo sung (docs/DE_XUAT_API_KET_QUA.md)
    examReview: function (attemptId) { return request('EXAM_REVIEW', { attempt_id: attemptId }); },
    roomResult: function (roomId) { return request('ROOM_RESULT', { room_id: roomId }); },
    roomStart: function (roomId) { return request('ROOM_START', { room_id: roomId }); },
    roomFinish: function (roomId) { return request('ROOM_FINISH', { room_id: roomId }); }
  };
})();
