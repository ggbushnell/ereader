// The browser-native companion. Everything shown comes from /view.json
// (pokeview --view-json, the same decoders the e-paper draws from) and the
// images under /assets/ (pokeview --dump-assets, Super Game Boy colours).
// This file only lays things out.
(function () {
  'use strict';
  const $ = (sel) => document.querySelector(sel);
  const el = (tag, cls, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; return e; };
  const pad3 = (n) => String(n).padStart(3, '0');
  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const money = (n) => '₽' + Number(n).toLocaleString();
  const hhmm = (min) => `${Math.floor(min / 60)}:${String(min % 60).padStart(2, '0')}`;

  let serverId = null, lastRenders = -1, lastToast = null;

  function hpBar(m) {
    return `<div class="hp"><div class="bar c${m.hpColor}"><i style="width:${(100 * m.hpFill / 48).toFixed(1)}%"></i></div>
      <div class="nums"><span>${m.hp} / ${m.maxHp}</span><span>${m.status ? esc(m.status) : (m.expToNext > 0 ? m.expToNext + ' exp to next' : '')}</span></div></div>`;
  }

  function renderHeader(d) {
    const p = d.player, l = d.location, a = d.achievements;
    $('#facts').innerHTML = [
      `<div><span class="l">Trainer</span><b>${esc(p.name)}</b></div>`,
      `<div><span class="l">Time</span><b>${esc(p.playTime)}</b></div>`,
      `<div><span class="l">Money</span><b>${money(p.money)}</b></div>`,
      `<div><span class="l">Badges</span><b>${p.badges}/8</b></div>`,
      `<div><span class="l">Pokédex</span><b>${p.dexOwned}</b> own · <b>${p.dexSeen}</b> seen</div>`,
      `<div><span class="l">Awards</span><b>${a.earned}/${a.total}</b></div>`,
      `<div><span class="l">At</span><b>${esc(l.label)}</b> <span class="l">${l.x},${l.y}</span></div>`,
    ].join('');
  }

  function renderParty(d) {
    const b = $('#card-party .body');
    if (!d.party.length) { b.innerHTML = '<p class="empty">No Pokémon yet.</p>'; return; }
    b.innerHTML = d.party.map((m) => `
      <div class="row">
        <img class="sprite" src="/assets/sprites/front/${pad3(m.dex)}.png" alt="">
        <div class="name">${esc(m.nick)}<div class="hint" style="margin:0">${esc(m.species)} · ${esc(m.types[0])}${m.types[1] !== m.types[0] ? '/' + esc(m.types[1]) : ''}</div></div>
        <div class="lv">L${m.level}</div>
        ${hpBar(m)}
      </div>`).join('');
  }

  function renderLocation(d) {
    const l = d.location;
    const pins = l.towns.filter((t) => t.tx >= 0).map((t) =>
      `<span class="pin ${t.visited ? 'visited' : ''}" style="left:${(100 * (t.tx + 0.5) / 20).toFixed(2)}%;top:${(100 * (t.ty + 0.5) / 18).toFixed(2)}%" title="${esc(t.name)}"></span>`).join('');
    const here = l.townX >= 0 ? `<span class="pin here" style="left:${(100 * (l.townX + 0.5) / 20).toFixed(2)}%;top:${(100 * (l.townY + 0.5) / 18).toFixed(2)}%"></span>` : '';
    $('#card-location .body').innerHTML = `
      <div class="townmap"><img src="/assets/townmap.png" alt="Kanto">${pins}${here}</div>
      <div class="kv" style="margin-top:12px">
        <span>Map</span><div><b>${esc(l.label)}</b> <span class="hint" style="display:inline">#${l.map}</span></div>
        <span>Position</span><div>${l.x}, ${l.y}${l.mapW ? ` <span class="hint" style="display:inline">of ${l.mapW}×${l.mapH} blocks</span>` : ''}</div>
        <span>Repel</span><div>${l.repelSteps ? l.repelSteps + ' steps' : 'none'}</div>
        <span>Visited</span><div>${l.towns.filter((t) => t.visited).map((t) => esc(t.name)).join(', ') || '—'}</div>
      </div>`;
  }

  function encRows(list) {
    return list.map((e) => `
      <div class="row">
        <img class="sprite sm" src="/assets/sprites/front/${pad3(e.dex)}.png" alt="">
        <span class="mark ${e.owned ? 'owned' : (e.seen ? '' : 'none')}" title="${e.owned ? 'owned' : (e.seen ? 'seen' : 'never met')}"></span>
        <div class="name">${esc(e.species)}</div>
        <div class="lv">L${e.levelLo}${e.levelHi !== e.levelLo ? '–' + e.levelHi : ''}</div>
        <div class="hp"><div class="bar"><i style="width:${e.odds}%;background:var(--accent)"></i></div></div>
        <div class="odds">${e.odds}%</div>
      </div>`).join('');
  }

  function renderEncounters(d) {
    const e = d.encounters, b = $('#card-encounters .body');
    if (!e.grass.length && !e.water.length) { b.innerHTML = '<p class="empty">Nothing wild on this map.</p>'; return; }
    let h = '';
    if (e.grass.length) h += `<div class="hint">Grass · ${e.grassRatePct}% per step</div>` + encRows(e.grass);
    if (e.water.length) h += `<div class="hint" style="margin-top:12px">Water · ${e.waterRatePct}% per step</div>` + encRows(e.water);
    h += '<div class="hint" style="margin-top:10px">● owned &nbsp; ○ seen &nbsp; <span style="opacity:.5">○</span> never met</div>';
    b.innerHTML = h;
  }

  function renderPickups(d) {
    const b = $('#card-pickups .body');
    if (!d.pickups.length) { b.innerHTML = '<p class="empty">No items to find on this map.</p>'; return; }
    const taken = d.pickups.filter((p) => p.taken).length;
    b.innerHTML = `<div class="hint">${taken} of ${d.pickups.length} picked up</div>` + d.pickups.map((p) => `
      <div class="row ${p.taken ? 'taken' : ''}">
        <span class="chip ${p.hidden ? 'warn' : 'muted'}">${p.hidden ? 'hidden' : 'ball'}</span>
        <div class="name">${esc(p.item)}</div>
        <div class="odds">${p.x}, ${p.y}</div>
      </div>`).join('');
  }

  function movesTable(moves) {
    if (!moves || !moves.length) return '<p class="empty">No moves known.</p>';
    return `<table class="moves">${moves.map((m) => `
      <tr><td class="mn">${esc(m.name)}</td><td>${esc(m.type)}</td><td>${m.power ? 'POW ' + m.power : '—'}</td><td>ACC ${m.accuracy}</td><td>${m.pp}/${m.maxPp}</td>
      <td>${m.stab ? '<span class="chip good">STAB</span>' : ''}${m.effPercent !== 100 && m.badge ? `<span class="chip ${m.effPercent > 100 ? 'good' : 'bad'}">${esc(m.badge.trim())}</span>` : ''}</td></tr>`).join('')}</table>`;
  }

  function renderBattle(d) {
    const c = $('#card-battle'), bt = d.battle;
    if (!bt.inBattle || !bt.enemy) { c.hidden = true; c.innerHTML = ''; return; }
    const e = bt.enemy, m = bt.mine;
    const dvCell = (v) => `<span class="dv ${v >= 13 ? 'hi' : ''}">DV ${v}</span>`;
    const chips = (arr, cls) => arr.length ? arr.map((t) => `<span class="chip ${cls}">${esc(t)}</span>`).join('') : '<span class="chip muted">none</span>';
    c.hidden = false;
    c.innerHTML = `<h2>${bt.trainer ? 'Trainer battle' + (bt.trainerClass ? ' · ' + esc(bt.trainerClass) + ` (${bt.enemyLeft}/${bt.enemyParty} left)` : '') : 'Wild battle'}</h2>
      <div class="battle">
        <div class="side">
          <img class="big" src="/assets/sprites/front/${pad3(e.dex)}.png" alt="">
          <div style="flex:1;min-width:0">
            <h3>${esc(e.nick)} <small>L${e.level} · ${esc(e.types[0])}${e.types[1] !== e.types[0] ? '/' + esc(e.types[1]) : ''} · #${e.dex} ${e.owned ? 'owned' : (e.seen ? 'seen' : 'new')}</small></h3>
            ${hpBar(e)}
            ${bt.trainer ? '' : `<div style="margin-top:8px"><span class="hint" style="display:inline">Catch odds</span> <span class="chip ${e.catchOdds.poke >= 70 ? 'good' : ''}">Poké ${e.catchOdds.poke}%</span><span class="chip ${e.catchOdds.great >= 70 ? 'good' : ''}">Great ${e.catchOdds.great}%</span><span class="chip ${e.catchOdds.ultra >= 70 ? 'good' : ''}">Ultra ${e.catchOdds.ultra}%</span> <span class="hint" style="display:inline">rate ${e.catchRate}</span></div>`}
            <div class="stats">
              <div><span>ATK</span><b>${e.baseStats.atk}</b>${dvCell(e.dv.atk)}</div>
              <div><span>DEF</span><b>${e.baseStats.def}</b>${dvCell(e.dv.def)}</div>
              <div><span>SPD</span><b>${e.baseStats.spd}</b>${dvCell(e.dv.spd)}</div>
              <div><span>SPC</span><b>${e.baseStats.spc}</b>${dvCell(e.dv.spc)}</div>
            </div>
            <div class="hint" style="margin-top:6px">DV sum <b class="${e.dv.sum >= 48 ? 'dv hi' : ''}">${e.dv.sum}/60</b> · 30 is average, 48+ is one in 36</div>
            <div style="margin-top:6px"><span class="hint" style="display:inline">Weak to</span> ${chips(e.weakTo, 'good')}</div>
            <div><span class="hint" style="display:inline">Resists</span> ${chips(e.resists, 'bad')} ${e.immuneTo.length ? '<span class="hint" style="display:inline">Immune</span> ' + chips(e.immuneTo, 'bad') : ''}</div>
            <div class="hint" style="margin-top:8px">Its moves (badges judged against ${m ? esc(m.nick) : 'you'})</div>
            ${movesTable(e.moves)}
          </div>
        </div>
        <div class="side">
          ${m ? `<img class="big" src="/assets/sprites/back/${pad3(d.party[m.slot] ? d.party[m.slot].dex : 0)}.png" alt="">
          <div style="flex:1;min-width:0">
            <h3>${esc(m.nick)} <small>L${m.level} · ${esc(m.types[0])}${m.types[1] !== m.types[0] ? '/' + esc(m.types[1]) : ''}</small></h3>
            ${hpBar(m)}
            <div class="stats">
              <div><span>ATK</span><b>${m.stats.atk}</b></div><div><span>DEF</span><b>${m.stats.def}</b></div><div><span>SPD</span><b>${m.stats.spd}</b></div><div><span>SPC</span><b>${m.stats.spc}</b></div>
            </div>
            <div style="margin-top:6px"><span class="hint" style="display:inline">Weak to</span> ${chips(m.weakTo || [], 'bad')}</div>
            <div><span class="hint" style="display:inline">Resists</span> ${chips(m.resists || [], 'good')} ${(m.immuneTo || []).length ? '<span class="hint" style="display:inline">Immune</span> ' + chips(m.immuneTo, 'good') : ''}</div>
            <div class="hint" style="margin-top:8px">Your moves (badges against ${esc(e.nick)})</div>
            ${movesTable(m.moves)}
          </div>` : '<p class="empty">No Pokémon out.</p>'}
        </div>
      </div>`;
  }

  function renderAwards(d) {
    const a = d.achievements;
    const row = (x) => `<div class="award ${a.next && x.title === a.next.title ? 'next' : ''}"><span class="mark ${x.earned ? 'owned' : 'none'}"></span><span class="t">${esc(x.title)}</span><span class="at">${x.earned && x.atMinutes >= 0 ? hhmm(x.atMinutes) : ''}</span></div>`;
    const story = a.list.filter((x) => x.story), mile = a.list.filter((x) => !x.story);
    $('#card-awards .body').innerHTML = `
      ${a.next ? `<div class="hint"><b>Next:</b> ${esc(a.next.title)} — ${esc(a.next.hint)}</div>` : '<div class="hint">Everything earned.</div>'}
      <div class="cols"><div><div class="hint">Story</div>${story.map(row).join('')}</div><div><div class="hint">Milestones</div>${mile.map(row).join('')}</div></div>`;
  }

  function renderBag(d) {
    const items = (list) => list.length ? list.map((i) => `<div class="row"><div class="name" style="min-width:0;flex:1">${esc(i.item)}</div><div class="odds">×${i.qty}</div></div>`).join('') : '<p class="empty">Empty.</p>';
    $('#card-bag .body').innerHTML = `<div class="cols"><div><div class="hint">Bag · ${d.bag.length}/20</div>${items(d.bag)}</div>
      <div><div class="hint">PC · ${d.pcItems.length}/50 · Box ${d.box.number} holds ${d.box.count}</div>${items(d.pcItems)}</div></div>`;
  }

  function toast(title) {
    const t = $('#toast');
    if (!title) { t.classList.remove('show'); lastToast = null; return; }
    if (title === lastToast) return;
    lastToast = title; t.textContent = '🏆 ' + title; t.classList.add('show');
  }

  async function tick() {
    try {
      const r = await fetch('/view.json', { cache: 'no-store' });
      const d = await r.json();
      if (serverId === null) serverId = d.server; else if (d.server !== serverId) { location.reload(); return; }
      if (!d.player) { $('#status').textContent = 'waiting for the game…'; return; }
      // Freshness is "when did the game's memory last change": a paused or
      // closed game stops changing, and that is what the player wants to know.
      const age = d.snapshotAge;
      $('#status').textContent = age < 0 ? 'no snapshot yet' : age < 5 ? 'live' : age < 90 ? `last change ${age}s ago` : `no game posting · last change ${Math.round(age / 60)} min ago`;
      $('#status').style.color = age >= 0 && age < 5 ? 'var(--good)' : 'var(--muted)';
      if (d.renders !== lastRenders) { lastRenders = d.renders; $('#frame').src = '/frame.png?' + lastRenders; $('#mapimg').src = '/map.png?' + lastRenders; }
      $('#mapcap').textContent = `${d.location.label} · ${d.location.x}, ${d.location.y}` + (d.location.repelSteps ? ` · repel ${d.location.repelSteps}` : '') + ' · red squares are items still on the ground (hollow = hidden)';
      renderHeader(d); renderBattle(d); renderParty(d); renderLocation(d); renderEncounters(d); renderPickups(d); renderAwards(d); renderBag(d);
      toast(d.achievements ? d.achievements.toast : null);
    } catch (e) { $('#status').textContent = 'companion server unreachable'; }
  }
  tick(); setInterval(tick, 1000);
})();
