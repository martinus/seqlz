/*
 * The score of chapter 17, bytes + lambda x time, for the six codecs of the kernel VM's run of 22nd
 * October 2026 (README.md, explored-designs.md "One rule for which literals are deltas"): zsmalloc
 * memory per page in % of 4 KiB, as rounded there, and the time per page written, swap-out + 0.73 x
 * swap-in, in µs. These are measured numbers, typed. One step per exchange rate lambda, on a log scale
 * from 1 to 2000 bytes per µs: the chart with the line of equal score through the codec with the lowest
 * score, and each codec's score as a bar, bytes and lambda x time.
 *
 * From the rounded means the exchange rates between neighbours on the hull come out at 705, 127 and 22
 * bytes per µs; the chapter quotes 689, 131 and 22, from the unrounded means. The sentences quote the
 * chapter's, and no step lies between the two, so both give the same codec at every step.
 */
BookAnim.define('score', function (ctx) {
  var C = [
    { n: 'lz4', pct: 36.5, t: 9.25 },
    { n: 'lzo-rle', pct: 34.9, t: 9.38 },
    { n: 'seqlz-1', pct: 29.1, t: 9.68 },
    { n: 'seqlz-2', pct: 26.5, t: 10.52 },
    { n: 'zstd 3', pct: 25.3, t: 20.02 },
    { n: 'seqlz-3', pct: 24.4, t: 14.42 }
  ];
  C.forEach(function (c, i) { c.i = i; c.b = c.pct * 4096 / 100; });
  var HULL = [0, 2, 3, 5];
  var LAM = [1, 2, 3, 5, 7, 10, 15, 20, 25, 30, 40, 50, 70, 100, 120, 150, 200, 300, 400, 500, 600, 800, 1000, 1500, 2000];
  var TMAX = 21, BMAX = 1600;

  function score(c, lam) { return c.b + lam * c.t; }
  function ranked(lam) { return C.slice().sort(function (a, b) { return score(a, lam) - score(b, lam); }); }
  function r0(v) { return Math.round(v).toString(); }
  function fmt(v) { return r0(v).replace(/\B(?=(\d{3})+(?!\d))/g, ' '); }

  // why the winner wins at this rate, with the chapter's exchange rates
  var WHY = {
    'seqlz-3': 'seqlz-2 is 3.90 µs faster per page written but stores 86 bytes more, 22 bytes per µs, more than a µs is worth at this rate.',
    'seqlz-2': 'seqlz-3 saves only 22 bytes per µs it adds, too few at this rate, and seqlz-1 costs 131 bytes per µs it saves, too many.',
    'seqlz-1': 'seqlz-2 saves only 131 bytes per µs it adds, too few at this rate, and lz4 costs 689 bytes per µs it saves, too many.',
    'lz4': 'seqlz-1 saves only 689 bytes per µs it adds, fewer than a µs is worth at this rate, and lz4 is the fastest of the six.'
  };
  function say(k) {
    var lam = LAM[k], r = ranked(lam), w = r[0], nx = r[1];
    var s = 'At ' + lam + ' byte' + (lam === 1 ? '' : 's') + ' per µs, ' + w.n + ' has the lowest score, ' + fmt(w.b) + ' + ' + lam + ' × ' + w.t.toFixed(2) + ' = ' + fmt(score(w, lam)) + ' bytes. ' + WHY[w.n];
    if (k === 0) s += ' zstd 3 stores more than seqlz-3 and takes more time, so it is behind it at every rate.';
    else if (nx.n === 'lzo-rle') s += ' lzo-rle is next, ' + fmt(score(nx, lam) - score(w, lam)) + ' bytes behind: ' + (w.n === 'lz4'
      ? 'it stores 65 bytes less than lz4, but its slower swap-ins make it 0.13 µs slower per page written, and here those µs are worth more.'
      : 'it is 0.30 µs faster than seqlz-1 per page written, but stores 238 bytes more.');
    else s += ' Next is ' + nx.n + ', ' + fmt(score(nx, lam) - score(w, lam)) + ' bytes behind.';
    return s;
  }

  var built = null, root, L, el;
  function layout(narrow) {
    if (narrow) return { W: 360, H: 500, PX0: 46, PX1: 350, PY0: 66, PY1: 266, AXY: 296, BY0: 314, ROW: 22, NX: 62, BX0: 68, BX1: 300, LEG: 462 };
    return { W: 740, H: 320, PX0: 56, PX1: 446, PY0: 44, PY1: 270, AXY: 304, BY0: 50, ROW: 30, NX: 546, BX0: 552, BX1: 690, LEG: 256 };
  }
  function X(t) { return L.PX0 + (L.PX1 - L.PX0) * t / TMAX; }
  function Y(b) { return L.PY1 - (L.PY1 - L.PY0) * b / BMAX; }

  function build() {
    if (root) root.parentNode.removeChild(root);
    built = ctx.narrow; L = layout(built); el = {};
    root = ctx.svg('svg', { viewBox: '0 0 ' + L.W + ' ' + L.H, role: 'img', 'aria-label': 'Six codecs at their time per page written and zsmalloc bytes per page, the line of equal score through the one with the lowest score at the chosen exchange rate, and each codec\'s score as a bar' }, ctx.stage);
    el.head = ctx.svg('text', { x: 2, y: 16, 'font-size': 13, class: 'sc-head' }, root);
    el.head2 = ctx.svg('text', { x: built ? 2 : L.W - 2, y: built ? 36 : 16, 'font-size': 13, 'text-anchor': built ? 'start' : 'end', class: 'sc-win' }, root);
    // the chart: axes from 0
    var ax = ctx.svg('g', { 'font-size': 11 }, root), t, b;
    for (t = 0; t <= TMAX; t += 3) {
      ctx.svg('line', { x1: X(t), y1: L.PY1, x2: X(t), y2: L.PY1 + 4, class: 'sc-axis' }, ax);
      ctx.svg('text', { x: X(t), y: L.PY1 + 16, 'text-anchor': 'middle', text: t }, ax);
    }
    for (b = 0; b <= BMAX; b += 400) {
      ctx.svg('line', { x1: L.PX0, y1: Y(b), x2: L.PX1, y2: Y(b), class: 'sc-grid' }, ax);
      ctx.svg('text', { x: L.PX0 - 6, y: Y(b) + 4, 'text-anchor': 'end', text: b }, ax);
    }
    ctx.svg('line', { x1: L.PX0, y1: L.PY1, x2: L.PX1, y2: L.PY1, class: 'sc-axis' }, ax);
    ctx.svg('line', { x1: L.PX0, y1: L.PY1, x2: L.PX0, y2: L.PY0, class: 'sc-axis' }, ax);
    ctx.svg('text', { x: (L.PX0 + L.PX1) / 2, y: L.AXY, 'text-anchor': 'middle', text: 'time per page written, µs' }, ax);
    ctx.svg('text', { x: L.PX0 - 6, y: L.PY0 - 10, text: 'zsmalloc bytes per page', 'text-anchor': 'start', class: 'sc-muted' }, ax);
    // the hull, the line of equal score, the points
    ctx.svg('polyline', { points: HULL.map(function (i) { return X(C[i].t) + ',' + Y(C[i].b); }).join(' '), class: 'sc-hull' }, root);
    el.iso = ctx.svg('line', { class: 'sc-iso' }, root);
    // label offsets per codec, wide and narrow: the four fast ones lie within half a µs
    var OFF = built
      ? [[8, -4, 'start'], [-8, 4, 'end'], [-8, 4, 'end'], [-8, 14, 'end'], [0, -10, 'middle'], [0, 18, 'middle']]
      : [[8, -4, 'start'], [-8, 6, 'end'], [9, 2, 'start'], [8, 14, 'start'], [0, -10, 'middle'], [0, 20, 'middle']];
    el.pt = []; el.lab = [];
    C.forEach(function (c, i) {
      el.pt.push(ctx.svg('circle', { cx: X(c.t), cy: Y(c.b), r: 4, class: 'sc-pt' }, root));
      el.lab.push(ctx.svg('text', { x: X(c.t) + OFF[i][0], y: Y(c.b) + OFF[i][1], 'text-anchor': OFF[i][2], 'font-size': 12, class: 'sc-halo', text: c.n }, root));
    });
    // the bars, one row per codec, sorted by score
    el.rows = [];
    C.forEach(function (c) {
      var g = ctx.svg('g', { class: 'an-move' }, root);
      var o = { g: g };
      o.name = ctx.svg('text', { x: L.NX, y: 15, 'text-anchor': 'end', 'font-size': 12, text: c.n }, g);
      o.rb = ctx.svg('rect', { x: L.BX0, y: 3, height: 16, class: 'sc-bytes' }, g);
      o.rt = ctx.svg('rect', { y: 3, height: 16, class: 'sc-time' }, g);
      o.val = ctx.svg('text', { y: 15, 'font-size': 11 }, g);
      el.rows.push(o);
    });
    var leg = ctx.svg('g', { 'font-size': 11 }, root), lx = built ? 10 : L.NX - 50, ly = L.LEG + 20;
    ctx.svg('rect', { x: lx, y: L.LEG - 10, width: 12, height: 12, class: 'sc-bytes' }, leg);
    ctx.svg('text', { x: lx + 16, y: L.LEG, text: 'bytes' }, leg);
    ctx.svg('rect', { x: lx + 90, y: L.LEG - 10, width: 12, height: 12, class: 'sc-time' }, leg);
    ctx.svg('text', { x: lx + 106, y: L.LEG, text: 'lambda × µs' }, leg);
    ctx.svg('line', { x1: lx, y1: ly - 4, x2: lx + 12, y2: ly - 4, class: 'sc-hull' }, leg);
    ctx.svg('text', { x: lx + 16, y: ly, text: 'hull' }, leg);
    ctx.svg('line', { x1: lx + 90, y1: ly - 4, x2: lx + 102, y2: ly - 4, class: 'sc-iso' }, leg);
    ctx.svg('text', { x: lx + 106, y: ly, text: 'equal score' }, leg);
  }

  function draw(k) {
    if (built !== ctx.narrow) build();
    var lam = LAM[k], r = ranked(lam), w = r[0], S = score(w, lam);
    el.head.textContent = 'lambda = ' + lam + ' byte' + (lam === 1 ? '' : 's') + ' per µs';
    el.head2.textContent = 'lowest score: ' + w.n + ', ' + fmt(S) + ' bytes';
    // the line bytes = S - lambda * t inside the chart
    var t0 = Math.max(0, (S - BMAX) / lam), t1 = Math.min(TMAX, S / lam);
    el.iso.setAttribute('x1', X(t0)); el.iso.setAttribute('y1', Y(S - lam * t0));
    el.iso.setAttribute('x2', X(t1)); el.iso.setAttribute('y2', Y(S - lam * t1));
    C.forEach(function (c, i) {
      el.pt[i].setAttribute('class', c === w ? 'sc-pt sc-best' : 'sc-pt');
      el.pt[i].setAttribute('r', c === w ? 6 : 4);
      el.lab[i].setAttribute('class', 'sc-halo' + (c === w ? ' sc-win' : ''));
    });
    var max = score(r[r.length - 1], lam), span = L.BX1 - L.BX0;
    r.forEach(function (c, pos) {
      var o = el.rows[c.i], wb = span * c.b / max, wt = span * lam * c.t / max;
      o.g.style.transform = 'translate(0px,' + (L.BY0 + pos * L.ROW) + 'px)';
      o.rb.setAttribute('width', wb);
      o.rt.setAttribute('x', L.BX0 + wb); o.rt.setAttribute('width', wt);
      o.val.setAttribute('x', L.BX0 + wb + wt + 4);
      o.val.textContent = fmt(score(c, lam));
      o.name.setAttribute('class', pos === 0 ? 'sc-win' : '');
      o.val.setAttribute('class', pos === 0 ? 'sc-win' : '');
    });
    ctx.say(say(k));
  }
  build();
  return { steps: LAM.length, draw: draw, interval: 1100, wide: 640 };
});
