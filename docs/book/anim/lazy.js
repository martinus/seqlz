/*
 * Level 3's lazy step and its step back on a made-up page: at pos the hash table finds a match of 5
 * bytes, seqlz_lazy() finds a longer one at pos + 1, and the step back takes the match two bytes back,
 * past pos. Three stretches of the page, the bytes at the same offset one above the other: the source
 * of the longer match at the top, the bytes around pos in the middle, the source of the short match at the
 * bottom. lazy.c builds the page and checks it against seqlz_find() at levels 2 and 3.
 */
BookAnim.define('lazy', function (ctx) {
  var D = ctx.data, P = D.P, A = D.A, B = D.B, BACK = D.back;
  var OFF_B = P + 1 - B, OFF_A = P - A, END_B = P + 1 + D.lenB;
  // the three rows: their bytes, and the page position at column 0, so that a match lines up
  var ROWS = {
    src: { d: D.src, col0: D.p.from - OFF_B, name: 'the longer match’s source' },
    p: { d: D.p, col0: D.p.from, name: 'the bytes around pos' },
    a: { d: D.a, col0: D.p.from - OFF_A, name: 'the short match’s source' }
  };
  var ORDER = ['src', 'p', 'a'];
  function hex(b) { return (b < 16 ? '0' : '') + b.toString(16); }
  function range(row, from, n, cls, h) { for (var i = 0; i < n; i++) h[row + ':' + (from + i)] = cls; }
  var before = D.before, l2 = D.l2, l3 = D.l3;
  var STEPS = [
    { say: 'A page of random bytes with copies in it. The match before covers bytes ' + before[0] + ' to ' + (before[0] + before[2] - 1) +
        ', at offset ' + before[3] + ', and put only one of its positions into the hash table, ' + (before[0] + before[2] - 2) +
        ', 2 bytes before its end. The matcher goes on at ' + (before[0] + before[2]) + '; up to ' + (P - 2) + ' it finds nothing.' },
    { pos: P - 1, say: 'At ' + (P - 1) + ' the 5 bytes were last at ' + (B - 2) + ', inside the match before, and the table does not have them. The repeat offset ' +
        D.rep + ' points at ' + (P - 1 - D.rep) + ', which differs. ' + (P - 1) + ' stays a literal.',
      h: function (h) { range('p', P - 1, 5, 'lz-win', h); range('src', B - 2, 5, 'lz-gone', h); } },
    { pos: P, m: A, say: 'At pos = ' + P + ' the table has ' + A + ' for the 5 bytes, and the match there is ' + D.lenA + ' bytes long, at offset ' + OFF_A +
        '. Levels 1 and 2 take it, and the rest of the copy becomes a second sequence: ' + l2[1][2] + ' bytes at offset ' + l2[1][3] + '.',
      h: function (h) { range('p', P, D.lenA, 'lz-cand', h); range('a', A, D.lenA, 'lz-cand', h); h['p:' + (P + D.lenA)] = h['a:' + (A + D.lenA)] = 'lz-bad'; } },
    { pos: P, m: A, look: P + 1, say: 'Level 3: ' + D.lenA + ' bytes are fewer than SEQLZ_LAZY_LEN, ' + D.lazy + ', so seqlz_lazy() looks at pos + 1 = ' + (P + 1) +
        '. The repeat offset ' + D.rep + ' points at ' + (P + 1 - D.rep) + ' and misses. The table has ' + B + ', put there by the match before, and ' + D.lenB + ' bytes match there.',
      h: function (h) { range('p', P, D.lenA, 'lz-dim', h); range('a', A, D.lenA, 'lz-dim', h); range('p', P + 1, D.lenB, 'lz-cand', h); range('src', B, D.lenB, 'lz-cand', h);
        h['p:' + END_B] = h['src:' + (B + D.lenB)] = 'lz-bad'; } },
    { pos: P + 1, m: B, len: D.lenB, say: D.lenB + ' is more than ' + D.lenA + ': the match moves to ' + (P + 1) + ', from ' + B + ', and ' + (P + 1) +
        ' goes into the table. Byte ' + P + ' is a literal now, and the step back starts.',
      h: function (h) { range('p', P + 1, D.lenB, 'lz-match', h); range('src', B, D.lenB, 'lz-match', h); h['p:' + P] = 'lz-lit'; } }
  ];
  for (var k = 1; k <= BACK; k++) {
    (function (k) {
      STEPS.push({ pos: P + 1 - k, m: B - k, len: D.lenB + k,
        say: 'Byte ' + (P + 1 - k) + ' equals byte ' + (B - k) + ', the one before the match’s source: the match starts at ' + (P + 1 - k) + ', from ' + (B - k) + ', ' +
          (D.lenB + k) + ' bytes, at the same offset.' + (P + 1 - k < P ? ' ' + (P + 1 - k) + ' comes before ' + P + ', where the table found the short match: the step back takes it too.' : ''),
        h: function (h) { range('p', P + 1 - k, D.lenB + k, 'lz-match', h); range('src', B - k, D.lenB + k, 'lz-match', h); h['p:' + (P + 1 - k)] = h['src:' + (B - k)] = 'lz-eq'; } });
    })(k);
  }
  STEPS.push({ pos: l3[0], m: l3[0] - l3[3], len: l3[2],
    say: 'Byte ' + (l3[0] - 1) + ' is not byte ' + (l3[0] - 1 - l3[3]) + ': the step back stops. Level 3 writes one sequence, ' + l3[1] + ' literals and ' + l3[2] +
      ' bytes at offset ' + l3[3] + '. Levels 1 and 2 write two: ' + l2[0][1] + ' literals and ' + l2[0][2] + ' bytes at offset ' + l2[0][3] + ', then ' + l2[1][2] +
      ' bytes at offset ' + l2[1][3] + '.',
    h: function (h) { range('p', l3[0], l3[2], 'lz-match', h); range('src', l3[0] - l3[3], l3[2], 'lz-match', h);
      h['p:' + (l3[0] - 1)] = h['src:' + (l3[0] - 1 - l3[3])] = 'lz-bad'; } });

  var root = ctx.svg('svg', { role: 'img', 'aria-label': 'Three stretches of the page, the bytes at one offset above each other: the match the hash table finds at pos, the longer one at pos + 1, and the step back' }, ctx.stage);
  var mEnd = before[0] + before[2];
  function layout() {
    var n = ctx.narrow;
    // wide: a row's name left of it; narrow: above it
    var L = n ? { W: 360, X0: 6, CW: 19, CH: 24, fs: 10, ps: 9, rowH: 84, top: 20 }
      : { W: 760, X0: 112, CW: 35, CH: 28, fs: 13, ps: 10, rowH: 86, top: 10 };
    L.legY = L.top + 3 * L.rowH + (n ? 4 : 8);
    L.H = L.legY + (n ? 38 : 8);
    root.setAttribute('viewBox', '0 0 ' + L.W + ' ' + L.H);
    return L;
  }
  function T(x, y, s, cls, anchor, size, weight) {
    var a = { x: x, y: y, 'text-anchor': anchor || 'start', class: cls || '', text: s, 'font-size': size || 12 };
    if (weight) a['font-weight'] = weight;
    return ctx.svg('text', a, root);
  }
  // a triangle pointing up at the cell above it, and its label under it
  function pointer(L, x, y, label, cls, anchor) {
    ctx.svg('path', { d: 'M' + x + ' ' + y + ' l-5 8 h10 z', class: cls }, root);
    var dx = anchor === 'end' ? 4 : anchor === 'start' ? -4 : 0;
    T(x + dx, y + 10 + L.ps + 2, label, cls + 't', anchor || 'middle', L.ps + 2, 600);
  }
  function colX(L, row, q) { return L.X0 + (q - ROWS[row].col0) * L.CW; }

  function draw(k) {
    var st = STEPS[k], L = layout(), h = {};
    while (root.firstChild) root.removeChild(root.firstChild);
    if (st.h) st.h(h);
    ORDER.forEach(function (name, r) {
      var R = ROWS[name], y = L.top + r * L.rowH, first = R.d.from, last = R.d.from + R.d.b.length - 1;
      var off = name === 'src' ? OFF_B : OFF_A;
      var title = name === 'p' ? 'around pos' : 'offset ' + off;
      if (ctx.narrow) T(L.X0, y - 6, title + ': bytes ' + first + ' to ' + last, 'lz-name', 'start', 10, 600);
      else {
        T(8, y + 12, title, 'lz-name', 'start', 11, 600);
        T(8, y + 26, 'bytes ' + first + ' to ' + last, 'lz-note', 'start', 10);
      }
      R.d.b.forEach(function (b, i) {
        var q = first + i, x = colX(L, name, q), cls = h[name + ':' + q] || '';
        // the bytes of the match before, which the table has only one position of
        var old = name === 'src' && q < mEnd && !cls ? ' lz-old' : '';
        ctx.svg('rect', { x: x + 1, y: y, width: L.CW - 2, height: L.CH, rx: 2, class: 'lz-cell ' + cls + old }, root);
        T(x + L.CW / 2, y + L.CH / 2 + L.fs * 0.36, hex(b), /lz-(match|cand|eq)/.test(cls) ? 'lz-on' : '', 'middle', L.fs, 600);
        T(x + L.CW / 2, y + L.CH + L.ps + 3, String(q), 'lz-note', 'middle', L.ps);
        if (name === 'src' && q === mEnd - 2) ctx.svg('circle', { cx: x + L.CW / 2, cy: y + L.CH - 4, r: 2.2, class: 'lz-tab' }, root);
      });
    });
    // pos under the middle row, m under the row its match comes from
    function yPtr(r) { return L.top + r * L.rowH + L.CH + L.ps + 7; }
    // with p + 1 next to it, the two labels go apart
    if (st.pos != null) pointer(L, colX(L, 'p', st.pos) + L.CW / 2, yPtr(1), 'pos', 'lz-ptr', st.look ? 'end' : null);
    if (st.look) pointer(L, colX(L, 'p', st.look) + L.CW / 2, yPtr(1), 'pos + 1', 'lz-ptr2', 'start');
    if (st.m != null) {
      var row = st.m === A ? 'a' : 'src';
      pointer(L, colX(L, row, st.m) + L.CW / 2, yPtr(row === 'a' ? 2 : 0), 'm', 'lz-ptr');
    }
    // the legend
    var leg = [['lz-old', 'in the match before'], ['lz-tab', 'in the hash table'], ['lz-cand', 'a match found'], ['lz-match', 'the match taken'], ['lz-bad', 'differs']];
    leg.forEach(function (g, i) {
      var x = ctx.narrow ? L.X0 + (i % 2) * 175 : 8 + i * 152, y = L.legY + (ctx.narrow ? Math.floor(i / 2) * 16 : 0);
      if (g[0] === 'lz-tab') ctx.svg('circle', { cx: x + 6, cy: y - 4, r: 2.5, class: 'lz-tab' }, root);
      else ctx.svg('rect', { x: x, y: y - 10, width: 12, height: 12, rx: 2, class: 'lz-cell ' + g[0] }, root);
      T(x + 18, y, g[1], 'lz-note', 'start', ctx.narrow ? 10 : 11);
    });
    ctx.say(st.say);
  }
  return { steps: STEPS.length, draw: draw, interval: 3200, wide: 640 };
});
