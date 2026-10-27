/*
 * own_lengths() of chapter 11 on the counts of a page's literals: for each byte its share of the
 * literals, f / n, and the share of the code space its code takes, 2^-l. The first loop gives each byte
 * the shortest length whose room is not above its share; the gain, share / room from 1 to 2, sorts the
 * bytes into 16 buckets; then the loop for room left shortens bytes from the top bucket down while their
 * shorter code still fits. Two pages: chapter 4's text page and the base64 page. Data: anim/ownlens.c,
 * which replays own_lengths() on the counts own_code() gets and checks it against the real one.
 */
BookAnim.define('ownlens', function (ctx) {
  var D = ctx.data, FULL = D.full, MAX = D.max, PAGES = {}, pageId = 'text';
  D.pages.forEach(function (p) { PAGES[p.id] = p; });
  function glyph(v) { return v === 32 ? '␣' : v === 10 ? '↵' : v > 32 && v < 127 ? String.fromCharCode(v) : v.toString(16).padStart(2, '0'); }
  function name(v) { return v === 32 ? '"\u2423"' : v === 10 ? 'the newline' : v > 32 && v < 127 ? '"' + String.fromCharCode(v) + '"' : '0x' + v.toString(16).padStart(2, '0'); }
  function cap(s) { return s.charAt(0).toUpperCase() + s.slice(1); }
  function list(a) { return a.length < 2 ? a.join('') : a.slice(0, -1).join(', ') + ' and ' + a[a.length - 1]; }
  function sup(n) { return String(n).replace(/\d/g, function (c) { return '⁰¹²³⁴⁵⁶⁷⁸⁹'[c]; }).replace('−', '⁻').replace('l', 'ˡ'); }
  function cap(s) { return s[0].toUpperCase() + s.slice(1); }

  // the plan of one page: a snapshot of every byte's length, the order and the space per step
  function build(P) {
    var B = {}, order0 = [], S = [];
    P.bytes.forEach(function (r) { B[r[0]] = { b: r[0], f: r[1], l0: r[2], lraw: r[3], q: r[4], gain: r[4] - 16 }; order0.push(r[0]); });
    var len = {}, space = 0;
    function snap(o) { o.len = Object.assign({}, len); o.space = space; S.push(o); return o; }
    var f0 = P.bytes.reduce(function (m, r) { return r[1] > m[1] ? r : m; });
    snap({ order: order0, counts: true, say: 'The page’s ' + P.n_lit + ' literals take ' + order0.length + ' byte values. A grey bar is a byte’s share of the literals, f / n: e.g. ' +
      name(f0[0]) + ', ' + f0[1] + ' of ' + P.n_lit + '. A Huffman code gives a byte about that share of the code space, a code of about −log₂(f / n) bits.' });
    order0.forEach(function (b) { len[b] = B[b].l0; space += FULL >> B[b].l0; });
    var e = B[f0[0]], cut = order0.filter(function (b) { return B[b].lraw > MAX; }).length;
    snap({ order: order0, first: true, say: 'The first loop: each byte gets the shortest l with f · 2' + sup('l') + ' ≥ n, so its room, 2' + sup('−l') + ', the narrow bar, is the largest power of 2 not above its share. ' +
      cap(name(e.b)) + ': ' + e.f + ' · 2' + sup(e.l0) + ' = ' + (e.f << e.l0) + ' ≥ ' + P.n_lit + ', l = ' + e.l0 + '. The lengths fill ' + space + ' of the ' + FULL + ' units of a 10-bit code space' +
      (cut ? ', more than ' + FULL + ' with ' + cut + ' lengths cut to ' + MAX + ' bits.' : ': no room bar is above its share, and no length was cut to ' + MAX + ' bits, so the code cannot be overfull.') });
    snap({ order: order0, first: true, gains: true, say: 'The gain of a byte is its share over its room, f · 2' + sup('l') + ' / n, from 1 to 2: the bits a byte saves per room it takes with a code one bit shorter. ' +
      'q = 16 times that, from 16 to 31, with inv = 2⁴⁰ / n = ' + P.inv + ' instead of a division, and q − 16 is its bucket, the number under the bar. ' + cap(name(e.b)) + ': ' +
      e.f + ' · 2' + sup(e.l0) + ' / ' + P.n_lit + ' = ' + ((e.f << e.l0) / P.n_lit).toFixed(3) + ', q = ' + e.q + ', bucket ' + e.gain + '.' });
    snap({ order: P.by_gain, first: true, gains: true, sorted: true, say: 'A counting sort lists the bytes by bucket in by_gain[], within a bucket by byte value: counts per bucket, their running sums in at[], ' +
      'then each byte to its place. No comparisons, and no sort by count.' });
    // the loops, a step per bucket a pass changes something in, with the bytes it visited there
    var V = P.visits, i = 0, bucketOf = {};
    P.by_gain.forEach(function (b) { bucketOf[b] = B[b].gain; });
    while (i < V.length) {
      var kind = V[i][0], rd = V[i][1], g = bucketOf[V[i][3]], before = i ? V[i - 1][6] : space, grp = [];
      while (i < V.length && V[i][0] === kind && V[i][1] === rd && bucketOf[V[i][3]] === g) grp.push(V[i++]);
      var got = grp.filter(function (v) { return v[5]; });
      grp.forEach(function (v) { if (v[5]) len[v[3]] = kind === 'room' ? v[4] - 1 : v[4] + 1; });
      space = grp[grp.length - 1][6];
      if (!got.length) continue;
      var t = (kind === 'room' ? 'Room left, ' + (FULL - before) + ' units: the loop goes through by_gain[] from its end, the top bucket first. Bucket ' + g + ': '
        : 'Overfull by ' + (before - FULL) + ' units: the loop goes from bucket 0 up. Bucket ' + g + ': ') +
        list(got.map(function (v) { return name(v[3]); })) + (got.length > 1 ? ' get' : ' gets') + ' a code one bit ' + (kind === 'room' ? 'shorter' : 'longer') +
        ', space = ' + space + '.';
      // the bytes of the bucket that did not change, grouped by why
      var why = {};
      grp.forEach(function (v, j) {
        if (v[5]) return;
        var left = FULL - (j ? grp[j - 1][6] : before);
        var key = kind === 'over' ? 'already ' + MAX + ' bits' : v[4] < 2 ? 'no code is shorter than 1 bit' :
          (FULL >> v[4]) + ' more units for ' + (v[4] - 1) + ' bits, ' + left + (left === 1 ? ' is' : ' are') + ' left';
        (why[key] = why[key] || []).push(name(v[3]));
      });
      Object.keys(why).forEach(function (key) {
        var who = why[key], many = who.length > 1;
        t += ' ' + cap(list(who)) + (many ? ' stay' : ' stays') + ': ' + (key.indexOf('more units') > 0 ? (many ? 'each would need ' : 'it would need ') + key : key) + '.';
      });
      snap({ order: P.by_gain, first: true, gains: true, sorted: true, now: got.map(function (v) { return v[3]; }), say: t });
    }
    var bits = 0;
    order0.forEach(function (b) { bits += B[b].f * len[b]; });
    var hist = {};
    order0.forEach(function (b) { hist[len[b]] = (hist[len[b]] || []).concat([b]); });
    var ls = Object.keys(hist).map(Number).sort(function (a, b) { return a - b; });
    snap({ order: P.by_gain, first: true, gains: true, sorted: true, end: true, say: 'space = ' + FULL + ': the code is complete, and the rest of the pass changes nothing. ' +
      ls.map(function (l) { return hist[l].length + ' byte' + (hist[l].length > 1 ? 's' : '') + ' of ' + l + ' bits'; }).join(', ') + ': the literals take ' + bits + ' bits, ' +
      (bits / P.n_lit).toFixed(2) + ' per literal.' + pairText() });
    // a byte that keeps its longer code while one of the same bucket with fewer literals got a shorter one
    function pairText() {
      var best = null;
      V.forEach(function (y) {
        if (y[0] !== 'room' || !y[5]) return;
        V.forEach(function (x) {
          if (x[0] === 'room' && !x[5] && bucketOf[x[3]] === bucketOf[y[3]] && x[4] === y[4] && B[x[3]].f > B[y[3]].f && (!best || B[x[3]].f - B[y[3]].f > best[2]))
            best = [x[3], y[3], B[x[3]].f - B[y[3]].f];
        });
      });
      if (!best) return '';
      var one = best[2] === 1;
      return ' ' + cap(name(best[0])) + ' occurs ' + (one ? 'once' : best[2] + ' times') + ' more than ' + name(best[1]) + ' and keeps the longer code: within bucket ' +
        bucketOf[best[0]] + ' the pass went by byte value. A Huffman code would not leave ' + (one ? 'that bit' : 'those ' + best[2] + ' bits') + '; the buckets save the sort.';
    }
    return { S: S, B: B, P: P };
  }
  var plan = build(PAGES[pageId]);

  var choice = ctx.el('div', 'an-choice', null, ctx.bar);
  var root = ctx.svg('svg', { role: 'img', 'aria-label': 'Each byte’s share of the literals and the share of the code space its code length takes, the gain buckets, and the code space' }, ctx.stage);
  var flat = ctx.svg('g', {}, root), cols = ctx.svg('g', {}, root);
  var btns = [['text', 'chapter 4’s text page'], ['base64', 'the base64 page']].map(function (m) {
    var b = ctx.el('button', null, m[1], choice);
    b.type = 'button'; b.setAttribute('aria-pressed', m[0] === pageId ? 'true' : 'false');
    b.addEventListener('click', function () {
      pageId = m[0];
      btns.forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); });
      while (cols.firstChild) cols.removeChild(cols.firstChild);
      plan = build(PAGES[pageId]); ctx.restart();
    });
    return b;
  });

  function layout() {
    var n = ctx.narrow, P = plan.P, nb = P.bytes.length, L = { W: n ? 360 : 744, X0: n ? 6 : 20 };
    L.per = n ? Math.min(nb, 33) : nb;
    L.rows = Math.ceil(nb / L.per);
    L.cw = Math.min(40, (L.W - 2 * L.X0) / L.per);
    L.chartH = n ? 96 : 130;
    L.blockH = L.chartH + 58;
    L.top = 26;
    // the scale: the largest share or room of the page
    var m = 0, last = plan.S[plan.S.length - 1].len;
    P.bytes.forEach(function (r) { m = Math.max(m, r[1] / P.n_lit, 1 / (1 << last[r[0]])); });
    L.scale = L.chartH / m;
    L.barHead = L.top + L.rows * L.blockH + 14;
    L.barY = L.barHead + 8;
    L.legY = L.barY + 42;
    L.H = L.legY + (n ? 24 : 8);
    root.setAttribute('viewBox', '0 0 ' + L.W + ' ' + L.H);
    return L;
  }

  function draw(k) {
    var st = plan.S[k], L = layout(), P = plan.P, B = plan.B;
    while (flat.firstChild) flat.removeChild(flat.firstChild);
    ctx.svg('text', { x: L.X0, y: 14, class: 'ol-head', text: ctx.narrow ? 'share f / n, and room 2⁻ˡ' : 'each byte: its share of the literals, f / n, and the room its code takes, 2⁻ˡ' }, flat);
    for (var r = 0; r < L.rows; r++) {
      var base = L.top + r * L.blockH + L.chartH;
      ctx.svg('line', { x1: L.X0, y1: base + 0.5, x2: L.X0 + L.per * L.cw, y2: base + 0.5, class: 'ol-axis' }, flat);
    }
    // the buckets, once sorted: a band under each run of the same bucket
    if (st.sorted) {
      var i0 = 0;
      for (var i = 1; i <= st.order.length; i++) {
        if (i < st.order.length && B[st.order[i]].gain === B[st.order[i0]].gain && i % L.per !== 0) continue;
        var rr = Math.floor(i0 / L.per), x0 = L.X0 + (i0 % L.per) * L.cw, wdt = (i - i0) * L.cw, by = L.top + rr * L.blockH + L.chartH + 32;
        ctx.svg('rect', { x: x0 + 1, y: by, width: wdt - 2, height: 14, rx: 2, class: B[st.order[i0]].gain % 2 ? 'ol-band ol-band2' : 'ol-band' }, flat);
        if (wdt >= 10) ctx.svg('text', { x: x0 + wdt / 2, y: by + 11, 'text-anchor': 'middle', 'font-size': wdt >= 14 ? 10 : 8, class: 'ol-note', text: B[st.order[i0]].gain }, flat);
        i0 = i;
      }
    }
    // a column per byte, which moves when the bytes are sorted
    st.order.forEach(function (b, idx) {
      var g = ctx.keyed(cols, pageId + '-' + b, function () { var e = ctx.svg('g', { class: 'an-move' }); return e; });
      var x = L.X0 + (idx % L.per) * L.cw, y = L.top + Math.floor(idx / L.per) * L.blockH;
      g.setAttribute('transform', 'translate(' + x + ',' + y + ')');
      while (g.firstChild) g.removeChild(g.firstChild);
      var by = B[b], share = by.f / P.n_lit * L.scale, w = L.cw;
      ctx.svg('rect', { x: 1, y: L.chartH - share, width: w - 2, height: share, class: 'ol-share' }, g);
      if (st.first) {
        var room = L.scale / (1 << st.len[b]), now = st.now && st.now.indexOf(b) >= 0;
        ctx.svg('rect', { x: w * 0.22, y: L.chartH - room, width: w * 0.56, height: room, class: now ? 'ol-room ol-now' : 'ol-room' }, g);
      }
      ctx.svg('text', { x: w / 2, y: L.chartH + 13, 'text-anchor': 'middle', 'font-size': w >= 10 ? 11 : 9, text: glyph(b) }, g);
      if (st.first) ctx.svg('text', { x: w / 2, y: L.chartH + 26, 'text-anchor': 'middle', 'font-size': w >= 10 ? 10 : 8, class: st.now && st.now.indexOf(b) >= 0 ? 'ol-mark' : 'ol-note', text: st.len[b] }, g);
      if (st.gains && !st.sorted && w >= 10) ctx.svg('text', { x: w / 2, y: L.chartH + 43, 'text-anchor': 'middle', 'font-size': w >= 14 ? 10 : 8, class: 'ol-note', text: by.gain }, g);
    });
    // the code space: the rooms in the order of the bytes as shown
    ctx.svg('text', { x: L.X0, y: L.barHead, class: 'ol-head', text: st.first ? 'space: ' + st.space + ' of ' + FULL : 'space: no lengths yet' }, flat);
    var bw = L.W - 2 * L.X0;
    ctx.svg('rect', { x: L.X0, y: L.barY, width: bw, height: 18, class: 'ol-empty' }, flat);
    if (st.first) {
      var u = 0;
      st.order.forEach(function (b) {
        var units = FULL >> st.len[b], x = L.X0 + bw * u / FULL, w2 = bw * units / FULL;
        ctx.svg('rect', { x: x, y: L.barY, width: Math.max(w2 - 0.8, 0.5), height: 18, class: st.now && st.now.indexOf(b) >= 0 ? 'ol-room ol-now' : 'ol-room' }, flat);
        if (w2 >= 12) ctx.svg('text', { x: x + w2 / 2, y: L.barY + 13, 'text-anchor': 'middle', 'font-size': 10, class: 'ol-tf', text: glyph(b) }, flat);
        u += units;
      });
    }
    // the key
    [['ol-share', 'share f / n'], ['ol-room', 'room 2⁻ˡ'], ['ol-room ol-now', 'changed']].forEach(function (kk, j) {
      var x = L.X0 + j * (ctx.narrow ? 112 : 140);
      ctx.svg('rect', { x: x, y: L.legY - 10, width: 14, height: 12, class: kk[0] }, flat);
      ctx.svg('text', { x: x + 19, y: L.legY, 'font-size': 11, class: 'ol-note ol-key', text: kk[1] }, flat);
    });
    if (st.sorted) ctx.svg('text', { x: L.X0 + 3 * (ctx.narrow ? 112 : 140), y: L.legY, 'font-size': 11, class: 'ol-note ol-key', text: ctx.narrow ? '' : 'numbers: length, bucket' }, flat);
    ctx.say(st.say);
  }
  return { steps: function () { return plan.S.length; }, draw: draw, interval: 2400, wide: 630 };
});
