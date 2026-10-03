// ============================================================================
// tools/analytics_page.js
// Cursor readouts for the usage-survey page (inlined by analytics_page.py).
//
// Reads only what analytics_svg.py wrote into each chart: a line chart's
// data-chart (every day's x, and each series' point and formatted value) and a
// bar's data-tip. It draws nothing the chart did not already compute, so the
// readout cannot disagree with the picture. Plain DOM, no library; text is set
// with textContent, never innerHTML.
// ============================================================================
(function () {
  'use strict';
  const NS = 'http://www.w3.org/2000/svg';
  const tip = document.createElement('div');
  tip.className = 'tip';
  tip.hidden = true;
  document.body.appendChild(tip);

  // Beside the pointer, flipped to the other side near a viewport edge.
  function place(ev) {
    const gap = 14;
    const r = tip.getBoundingClientRect();
    let x = ev.clientX + gap;
    let y = ev.clientY + gap;
    if (x + r.width > window.innerWidth - 8) x = ev.clientX - gap - r.width;
    if (y + r.height > window.innerHeight - 8) y = ev.clientY - gap - r.height;
    tip.style.left = Math.max(4, x) + 'px';
    tip.style.top = Math.max(4, y) + 'px';
  }

  function row(color, name, value) {
    const el = document.createElement('div');
    el.className = 'row';
    if (color) {
      const sw = document.createElement('span');
      sw.className = 'sw';
      sw.style.background = color;
      el.appendChild(sw);
    }
    const n = document.createElement('span');
    n.className = 'name';
    n.textContent = name;
    el.appendChild(n);
    if (value !== undefined) {
      const v = document.createElement('span');
      v.className = 'val';
      v.textContent = value;
      el.appendChild(v);
    }
    return el;
  }

  // Bars, segments and cells: the mark's own value.
  document.querySelectorAll('svg [data-tip]').forEach(function (el) {
    el.addEventListener('pointerenter', function (ev) {
      tip.replaceChildren(row(null, el.getAttribute('data-tip')));
      tip.hidden = false;
      el.classList.add('hot');
      place(ev);
    });
    el.addEventListener('pointermove', place);
    el.addEventListener('pointerleave', function () {
      tip.hidden = true;
      el.classList.remove('hot');
    });
  });

  // Line charts: a guide on the nearest day, a dot on each line, and every
  // series' value there, highest first. A series sitting on the axis (a version
  // not yet released, one long gone) is left out: ten "0%" rows buried the
  // three that mattered on the version chart.
  document.querySelectorAll('svg[data-chart]').forEach(function (svg) {
    let d;
    try {
      d = JSON.parse(svg.getAttribute('data-chart'));
    } catch {
      return;
    }
    if (d.type !== 'lines' || !d.x.length) return;
    const g = document.createElementNS(NS, 'g');
    g.setAttribute('class', 'cursor');
    g.style.display = 'none';
    const guide = document.createElementNS(NS, 'line');
    guide.setAttribute('y1', d.top);
    guide.setAttribute('y2', d.bottom);
    g.appendChild(guide);
    const dots = d.series.map(function (s) {
      const c = document.createElementNS(NS, 'circle');
      c.setAttribute('r', '4');
      c.setAttribute('fill', s.color);
      g.appendChild(c);
      return c;
    });
    svg.appendChild(g);

    function hide() {
      g.style.display = 'none';
      tip.hidden = true;
    }

    svg.addEventListener('pointermove', function (ev) {
      const m = svg.getScreenCTM();
      if (!m) return;
      const p = new DOMPoint(ev.clientX, ev.clientY).matrixTransform(m.inverse());
      const first = d.x[0];
      const last = d.x[d.x.length - 1];
      if (p.y < d.top - 8 || p.y > d.bottom + 8 || p.x < first - 12 || p.x > last + 12) {
        hide();
        return;
      }
      let i = 0;
      for (let k = 1; k < d.x.length; k++) {
        if (Math.abs(d.x[k] - p.x) < Math.abs(d.x[i] - p.x)) i = k;
      }
      const x = d.x[i];
      guide.setAttribute('x1', x);
      guide.setAttribute('x2', x);
      const lines = [];
      d.series.forEach(function (s, k) {
        const y = s.y[i];
        if (y === null || y >= d.bottom) {
          dots[k].style.display = 'none';
          return;
        }
        dots[k].style.display = '';
        dots[k].setAttribute('cx', x);
        dots[k].setAttribute('cy', y);
        lines.push({ y: y, el: row(s.color, s.name, s.values[i]) });
      });
      if (!lines.length) {
        hide();
        return;
      }
      lines.sort(function (a, b) { return a.y - b.y; });
      const head = document.createElement('div');
      head.className = 'head';
      head.textContent = d.labels[i];
      tip.replaceChildren(head);
      lines.forEach(function (l) { tip.appendChild(l.el); });
      g.style.display = '';
      tip.hidden = false;
      place(ev);
    });
    svg.addEventListener('pointerleave', hide);
  });
})();
