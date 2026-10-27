/*
 * code_literals() of chapter 11 writing the coded streams over the raw literals of chapter 16's page in
 * dst: the copy of the raw literals in the matcher's table, which the runs of lit_streams() read, drawn
 * under the bytes of dst where those literals were; the 8 streams' rooms from q[0] to q[8], each
 * flush's store, and the bitstream behind the raw literals, which no store reaches; then the header and
 * the move of the bitstream. Data: anim/inplace.c, the real encoder's lit_put() and lit_flush(),
 * checked against code_literals().
 */
BookAnim.define('inplace', function (ctx) {
  var D = ctx.data, N = D.n_lit, LEN = D.len, Q = D.q, BS = 2 + N, RUNS = D.runs, CODED = D.coded;
  function list(a) { return a.slice(0, -1).join(', ') + ' and ' + a[a.length - 1]; }
  function hx(v) { return '0x' + v.toString(16).padStart(2, '0'); }

  // the plan: a step per run of literals and its 4 flushes, around them the copy, the header, the move
  var S = [], wrote = [0, 0, 0, 0, 0, 0, 0, 0].map(function (_, j) { return Q[j]; });
  S.push({ say: 'dst after compress_block(): ' + LEN + ' bytes, the u16, the ' + N + ' raw literals from byte 2 and the sequences’ bitstream from byte ' + BS +
    '. The coded literals will go where the raw ones are, from the front of dst.' });
  S.push({ copy: true, say: 'The literals get coded, so code_literals() copies the ' + N + ' raw literals into spare, the matcher’s table, which is garbage now. Drawn under dst, each literal sits below the byte it came from: literal k was at byte 2 + k.' });
  S.push({ copy: true, rooms: true, say: 'The sizes say where each stream goes: q[0] = ' + Q[0] + ', right after the header, and q[8] = ' + Q[8] + '. The bitstream starts at byte ' + BS +
    ': the rule that coding has to save enough keeps them apart, here by ' + (BS - Q[8]) + ' bytes. The bytes from q[0] to q[8] still hold raw literals.' });
  RUNS.forEach(function (r, i) {
    var st = { copy: true, rooms: true, run: i };
    r.flush.forEach(function (f) { wrote[f[0]] = f[2]; });
    st.wrote = wrote.slice();
    var streams = r.flush.map(function (f) { return f[0]; }), first = streams[0], last = streams[3];
    var stores = r.flush.map(function (f) { return f[3]; }), at = r.flush.map(function (f) { return f[1]; });
    var moved = r.flush.map(function (f) { return f[2] - f[1]; });
    var hi = Math.max.apply(null, r.flush.map(function (f) { return f[1] + f[3]; }));
    var t;
    if (r.from >= r.to) {
      t = 'No literals are left for streams ' + first + ' to ' + last + '. Their last flushes write the bits still in the accumulators: store_tail() writes ' + list(stores) +
        ' byte' + (Math.max.apply(null, stores) > 1 ? 's' : '') + ' at ' + list(at) + ', the bytes that carry bits, and no store reaches the next stream.';
    } else {
      var old = 2 + r.to - 1, ahead = r.flush.filter(function (f) { return f[1] + f[3] > old; }).length;
      t = (r.tail ? 'The tail, literals ' : 'Literals ') + r.from + ' to ' + (r.to - 1) + ' of the copy, the ' + (r.tail ? '' : '4 of every 8 ') + 'that go to streams ' + first + ' to ' + last + ', then a flush each: ' +
        (stores.every(function (x) { return x === 8; }) ? '8 bytes stored' : list(stores) + ' bytes stored') + ' at ' + list(at) + ', and p moves on by ' + list(moved) + '. ';
      if (stores.some(function (x) { return x < 8; })) t += 'Where fewer than 8 bytes are left before the next stream, store_tail() writes only the bytes with bits. ';
      t += 'Literal ' + (r.to - 1) + ' was at byte ' + old + ' of dst' + (ahead === 0 ? ', and all 4 stores are below it. ' :
        ': ' + (ahead === 4 ? 'all 4' : ahead + ' of the 4') + ' stores are past it, over raw literals not read yet, which is fine only because they are read from the copy. ') +
        'The highest store ends at byte ' + hi + ', the bitstream starts at ' + BS + '.';
    }
    st.say = t;
    S.push(st);
  });
  S.push({ copy: true, rooms: true, wrote: Q.slice(1), header: true, say: 'All 8 streams are written, each up to the start of the next. Then the header goes over the first raw literals: the u16 ' +
    hx(D.head[0]) + ' ' + hx(D.head[1]) + ' with SEQLZ_LIT_CODED, byte 2 ' + hx(D.head[2]) + ' for table ' + D.set + ' and a width of ' + D.width + ', and the 8 sizes in bytes 3 to ' + (D.header - 1) + '.' });
  S.push({ rooms: true, wrote: Q.slice(1), header: true, moved: true, say: 'memmove() moves the ' + (LEN - BS) + ' bytes of the bitstream from byte ' + BS + ' down to q[8] = ' + Q[8] +
    ': the page is ' + CODED + ' bytes instead of ' + LEN + '. ' + (CODED > BS ? 'A memmove(), not a memcpy(), as the two places overlap: bytes ' + BS + ' to ' + (CODED - 1) + ' are in both. ' : '') + 'Bytes ' + CODED + ' to ' + (LEN - 1) + ' keep what they held, but are no longer part of the page.' });

  var root = ctx.svg('svg', { role: 'img', 'aria-label': 'dst while code_literals() writes the 8 streams over the raw literals, which it reads from a copy, and moves the bitstream down' }, ctx.stage);
  function layout() {
    var n = ctx.narrow, L = { W: n ? 360 : 744, X0: n ? 6 : 20 };
    L.barW = L.W - 2 * L.X0;
    L.x = function (b) { return L.X0 + L.barW * b / LEN; };
    L.dstHead = 16; L.lab = 36; L.dstY = 42; L.dstH = 30;
    L.num = L.dstY + L.dstH + 14;
    L.copyY = L.num + 12; L.copyH = 22; L.copyHead = L.copyY + L.copyH + 16;
    L.legY = L.copyHead + 26;
    L.H = L.legY + (n ? 42 : 20);
    root.setAttribute('viewBox', '0 0 ' + L.W + ' ' + L.H);
    return L;
  }
  function box(L, from, to, y, h, cls) {
    return ctx.svg('rect', { x: L.x(from), y: y, width: Math.max(L.x(to) - L.x(from), 1), height: h, class: cls }, root);
  }

  function draw(k) {
    var st = S[k], L = layout(), r = st.run != null ? RUNS[st.run] : null;
    while (root.firstChild) root.removeChild(root.firstChild);
    // dst: the u16, the raw literals, the bitstream, and over them what code_literals() wrote
    ctx.svg('text', { x: L.X0, y: L.dstHead, class: 'ip-head', text: 'dst, ' + (st.moved ? CODED : LEN) + ' bytes' }, root);
    box(L, 0, 2, L.dstY, L.dstH, 'ip-hdr');
    box(L, 2, st.moved ? Q[8] : BS, L.dstY, L.dstH, 'ip-raw');
    if (!st.moved) box(L, BS, LEN, L.dstY, L.dstH, 'ip-bits');
    if (st.header) box(L, 0, D.header, L.dstY, L.dstH, 'ip-hdr ip-now');
    if (st.rooms) {
      for (var j = 0; j < 8; j++) {
        var w = st.wrote ? st.wrote[j] : Q[j];
        box(L, Q[j], Q[j + 1], L.dstY, L.dstH, 'ip-room');
        if (w > Q[j]) box(L, Q[j], w, L.dstY + 1, L.dstH - 2, j < 4 ? 'ip-s0' : 'ip-s1');
        var lx = (L.x(Q[j]) + L.x(Q[j + 1])) / 2;
        ctx.svg('text', { x: lx, y: L.lab, 'text-anchor': 'middle', 'font-size': 11, class: r && r.flush.some(function (f) { return f[0] === j; }) ? '' : 'ip-note', text: 's' + j }, root);
      }
    }
    if (r) r.flush.forEach(function (f) { box(L, f[1], f[1] + f[3], L.dstY - 3, L.dstH + 6, 'ip-store'); });
    if (st.moved) {
      box(L, Q[8], CODED, L.dstY, L.dstH, 'ip-bits ip-now');
      box(L, CODED, LEN, L.dstY, L.dstH, 'ip-stale');
    }
    // the copy, each literal under the byte of dst it came from
    ctx.svg('text', { x: L.X0, y: L.copyHead, class: 'ip-head', text: ctx.narrow ? 'spare, the copy of the literals' : 'spare = st->literals, the copy of the ' + N + ' raw literals' }, root);
    box(L, 2, BS, L.copyY, L.copyH, st.copy ? 'ip-raw' : 'ip-gone');
    if (r && r.to > r.from) {
      // what this half read so far, and this run; the reader is here, not in dst
      var half0 = RUNS.filter(function (x, i) { return i < st.run && x.half === r.half; });
      if (half0.length) box(L, 2 + r.half, 2 + half0[half0.length - 1].to, L.copyY + 1, L.copyH - 2, 'ip-read');
      box(L, 2 + r.from, 2 + r.to, L.copyY - 2, L.copyH + 4, 'ip-reader');
      var ox = L.x(2 + r.to - 0.5);
      ctx.svg('line', { x1: ox, y1: L.copyY - 2, x2: ox, y2: L.dstY + L.dstH + 2, class: 'ip-ghost' }, root);
    }
    // the numbers under dst: where the bitstream starts or went, and the end
    var marks = st.moved ? [[0, '0'], [Q[8], String(Q[8])], [CODED, String(CODED)], [LEN, String(LEN)]] : [[0, '0'], [BS, String(BS)], [LEN, String(LEN)]];
    if (st.rooms && !st.moved) marks.splice(1, 0, [Q[8], String(Q[8])]);
    marks.forEach(function (m) { ctx.svg('text', { x: L.x(m[0]), y: L.num, 'text-anchor': m[0] === 0 ? 'start' : m[0] === LEN ? 'end' : 'middle', 'font-size': 11, class: 'ip-note ip-halo', text: m[1] }, root); });
    // the key
    var keys = [['ip-raw', 'raw literals'], ['ip-s0', 'streams 0 to 3'], ['ip-s1', 'streams 4 to 7'], ['ip-bits', 'bitstream'], ['ip-store', 'a flush’s store']];
    keys.forEach(function (kk, i) {
      var per = ctx.narrow ? 3 : 5, x = L.X0 + (i % per) * (ctx.narrow ? 118 : 146), y = L.legY + Math.floor(i / per) * 20;
      ctx.svg('rect', { x: x, y: y - 10, width: 14, height: 12, class: kk[0] }, root);
      ctx.svg('text', { x: x + 19, y: y, 'font-size': 11, class: 'ip-note ip-key', text: kk[1] }, root);
    });
    ctx.say(st.say);
  }
  return { steps: S.length, draw: draw, interval: 1600, wide: 630 };
});
