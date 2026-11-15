// ==UserScript==
// @name         UC Thread Archiver (parallel)
// @namespace    venice/uc-archiver
// @version      2.1
// @description  Save every page of a vBulletin thread on unknowncheats.me into a single local file, with a worker pool
// @match        https://www.unknowncheats.me/forum/*
// @run-at       document-idle
// @grant        none
// ==/UserScript==

(function () {
  'use strict';

  const tid = (location.search.match(/[?&]t=(\d+)/) || [])[1]
           || (location.pathname.match(/\/(\d+)-[^/]*\.html$/) || [])[1];
  if (!tid || !document.querySelector('#posts')) return;

  const canonical = (document.querySelector('link[rel="canonical"]') || {}).href
                 || location.href.split('?')[0];
  const baseUrl = canonical.replace(/-?\d*\.html$/, '');
  const threadTitle = (document.title || 'thread')
    .replace(/\s*-\s*UnKnoWnCheaTs.*$/i, '')
    .replace(/\s+/g, ' ').trim();

  const buildUrl = p => p <= 1 ? baseUrl + '.html' : `${baseUrl}-${p}.html`;

  const lastPage = (() => {
    const a = document.querySelector('.pagenav a[title^="Last"]');
    if (a) { const m = a.href.match(/-(\d+)\.html/); if (m) return +m[1]; }
    const t = (document.querySelector('.pagenav') || {}).textContent || '';
    const m = t.match(/Page\s+\d+\s+of\s+([\d,]+)/i);
    return m ? +m[1].replace(/,/g, '') : 1;
  })();

  const FORUM = 'https://www.unknowncheats.me/forum/';
  const OLD_KEY = `ucarch_done_${tid}`;

  const S = {
    running: false, stop: false, cooldownUntil: 0,
    from: 1, to: lastPage, delay: 1200, workers: 3,
    format: 'md', done: 0, total: 0, cursor: 0,
    queue: [], results: new Map(), ok: new Set(), bad: new Set()
  };

  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const clean = s => (s || '').replace(/\s+/g, ' ').trim();
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const abs = u => { try { return new URL(u, FORUM).href; } catch { return u; } };

  /* ---------- storage ---------- */

  let dbp = null;
  function db() {
    if (!dbp) dbp = new Promise((res, rej) => {
      const r = indexedDB.open('ucarch', 1);
      r.onupgradeneeded = () => {
        const s = r.result.createObjectStore('pages', { keyPath: 'k' });
        s.createIndex('tid', 'tid');
      };
      r.onsuccess = () => res(r.result);
      r.onerror = () => rej(r.error);
    });
    return dbp;
  }

  async function dbAll() {
    const d = await db();
    return new Promise((res, rej) => {
      const out = [];
      const req = d.transaction('pages', 'readonly').objectStore('pages').index('tid').openCursor(IDBKeyRange.only(tid));
      req.onsuccess = () => {
        const c = req.result;
        if (c) { out.push(c.value); c.continue(); } else res(out);
      };
      req.onerror = () => rej(req.error);
    });
  }

  async function dbPut(page, text) {
    const d = await db();
    return new Promise((res, rej) => {
      const tx = d.transaction('pages', 'readwrite');
      tx.objectStore('pages').put({ k: `${tid}|${page}`, tid, page, text });
      tx.oncomplete = res;
      tx.onerror = () => rej(tx.error);
    });
  }

  async function dbClear() {
    const d = await db();
    return new Promise((res, rej) => {
      const tx = d.transaction('pages', 'readwrite');
      const store = tx.objectStore('pages');
      const req = store.index('tid').openCursor(IDBKeyRange.only(tid));
      req.onsuccess = () => {
        const c = req.result;
        if (c) { store.delete(c.primaryKey); c.continue(); }
      };
      tx.oncomplete = res;
      tx.onerror = () => rej(tx.error);
    });
  }

  async function countSaved() {
    try { return (await dbAll()).length; } catch { return 0; }
  }

  /* ---------- parsing ---------- */

  function parsePosts(doc) {
    const out = [];
    for (const tbl of doc.querySelectorAll('table[id^="post"]')) {
      const id = tbl.id.slice(4);
      const msg = tbl.querySelector('#post_message_' + id);
      if (!msg) continue;
      out.push({
        id,
        author: clean((tbl.querySelector('a.bigusername') || {}).textContent) || '?',
        date: clean((tbl.querySelector('td.thead') || {}).textContent),
        msg
      });
    }
    return out;
  }

  function toMarkdown(node, out) {
    for (const n of node.childNodes) {
      if (n.nodeType === 3) { out.push(n.nodeValue.replace(/\s+/g, ' ')); continue; }
      if (n.nodeType !== 1) continue;

      const t = n.tagName;
      if (t === 'SCRIPT' || t === 'STYLE' || t === 'BUTTON' || t === 'INPUT') continue;
      if (t === 'BR') { out.push('\n'); continue; }
      if (t === 'PRE') { out.push('\n```\n' + n.textContent.replace(/\n+$/, '') + '\n```\n'); continue; }
      if (t === 'IMG') {
        const s = n.getAttribute('src');
        if (s) out.push(`\n![${clean(n.alt) || 'image'}](${abs(s)})\n`);
        continue;
      }
      if (t === 'A') {
        const h = n.getAttribute('href'), txt = clean(n.textContent);
        if (h && txt) out.push(`[${txt}](${abs(h)})`);
        else if (txt) out.push(txt);
        continue;
      }
      if (t === 'LI') { out.push('\n- '); toMarkdown(n, out); continue; }

      if (n.querySelector && n.querySelector('td.alt2') && /Originally Posted by/i.test(n.textContent)) {
        const who = clean((n.querySelector('strong') || {}).textContent) || 'quote';
        const sub = [];
        toMarkdown(n.querySelector('td.alt2'), sub);
        out.push(`\n> **${who}:**\n` +
          sub.join('').trim().split('\n').map(l => '> ' + l).join('\n') + '\n\n');
        continue;
      }

      toMarkdown(n, out);
    }
  }

  function absolutize(root) {
    root.querySelectorAll('a[href]').forEach(a => a.href = abs(a.getAttribute('href')));
    root.querySelectorAll('img[src]').forEach(i => i.src = abs(i.getAttribute('src')));
    root.querySelectorAll('script,style,iframe,ins,noscript').forEach(e => e.remove());
  }

  function renderMarkdown(posts) {
    return posts.map(p => {
      const sub = [];
      toMarkdown(p.msg, sub);
      const body = sub.join('').replace(/[ \t]+\n/g, '\n').replace(/\n{3,}/g, '\n\n').trim();
      return `\n---\n\n### Post #${p.id} — ${p.author} — ${p.date}\n\n${body}\n`;
    }).join('');
  }

  function renderHtml(posts) {
    return posts.map(p => {
      const c = p.msg.cloneNode(true);
      absolutize(c);
      return `<div class="p"><div class="h">#${p.id} — ${esc(p.author)} — ${esc(p.date)}</div>${c.innerHTML}</div>`;
    }).join('');
  }

  /* ---------- fetching ---------- */

  async function gate() {
    while (Date.now() < S.cooldownUntil && !S.stop) await sleep(250);
  }

  async function fetchPage(page) {
    const url = buildUrl(page);
    let lastErr;
    for (let attempt = 1; attempt <= 4; attempt++) {
      if (S.stop) throw new Error('stopped');
      try {
        const r = await fetch(url, {
          credentials: 'same-origin',
          headers: { 'Accept': 'text/html,application/xhtml+xml' }
        });
        if (r.status === 429 || r.status === 503) {
          S.cooldownUntil = Date.now() + 8000;
          throw new Error('HTTP ' + r.status);
        }
        if (!r.ok) throw new Error('HTTP ' + r.status);
        const txt = await r.text();
        if (/Just a moment|Checking your browser|cf-browser-verification/i.test(txt)) {
          S.cooldownUntil = Date.now() + 10000;
          throw new Error('cloudflare');
        }
        const doc = new DOMParser().parseFromString(txt, 'text/html');
        const posts = parsePosts(doc);
        if (!posts.length) throw new Error('no posts found');
        return posts;
      } catch (e) {
        if (e.message === 'stopped') throw e;
        lastErr = e;
        log(`p${page} retry ${attempt} · ${e.message}`);
        await sleep(2000 * attempt + Math.random() * 1200);
      }
    }
    throw new Error(lastErr.message);
  }

  async function worker() {
    while (!S.stop) {
      const i = S.cursor++;
      if (i >= S.queue.length) return;
      const page = S.queue[i];

      await gate();
      if (S.stop) return;

      try {
        const posts = await fetchPage(page);
        const text = S.format === 'html' ? renderHtml(posts) : renderMarkdown(posts);
        S.results.set(page, text);
        S.ok.add(page);
        await dbPut(page, text);
        log(`p${page} ok · ${posts.length} post(s)`);
      } catch (e) {
        if (e.message === 'stopped') return;
        S.bad.add(page);
        log(`p${page} FAILED · ${e.message}`);
      }

      S.done++;
      setProgress();
      await sleep(S.delay + Math.random() * S.delay * 0.8);
    }
  }

  /* ---------- output ---------- */

  function save() {
    const pages = [...S.ok].sort((a, b) => a - b);
    if (!pages.length) { log('nothing to save'); return; }

    const lo = pages[0], hi = pages[pages.length - 1];
    const gaps = [];
    for (let p = lo; p <= hi; p++) if (!S.ok.has(p)) gaps.push(p);

    const ext = S.format === 'html' ? 'html' : 'md';
    const name = threadTitle.replace(/[^\w\-. ]+/g, '_').slice(0, 80) || `thread_${tid}`;
    const gapNote = gaps.length ? `\nMissing pages (${gaps.length}): ${gaps.join(', ')}` : '';
    const body = pages.map(p => S.results.get(p) || '').join('');

    const text = S.format === 'html'
      ? `<!doctype html><meta charset="utf-8"><title>${esc(threadTitle)}</title>
<style>
body{font:15px/1.65 system-ui,Segoe UI,sans-serif;max-width:920px;margin:2rem auto;padding:0 1rem;background:#fff;color:#111}
pre{background:#f3f4f6;padding:.8rem;overflow:auto;font-size:13px}
.p{border-top:1px solid #d1d5db;padding-top:1rem;margin-top:1.6rem}
.h{color:#6b7280;font-size:12px;margin-bottom:.5rem}
img{max-width:100%}blockquote,td.alt2{border-left:3px solid #d1d5db;margin:.5rem 0;padding-left:.7rem;color:#374151}
</style>
<h1>${esc(threadTitle)}</h1>
<p class="h">${esc(canonical)} · pages ${lo}–${hi}${esc(gapNote)}</p>
` + body
      : `# ${threadTitle}\n\nSource: ${canonical}\nPages saved: ${lo}–${hi}${gapNote}\n\n` + body;

    const blob = new Blob([text], { type: S.format === 'html' ? 'text/html;charset=utf-8' : 'text/markdown;charset=utf-8' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `${name}.${ext}`;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 60000);
    log(`saved ${pages.length} page(s)${gaps.length ? `, ${gaps.length} gap(s)` : ''}`);
  }

  /* ---------- ui ---------- */

  const panel = document.createElement('div');
  panel.style.cssText = 'position:fixed;right:14px;bottom:14px;z-index:99999;width:300px;background:#11161d;color:#d7dde5;font:12px/1.5 system-ui,sans-serif;border:1px solid #2b3542;border-radius:8px;padding:10px;box-shadow:0 8px 24px rgba(0,0,0,.5)';
  panel.innerHTML = `
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px">
      <b style="color:#fff">Archiver · parallel</b>
      <span id="uca-x" style="cursor:pointer;color:#8b98a8">✕</span>
    </div>
    <div style="color:#8b98a8;margin-bottom:8px">t=${tid} · ${lastPage} page(s) · <span id="uca-saved">…</span></div>
    <div style="display:flex;gap:6px;margin-bottom:6px">
      <label style="flex:1">from<input id="uca-from" type="number" min="1" max="${lastPage}" value="1" style="width:100%;background:#0a0e13;color:#d7dde5;border:1px solid #2b3542;border-radius:4px;padding:3px"></label>
      <label style="flex:1">to<input id="uca-to" type="number" min="1" max="${lastPage}" value="${lastPage}" style="width:100%;background:#0a0e13;color:#d7dde5;border:1px solid #2b3542;border-radius:4px;padding:3px"></label>
    </div>
    <div style="display:flex;gap:6px;margin-bottom:6px">
      <label style="flex:1">workers<input id="uca-w" type="number" min="1" max="8" value="3" style="width:100%;background:#0a0e13;color:#d7dde5;border:1px solid #2b3542;border-radius:4px;padding:3px"></label>
      <label style="flex:1">gap ms<input id="uca-delay" type="number" min="0" step="100" value="1200" style="width:100%;background:#0a0e13;color:#d7dde5;border:1px solid #2b3542;border-radius:4px;padding:3px"></label>
      <label style="flex:1">fmt<select id="uca-fmt" style="width:100%;background:#0a0e13;color:#d7dde5;border:1px solid #2b3542;border-radius:4px;padding:3px"><option value="md">md</option><option value="html">html</option></select></label>
    </div>
    <label style="display:flex;align-items:center;gap:5px;margin-bottom:6px;color:#c3ccd6">
      <input id="uca-ignore" type="checkbox"> ignore saved progress
    </label>
    <div style="display:flex;gap:6px;margin-bottom:6px">
      <button id="uca-go" style="flex:1;background:#2563eb;color:#fff;border:0;border-radius:4px;padding:5px;cursor:pointer">Start</button>
      <button id="uca-stop" style="flex:1;background:#374151;color:#fff;border:0;border-radius:4px;padding:5px;cursor:pointer">Stop</button>
      <button id="uca-save" style="flex:1;background:#374151;color:#fff;border:0;border-radius:4px;padding:5px;cursor:pointer">Save</button>
      <button id="uca-clear" style="background:#7f1d1d;color:#fff;border:0;border-radius:4px;padding:5px;cursor:pointer" title="Delete stored pages">Clear</button>
    </div>
    <div id="uca-bar" style="height:4px;background:#1f2733;border-radius:2px;overflow:hidden;margin-bottom:6px"><div id="uca-fill" style="height:100%;width:0;background:#22c55e"></div></div>
    <div id="uca-log" style="height:96px;overflow:auto;background:#0a0e13;border:1px solid #2b3542;border-radius:4px;padding:5px;font-size:11px;color:#8b98a8"></div>
  `;
  document.body.appendChild(panel);

  const $ = id => panel.querySelector('#' + id);
  const logEl = $('uca-log');
  function log(m) {
    const line = document.createElement('div');
    line.textContent = `${new Date().toLocaleTimeString()} ${m}`;
    logEl.appendChild(line);
    logEl.scrollTop = logEl.scrollHeight;
  }
  const setProgress = () => { $('uca-fill').style.width = S.total ? `${(S.done / S.total) * 100}%` : '0%'; };

  const refreshSaved = async () => {
    const n = await countSaved();
    $('uca-saved').textContent = n ? `${n} stored` : 'none stored';
  };

  $('uca-x').onclick = () => panel.remove();
  $('uca-stop').onclick = () => { S.stop = true; log('stop requested'); };
  $('uca-save').onclick = save;

  $('uca-clear').onclick = async () => {
    if (S.running) { log('stop the run first'); return; }
    const n = await countSaved();
    if (!n) { log('nothing stored'); return; }
    if (!confirm(`Delete ${n} stored page(s) for this thread?`)) return;
    await dbClear();
    localStorage.removeItem(OLD_KEY);
    await refreshSaved();
    log(`cleared ${n} page(s)`);
  };

  $('uca-go').onclick = async () => {
    if (S.running) return;

    const from = Math.max(1, Math.min(lastPage, +$('uca-from').value || 1));
    const to = Math.max(from, Math.min(lastPage, +$('uca-to').value || lastPage));
    S.delay = Math.max(0, +$('uca-delay').value || 0);
    S.workers = Math.max(1, Math.min(8, +$('uca-w').value || 3));
    S.format = $('uca-fmt').value;
    const ignore = $('uca-ignore').checked;

    S.from = from; S.to = to;
    S.results = new Map(); S.ok = new Set(); S.bad = new Set();
    S.cursor = 0; S.done = 0; S.stop = false; S.cooldownUntil = 0;

    if (!ignore) {
      const stored = (await dbAll()).filter(r => r.page >= from && r.page <= to);
      for (const r of stored) { S.results.set(r.page, r.text); S.ok.add(r.page); }
      if (stored.length) log(`reusing ${stored.length} stored page(s)`);
    }

    S.queue = [];
    for (let p = from; p <= to; p++) if (!S.ok.has(p)) S.queue.push(p);
    S.total = S.queue.length;

    if (!S.queue.length) {
      log('range already complete — writing file');
      save();
      return;
    }
    if (S.ok.size && !confirm(`${S.ok.size} page(s) already stored. Continue with the remaining ${S.queue.length}?`)) return;

    S.running = true;
    setProgress();
    log(`start · ${S.queue.length} to fetch · ${S.workers} worker(s)`);
    await Promise.all(Array.from({ length: S.workers }, worker));
    S.running = false;
    log(S.stop ? 'stopped' : 'finished');
    await refreshSaved();
    save();
  };

  (async () => {
    localStorage.removeItem(OLD_KEY);
    await refreshSaved();
    log('ready');
  })();
})();
