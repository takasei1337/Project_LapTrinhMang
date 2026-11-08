/*
 * mock-server.js - Server gia lap chay ngay trong trinh duyet.
 *
 * Muc dich: dung va kiem thu giao dien truoc khi backend + gateway hoan thien.
 * Mo phong dung cau truc request/response va ma loi trong docs/protocol.md
 * va docs/DE_XUAT_API_KET_QUA.md, cung quy tac voi server/src/exam/exam.c.
 *
 * Du lieu chi nam trong bo nho, tai lai trang se mat.
 * Tai khoan mau (mat khau deu la 123456):
 *   giaovien (ADMIN, chu cac phong) | minh, an, binh, chi, dung, em (thi sinh)
 */
var MockServer = (function () {
  var MIN = 60000, DAY = 24 * 3600 * 1000;

  /* ---------- Du lieu mau ---------- */
  var users = [
    { user_id: 1, username: 'minh', role: 'USER' },
    { user_id: 2, username: 'giaovien', role: 'ADMIN' },
    { user_id: 3, username: 'an', role: 'USER' },
    { user_id: 4, username: 'binh', role: 'USER' },
    { user_id: 5, username: 'chi', role: 'USER' },
    { user_id: 6, username: 'dung', role: 'USER' },
    { user_id: 7, username: 'em', role: 'USER' }
  ];
  users.forEach(function (u) { u.password = '123456'; });

  // Ngan hang cau hoi: correct la chi so dap an dung (chi server biet)
  var bank = [
    { c: 'Giao thức nào hoạt động ở tầng vận chuyển?', t: 'Mạng máy tính', d: 'EASY', a: ['TCP', 'IP', 'ARP', 'ICMP'], correct: 0 },
    { c: 'Cổng mặc định của HTTP là?', t: 'Mạng máy tính', d: 'EASY', a: ['21', '25', '80', '443'], correct: 2 },
    { c: 'Hàm nào dùng để chờ kết nối đến trên socket TCP phía server?', t: 'Lập trình socket', d: 'EASY', a: ['connect()', 'accept()', 'send()', 'socket()'], correct: 1 },
    { c: 'htonl() dùng để làm gì?', t: 'Lập trình socket', d: 'MEDIUM', a: ['Chuyển chuỗi sang số', 'Chuyển số 32-bit sang network byte order', 'Mã hoá dữ liệu', 'Nén dữ liệu'], correct: 1 },
    { c: 'TCP đảm bảo điều gì mà UDP không đảm bảo?', t: 'Mạng máy tính', d: 'EASY', a: ['Tốc độ cao hơn', 'Truyền tin cậy, đúng thứ tự', 'Hỗ trợ broadcast', 'Không cần kết nối'], correct: 1 },
    { c: 'poll() khác select() ở điểm nào?', t: 'Lập trình socket', d: 'MEDIUM', a: ['poll() chỉ dùng cho UDP', 'poll() không giới hạn bởi FD_SETSIZE', 'select() không chặn', 'Không có khác biệt'], correct: 1 },
    { c: 'Địa chỉ 127.0.0.1 là?', t: 'Mạng máy tính', d: 'EASY', a: ['Địa chỉ broadcast', 'Địa chỉ loopback', 'Địa chỉ gateway', 'Địa chỉ multicast'], correct: 1 },
    { c: 'Vì sao cần tiền tố độ dài khi gửi JSON qua TCP?', t: 'Lập trình socket', d: 'HARD', a: ['Để mã hoá dữ liệu', 'Vì TCP là luồng byte, không giữ ranh giới thông điệp', 'Để tăng tốc độ', 'Vì JSON bắt buộc'], correct: 1 },
    { c: 'Bắt tay ba bước của TCP gồm?', t: 'Mạng máy tính', d: 'MEDIUM', a: ['SYN, ACK, FIN', 'SYN, SYN-ACK, ACK', 'ACK, ACK, ACK', 'HELLO, OK, DONE'], correct: 1 },
    { c: 'SO_REUSEADDR thường dùng để?', t: 'Lập trình socket', d: 'HARD', a: ['Cho phép bind lại cổng đang ở TIME_WAIT', 'Tăng bộ đệm gửi', 'Bật chế độ không chặn', 'Bật mã hoá'], correct: 0 },
    { c: 'DNS dùng để?', t: 'Mạng máy tính', d: 'EASY', a: ['Cấp phát IP động', 'Phân giải tên miền thành địa chỉ IP', 'Định tuyến gói tin', 'Mã hoá kết nối'], correct: 1 },
    { c: 'recv() trả về 0 nghĩa là?', t: 'Lập trình socket', d: 'MEDIUM', a: ['Có lỗi', 'Phía bên kia đã đóng kết nối', 'Chưa có dữ liệu', 'Bộ đệm đầy'], correct: 1 }
  ];

  var questions = bank.map(function (q, i) {
    var qid = 101 + i;
    return {
      question_id: qid, content: q.c, topic: q.t, difficulty: q.d,
      answers: q.a.map(function (text, j) {
        return { answer_id: qid * 10 + j, content: text, is_correct: j === q.correct };
      })
    };
  });

  var rooms = [
    { room_id: 1, room_name: 'Kiểm tra giữa kỳ Lập trình mạng', owner_id: 2, question_count: 10, duration_minutes: 15, start_time: null, end_time: null, status: 'IN_PROGRESS' },
    { room_id: 2, room_name: 'Ôn tập Socket TCP', owner_id: 2, question_count: 5, duration_minutes: 5, start_time: null, end_time: null, status: 'WAITING' },
    { room_id: 3, room_name: 'Thi thử tuần 1', owner_id: 2, question_count: 8, duration_minutes: 10, start_time: null, end_time: null, status: 'FINISHED' }
  ];
  // Bo de cua phong (room_questions). That ra do phan Room cua Tuan Anh thiet lap.
  var roomQuestions = {
    1: questions.slice(0, 10).map(function (q) { return q.question_id; }),
    2: questions.slice(5, 10).map(function (q) { return q.question_id; }),
    3: questions.slice(2, 10).map(function (q) { return q.question_id; })
  };
  var members = [];
  [[1, 1], [1, 3], [1, 4], [1, 7], [2, 3], [3, 1], [3, 3], [3, 4], [3, 5], [3, 6], [3, 7]].forEach(function (p) {
    members.push({ room_id: p[0], user_id: p[1] });
  });
  var attempts = [];
  var attemptAnswers = {}; // attempt_id -> { question_id: answer_id }
  var nextIds = { user: 8, room: 4, attempt: 1 };

  var currentUser = null; // phien dang nhap (gateway that se quan ly bang session)

  /* ---------- Tien ich ---------- */
  function iso(ms) { return new Date(ms).toISOString().replace(/\.\d{3}Z$/, 'Z'); }
  function now() { return Date.now(); }
  function round2(x) { return Math.round(x * 100) / 100; }
  function ok(req, data) {
    return { type: req.type + '_RESULT', request_id: req.request_id, success: true, data: data, error: null };
  }
  function fail(req, code, message) {
    return { type: req.type + '_RESULT', request_id: req.request_id, success: false, data: null, error: { code: code, message: message } };
  }
  function isPosInt(v) { return typeof v === 'number' && Number.isInteger(v) && v > 0; }
  function findRoom(id) { return rooms.find(function (r) { return r.room_id === id; }); }
  function findQuestion(id) { return questions.find(function (q) { return q.question_id === id; }); }
  function findUser(id) { return users.find(function (u) { return u.user_id === id; }); }
  function isMember(roomId, userId) {
    return members.some(function (m) { return m.room_id === roomId && m.user_id === userId; });
  }
  function canManage(room, user) { return room.owner_id === user.user_id || user.role === 'ADMIN'; }
  function roomEnded(room) {
    return room.status === 'FINISHED' || (room.end_time && now() >= new Date(room.end_time).getTime());
  }
  function memberCount(room) {
    return members.filter(function (m) { return m.room_id === room.room_id && m.user_id !== room.owner_id; }).length;
  }

  // GIA LAP phan "thiet lap bo de" cua Tuan Anh: chon ngau nhien khi tao phong
  function mockSetupRoomQuestions(room) {
    var pool = questions.map(function (q) { return q.question_id; });
    pool.sort(function () { return Math.random() - 0.5; });
    roomQuestions[room.room_id] = pool.slice(0, Math.min(room.question_count, pool.length));
  }

  function countCorrect(a) {
    var ans = attemptAnswers[a.attempt_id] || {};
    return a.question_ids.filter(function (qid) {
      var q = findQuestion(qid);
      return q.answers.some(function (x) { return x.answer_id === ans[qid] && x.is_correct; });
    }).length;
  }

  // Cham diem mot luot thi (chi server lam)
  function grade(a, submittedAtMs) {
    a.correct_count = countCorrect(a);
    a.score = round2(a.correct_count * 10.0 / a.question_ids.length);
    a.submitted_at = iso(submittedAtMs);
    a.status = 'SUBMITTED';
  }

  // Server tu nop bai khi het gio, khong phu thuoc client
  function autoFinalize() {
    attempts.forEach(function (a) {
      if (a.status === 'IN_PROGRESS' && now() > a.deadline_ms) grade(a, a.deadline_ms);
    });
  }

  function newAttempt(room, userId, startMs) {
    var a = {
      attempt_id: nextIds.attempt++, room_id: room.room_id, user_id: userId, mode: 'EXAM',
      question_ids: roomQuestions[room.room_id], started_at: iso(startMs),
      deadline_ms: startMs + room.duration_minutes * MIN,
      status: 'IN_PROGRESS', score: null, correct_count: null, submitted_at: null
    };
    attempts.push(a);
    attemptAnswers[a.attempt_id] = {};
    return a;
  }

  /*
   * Tao luot thi mau: k cau dau tra loi dung, cac cau con lai xen ke sai / bo trong.
   * Dung de man hinh lich su, bang xep hang va xem lai bai co du lieu.
   */
  function seedAttempt(roomId, userId, k, startMs, minutes, submitted) {
    var room = findRoom(roomId);
    var a = newAttempt(room, userId, startMs);
    a.question_ids.forEach(function (qid, i) {
      var q = findQuestion(qid);
      var right = q.answers.findIndex(function (x) { return x.is_correct; });
      if (i < k) attemptAnswers[a.attempt_id][qid] = q.answers[right].answer_id;
      else if (i % 2 === 0) attemptAnswers[a.attempt_id][qid] = q.answers[(right + 1) % 4].answer_id;
    });
    if (submitted) grade(a, startMs + minutes * MIN);
    return a;
  }

  (function seed() {
    var t = now();
    // Lich su cua minh trong phong 3 (da ket thuc)
    [4, 5, 5, 6, 7, 6].forEach(function (k, i) { seedAttempt(3, 1, k, t - (6 - i) * DAY, 7 + i % 3, true); });
    // Ca lop trong phong 3: an, binh, chi, dung da thi; em chua thi
    seedAttempt(3, 3, 7, t - DAY, 6, true);
    seedAttempt(3, 4, 6, t - DAY, 9, true);
    seedAttempt(3, 5, 8, t - DAY, 8, true);
    seedAttempt(3, 6, 3, t - DAY, 10, true);
    // Phong 1 dang thi: an da nop, binh dang lam, em chua thi
    seedAttempt(1, 3, 6, t - 20 * MIN, 12, true);
    seedAttempt(1, 4, 2, t - 3 * MIN, 0, false);
  })();

  function myAttempt(p) {
    return attempts.find(function (x) { return x.attempt_id === p.attempt_id && x.user_id === currentUser.user_id; });
  }

  /* ---------- Xu ly request ---------- */
  var handlers = {
    AUTH_REGISTER: function (req, p) {
      if (!p.username || !p.password) return fail(req, 'INVALID_PAYLOAD', 'Vui lòng nhập tên đăng nhập và mật khẩu');
      if (String(p.password).length < 6) return fail(req, 'INVALID_PAYLOAD', 'Mật khẩu tối thiểu 6 ký tự');
      if (users.some(function (u) { return u.username === p.username; })) return fail(req, 'USERNAME_EXISTS', 'Tên đăng nhập đã tồn tại');
      var u = { user_id: nextIds.user++, username: p.username, password: p.password, role: 'USER' };
      users.push(u);
      return ok(req, { user_id: u.user_id, username: u.username });
    },

    AUTH_LOGIN: function (req, p) {
      var u = users.find(function (x) { return x.username === p.username && x.password === p.password; });
      if (!u) return fail(req, 'INVALID_CREDENTIALS', 'Sai tên đăng nhập hoặc mật khẩu');
      currentUser = u;
      return ok(req, { user_id: u.user_id, username: u.username, role: u.role });
    },

    AUTH_LOGOUT: function (req) { currentUser = null; return ok(req, {}); },

    ROOM_CREATE: function (req, p) {
      // Chi giao vien (ADMIN) duoc tao phong thi
      if (currentUser.role !== 'ADMIN') return fail(req, 'FORBIDDEN', 'Chỉ giáo viên mới được tạo phòng thi');
      if (!p.room_name || !String(p.room_name).trim() || !isPosInt(p.question_count) || !isPosInt(p.duration_minutes)) {
        return fail(req, 'INVALID_PAYLOAD', 'Thông tin phòng không hợp lệ');
      }
      var r = {
        room_id: nextIds.room++, room_name: String(p.room_name).trim(), owner_id: currentUser.user_id,
        question_count: p.question_count, duration_minutes: p.duration_minutes,
        start_time: null, end_time: null, status: 'WAITING'
      };
      rooms.push(r);
      mockSetupRoomQuestions(r);
      return ok(req, { room_id: r.room_id, status: r.status });
    },

    // De xuat them member_count va joined (xem docs/DE_XUAT_API_KET_QUA.md)
    ROOM_LIST: function (req) {
      return ok(req, {
        rooms: rooms.map(function (r) {
          return Object.assign({}, r, { member_count: memberCount(r), joined: isMember(r.room_id, currentUser.user_id) });
        })
      });
    },

    ROOM_JOIN: function (req, p) {
      if (!isPosInt(p.room_id)) return fail(req, 'INVALID_PAYLOAD', 'ID phòng không hợp lệ');
      var r = findRoom(p.room_id);
      if (!r) return fail(req, 'ROOM_NOT_FOUND', 'Phòng không tồn tại');
      if (isMember(r.room_id, currentUser.user_id)) return fail(req, 'ALREADY_JOINED', 'Bạn đã tham gia phòng này');
      if (r.status !== 'WAITING') return fail(req, 'ROOM_NOT_WAITING', 'Phòng không còn nhận thành viên');
      members.push({ room_id: r.room_id, user_id: currentUser.user_id });
      return ok(req, { room_id: r.room_id, status: r.status, joined: true });
    },

    // De xuat cho Tuan Anh: chu phong mo phong thi
    ROOM_START: function (req, p) {
      var r = findRoom(p.room_id);
      if (!r) return fail(req, 'ROOM_NOT_FOUND', 'Phòng không tồn tại');
      if (!canManage(r, currentUser)) return fail(req, 'FORBIDDEN', 'Chỉ chủ phòng mới được mở phòng thi');
      if (r.status !== 'WAITING') return fail(req, 'ROOM_NOT_WAITING', 'Phòng không ở trạng thái chờ');
      r.status = 'IN_PROGRESS';
      r.start_time = iso(now());
      return ok(req, { room_id: r.room_id, status: r.status });
    },

    // De xuat cho Tuan Anh: chu phong ket thuc phong, server thu bai dang lam
    ROOM_FINISH: function (req, p) {
      var r = findRoom(p.room_id);
      if (!r) return fail(req, 'ROOM_NOT_FOUND', 'Phòng không tồn tại');
      if (!canManage(r, currentUser)) return fail(req, 'FORBIDDEN', 'Chỉ chủ phòng mới được kết thúc phòng thi');
      if (r.status !== 'IN_PROGRESS') return fail(req, 'ROOM_NOT_IN_PROGRESS', 'Phòng không đang diễn ra');
      r.status = 'FINISHED';
      r.end_time = iso(now());
      attempts.forEach(function (a) { if (a.room_id === r.room_id && a.status === 'IN_PROGRESS') grade(a, now()); });
      return ok(req, { room_id: r.room_id, status: r.status });
    },

    EXAM_START: function (req, p) {
      if (!isPosInt(p.room_id)) return fail(req, 'INVALID_PAYLOAD', 'ID phòng không hợp lệ');
      var r = findRoom(p.room_id);
      if (!r) return fail(req, 'ROOM_NOT_FOUND', 'Phòng không tồn tại');
      if (!isMember(r.room_id, currentUser.user_id)) return fail(req, 'NOT_ROOM_MEMBER', 'Bạn chưa tham gia phòng này');
      // Giong exam.c: chi thi khi phong dang IN_PROGRESS
      if (r.status !== 'IN_PROGRESS') return fail(req, 'EXAM_NOT_AVAILABLE', 'Phòng chưa cho phép bắt đầu thi');
      if (!(roomQuestions[r.room_id] || []).length) return fail(req, 'EXAM_NOT_AVAILABLE', 'Phòng chưa có bộ đề');
      autoFinalize();
      if (attempts.some(function (a) { return a.room_id === r.room_id && a.user_id === currentUser.user_id && a.status === 'IN_PROGRESS'; })) {
        return fail(req, 'ATTEMPT_ALREADY_STARTED', 'Bạn đang có lượt thi chưa nộp trong phòng này');
      }
      var a = newAttempt(r, currentUser.user_id, now());
      return ok(req, {
        attempt_id: a.attempt_id, room_id: r.room_id, duration_minutes: r.duration_minutes,
        started_at: a.started_at, deadline: iso(a.deadline_ms),
        questions: a.question_ids.map(function (qid) {
          var q = findQuestion(qid);
          return {
            question_id: q.question_id, content: q.content, topic: q.topic, difficulty: q.difficulty,
            // Khong gui is_correct khi dang thi
            answers: q.answers.map(function (x) { return { answer_id: x.answer_id, content: x.content }; })
          };
        })
      });
    },

    EXAM_ANSWER: function (req, p) {
      if (!isPosInt(p.attempt_id) || !isPosInt(p.question_id) || !isPosInt(p.answer_id)) {
        return fail(req, 'INVALID_PAYLOAD', 'Dữ liệu không hợp lệ');
      }
      var a = myAttempt(p);
      if (!a) return fail(req, 'ATTEMPT_NOT_FOUND', 'Lượt thi không tồn tại');
      if (a.status === 'SUBMITTED') return fail(req, 'EXAM_ALREADY_SUBMITTED', 'Bài thi đã nộp');
      if (now() > a.deadline_ms) { grade(a, a.deadline_ms); return fail(req, 'EXAM_TIME_EXPIRED', 'Đã hết thời gian làm bài'); }
      if (a.question_ids.indexOf(p.question_id) < 0) return fail(req, 'QUESTION_NOT_IN_EXAM', 'Câu hỏi không thuộc bài thi');
      var q = findQuestion(p.question_id);
      if (!q.answers.some(function (x) { return x.answer_id === p.answer_id; })) {
        return fail(req, 'ANSWER_NOT_IN_QUESTION', 'Đáp án không thuộc câu hỏi');
      }
      attemptAnswers[a.attempt_id][p.question_id] = p.answer_id; // cap nhat, khong tao ban ghi thu hai
      return ok(req, { attempt_id: a.attempt_id, question_id: p.question_id, answer_id: p.answer_id, saved: true });
    },

    EXAM_SUBMIT: function (req, p) {
      if (!isPosInt(p.attempt_id)) return fail(req, 'INVALID_PAYLOAD', 'Dữ liệu không hợp lệ');
      var a = myAttempt(p);
      if (!a) return fail(req, 'ATTEMPT_NOT_FOUND', 'Lượt thi không hợp lệ');
      if (a.status === 'SUBMITTED') return fail(req, 'EXAM_ALREADY_SUBMITTED', 'Bài thi đã được nộp');
      grade(a, Math.min(now(), a.deadline_ms)); // nop sau han tinh thoi diem nop = han nop
      return ok(req, {
        attempt_id: a.attempt_id, total_questions: a.question_ids.length, correct_count: a.correct_count,
        score: a.score, submitted_at: a.submitted_at
      });
    },

    EXAM_RESULT: function (req, p) {
      if (!isPosInt(p.attempt_id)) return fail(req, 'INVALID_PAYLOAD', 'Dữ liệu không hợp lệ');
      autoFinalize();
      var a = myAttempt(p);
      if (!a) return fail(req, 'ATTEMPT_NOT_FOUND', 'Lượt thi không tồn tại');
      if (a.status !== 'SUBMITTED') return fail(req, 'EXAM_NOT_SUBMITTED', 'Bài thi chưa hoàn thành');
      return ok(req, {
        attempt_id: a.attempt_id, room_id: a.room_id, total_questions: a.question_ids.length,
        correct_count: a.correct_count, score: a.score, started_at: a.started_at, submitted_at: a.submitted_at
      });
    },

    EXAM_HISTORY: function (req) {
      autoFinalize();
      var list = attempts
        .filter(function (a) { return a.user_id === currentUser.user_id && a.mode === 'EXAM' && a.status === 'SUBMITTED'; })
        .sort(function (x, y) { return y.started_at < x.started_at ? -1 : 1; }) // moi nhat truoc
        .map(function (a) {
          var r = findRoom(a.room_id);
          return {
            attempt_id: a.attempt_id, room_id: a.room_id, room_name: r ? r.room_name : '',
            score: a.score, started_at: a.started_at, submitted_at: a.submitted_at
          };
        });
      return ok(req, { attempts: list });
    },

    // Giong exam_review() trong exam.c
    EXAM_REVIEW: function (req, p) {
      if (!isPosInt(p.attempt_id)) return fail(req, 'INVALID_PAYLOAD', 'Dữ liệu không hợp lệ');
      autoFinalize();
      var a = attempts.find(function (x) { return x.attempt_id === p.attempt_id; });
      var room = a && findRoom(a.room_id);
      var manager = room && canManage(room, currentUser);
      if (!a || (!manager && a.user_id !== currentUser.user_id)) return fail(req, 'ATTEMPT_NOT_FOUND', 'Lượt thi không tồn tại');
      if (a.status !== 'SUBMITTED') return fail(req, 'EXAM_NOT_SUBMITTED', 'Bài thi chưa hoàn thành');
      if (!manager && !roomEnded(room)) return fail(req, 'ROOM_NOT_FINISHED', 'Chỉ xem được khi phòng thi đã kết thúc');
      var ans = attemptAnswers[a.attempt_id] || {};
      return ok(req, {
        attempt_id: a.attempt_id, room_id: a.room_id, room_name: room.room_name,
        user_id: a.user_id, username: findUser(a.user_id).username,
        total_questions: a.question_ids.length, correct_count: a.correct_count, score: a.score,
        started_at: a.started_at, submitted_at: a.submitted_at,
        questions: a.question_ids.map(function (qid, i) {
          var q = findQuestion(qid);
          var sel = ans[qid] || null;
          var right = q.answers.find(function (x) { return x.is_correct; });
          return {
            position: i + 1, question_id: qid, content: q.content, topic: q.topic, difficulty: q.difficulty,
            selected_answer_id: sel, correct_answer_id: right.answer_id,
            result: !sel ? 'UNANSWERED' : (sel === right.answer_id ? 'CORRECT' : 'WRONG'),
            answers: q.answers.map(function (x) { return { answer_id: x.answer_id, content: x.content, is_correct: x.is_correct }; })
          };
        })
      });
    },

    // Giong exam_room_result() trong exam.c: chi giao vien xem, khong xep hang
    ROOM_RESULT: function (req, p) {
      if (!isPosInt(p.room_id)) return fail(req, 'INVALID_PAYLOAD', 'ID phòng không hợp lệ');
      autoFinalize();
      var room = findRoom(p.room_id);
      if (!room) return fail(req, 'ROOM_NOT_FOUND', 'Phòng không tồn tại');
      if (!canManage(room, currentUser)) return fail(req, 'FORBIDDEN', 'Bạn không có quyền xem nội dung này');

      var inRoom = attempts.filter(function (a) { return a.room_id === room.room_id; });
      function dur(a) { return (new Date(a.submitted_at) - new Date(a.started_at)) / 1000; }
      // Moi thi sinh lay luot co diem cao nhat
      var best = {};
      inRoom.forEach(function (a) {
        if (a.status !== 'SUBMITTED') return;
        var cur = best[a.user_id];
        if (!cur || a.score > cur.score) best[a.user_id] = a;
      });
      var results = Object.keys(best).map(function (uid) {
        var a = best[uid];
        return {
          user_id: a.user_id, username: findUser(a.user_id).username, attempt_id: a.attempt_id,
          score: a.score, correct_count: a.correct_count, total_questions: a.question_ids.length,
          started_at: a.started_at, submitted_at: a.submitted_at, duration_seconds: dur(a)
        };
      }).sort(function (x, y) { return x.username.localeCompare(y.username); });

      var mem = members.filter(function (m) { return m.room_id === room.room_id && m.user_id !== room.owner_id; });
      var startedUsers = {}, inProgressUsers = {};
      inRoom.forEach(function (a) {
        startedUsers[a.user_id] = true;
        if (a.status === 'IN_PROGRESS') inProgressUsers[a.user_id] = true;
      });
      var notStarted = mem.filter(function (m) { return !startedUsers[m.user_id]; })
        .map(function (m) { return { user_id: m.user_id, username: findUser(m.user_id).username }; });
      var scores = results.map(function (r) { return r.score; });

      return ok(req, {
        room_id: room.room_id, room_name: room.room_name, owner_id: room.owner_id, status: room.status,
        question_count: room.question_count, duration_minutes: room.duration_minutes,
        member_count: mem.length, submitted_count: results.length,
        in_progress_count: Object.keys(inProgressUsers).length, not_started_count: notStarted.length,
        average_score: scores.length ? round2(scores.reduce(function (s, x) { return s + x; }, 0) / scores.length) : null,
        highest_score: scores.length ? Math.max.apply(null, scores) : null,
        lowest_score: scores.length ? Math.min.apply(null, scores) : null,
        results: results,
        not_started: notStarted
      });
    }
  };

  var publicTypes = { AUTH_REGISTER: 1, AUTH_LOGIN: 1 };

  function handleSync(req) {
    if (!req || typeof req.type !== 'string' || typeof req.request_id !== 'string' ||
        typeof req.payload !== 'object' || req.payload === null) {
      return fail(req || { type: 'UNKNOWN', request_id: '' }, 'INVALID_REQUEST', 'Request không đúng cấu trúc');
    }
    var h = handlers[req.type];
    if (!h) return fail(req, 'UNKNOWN_REQUEST', 'Loại request không được hỗ trợ');
    if (!publicTypes[req.type] && !currentUser) return fail(req, 'UNAUTHORIZED', 'Bạn chưa đăng nhập');
    return h(req, req.payload);
  }

  // Gia lap do tre mang 100-350ms de giao dien hien trang thai "dang luu"
  function handle(req) {
    var copy = JSON.parse(JSON.stringify(req));
    return new Promise(function (resolve) {
      setTimeout(function () { resolve(handleSync(copy)); }, 100 + Math.random() * 250);
    });
  }

  return { handle: handle };
})();
