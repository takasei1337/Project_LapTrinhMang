/*
 * app.js - Dieu huong va cac man hinh cua giao dien.
 * Dieu huong bang hash: #/login, #/register, #/rooms, #/exam/<room_id>,
 * #/result/<attempt_id>, #/history, #/practice
 */
(function () {
  var app = document.getElementById('app');
  var state = {
    user: null,          // { user_id, username, role }
    joinedRooms: {},     // room_id -> true (ROOM_LIST chua tra ve trang thai tham gia)
    exam: null,          // trang thai bai thi dang lam
    timerId: null,
    roomsTimer: null,    // tu dong tai lai danh sach phong
    knownRooms: null     // room_id da thay, de bao phong moi
  };
  var ROOMS_REFRESH_MS = 5000;

  /* ---------- Tien ich ---------- */
  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }
  function fmtTime(isoStr) {
    if (!isoStr) return '—';
    return new Date(isoStr).toLocaleString('vi-VN', { hour: '2-digit', minute: '2-digit', day: '2-digit', month: '2-digit', year: 'numeric' });
  }
  function fmtScore(x) { return x == null ? '—' : (Math.round(x * 100) / 100).toFixed(2); }
  var STATUS_TEXT = { WAITING: 'Đang chờ', IN_PROGRESS: 'Đang diễn ra', FINISHED: 'Đã kết thúc' };
  var DIFF_TEXT = { EASY: 'Dễ', MEDIUM: 'Trung bình', HARD: 'Khó' };

  var toastTimer = null;
  function toast(msg, type) {
    var t = document.getElementById('toast');
    t.textContent = msg;
    t.className = 'toast ' + (type || '');
    t.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { t.hidden = true; }, 3000);
  }

  function confirmBox(title, message, okText) {
    return new Promise(function (resolve) {
      var bg = document.createElement('div');
      bg.className = 'modal-bg';
      bg.innerHTML =
        '<div class="card modal stack">' +
        '<h3>' + esc(title) + '</h3><p>' + esc(message) + '</p>' +
        '<div class="row" style="justify-content:flex-end">' +
        '<button class="btn btn-ghost" data-act="no">Huỷ</button>' +
        '<button class="btn" data-act="yes">' + esc(okText || 'Đồng ý') + '</button></div></div>';
      bg.addEventListener('click', function (e) {
        var act = e.target.getAttribute('data-act');
        if (act || e.target === bg) { bg.remove(); resolve(act === 'yes'); }
      });
      document.body.appendChild(bg);
    });
  }

  function go(hash) { location.hash = hash; }

  function stopTimer() {
    if (state.timerId) { clearInterval(state.timerId); state.timerId = null; }
  }

  // Xu ly loi chung: het phien thi quay ve dang nhap
  function handleError(err) {
    if (err.code === 'UNAUTHORIZED') { state.user = null; go('#/login'); }
    toast(err.message || 'Có lỗi xảy ra', 'error');
  }

  function updateChrome(route) {
    document.getElementById('topbar').hidden = !state.user;
    document.getElementById('mock-banner').hidden = Api.config.mode !== 'mock';
    document.getElementById('user-name').textContent = state.user ? state.user.username : '';
    document.querySelectorAll('[data-nav]').forEach(function (a) {
      a.classList.toggle('active', a.getAttribute('data-nav') === route);
    });
  }

  /* ---------- Dang nhap / dang ky ---------- */
  function viewLogin() {
    app.innerHTML =
      '<div class="card auth-box">' +
      '<h2>Đăng nhập</h2>' +
      '<form class="form" id="f">' +
      '<label>Tên đăng nhập<input name="username" autocomplete="username" required></label>' +
      '<label>Mật khẩu<input name="password" type="password" autocomplete="current-password" required></label>' +
      '<div class="form-error" id="err"></div>' +
      '<button class="btn" type="submit">Đăng nhập</button>' +
      '<p class="muted">Chưa có tài khoản? <a href="#/register">Đăng ký</a></p>' +
      '</form></div>';
    var f = document.getElementById('f');
    f.addEventListener('submit', function (e) {
      e.preventDefault();
      var btn = f.querySelector('button');
      btn.disabled = true;
      Api.login(f.username.value.trim(), f.password.value)
        .then(function (u) { state.user = u; go('#/rooms'); })
        .catch(function (err) { document.getElementById('err').textContent = err.message; })
        .finally(function () { btn.disabled = false; });
    });
  }

  function viewRegister() {
    app.innerHTML =
      '<div class="card auth-box">' +
      '<h2>Đăng ký</h2>' +
      '<form class="form" id="f">' +
      '<label>Tên đăng nhập<input name="username" autocomplete="username" required></label>' +
      '<label>Mật khẩu<input name="password" type="password" autocomplete="new-password" required minlength="6"></label>' +
      '<label>Nhập lại mật khẩu<input name="password2" type="password" autocomplete="new-password" required></label>' +
      '<div class="form-error" id="err"></div>' +
      '<button class="btn" type="submit">Tạo tài khoản</button>' +
      '<p class="muted">Đã có tài khoản? <a href="#/login">Đăng nhập</a></p>' +
      '</form></div>';
    var f = document.getElementById('f');
    f.addEventListener('submit', function (e) {
      e.preventDefault();
      var errEl = document.getElementById('err');
      if (f.password.value !== f.password2.value) { errEl.textContent = 'Mật khẩu nhập lại không khớp'; return; }
      var btn = f.querySelector('button');
      btn.disabled = true;
      Api.register(f.username.value.trim(), f.password.value)
        .then(function () { toast('Đăng ký thành công, hãy đăng nhập', 'success'); go('#/login'); })
        .catch(function (err) { errEl.textContent = err.message; })
        .finally(function () { btn.disabled = false; });
    });
  }

  /* ---------- Danh sach phong ---------- */
  function viewRooms() {
    app.innerHTML =
      '<div class="row spread"><h2>Phòng thi</h2>' +
      '<div class="row"><span class="muted" style="font-size:13px">Tự cập nhật mỗi ' + (ROOMS_REFRESH_MS / 1000) + ' giây</span>' +
      '<button class="btn btn-ghost" id="btn-reload">Tải lại</button>' +
      (isTeacher() ? '<button class="btn" id="btn-new">+ Tạo phòng</button>' : '') + '</div></div>' +
      '<div class="card" id="create-box" hidden>' +
      '<form class="form" id="f-create">' +
      '<label>Tên phòng<input name="room_name" required maxlength="100"></label>' +
      '<div class="row">' +
      '<label style="flex:1">Số câu hỏi<input name="question_count" type="number" min="1" value="10" required></label>' +
      '<label style="flex:1">Thời gian (phút)<input name="duration_minutes" type="number" min="1" value="15" required></label>' +
      '</div><div class="form-error" id="create-err"></div>' +
      '<div class="row"><button class="btn" type="submit">Tạo phòng</button>' +
      '<button class="btn btn-ghost" type="button" id="btn-cancel">Huỷ</button></div>' +
      '</form></div>' +
      '<div id="room-list" style="margin-top:16px"><p class="muted">Đang tải...</p></div>';

    var box = document.getElementById('create-box');
    if (isTeacher()) document.getElementById('btn-new').onclick = function () { box.hidden = !box.hidden; };
    document.getElementById('btn-cancel').onclick = function () { box.hidden = true; };
    document.getElementById('btn-reload').onclick = function () { loadRooms(); };

    var f = document.getElementById('f-create');
    f.addEventListener('submit', function (e) {
      e.preventDefault();
      var qc = parseInt(f.question_count.value, 10), dm = parseInt(f.duration_minutes.value, 10);
      Api.roomCreate(f.room_name.value.trim(), qc, dm)
        .then(function (d) {
          toast('Đã tạo phòng #' + d.room_id + '. Bấm "Mở phòng" khi muốn bắt đầu thi', 'success');
          f.reset(); box.hidden = true; loadRooms();
        })
        .catch(function (err) { document.getElementById('create-err').textContent = err.message; });
    });

    loadRooms();
    // Tu dong tai lai de hoc sinh thay phong moi / phong vua mo ma khong can F5.
    // Khi gateway ho tro day thong bao (broadcast) thi co the bo polling nay.
    state.roomsTimer = setInterval(function () { loadRooms(true); }, ROOMS_REFRESH_MS);
  }

  function isTeacher() { return !!state.user && state.user.role === 'ADMIN'; }

  function stopRoomsTimer() {
    if (state.roomsTimer) { clearInterval(state.roomsTimer); state.roomsTimer = null; }
  }

  // silent = true: lan tai tu dong, khong bao loi lien tuc
  function loadRooms(silent) {
    var list = document.getElementById('room-list');
    if (!list) return;
    Api.roomList().then(function (d) {
      if (!document.getElementById('room-list')) return; // da roi trang
      notifyNewRooms(d.rooms);
      if (!d.rooms.length) { list.innerHTML = '<div class="card empty">Chưa có phòng thi nào.</div>'; return; }
      list.className = 'grid';
      list.innerHTML = d.rooms.map(function (r) {
        var manager = r.owner_id === state.user.user_id || state.user.role === 'ADMIN';
        var joined = r.joined === true || !!state.joinedRooms[r.room_id];
        var canJoin = !manager && r.status === 'WAITING' && !joined;
        var canStart = !manager && r.status === 'IN_PROGRESS';
        var actions = '';
        if (manager) {
          actions += '<a class="btn btn-ghost" href="#/room-result/' + r.room_id + '">📊 Kết quả</a>';
          if (r.status === 'WAITING') actions += '<button class="btn" data-room-start="' + r.room_id + '">Mở phòng</button>';
          if (r.status === 'IN_PROGRESS') actions += '<button class="btn btn-danger" data-room-finish="' + r.room_id + '">Kết thúc</button>';
        }
        return '<div class="card">' +
          '<div class="row spread"><span class="badge badge-' + esc(r.status) + '">' + esc(STATUS_TEXT[r.status] || r.status) + '</span>' +
          '<span class="muted">#' + esc(r.room_id) + '</span></div>' +
          '<h3 style="margin-top:10px">' + esc(r.room_name) + '</h3>' +
          '<div class="room-meta">' + esc(r.question_count) + ' câu · ' + esc(r.duration_minutes) + ' phút' +
          (r.member_count != null ? ' · ' + esc(r.member_count) + ' thí sinh' : '') +
          (manager ? ' · <b>Phòng bạn quản lý</b>' : joined ? ' · <b>Đã tham gia</b>' : '') + '</div>' +
          '<div class="row">' + actions +
          (canJoin ? '<button class="btn btn-ghost" data-join="' + r.room_id + '">Tham gia</button>' : '') +
          (canStart ? '<button class="btn" data-start="' + r.room_id + '">Vào thi</button>' : '') +
          '</div></div>';
      }).join('');
    }).catch(function (err) {
      if (silent && err.code !== 'UNAUTHORIZED') return;
      list.innerHTML = '<div class="card empty">Không tải được danh sách phòng.</div>';
      handleError(err);
    });
  }

  // Bao cho hoc sinh khi co phong moi xuat hien hoac phong vua duoc mo
  function notifyNewRooms(rooms) {
    var cur = {};
    rooms.forEach(function (r) { cur[r.room_id] = r.status; });
    if (state.knownRooms && !isTeacher()) {
      var msgs = [];
      rooms.forEach(function (r) {
        var old = state.knownRooms[r.room_id];
        if (old === undefined) msgs.push('Có phòng thi mới: ' + r.room_name);
        else if (old !== 'IN_PROGRESS' && r.status === 'IN_PROGRESS') msgs.push('Phòng "' + r.room_name + '" đã mở, có thể vào thi');
      });
      if (msgs.length) toast(msgs.join(' · '), 'success');
    }
    state.knownRooms = cur;
  }

  app.addEventListener('click', function (e) {
    var join = e.target.getAttribute && e.target.getAttribute('data-join');
    var start = e.target.getAttribute && e.target.getAttribute('data-start');
    if (join) {
      e.target.disabled = true;
      Api.roomJoin(Number(join))
        .then(function () { state.joinedRooms[join] = true; toast('Đã tham gia phòng', 'success'); loadRooms(); })
        .catch(function (err) {
          if (err.code === 'ALREADY_JOINED') { state.joinedRooms[join] = true; loadRooms(); }
          e.target.disabled = false;
          handleError(err);
        });
    }
    if (start) go('#/exam/' + start);

    var rs = e.target.getAttribute && e.target.getAttribute('data-room-start');
    var rf = e.target.getAttribute && e.target.getAttribute('data-room-finish');
    if (rs) {
      Api.roomStart(Number(rs)).then(function () { toast('Đã mở phòng, thí sinh có thể vào thi', 'success'); loadRooms(); })
        .catch(handleError);
    }
    if (rf) {
      confirmBox('Kết thúc phòng thi?', 'Các bài đang làm sẽ được thu và chấm ngay. Thí sinh sẽ xem được đáp án.', 'Kết thúc')
        .then(function (yes) {
          if (!yes) return;
          Api.roomFinish(Number(rf)).then(function () { toast('Đã kết thúc phòng thi', 'success'); loadRooms(); })
            .catch(handleError);
        });
    }
  });

  /* ---------- Lam bai thi ---------- */
  function viewExam(roomId) {
    app.innerHTML = '<p class="muted">Đang bắt đầu bài thi...</p>';
    Api.examStart(roomId).then(function (d) {
      state.exam = {
        attemptId: d.attempt_id,
        roomId: d.room_id,
        deadline: new Date(d.deadline).getTime(),
        questions: d.questions,
        index: 0,
        // question_id -> { answerId, savedAnswerId, status: 'saving'|'saved'|'error', seq }
        answers: {},
        locked: false
      };
      renderExam();
      startTimer();
    }).catch(function (err) {
      app.innerHTML = '<div class="card empty"><p>' + esc(err.message) + '</p>' +
        '<a class="btn" href="#/rooms">Quay lại danh sách phòng</a></div>';
      if (err.code === 'UNAUTHORIZED') handleError(err);
    });
  }

  function renderExam() {
    var ex = state.exam;
    var q = ex.questions[ex.index];
    var cur = ex.answers[q.question_id] || {};
    var answeredCount = Object.keys(ex.answers).filter(function (k) { return ex.answers[k].savedAnswerId; }).length;

    var saveText = { saving: 'Đang lưu...', saved: '✓ Đã lưu', error: '✗ Lưu thất bại, vui lòng chọn lại' }[cur.status] || '';

    app.innerHTML =
      '<div class="exam-layout">' +
      '<div class="card">' +
      '<div class="row spread"><span class="question-tag">Câu ' + (ex.index + 1) + '/' + ex.questions.length +
      ' · ' + esc(q.topic) + ' · ' + esc(DIFF_TEXT[q.difficulty] || q.difficulty) + '</span>' +
      '<span class="save-state ' + (cur.status || '') + '">' + saveText + '</span></div>' +
      '<div class="question-content">' + esc(q.content) + '</div>' +
      q.answers.map(function (a, i) {
        var sel = cur.answerId === a.answer_id;
        return '<button class="answer' + (sel ? ' selected' : '') + '" data-answer="' + a.answer_id + '"' + (ex.locked ? ' disabled' : '') + '>' +
          '<span class="key">' + String.fromCharCode(65 + i) + '</span><span>' + esc(a.content) + '</span></button>';
      }).join('') +
      '<div class="row spread" style="margin-top:8px">' +
      '<button class="btn btn-ghost" id="prev"' + (ex.index === 0 ? ' disabled' : '') + '>← Câu trước</button>' +
      '<button class="btn btn-ghost" id="next"' + (ex.index === ex.questions.length - 1 ? ' disabled' : '') + '>Câu sau →</button>' +
      '</div></div>' +
      '<div class="card">' +
      '<div class="muted">Thời gian còn lại</div><div class="timer" id="timer">--:--</div>' +
      '<div class="muted" style="margin-top:8px">Đã trả lời: <b>' + answeredCount + '/' + ex.questions.length + '</b></div>' +
      '<div class="qnav">' + ex.questions.map(function (qq, i) {
        var st = ex.answers[qq.question_id] || {};
        var cls = [];
        if (st.status === 'saving') cls.push('saving');
        else if (st.savedAnswerId) cls.push('answered');
        if (i === ex.index) cls.push('current');
        return '<button class="' + cls.join(' ') + '" data-goto="' + i + '">' + (i + 1) + '</button>';
      }).join('') + '</div>' +
      '<div class="legend">🟩 đã lưu · 🟨 đang lưu</div>' +
      '<button class="btn btn-success" id="submit" style="width:100%;margin-top:12px"' + (ex.locked ? ' disabled' : '') + '>Nộp bài</button>' +
      '</div></div>';

    updateTimer();

    app.querySelectorAll('[data-answer]').forEach(function (b) {
      b.onclick = function () { chooseAnswer(q.question_id, Number(b.getAttribute('data-answer'))); };
    });
    app.querySelectorAll('[data-goto]').forEach(function (b) {
      b.onclick = function () { ex.index = Number(b.getAttribute('data-goto')); renderExam(); };
    });
    document.getElementById('prev').onclick = function () { ex.index--; renderExam(); };
    document.getElementById('next').onclick = function () { ex.index++; renderExam(); };
    document.getElementById('submit').onclick = submitExam;
  }

  // Moi lan chon/doi dap an gui ngay EXAM_ANSWER; chi hien "da luu" khi server xac nhan
  function chooseAnswer(questionId, answerId) {
    var ex = state.exam;
    if (!ex || ex.locked) return;
    var st = ex.answers[questionId] || (ex.answers[questionId] = { seq: 0 });
    st.answerId = answerId;
    st.status = 'saving';
    st.seq += 1;
    var mySeq = st.seq;
    renderExam();

    Api.examAnswer(ex.attemptId, questionId, answerId).then(function () {
      if (state.exam !== ex || st.seq !== mySeq) return; // da co lua chon moi hon
      st.savedAnswerId = answerId;
      st.status = 'saved';
      renderExam();
    }).catch(function (err) {
      if (state.exam !== ex) return;
      if (err.code === 'EXAM_TIME_EXPIRED' || err.code === 'EXAM_ALREADY_SUBMITTED') {
        finishExam(err.message);
        return;
      }
      if (st.seq === mySeq) {
        st.answerId = st.savedAnswerId; // quay ve lua chon da luu gan nhat
        st.status = 'error';
        renderExam();
      }
      handleError(err);
    });
  }

  function startTimer() {
    stopTimer();
    state.timerId = setInterval(updateTimer, 1000);
  }

  function updateTimer() {
    var ex = state.exam;
    var el = document.getElementById('timer');
    if (!ex || !el) return;
    var left = Math.max(0, Math.floor((ex.deadline - Date.now()) / 1000));
    var m = Math.floor(left / 60), s = left % 60;
    el.textContent = String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
    el.className = 'timer' + (left <= 30 ? ' danger' : left <= 120 ? ' warn' : '');
    // Dong ho client chi de hien thi; server van tu cham khi het gio
    if (left === 0 && !ex.locked) finishExam('Đã hết thời gian làm bài');
  }

  function submitExam() {
    var ex = state.exam;
    var total = ex.questions.length;
    var answered = Object.keys(ex.answers).filter(function (k) { return ex.answers[k].savedAnswerId; }).length;
    var saving = Object.keys(ex.answers).some(function (k) { return ex.answers[k].status === 'saving'; });
    if (saving) { toast('Đang lưu đáp án, vui lòng đợi một chút', ''); return; }
    var msg = answered < total
      ? 'Bạn mới trả lời ' + answered + '/' + total + ' câu. Sau khi nộp sẽ không thể sửa đáp án.'
      : 'Sau khi nộp sẽ không thể sửa đáp án.';
    confirmBox('Nộp bài?', msg, 'Nộp bài').then(function (yes) {
      if (!yes || state.exam !== ex) return;
      ex.locked = true;
      renderExam();
      Api.examSubmit(ex.attemptId).then(function (d) {
        stopTimer();
        state.exam = null;
        go('#/result/' + d.attempt_id);
      }).catch(function (err) {
        if (err.code === 'EXAM_ALREADY_SUBMITTED') { finishExam(err.message); return; }
        ex.locked = false;
        renderExam();
        handleError(err);
      });
    });
  }

  // Bai da duoc server cham (het gio hoac da nop): khoa va chuyen sang ket qua
  function finishExam(message) {
    var ex = state.exam;
    if (!ex) return;
    ex.locked = true;
    stopTimer();
    toast(message, 'error');
    Api.examSubmit(ex.attemptId).catch(function () { /* server co the da tu cham */ })
      .finally(function () {
        state.exam = null;
        go('#/result/' + ex.attemptId);
      });
  }

  /* ---------- Ket qua ---------- */
  function viewResult(attemptId) {
    app.innerHTML = '<p class="muted">Đang tải kết quả...</p>';
    Api.examResult(attemptId).then(function (d) {
      var wrong = d.total_questions - d.correct_count;
      app.innerHTML =
        '<div class="card stack" style="max-width:640px;margin:0 auto">' +
        '<h2>Kết quả bài thi</h2>' +
        '<div class="row" style="align-items:flex-end"><div class="score-big">' + fmtScore(d.score) + '</div><div class="muted">/ 10 điểm</div></div>' +
        '<div class="stats">' +
        '<div class="stat"><div class="label">Số câu đúng</div><div class="value">' + d.correct_count + '/' + d.total_questions + '</div></div>' +
        '<div class="stat"><div class="label">Sai / bỏ trống</div><div class="value">' + wrong + '</div></div>' +
        '<div class="stat"><div class="label">Bắt đầu</div><div class="value" style="font-size:15px">' + fmtTime(d.started_at) + '</div></div>' +
        '<div class="stat"><div class="label">Nộp bài</div><div class="value" style="font-size:15px">' + fmtTime(d.submitted_at) + '</div></div>' +
        '</div>' +
        '<div class="row"><a class="btn" href="#/review/' + d.attempt_id + '">Xem lại bài làm</a>' +
        '<a class="btn btn-ghost" href="#/rooms">Về danh sách phòng</a><a class="btn btn-ghost" href="#/history">Xem lịch sử</a></div>' +
        '</div>';
    }).catch(function (err) {
      app.innerHTML = '<div class="card empty"><p>' + esc(err.message) + '</p><a class="btn" href="#/history">Xem lịch sử</a></div>';
      if (err.code === 'UNAUTHORIZED') handleError(err);
    });
  }

  /* ---------- Lich su va bieu do ---------- */
  function viewHistory() {
    app.innerHTML = '<h2>Lịch sử thi</h2><p class="muted">Đang tải...</p>';
    Api.examHistory().then(function (d) {
      var list = d.attempts;
      if (!list.length) {
        app.innerHTML = '<h2>Lịch sử thi</h2><div class="card empty">Bạn chưa có lượt thi nào.</div>';
        return;
      }
      var scores = list.map(function (a) { return a.score; });
      var avg = scores.reduce(function (s, x) { return s + x; }, 0) / scores.length;
      var best = Math.max.apply(null, scores);

      app.innerHTML =
        '<h2>Lịch sử thi</h2>' +
        '<div class="stack">' +
        '<div class="stats">' +
        '<div class="stat"><div class="label">Số lượt thi</div><div class="value">' + list.length + '</div></div>' +
        '<div class="stat"><div class="label">Điểm trung bình</div><div class="value">' + fmtScore(avg) + '</div></div>' +
        '<div class="stat"><div class="label">Điểm cao nhất</div><div class="value">' + fmtScore(best) + '</div></div>' +
        '<div class="stat"><div class="label">Lần gần nhất</div><div class="value">' + fmtScore(list[0].score) + '</div></div>' +
        '</div>' +
        '<div class="card"><h3>Điểm qua các lượt thi</h3><div class="chart" id="chart"></div></div>' +
        '<div class="card"><table><thead><tr><th>#</th><th>Phòng</th><th>Bắt đầu</th><th>Nộp bài</th><th>Điểm</th><th></th></tr></thead><tbody>' +
        list.map(function (a) {
          return '<tr><td>' + a.attempt_id + '</td><td>' + esc(a.room_name) + '</td><td>' + fmtTime(a.started_at) +
            '</td><td>' + fmtTime(a.submitted_at) + '</td><td><b>' + fmtScore(a.score) + '</b></td>' +
            '<td><a href="#/result/' + a.attempt_id + '">Chi tiết</a> · <a href="#/review/' + a.attempt_id + '">Xem lại</a></td></tr>';
        }).join('') +
        '</tbody></table></div></div>';

      // Server tra moi nhat truoc; bieu do ve theo thu tu thoi gian
      var points = list.slice().reverse().map(function (a) {
        var dt = new Date(a.started_at);
        return { label: dt.getDate() + '/' + (dt.getMonth() + 1), value: a.score };
      });
      Chart.line(document.getElementById('chart'), points, { maxY: 10, title: 'Điểm qua các lượt thi' });
    }).catch(handleError);
  }

  /* ---------- Xem lai bai lam (EXAM_REVIEW) ---------- */
  var RESULT_TEXT = { CORRECT: '✓ Đúng', WRONG: '✗ Sai', UNANSWERED: '— Bỏ trống' };

  function viewReview(attemptId) {
    app.innerHTML = '<p class="muted">Đang tải bài làm...</p>';
    Api.examReview(attemptId).then(function (d) {
      var count = { CORRECT: 0, WRONG: 0, UNANSWERED: 0 };
      d.questions.forEach(function (q) { count[q.result]++; });
      var isOther = d.user_id !== state.user.user_id;

      app.innerHTML =
        '<div class="stack">' +
        '<div class="row spread"><h2>Xem lại bài làm' + (isOther ? ' của ' + esc(d.username) : '') + '</h2>' +
        '<a class="btn btn-ghost" href="javascript:history.back()">← Quay lại</a></div>' +
        '<div class="card"><div class="row spread">' +
        '<div><div class="muted">' + esc(d.room_name) + ' · nộp lúc ' + fmtTime(d.submitted_at) + '</div>' +
        '<div class="row" style="align-items:flex-end;margin-top:6px"><div class="score-big" style="font-size:44px">' +
        fmtScore(d.score) + '</div><div class="muted">/ 10 · đúng ' + d.correct_count + '/' + d.total_questions + ' câu</div></div></div>' +
        '<div class="row filter" id="filter">' +
        '<button class="btn btn-sm active" data-filter="ALL">Tất cả (' + d.questions.length + ')</button>' +
        '<button class="btn btn-sm btn-ghost" data-filter="WRONG">Sai (' + count.WRONG + ')</button>' +
        '<button class="btn btn-sm btn-ghost" data-filter="UNANSWERED">Bỏ trống (' + count.UNANSWERED + ')</button>' +
        '<button class="btn btn-sm btn-ghost" data-filter="CORRECT">Đúng (' + count.CORRECT + ')</button>' +
        '</div></div></div>' +
        '<div id="review-list" class="stack"></div></div>';

      function render(filter) {
        var list = d.questions.filter(function (q) { return filter === 'ALL' || q.result === filter; });
        document.getElementById('review-list').innerHTML = list.length ? list.map(function (q) {
          return '<div class="card review-q ' + q.result.toLowerCase() + '">' +
            '<div class="row spread"><span class="question-tag">Câu ' + q.position + ' · ' + esc(q.topic) + ' · ' +
            esc(DIFF_TEXT[q.difficulty] || q.difficulty) + '</span>' +
            '<span class="result-tag ' + q.result.toLowerCase() + '">' + RESULT_TEXT[q.result] + '</span></div>' +
            '<div class="question-content">' + esc(q.content) + '</div>' +
            q.answers.map(function (a, i) {
              var cls = 'answer review';
              var note = '';
              if (a.answer_id === q.correct_answer_id) { cls += ' is-correct'; note = 'Đáp án đúng'; }
              if (a.answer_id === q.selected_answer_id) {
                cls += a.answer_id === q.correct_answer_id ? '' : ' is-wrong';
                note = a.answer_id === q.correct_answer_id ? 'Bạn chọn · Đúng' : 'Bạn chọn · Sai';
              }
              return '<div class="' + cls + '"><span class="key">' + String.fromCharCode(65 + i) + '</span>' +
                '<span style="flex:1">' + esc(a.content) + '</span>' +
                (note ? '<span class="answer-note">' + note + '</span>' : '') + '</div>';
            }).join('') + '</div>';
        }).join('') : '<div class="card empty">Không có câu nào.</div>';
      }
      render('ALL');

      document.getElementById('filter').onclick = function (e) {
        var f = e.target.getAttribute('data-filter');
        if (!f) return;
        this.querySelectorAll('button').forEach(function (b) {
          var on = b === e.target;
          b.classList.toggle('active', on);
          b.classList.toggle('btn-ghost', !on);
        });
        render(f);
      };
    }).catch(function (err) {
      var hint = err.code === 'ROOM_NOT_FINISHED'
        ? '<p class="muted">Để tránh lộ đề cho bạn đang thi, đáp án chỉ được công bố khi giáo viên kết thúc phòng thi.</p>' : '';
      app.innerHTML = '<div class="card empty"><p>' + esc(err.message) + '</p>' + hint +
        '<a class="btn" href="javascript:history.back()">Quay lại</a></div>';
      if (err.code === 'UNAUTHORIZED') handleError(err);
    });
  }

  /* ---------- Ket qua thi cua phong - danh cho giao vien (ROOM_RESULT) ---------- */
  function fmtDuration(sec) {
    sec = Math.round(sec || 0);
    return Math.floor(sec / 60) + ' phút ' + String(sec % 60).padStart(2, '0') + ' giây';
  }

  function viewRoomResult(roomId) {
    app.innerHTML = '<p class="muted">Đang tải kết quả phòng thi...</p>';
    Api.roomResult(roomId).then(function (d) {

      app.innerHTML =
        '<div class="stack">' +
        '<div class="row spread"><div><h2 style="margin:0">' + esc(d.room_name) + '</h2>' +
        '<span class="badge badge-' + esc(d.status) + '">' + esc(STATUS_TEXT[d.status] || d.status) + '</span> ' +
        '<span class="muted">' + d.question_count + ' câu · ' + d.duration_minutes + ' phút</span></div>' +
        '<a class="btn btn-ghost" href="#/rooms">← Danh sách phòng</a></div>' +
        '<div class="stats">' +
        '<div class="stat"><div class="label">Thí sinh trong phòng</div><div class="value">' + d.member_count + '</div></div>' +
        '<div class="stat"><div class="label">Đã nộp bài</div><div class="value" style="color:var(--success)">' + d.submitted_count + '</div></div>' +
        '<div class="stat"><div class="label">Đang làm</div><div class="value" style="color:var(--warning)">' + d.in_progress_count + '</div></div>' +
        '<div class="stat"><div class="label">Chưa thi</div><div class="value" style="color:var(--danger)">' + d.not_started_count + '</div></div>' +
        '<div class="stat"><div class="label">Điểm trung bình</div><div class="value">' + fmtScore(d.average_score) + '</div></div>' +
        '<div class="stat"><div class="label">Cao nhất / Thấp nhất</div><div class="value">' + fmtScore(d.highest_score) + ' / ' + fmtScore(d.lowest_score) + '</div></div>' +
        '</div>' +
        '<div class="card"><h3>Kết quả từng thí sinh</h3>' +
        (d.results.length ? '<table><thead><tr><th>Thí sinh</th><th>Điểm</th><th>Số câu đúng</th><th>Thời gian làm</th><th>Nộp lúc</th><th></th></tr></thead><tbody>' +
          d.results.map(function (r) {
            var link = '<a href="#/review/' + r.attempt_id + '">Xem bài</a>';
            return '<tr><td>' + esc(r.username) + '</td><td><b>' + fmtScore(r.score) + '</b></td>' +
              '<td>' + r.correct_count + '/' + r.total_questions + '</td><td>' + fmtDuration(r.duration_seconds) + '</td>' +
              '<td>' + fmtTime(r.submitted_at) + '</td><td>' + link + '</td></tr>';
          }).join('') + '</tbody></table>'
          : '<div class="empty">Chưa có thí sinh nào nộp bài.</div>') +
        '</div>' +
        (d.results.length ? '<div class="card"><h3>Phổ điểm</h3><div class="chart" id="dist"></div></div>' : '') +
        (d.not_started.length ? '<div class="card"><h3>Chưa thi (' + d.not_started.length + ')</h3><div class="row">' +
          d.not_started.map(function (u) { return '<span class="chip">' + esc(u.username) + '</span>'; }).join('') + '</div></div>' : '') +
        '</div>';

      if (d.results.length) {
        var bins = [
          { label: '< 5', value: 0 }, { label: '5 – < 6.5', value: 0 },
          { label: '6.5 – < 8', value: 0 }, { label: '8 – 10', value: 0 }
        ];
        d.results.forEach(function (r) {
          var i = r.score < 5 ? 0 : r.score < 6.5 ? 1 : r.score < 8 ? 2 : 3;
          bins[i].value++;
        });
        Chart.bar(document.getElementById('dist'), bins, { title: 'Số thí sinh theo khoảng điểm' });
      }
    }).catch(function (err) {
      app.innerHTML = '<div class="card empty"><p>' + esc(err.message) + '</p>' +
        '<a class="btn" href="#/rooms">Về danh sách phòng</a></div>';
      if (err.code === 'UNAUTHORIZED') handleError(err);
    });
  }

  /* ---------- Luyen tap ---------- */
  function viewPractice() {
    app.innerHTML =
      '<h2>Luyện tập</h2>' +
      '<div class="card empty">' +
      '<p>Chế độ luyện tập dùng <code>PRACTICE_START</code> / <code>PRACTICE_SUBMIT</code> (phần của Thế Anh).</p>' +
      '<p>Định dạng JSON chưa được đặc tả trong <code>docs/protocol.md</code> nên màn hình này sẽ được làm khi API được chốt.</p>' +
      '</div>';
  }

  /* ---------- Dieu huong ---------- */
  function router() {
    // Roi trang thi giua chung: dong ho dung, server van tu cham khi het gio
    if (state.exam && !/^#\/exam\//.test(location.hash)) { stopTimer(); state.exam = null; }
    stopRoomsTimer();

    var parts = (location.hash || '#/login').replace(/^#\//, '').split('/');
    var route = parts[0] || 'login';
    var publicRoutes = { login: 1, register: 1 };

    if (!state.user && !publicRoutes[route]) { go('#/login'); return; }
    if (state.user && publicRoutes[route]) { go('#/rooms'); return; }

    updateChrome(route);
    switch (route) {
      case 'login': return viewLogin();
      case 'register': return viewRegister();
      case 'rooms': return viewRooms();
      case 'exam': return viewExam(Number(parts[1]));
      case 'result': return viewResult(Number(parts[1]));
      case 'history': return viewHistory();
      case 'review': return viewReview(Number(parts[1]));
      case 'room-result': return viewRoomResult(Number(parts[1]));
      case 'practice': return viewPractice();
      default: go('#/rooms');
    }
  }

  document.getElementById('btn-logout').onclick = function () {
    Api.logout().catch(function () {}).finally(function () {
      stopTimer();
      stopRoomsTimer();
      state.user = null; state.exam = null; state.joinedRooms = {}; state.knownRooms = null;
      go('#/login');
    });
  };

  window.addEventListener('hashchange', router);
  router();
})();
