/* Lake Hill Thunder — headless playtest harness.
 *
 * Runs a full simulated race (pace lap → green → all laps → results) by
 * driving the player car through the game's REAL input paths (tilt-steer
 * proportional input + gas/brake booleans), then prints telemetry JSON.
 *
 * Usage:
 *   NODE_PATH=$(npm root -g) node tools/playtest.js [--shots]
 *
 * Requires: playwright installed globally, chromium at /opt/pw-browsers
 * (or set CHROMIUM_PATH). --shots also writes pace/race/results PNGs
 * next to this script.
 *
 * Pass criteria to eyeball in the output:
 *   finished:true, wallHits:0 (a competent line shouldn't hit walls),
 *   playerBest within ~1s of aiBest, finalPos improves from P8 start.
 */
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const ROOT = path.join(__dirname, '..');
const SHOTS = process.argv.includes('--shots');
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

(async () => {
  await new Promise(r => server.listen(0, r));
  const port = server.address().port;

  const browser = await chromium.launch({
    executablePath: EXE,
    args: ['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist']
  });
  const page = await browser.newPage({ viewport: { width: 900, height: 500 } });
  page.on('pageerror', err => console.error('PAGE ERROR:', err.message));
  await page.goto(`http://localhost:${port}/index.html`);
  await page.waitForTimeout(400);
  await page.click('#startBtn');
  await page.waitForTimeout(300);

  // driver brain: proportional steering via the tilt path, lift for tight ends
  await page.evaluate(() => {
    S.tilt = true;
    window.__brain = function () {
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
        const cA = TRACK.pointAt(c.s + c.v * 0.55).curv;
        const tight = Math.abs(cA) > 1e-6 && 1 / Math.abs(cA) < 130;
        input.gas = !(tight && c.v > 67.5);
        input.brake = false;
      }
    };
  });

  if (SHOTS) { await page.waitForTimeout(1200); await page.screenshot({ path: path.join(__dirname, 'shot_pace.png') }); }

  const out = await page.evaluate(() => {
    const T = { wallHits: 0, maxV: 0, laps: [], finished: false, draftTicks: 0, raceTicks: 0 };
    let prevShake = 0, prevLap = -1;
    for (let i = 0; i < 120000; i++) {
      window.__brain(); tick();
      const P = S.player;
      if (S.mode === 'race') {
        T.maxV = Math.max(T.maxV, P.v); T.raceTicks++;
        if (P.draftF > 0.3) T.draftTicks++;
      }
      if (S.shakeT > prevShake + 0.2) T.wallHits++;
      prevShake = S.shakeT;
      if (P.lap > prevLap && P.lap >= 1) {
        T.laps.push({ lap: P.lap, t: +P.lastLapT.toFixed(2), pos: S.order.indexOf(P) + 1 });
        prevLap = P.lap;
      }
      if (S.mode === 'done') { T.finished = true; break; }
    }
    T.playerBest = +S.player.bestLapT.toFixed(2);
    T.aiBest = +Math.min(...S.cars.filter(c => !c.isPlayer).map(c => c.bestLapT || 1e9)).toFixed(2);
    T.finalPos = S.finishOrder.indexOf(S.player) + 1 || (S.order.indexOf(S.player) + 1);
    T.simT = +S.t.toFixed(1);
    return T;
  });
  console.log(JSON.stringify(out, null, 2));

  if (SHOTS) {
    await page.waitForTimeout(1600);
    await page.screenshot({ path: path.join(__dirname, 'shot_results.png') });
  }
  await browser.close();
  server.close();
  if (!out.finished) { console.error('FAIL: race did not finish'); process.exit(1); }
})();
