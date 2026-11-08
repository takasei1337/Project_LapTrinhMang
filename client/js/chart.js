/*
 * chart.js - Ve bieu do duong diem so bang SVG, khong can thu vien ngoai.
 * points: [{ label: 'dd/mm', value: so diem 0..10 }]
 */
var Chart = (function () {
  var NS = 'http://www.w3.org/2000/svg';

  function el(name, attrs, text) {
    var e = document.createElementNS(NS, name);
    Object.keys(attrs || {}).forEach(function (k) { e.setAttribute(k, attrs[k]); });
    if (text !== undefined) e.textContent = text;
    return e;
  }

  function line(container, points, opts) {
    opts = opts || {};
    var W = 640, H = 240, pad = { l: 36, r: 16, t: 16, b: 32 };
    var maxY = opts.maxY || 10;
    var svg = el('svg', { viewBox: '0 0 ' + W + ' ' + H, role: 'img', 'aria-label': opts.title || 'Biểu đồ điểm' });
    var iw = W - pad.l - pad.r, ih = H - pad.t - pad.b;

    function x(i) { return pad.l + (points.length === 1 ? iw / 2 : i * iw / (points.length - 1)); }
    function y(v) { return pad.t + ih - (v / maxY) * ih; }

    // Luoi va truc Y
    for (var v = 0; v <= maxY; v += 2) {
      svg.appendChild(el('line', { x1: pad.l, x2: W - pad.r, y1: y(v), y2: y(v), class: v === 0 ? 'axis' : 'grid-line' }));
      svg.appendChild(el('text', { x: pad.l - 8, y: y(v) + 4, 'text-anchor': 'end' }, String(v)));
    }

    if (points.length) {
      var d = points.map(function (p, i) { return (i ? 'L' : 'M') + x(i) + ' ' + y(p.value); }).join(' ');
      svg.appendChild(el('path', { d: d, class: 'line' }));
      points.forEach(function (p, i) {
        var dot = el('circle', { cx: x(i), cy: y(p.value), r: 4, class: 'dot' });
        dot.appendChild(el('title', {}, p.label + ': ' + p.value));
        svg.appendChild(dot);
        svg.appendChild(el('text', { x: x(i), y: H - 10, 'text-anchor': 'middle' }, p.label));
      });
    }

    container.innerHTML = '';
    container.appendChild(svg);
  }

  /* Bieu do cot. bars: [{ label, value }] */
  function bar(container, bars, opts) {
    opts = opts || {};
    var W = 640, H = 220, pad = { l: 36, r: 16, t: 20, b: 32 };
    var maxY = Math.max(1, Math.max.apply(null, bars.map(function (b) { return b.value; })));
    var step = Math.max(1, Math.ceil(maxY / 4));
    maxY = step * 4;
    var svg = el('svg', { viewBox: '0 0 ' + W + ' ' + H, role: 'img', 'aria-label': opts.title || 'Biểu đồ cột' });
    var iw = W - pad.l - pad.r, ih = H - pad.t - pad.b;
    function y(v) { return pad.t + ih - (v / maxY) * ih; }

    for (var v = 0; v <= maxY; v += step) {
      svg.appendChild(el('line', { x1: pad.l, x2: W - pad.r, y1: y(v), y2: y(v), class: v === 0 ? 'axis' : 'grid-line' }));
      svg.appendChild(el('text', { x: pad.l - 8, y: y(v) + 4, 'text-anchor': 'end' }, String(v)));
    }
    var slot = iw / bars.length, bw = Math.min(64, slot * 0.6);
    bars.forEach(function (b, i) {
      var cx = pad.l + slot * i + slot / 2;
      var rect = el('rect', { x: cx - bw / 2, y: y(b.value), width: bw, height: y(0) - y(b.value), rx: 4, class: 'bar' });
      rect.appendChild(el('title', {}, b.label + ': ' + b.value));
      svg.appendChild(rect);
      if (b.value > 0) svg.appendChild(el('text', { x: cx, y: y(b.value) - 6, 'text-anchor': 'middle', class: 'bar-label' }, String(b.value)));
      svg.appendChild(el('text', { x: cx, y: H - 10, 'text-anchor': 'middle' }, b.label));
    });
    container.innerHTML = '';
    container.appendChild(svg);
  }

  return { line: line, bar: bar };
})();
