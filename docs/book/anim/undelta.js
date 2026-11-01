/*
 * The deltas of a page's first two sequences turned back into literals, as seqlz_decode() does it at
 * level 3 with SEQLZ_DELTA_4: the copy, undelta_literals() word by word in steps of 16 bytes, the match,
 * then undelta_fast() on the 16 bytes of the fast path. Data: anim/undelta.c, the real decoder with a
 * hook on add4(): every byte drawn is one the decoder had in the page at that step.
 */
BookAnim.define('undelta', function (ctx) {
  var D = ctx.data, F = D.frames, N = D.w, DIST = D.dist;
  var EMPTY = 0, DELTA = 1, SPARE = 2, LIT = 3, MATCH = 4;
  var CLS = ['ud-empty', 'ud-delta', 'ud-spare', 'ud-lit', 'ud-match'];
  var R, CW, CH, X0, Y0, RH, W, H;

  function hex(b) { return (b < 16 ? '0' : '') + b.toString(16); }
  function u32(b, i) { return b[i] + 256 * (b[i + 1] + 256 * (b[i + 2] + 256 * b[i + 3])); }
  function bytes(a, n) { return n === 1 ? 'byte ' + a : 'bytes ' + a + ' to ' + (a + n - 1); }
  function Bytes(a, n) { var t = bytes(a, n); return t[0].toUpperCase() + t.slice(1); }
  // the counters of 4 bytes the page holds after the last step
  var last = F[F.length - 1].b, counters = [];
  for (var i = 0; i + 4 <= N; i += 4) counters.push(u32(last, i));

  var root = ctx.svg('svg', { role: 'img', 'aria-label': 'The first 56 bytes of a page of counters as the decoder turns its literals back from deltas, step by step' }, ctx.stage);
  var gCells = ctx.svg('g', { 'font-size': 13 }, root);
  var gMarks = ctx.svg('g', { fill: 'none' }, root);
  var gText = ctx.svg('g', { 'font-size': 11 }, root);

  function layout() {
    R = ctx.narrow ? 8 : 16; CW = 40; CH = 28; X0 = ctx.narrow ? 30 : 60; Y0 = 26; RH = CH + 26;
    W = X0 + R * CW + 6;
    H = Y0 + Math.ceil(N / R) * RH + (ctx.narrow ? 40 : 20);
    root.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
  }
  function cx(i) { return X0 + (i % R) * CW; }
  function cy(i) { return Y0 + Math.floor(i / R) * RH; }

  // a frame around the cells a to a + n - 1, one per row they are in
  function box(a, n, cls) {
    for (var s = a; s < a + n;) {
      var e = Math.min(a + n, (Math.floor(s / R) + 1) * R);
      ctx.svg('rect', { x: cx(s) - 1.5, y: cy(s) - 1.5, width: (e - s) * CW - 1, height: CH + 3, rx: 4, class: cls }, gMarks);
      s = e;
    }
  }
  // an arrow from the middle of the cells at a to the middle of those at b, n each
  function arrow(a, b, n) {
    var x1 = cx(a) + Math.min(n, R - a % R) * CW / 2 - 2, x2 = cx(b) + Math.min(n, R - b % R) * CW / 2 - 2, d, tip;
    if (Math.floor(a / R) === Math.floor(b / R)) {
      var y = cy(a) - 3, top = y - 18;
      d = 'M' + x1 + ',' + y + ' C' + x1 + ',' + top + ' ' + x2 + ',' + top + ' ' + x2 + ',' + y;
      tip = [x2, y, 0, 1];
    } else {
      var y1 = cy(a) + CH + 3, y2 = cy(b) - 3, l = Math.hypot(x2 - x1, y2 - y1);
      d = 'M' + x1 + ',' + y1 + ' L' + x2 + ',' + y2;
      tip = [x2, y2, (x2 - x1) / l, (y2 - y1) / l];
    }
    ctx.svg('path', { d: d, class: 'ud-arrow' }, gMarks);
    var ux = tip[2], uy = tip[3], px = -uy, py = ux, x = tip[0], y0 = tip[1];
    ctx.svg('path', { d: 'M' + x + ',' + y0 + ' L' + (x - 7 * ux + 4 * px) + ',' + (y0 - 7 * uy + 4 * py) + ' L' + (x - 7 * ux - 4 * px) + ',' + (y0 - 7 * uy - 4 * py) + 'z', class: 'ud-tip' }, gMarks);
  }

  // on a phone two columns: delta and literal, then what was copied past the literals and match
  function legend() {
    var items = [[DELTA, 'delta'], [LIT, 'literal'], [SPARE, 'copied past the literals'], [MATCH, 'match']];
    var y = H - (ctx.narrow ? 32 : 10), x = X0;
    items.forEach(function (it, j) {
      if (ctx.narrow) { x = j % 2 ? 200 : 4; y = H - (j < 2 ? 32 : 12); }
      ctx.svg('rect', { x: x, y: y - 10, width: 14, height: 12, rx: 2, class: CLS[it[0]] }, gText);
      ctx.svg('text', { x: x + 19, y: y, class: 'ud-key', text: it[1] }, gText);
      x += 19 + it[1].length * 7 + 28;
    });
  }

  function say(k) {
    var f = F[k], s = D.seqs[f.seq], prev = k ? F[k - 1].b : null, b = f.b;
    var spare = f.kind === 'copy' ? f.n - s.ll : 0;
    if (f.kind === 'copy' && f.seq === 0)
      return 'The page holds counters of 4 bytes, ' + counters.slice(0, 4).join(', ') + ' and so on, each 0 to 11 more than the one before. Its first sequence is ' + s.ll +
        ' literals at byte 0, then a match of ' + s.ml + ' bytes at offset ' + s.off + '. So many literals need a length value, and the slow path takes it: copy_literals() copies 16 bytes at a time from the scratch, the ' +
        s.ll + ' deltas and ' + spare + ' bytes of the next sequence’s deltas.';
    if (f.kind === 'keep')
      return 'The first ' + DIST + ' bytes of the page have no byte ' + DIST + ' before them, so they were stored as they are. undelta_literals() starts at k = dist − at = ' + DIST + ' − 0 = ' + f.n + '.';
    if (f.kind === 'word') {
      var nth = 0, j;
      for (j = k - 1; j >= 0 && F[j].kind === 'word' && F[j].seq === f.seq; j--) nth++;
      var head = 'add4() at byte ' + f.at + ' adds ' + bytes(f.src, 4) + ' to ' + bytes(f.at, 4) + ', each byte apart, modulo 256';
      var carry = -1;
      for (j = 0; j < 4; j++) if (prev[f.at + j] + b[f.src + j] > 255) { carry = j; break; }
      if (carry >= 0) {
        var c = f.at + carry;
        return head + '. Byte ' + c + ': ' + hex(prev[c]) + ' + ' + hex(b[c - DIST]) + ' = ' + (prev[c] + b[c - DIST]).toString(16) + ', which is ' + hex(b[c]) +
          ' modulo 256. The carry does not go into byte ' + (c + 1) + ', which gets its own delta ' + hex(prev[c + 1]) + ': ' + hex(b[c + 1 - DIST]) + ' + ' + hex(prev[c + 1]) + ' = ' + hex(b[c + 1]) + '.';
      }
      if (f.seq === 0 && nth === 0)
        return head + ': ' + hex(prev[f.at]) + ' + ' + hex(b[f.src]) + ' = ' + hex(b[f.at]) + '. Bytes 0 to 3 are final: they are the page’s own.';
      if (f.seq === 0 && nth === 1)
        return 'add4() at byte ' + f.at + ' reads ' + bytes(f.src, 4) + ', which the word before has just restored. That is why a distance of 4 takes words of 4: add8() here would read ' +
          bytes(f.src, 8) + ', and ' + bytes(f.at, 4) + ' are still deltas.';
      if (f.seq === 0) {
        var left = s.pos + s.ll - f.at;
        if (left >= 4) return head + '. ' + Bytes(f.src, 4) + ' are final, restored by the word before.' + (nth % 4 === 0 ? ' A new step of 16 bytes, undelta16(), starts here.' : '');
        if (left > 0) return head + '. ' + (left === 1 ? 'Byte ' + f.at + ' is the last literal' : Bytes(f.at, left) + ' are the last literals') + '; the step of 16 goes on past the literals, as the page has room for 15 bytes more, so no byte loop is needed. The sums past them are garbage.';
        return head + ': all 4 bytes are past the literals, garbage, which the match writes over. The step of 16 ends here.';
      }
      if (nth === 0)
        return 'undelta_fast() runs four add4() over the 16 bytes, with no loop and no look at nl. The first adds ' + bytes(f.src, 4) + ', the match before, which the loop copied in full: final bytes.';
      var lits = Math.max(0, Math.min(4, s.pos + s.ll - f.at));
      if (lits === 4) return head + '. ' + Bytes(f.src, 4) + ' are the word just restored.';
      if (lits > 0) return head + '. ' + (lits === 1 ? 'Byte ' + f.at + ' is the last literal' : Bytes(f.at, lits) + ' are literals') + '; ' + bytes(f.at + lits, 4 - lits) + ' were copied past the literals, and their sums are garbage.';
      return head + '. All 4 bytes were copied past the literals: garbage, which nothing reads, as the match comes next.';
    }
    if (f.kind === 'byte')
      return 'Byte ' + f.at + ' is left, and no word fits: the byte loop adds byte ' + f.src + ', ' + hex(prev[f.at]) + ' + ' + hex(b[f.src]) + ' = ' + hex(b[f.at]) +
        '. The ' + s.ll + ' literals are the page’s bytes again.';
    if (f.kind === 'copy')
      return 'The second sequence is ' + s.ll + ' literals at byte ' + s.pos + ' and a match of ' + s.ml + ' bytes: no length value, so the fast path takes it. copy16() copies 16 bytes, the ' + s.ll +
        ' deltas and ' + spare + ' bytes of the next sequence’s.';
    // the match
    var end = f.at + f.n - 1;
    return 'The match copies ' + f.n + ' bytes from ' + s.off + ' before, ' + bytes(f.at, Math.min(f.n, N - f.at)) + (end >= N ? ' and on' : '') + ', over every byte copied past the literals.' +
      (k === F.length - 1 ? ' The page holds its counters again: ' + counters.join(', ') + '.' : '') +
      (k < F.length - 1 && counters[Math.floor(f.at / 4)] === counters[Math.floor(f.at / 4) + 1] ? ' The counter ' + counters[Math.floor(f.at / 4)] + ' is there twice: it did not grow.' : '');
  }

  function draw(k) {
    layout();
    var f = F[k];
    [gCells, gMarks, gText].forEach(function (g) { while (g.firstChild) g.removeChild(g.firstChild); });
    for (var i = 0; i < N; i++) {
      var x = cx(i), y = cy(i), c = f.c[i];
      ctx.svg('rect', { x: x, y: y, width: CW - 4, height: CH, rx: 3, class: CLS[c] }, gCells);
      if (c !== EMPTY) ctx.svg('text', { x: x + (CW - 4) / 2, y: y + 19, 'text-anchor': 'middle', class: c === SPARE ? 'ud-tspare' : c === LIT ? 'ud-tlit' : '', text: hex(f.b[i]) }, gCells);
      if (i % R === 0) ctx.svg('text', { x: X0 - 8, y: y + 18, 'text-anchor': 'end', class: 'ud-key', text: (ctx.narrow ? '' : 'byte ') + i }, gText);
    }
    if (f.kind !== 'copy') box(f.at, Math.min(f.n, N - f.at), 'ud-cur');
    else box(f.at, f.n, 'ud-copy');
    if (f.src >= 0) {
      var n = f.kind === 'match' ? Math.min(f.n, D.seqs[f.seq].off) : f.n;
      box(f.src, n, 'ud-src');
      arrow(f.src, f.at, n);
    }
    legend();
    ctx.say(say(k));
  }
  return { steps: F.length, draw: draw, interval: 1600 };
});
