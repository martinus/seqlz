/*
 * read_own_table() and own_fill() of chapter 13 on a page's own literal table: the description's bits,
 * the 64-bit window bits with avail bits in it, the lookup of quad[] with the next 8 bits, the symbols
 * a step takes, the buckets by code length and the code space; then the decode table, written length by
 * length. Two pages: chapter 4's text page, whose codes of 3 and 4 bits take one lookup each, and the
 * base64 page, whose lengths of 6 bits come two per lookup. Data: anim/ownread.c, which replays the
 * loop and checks the replay against the real read_own_table().
 */
BookAnim.define('ownread', function (ctx) {
  var D = ctx.data, FULL = D.full, NONE = D.none, STEPMAX = D.step_max;
  var PAGES = {}, pageId = 'text';
  D.pages.forEach(function (p) { PAGES[p.id] = p; });

  function hx(v) { return '0x' + v.toString(16).padStart(2, '0'); }
  function hex8(v) { return '0x' + (v >>> 0).toString(16).padStart(8, '0'); }
  function name(v) { return v === 32 ? 'the space' : v === 10 ? 'the newline' : v > 32 && v < 127 ? '"' + String.fromCharCode(v) + '"' : hx(v); }
  function glyph(v) { return v === 32 ? '␣' : v === 10 ? '↵' : v > 32 && v < 127 ? String.fromCharCode(v) : v.toString(16).padStart(2, '0'); }
  function bin(v, n) { return v.toString(2).padStart(n, '0'); }
  function plural(n, w) { return n + ' ' + w + (n === 1 ? '' : 's'); }
  function wbits(h) { var s = ''; for (var i = 0; i < 16; i++) s += bin(parseInt(h[i], 16), 4); return s; }
  function segText(g) { return g[0] === 'run' ? 'a run of ' + g[2] : 'length ' + g[2]; }

  // the plan of one page: a step per loop step, the checks, then own_fill() length by length
  function build(P) {
    var S = [], fill = [], buckets = {}, none = [], spaceParts = [], st = P.steps, LENS = [];
    P.buckets.forEach(function (bk, i) { if (bk.length) LENS.push(i + 1); });
    var DBYTES = P.desc.length, DBITS = 8 * DBYTES, BITS = '';
    P.desc.forEach(function (v) { BITS += bin(v, 8); });
    function snap(o) {
      var b = {};
      Object.keys(buckets).forEach(function (l) { b[l] = buckets[l].slice(); });
      o.buckets = b; o.none = none.slice(); o.parts = spaceParts.slice();
      S.push(o);
    }
    snap({ intro: true, space: 0, b: 0 });
    st.forEach(function (s, i) {
      var pos = 8 * s.at - s.avail, now = [];
      if (s.kind !== 'alone') {
        (buckets[s.l1] = buckets[s.l1] || []).push(s.b1); now.push([s.l1, s.b1]);
        spaceParts.push([s.l1, s.b1]);
        if (s.kind === 'two') { (buckets[s.l2] = buckets[s.l2] || []).push(s.b2); now.push([s.l2, s.b2]); spaceParts.push([s.l2, s.b2]); }
        else none.push(s.b2);
      }
      snap({ i: i, s: s, pos: pos, now: now, space: s.space, b: s.b });
    });
    var last = st[st.length - 1], used = P.used;
    snap({ checks: true, pos: used, space: FULL, b: last.b, s: last, after: true });
    var o = 0;
    LENS.forEach(function (l) {
      var bytes = P.buckets[l - 1], run = FULL >> l;
      fill.push({ l: l, from: o, to: o + bytes.length * run, bytes: bytes });
      o += bytes.length * run;
    });
    fill.forEach(function (f, k) { snap({ fill: k, space: FULL, b: last.b, after: true }); });
    snap({ fill: fill.length, space: FULL, b: last.b, after: true, done: true });
    return { S: S, fill: fill, LENS: LENS, BITS: BITS, DBITS: DBITS, DBYTES: DBYTES, P: P };
  }
  var plan = build(PAGES[pageId]);

  function sayOf(k) {
    var p = plan, st = p.S[k], P = p.P, s = st.s, what = P.id === 'text' ? 'chapter 4’s text page' : 'the base64 page';
    if (st.intro) {
      return 'The description of ' + what + '’s own table, ' + p.DBYTES + ' bytes from byte ' + P.at + ' of the compressed page. bits is empty, avail = 0, ' +
        'space = 0 of ' + FULL + ', b = 0: the next length is for byte value 0.';
    }
    if (st.checks) {
      var pad = (8 - P.used % 8) % 8;
      return 'space = ' + FULL + ' ends the loop after ' + P.steps.length + ' steps. used = 8 · at − avail = ' + P.used + ' bits; the checks pass: the code space is exactly full, b = ' + st.b +
        ' is at most 256, ' + (pad ? 'the ' + plural(pad, 'bit') + ' after the last symbol in its byte ' + (pad === 1 ? 'is' : 'are') + ' 0' : 'the last symbol ends a byte, so no bits are left to check') +
        ', and the table ends after ' + p.DBYTES + ' bytes.';
    }
    if (st.fill != null && !st.done) {
      var f = p.fill[st.fill], run = FULL >> f.l;
      var how = f.l <= 8 ? 'one 8-byte store writes 4 of them, ' + plural(run / 4, 'store') + ' per byte' : f.l === 9 ? 'one 4-byte store writes both' : 'one 2-byte store';
      return 'own_fill(), length ' + f.l + ': the ' + plural(f.bytes.length, 'byte') + ' of bucket ' + f.l + ', in the order of their values, each ' + run + ' entries of l | byte << 8 in a row; ' + how +
        '. Entries ' + f.from + ' to ' + (f.to - 1) + '.' + (st.fill === 0 ? ' The first byte gets the code of all zeros, as the canonical code wants.' : '');
    }
    if (st.done) {
      return 'The decode table is complete, ' + FULL + ' entries, the same layout as a fixed table’s. A code of l bits fills the ' + FULL + ' >> l entries whose 10-bit index starts with it: ' +
        'decoding looks up the next 10 bits of a stream and finds the byte and the code’s length.';
    }
    var t = '';
    if (s.load) t += 'avail was below ' + STEPMAX + ': 8 bytes are loaded big-endian and the ' + s.load + ' whole bytes that fit below 64 bits are kept, so at = ' + s.at + ', avail = ' + s.avail + '. ';
    t += 'top = ' + bin(s.top, 8) + ', quad[top] = ' + hex8(s.e) + ': ';
    if (s.kind === 'alone') {
      var g = s.segs[0], nb = s.sym - 9;
      return t + 'bit 31, a run that no entry holds. decode[top] gives symbol ' + s.sym + ', a run ' + plural(nb, 'bit') + ' wide, and ' +
        (nb > 1 ? 'the ' + plural(nb - 1, 'bit') + ' below its top 1 follow' : 'no bits follow') + ': a run of ' + g[2] + ', ' + g[1] + ' bits. b goes from ' + s.b0 + ' to ' + s.b +
        ', so ' + (g[2] === 1 ? name(s.b0) + ' has' : 'the byte values ' + hx(s.b0) + ' to ' + hx(s.b - 1) + ' have') + ' no code; space stays at ' + s.space + '.';
    }
    var parts = s.segs.map(segText).join(', ');
    if (s.kind === 'two') {
      return t + parts + ', ' + s.k + ' bits in one lookup: ' + name(s.b1) + ' gets ' + s.l1 + ' bits, ' + name(s.b2) + ' ' + s.l2 + '. space = ' + s.space + ', b = ' + s.b + '.';
    }
    var why = s.cut ? 'The entry has a second length too, but ' + name(s.b1) + '’s code fills the code space, so two is 0' : 'The 8 bits do not hold a second length whole, so the entry has one';
    return t + parts + ', ' + s.k + ' bits: ' + name(s.b1) + ' gets a code of ' + s.l1 + ' bits, space = ' + s.space + '. ' + why +
      ', and the second store goes to row ' + NONE + ', "none", which takes no code space and moves b by 0.';
  }

  var choice = ctx.el('div', 'an-choice', null, ctx.bar);
  var btns = [['text', 'chapter 4’s text page'], ['base64', 'the base64 page']].map(function (m) {
    var b = ctx.el('button', null, m[1], choice);
    b.type = 'button'; b.setAttribute('aria-pressed', m[0] === pageId ? 'true' : 'false');
    b.addEventListener('click', function () {
      pageId = m[0];
      btns.forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); });
      plan = build(PAGES[pageId]); ctx.restart();
    });
    return b;
  });

  var root = ctx.svg('svg', { role: 'img', 'aria-label': 'The bits of the description, the 64-bit window, the entry of quad[], the buckets by code length and the code space; then the decode table' }, ctx.stage);
  function lenClass(l) { return 'or-c' + Math.min(plan.LENS.indexOf(l), 2); }

  function layout() {
    var n = ctx.narrow, p = plan, L = { W: n ? 360 : 744, X0: n ? 4 : 20 };
    L.perRow = n ? 32 : 64; L.CW = 10; L.bitH = 16; L.rowH = 21;
    L.descRows = Math.ceil(p.DBITS / L.perRow);
    L.descY = 26;
    L.winHead = L.descY + L.descRows * L.rowH + 22;
    L.winY = L.winHead + 18;
    L.winRows = n ? 2 : 1;
    L.quadHead = L.winY + L.winRows * L.rowH + 26;
    L.quadY = L.quadHead + 8;
    L.fieldW = n ? 116 : 118; L.fieldsPer = n ? 3 : 6;
    L.r1 = L.quadY + Math.ceil(6 / L.fieldsPer) * 40;
    // the decode table in place of the bits, once the loop is done
    L.ltCols = n ? 32 : 64; L.ltW = 10; L.ltH = n ? 10 : 12;
    L.ltY = 26;
    L.r2 = L.ltY + (FULL / L.ltCols) * L.ltH + 10;
    L.bkHead = Math.max(L.r1, L.r2) + 12;
    L.cell = 14; L.cellH = 16; L.bkX = L.X0 + 76;
    L.bkPer = Math.floor((L.W - L.X0 - L.bkX) / L.cell);
    var y = L.bkHead + 10;
    L.bkY = {};
    p.LENS.concat([NONE]).forEach(function (l) {
      var cnt = l === NONE ? p.P.steps.filter(function (s) { return s.kind === 'one'; }).length : p.P.buckets[l - 1].length;
      L.bkY[l] = y;
      y += Math.max(1, Math.ceil(cnt / L.bkPer)) * (L.cellH + 2) + 4;
    });
    L.barHead = y + 18;
    L.barY = L.barHead + 8;
    L.barW = L.W - 2 * L.X0;
    L.H = L.barY + 30;
    root.setAttribute('viewBox', '0 0 ' + L.W + ' ' + L.H);
    return L;
  }
  function bitX(L, i, per) { var c = i % per; return L.X0 + c * L.CW + Math.floor(c / 8) * 4; }

  // a symbol of the step: a run or a length, every second one lighter, so that two in a row stay apart
  function segClass(g) { return (g[2] === 'run' ? 'or-run' : 'or-len') + (g[3] % 2 ? ' or-alt' : ''); }
  function head(L, x, y, text) { ctx.svg('text', { x: x, y: y, class: 'or-head', text: text }, root); }

  function draw(k) {
    var p = plan, st = p.S[k], s = st.s, L = layout(), P = p.P;
    while (root.firstChild) root.removeChild(root.firstChild);
    var loop = st.fill == null;
    if (loop) {
      // the description's bits: read, this step's symbols, in the window, not loaded yet
      head(L, L.X0, 14, 'the description, ' + p.DBYTES + ' bytes, bit 7 first');
      var at8 = s ? 8 * s.at : 0, pos = st.pos || 0, segAt = [];
      if (s && !st.after) { var q = pos; s.segs.forEach(function (g, j) { segAt.push([q, q + g[1], g[0], j]); q += g[1]; }); }
      for (var i = 0; i < p.DBITS; i++) {
        var cls = 'or-unread', tc = '';
        if (i < pos) { cls = 'or-read'; tc = 'or-tf'; }
        else if (i < at8) cls = 'or-win';
        segAt.forEach(function (g) { if (i >= g[0] && i < g[1]) { cls = segClass(g); tc = 'or-tf'; } });
        if (st.checks && i >= P.used) { cls = 'or-pad'; tc = ''; }
        var x = bitX(L, i, L.perRow), y = L.descY + Math.floor(i / L.perRow) * L.rowH;
        ctx.svg('rect', { x: x, y: y, width: L.CW - 1.5, height: L.bitH, rx: 2, class: cls }, root);
        ctx.svg('text', { x: x + (L.CW - 1.5) / 2, y: y + L.bitH - 4, 'text-anchor': 'middle', 'font-size': 11, class: tc, text: p.BITS[i] }, root);
      }
      // the window: avail bits on top, zeros below; the step's symbols in the first k
      var W64 = s ? wbits(s.w) : '0'.repeat(64), avail = s ? s.avail : 0;
      // after the loop: the window as the last step left it, its top bits the padding of the last byte
      if (st.after) { W64 = W64.slice(s.k) + '0'.repeat(s.k); avail -= s.k; }
      var padN = st.after ? (8 - P.used % 8) % 8 : 0;
      head(L, L.X0, L.winHead, 'bits: avail = ' + avail + (s && !st.after ? ', top = the first 8' : padN ? ', the first ' + padN + ' must be 0' : ''));
      var per = 64 / L.winRows, kk = s && !st.after ? s.k : 0, newFrom = s && s.load ? avail - 8 * s.load : 99;
      var wsegs = [], q2 = 0;
      if (s && !st.after) s.segs.forEach(function (g, j) { wsegs.push([q2, q2 + g[1], g[0], j]); q2 += g[1]; });
      for (i = 0; i < 64; i++) {
        var c2 = i < avail ? 'or-win' : 'or-zero', t2 = '';
        wsegs.forEach(function (g) { if (i >= g[0] && i < g[1]) { c2 = segClass(g); t2 = 'or-tf'; } });
        if (st.after && i < avail) c2 = 'or-win';
        var wx = bitX(L, i, per), wy = L.winY + Math.floor(i / per) * L.rowH;
        ctx.svg('rect', { x: wx, y: wy, width: L.CW - 1.5, height: L.bitH, rx: 2, class: c2 }, root);
        if (i >= newFrom && i < avail && !st.after) ctx.svg('rect', { x: wx - 0.5, y: wy + L.bitH + 1.5, width: L.CW, height: 2.5, class: 'or-new' }, root);
        ctx.svg('text', { x: wx + (L.CW - 1.5) / 2, y: wy + L.bitH - 4, 'text-anchor': 'middle', 'font-size': 11, class: t2 || (i < avail ? '' : 'or-note'), text: W64[i] }, root);
      }
      if (s && (!st.after || padN)) {
        ctx.svg('rect', { x: L.X0 - 2, y: L.winY - 3, width: bitX(L, (st.after ? padN : 8) - 1, per) + L.CW - L.X0 + 2, height: L.bitH + 6, rx: 3, class: 'or-top' }, root);
      }
      // the entry of quad[]
      if (s && !st.after) {
        head(L, L.X0, L.quadHead, 'quad[' + bin(s.top, 8) + '] = ' + hex8(s.e));
        var F;
        if (s.kind === 'alone') {
          F = [['bit 31', 1], ['symbol', s.sym], ['code bits', D.own[s.sym]], ['run bits', s.sym - 10], ['run', s.segs[0][2]], ['bits', s.k]];
        } else {
          var e = s.e, two = s.kind === 'two';
          F = [['run 1', e >>> 16 & 7], ['length 1', e & 15], ['end 1', e >>> 8 & 15], ['run 2', e >>> 19 & 7, !two], ['length 2', (e >>> 4 & 15) === NONE ? 'none' : e >>> 4 & 15, !two], ['end 2', e >>> 12 & 15, !two]];
        }
        F.forEach(function (f, j) {
          var fx = L.X0 + (j % L.fieldsPer) * L.fieldW, fy = L.quadY + Math.floor(j / L.fieldsPer) * 40;
          ctx.svg('rect', { x: fx, y: fy, width: L.fieldW - 6, height: 32, rx: 3, class: f[2] ? 'or-field or-off' : 'or-field' }, root);
          ctx.svg('text', { x: fx + 6, y: fy + 13, 'font-size': 10, class: 'or-note', text: f[0] }, root);
          ctx.svg('text', { x: fx + 6, y: fy + 27, 'font-size': 12, class: f[2] ? 'or-note' : '', text: String(f[1]) }, root);
        });
      }
    } else {
      // the decode table, filled up to this length
      var upto = st.done ? p.fill.length : st.fill + 1;
      head(L, L.X0, 14, 'the decode table lt, ' + FULL + ' entries, ' + L.ltCols + ' per row');
      for (i = 0; i < FULL; i++) {
        var cx = L.X0 + (i % L.ltCols) * L.ltW, cy = L.ltY + Math.floor(i / L.ltCols) * L.ltH;
        ctx.svg('rect', { x: cx, y: cy, width: L.ltW, height: L.ltH, class: 'or-empty' }, root);
      }
      for (var fk = 0; fk < upto; fk++) {
        var f = p.fill[fk], run = FULL >> f.l;
        f.bytes.forEach(function (v, j) {
          var a = f.from + j * run;
          for (var r = 0; r < run; r += L.ltCols - (a + r) % L.ltCols) {
            var at = a + r, w = Math.min(run - r, L.ltCols - at % L.ltCols);
            ctx.svg('rect', { x: L.X0 + (at % L.ltCols) * L.ltW + 0.5, y: L.ltY + Math.floor(at / L.ltCols) * L.ltH + 0.5, width: w * L.ltW - 1, height: L.ltH,
              class: lenClass(f.l) + (fk < st.fill && !st.done ? ' or-old' : '') + (j % 2 ? ' or-alt' : '') }, root);
          }
          var gx = L.X0 + (a % L.ltCols) * L.ltW, gy = L.ltY + Math.floor(a / L.ltCols) * L.ltH;
          var code = (a >> (10 - f.l)).toString(2).padStart(f.l, '0');
          var label = run * L.ltW >= 90 || (run > L.ltCols && L.ltCols * L.ltW >= 90) ? glyph(v) + ' ' + code : run * L.ltW >= 14 ? glyph(v) : '';
          if (label) ctx.svg('text', { x: gx + 3, y: gy + L.ltH - 2, 'font-size': L.ltH >= 12 ? 10.5 : 9.5, class: 'or-tf', text: label }, root);
        });
      }
    }
    // the buckets: a row of 256 bytes per length, filled in order of the byte values
    head(L, L.X0, L.bkHead, 'bucket: the bytes of each length');
    p.LENS.concat([NONE]).forEach(function (l) {
      var y0 = L.bkY[l], list = l === NONE ? st.none : st.buckets[l] || [];
      ctx.svg('text', { x: L.X0, y: y0 + 12, 'font-size': 11, class: 'or-note', text: l === NONE ? 'none (' + NONE + ')' : l + ' bits' }, root);
      list.forEach(function (v, j) {
        var x = L.bkX + (j % L.bkPer) * L.cell, y = y0 + Math.floor(j / L.bkPer) * (L.cellH + 2);
        var isNow = st.now && st.now.some(function (n) { return n[0] === l && n[1] === v; }) && j >= list.length - 2;
        var filling = st.fill != null && !st.done && p.fill[st.fill].l === l;
        ctx.svg('rect', { x: x, y: y, width: L.cell - 2, height: L.cellH, rx: 2, class: (l === NONE ? 'or-none' : lenClass(l)) + (isNow || filling ? ' or-now' : '') }, root);
        ctx.svg('text', { x: x + (L.cell - 2) / 2, y: y + 12, 'text-anchor': 'middle', 'font-size': 10, class: l === NONE ? 'or-note' : 'or-tf', text: glyph(v) }, root);
      });
    });
    // the code space, in the order the lengths came, and b
    head(L, L.X0, L.barHead, 'space: ' + st.space + ' of ' + FULL + (st.fill == null ? ', b = ' + st.b : ''));
    ctx.svg('rect', { x: L.X0, y: L.barY, width: L.barW, height: 20, class: 'or-unread' }, root);
    var u = 0;
    st.parts.forEach(function (pt, j) {
      var bw = L.barW * (FULL >> pt[0]) / FULL, bx = L.X0 + L.barW * u / FULL;
      var isNow = st.now && st.now.some(function (n) { return n[0] === pt[0] && n[1] === pt[1]; });
      ctx.svg('rect', { x: bx, y: L.barY, width: Math.max(bw - 1, 1), height: 20, class: lenClass(pt[0]) + (isNow ? ' or-now' : '') }, root);
      if (bw >= 14) ctx.svg('text', { x: bx + bw / 2, y: L.barY + 14, 'text-anchor': 'middle', 'font-size': 10, class: 'or-tf', text: glyph(pt[1]) }, root);
      u += FULL >> pt[0];
    });
    ctx.say(sayOf(k));
  }
  return { steps: function () { return plan.S.length; }, draw: draw, interval: 2200, wide: 630 };
});
