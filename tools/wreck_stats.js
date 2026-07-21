/* Lake Hill Thunder — AI wreck-frequency measurement harness.
 *
 * Runs a full simulated race per track (same real input paths as
 * tools/playtest.js's brain) and counts caution-flag transitions and
 * spin (wreck) events, split by whether the car had a blown tire at the
 * moment it started spinning. Organic AI only — no synthetic forcing.
 *
 * The game's AI RNG streams (rng/rngR) are fixed seeds, so a given track
 * always produces the same race deterministically; running all 4 tracks
 * is this project's full "N races" sample (same constraint tools/playtest.js
 * already lives with — there's no seed-override hook to vary further).
 *
 * Usage:
 *   NODE_PATH=$(npm root -g) node tools/wreck_stats.js
 *
 * Use this before/after a wreck-tuning change and diff the two JSON blobs
 * by eye — this is a measurement tool, not a pass/fail assertion suite.
 */
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const ROOT = path.join(__dirname, '..');
const EXE = process.env.CHROMIUM_PATH || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome';

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.png': 'image/png', '.json': 'application/json' };
const server = http.createServer((req, res) => {
  const f = path.join(ROOT, req.url === '/' ? 'index.html' : req.url.split('?')[0]);
  fs.readFile(f, (err, data) => {
    if (err) { res.writeHead(404); res.end('nf'); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(f)] || 'application/octet-stream' });
    res.end(data);
  });
});

const BRAIN = function () {
  const c = S.player;
  const LA = Math.max(14, c.v * 0.6);
  const pT = TRACK.pointAt(c.s + LA);
  const lane = -2.0;
  const tx = pT.x - Math.sin(pT.hdg) * lane, ty = pT.y + Math.cos(pT.hdg) * lane;
  let dH = Math.atan2(ty - c.y, tx - c.x) - c.hdg;
  while (dH > Math.PI) dH -= 2 * Math.PI; while (dH < -Math.PI) dH += 2 * Math.PI;
  const curvFF = TRACK.pointAt(c.s + Math.max(6, c.v * 0.3)).curv;
  S.tiltG = Math.max(-1, Math.min(1, curvFF * c.v * 1.9 + dH * 2.2)) * 22;
  input.left = input.right = false;
  if (S.mode === 'pace') {
    const ahead = S.cars[c.gridAhead >= 0 ? c.gridAhead : 0];
    let ds = ahead.s - c.s; if (ds < -TRACK.total / 2) ds += TRACK.total; if (ds > TRACK.total / 2) ds -= TRACK.total;
    input.gas = ds > 9; input.brake = ds < 5;
  } else {
    const sA = c.s + c.v * 0.55;
    const pA = TRACK.pointAt(sA);
    input.gas = true; input.brake = false;
    if (Math.abs(pA.curv) > 1e-6) {
      const R = 1 / Math.abs(pA.curv);
      const mu = 1 + 0.00016 * c.v * c.v;
      const tb = Math.tan(TRACK.bankAt(sA));
      const cap = 9.81 * (mu + tb) / Math.max(0.25, 1 - mu * tb);
      const need = c.v * c.v / R;
      if (need > cap * 0.92) input.gas = false;
      if (need > cap * 1.18) input.brake = true;
    }
  }
};

(async () => {
  await new Promise(r => server.listen(0, r));
  const port = server.address().port;
  const browser = await chromium.launch({
    executablePath: EXE,
    args: ['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist']
  });

  const report = { tracks: [] };
  for (const trackIdx of [0, 1, 2, 3]) {
    const page = await browser.newPage({ viewport: { width: 900, height: 500 } });
    page.on('pageerror', err => console.error('PAGE ERROR:', err.message));
    await page.goto(`http://localhost:${port}/index.html`);
    await page.waitForTimeout(300);
    await page.evaluate(i => { if (typeof selectTrack === 'function') selectTrack(i); }, trackIdx);
    await page.click('#startBtn');
    await page.waitForTimeout(300);
    await page.addScriptTag({ content: `window.__brain = ${BRAIN.toString()};` });
    await page.evaluate(() => { S.tilt = true; });

    const out = await page.evaluate(() => {
      const T = {
        cautions: 0, spinEvents: 0, spinEventsBlown: 0, spinEventsClean: 0,
        aiSpinEvents: 0, aiSpinEventsBlown: 0, aiSpinEventsClean: 0,
        wallHits: 0, finished: false,
      };
      let prevFlag = S.flag;
      let prevShake = 0;
      const wasSpinning = new Map();
      for (const c of S.cars) wasSpinning.set(c, c.spinT > 0);
      for (let i = 0; i < 200000; i++) {
        window.__brain(); tick();
        if (prevFlag !== 'yellow' && S.flag === 'yellow') T.cautions++;
        prevFlag = S.flag;
        if (S.shakeT > prevShake + 0.2) T.wallHits++;
        prevShake = S.shakeT;
        for (const c of S.cars) {
          const now = c.spinT > 0;
          if (now && !wasSpinning.get(c)) {
            T.spinEvents++;
            if (c.blown) T.spinEventsBlown++; else T.spinEventsClean++;
            if (!c.isPlayer) {
              T.aiSpinEvents++;
              if (c.blown) T.aiSpinEventsBlown++; else T.aiSpinEventsClean++;
            }
          }
          wasSpinning.set(c, now);
        }
        if (S.mode === 'done') { T.finished = true; break; }
      }
      T.simT = +S.t.toFixed(1);
      T.finalPos = S.finishOrder.indexOf(S.player) + 1 || (S.order.indexOf(S.player) + 1);
      return T;
    });
    out.track = trackIdx;
    report.tracks.push(out);
    await page.close();
  }

  console.log(JSON.stringify(report, null, 2));
  await browser.close();
  server.close();
})();
