/* Lake Hill Thunder C++ port -- determinism ground-truth generator.
 *
 * Runs the REAL JS game (index.html, unmodified) headlessly via Playwright,
 * starting a race on a fixed track with the default grid order (qualifying
 * off, so gridStart() consumes the shared rng(seed 12345) stream in the
 * exact order car_test.cpp already verified), drives the player with the
 * same synthetic "brain" tools/playtest.js uses (a pure function of car/
 * track state, so it doesn't introduce its own nondeterminism), and dumps
 * every car's full physics-relevant state every tick to a flat text file.
 *
 * This is the ground truth the C++ port's stepCar()/tick() port (Phase 1f/1g)
 * will be diffed against, tick by tick, as it's built out. See
 * cpp-port/src/sim/trace.h for the C++-side reader/comparator and
 * PORT_PROGRESS.md's Phase 1e notes for the file format.
 *
 * Usage:
 *   NODE_PATH=$(npm root -g) node cpp-port/tests/determinism/dump_js_trace.js \
 *     --track=0 --ticks=400 --out=cpp-port/tests/fixtures/trace_track0_green.txt
 *
 * Optional --force=idx:scenario[,idx:scenario...] seeds specific cars'
 * state (still unmodified index.html -- this just pokes S.cars from the
 * test script, exactly like a real incident would set that state), for
 * exercising branches that rarely occur organically in a short trace:
 * spin/pit/etc. Applied right after the grid forms by default; use
 * --force-tick=N to apply it mid-run instead (e.g. to force a spin well
 * into green-flag racing, since spinT decays in 80 ticks and S.mode only
 * becomes 'race' after the pace phase ends). Scenario values:
 *   spin  -> spinT=1.6, spinDir=1, spinCd=10 (matches collide()'s own wreck-roll seed)
 *   pit1  -> pit=1 (approach)
 *   pit2  -> pit=2, pitT=2 (in service)
 *   pit3  -> pit=3 (exit lane)
 *   pit4  -> pit=4, dtPending=true (drive-through penalty)
 *
 * Requires playwright installed globally, chromium at /opt/pw-browsers (or
 * set CHROMIUM_PATH) -- same requirements as tools/playtest.js.
 */
const http = require('http');
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const ROOT = path.join(__dirname, '..', '..', '..'); // repo root (index.html lives here)
const TRACK_ARG = (process.argv.find(a => a.startsWith('--track=')) || '--track=0').split('=')[1] | 0;
const TICKS_ARG = (process.argv.find(a => a.startsWith('--ticks=')) || '--ticks=400').split('=')[1] | 0;
const OUT_ARG = (process.argv.find(a => a.startsWith('--out=')) || '--out=').split('=')[1];
const FORCE_ARG = (process.argv.find(a => a.startsWith('--force=')) || '--force=').split('=')[1];
const FORCE_TICK_ARG = (process.argv.find(a => a.startsWith('--force-tick=')) || '--force-tick=0').split('=')[1] | 0;
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
  // index.html runs its own requestAnimationFrame(frame) loop from page load,
  // which calls tick() at real-wall-clock rate independent of anything this
  // script does -- neutering rAF before that loop ever gets to schedule its
  // first real callback is the only way to make tick() calls fully
  // deterministic and exclusively driven by this script's own explicit loop
  // below. Without this, every await (waitForTimeout, click, etc.) lets real
  // time pass and the game's own loop silently ticks the sim in the
  // background, corrupting the trace with wall-clock-dependent extra ticks.
  await page.addInitScript(() => { window.requestAnimationFrame = () => 0; });
  await page.goto(`http://localhost:${port}/index.html`);
  await page.waitForTimeout(400);
  await page.evaluate(i => { if (typeof selectTrack === 'function') selectTrack(i); }, TRACK_ARG);
  await page.click('#startBtn');
  await page.waitForTimeout(300);

  if (FORCE_ARG) {
    await page.evaluate((forceSpec) => {
      window.__applyForce = function () {
        for (const entry of forceSpec.split(',')) {
          const [idxStr, scenario] = entry.split(':');
          const idx = +idxStr;
          const c = S.cars.find(c => c.idx === idx);
          if (!c) throw new Error(`--force: no car with idx ${idx}`);
          if (scenario === 'spin') { c.spinT = 1.6; c.spinDir = 1; c.spinCd = 10; }
          else if (scenario === 'pit1') { c.pit = 1; }
          else if (scenario === 'pit2') { c.pit = 2; c.pitT = 2; }
          else if (scenario === 'pit3') { c.pit = 3; }
          else if (scenario === 'pit4') { c.pit = 4; c.dtPending = true; }
          else throw new Error(`--force: unknown scenario '${scenario}'`);
        }
      };
    }, FORCE_ARG);
  }

  // Same synthetic driver brain as tools/playtest.js, verbatim -- a pure
  // function of car/track state (no RNG), so running it doesn't change the
  // game's own rng()/rngR() call sequence and stays fully reproducible.
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
  });

  const trace = await page.evaluate(({ nTicks, forceTick, hasForce }) => {
    const lines = [];
    const fmt = (v) => (typeof v === 'number' ? (Number.isFinite(v) ? v.toFixed(9) : '0') : (v ? 1 : 0));
    for (let i = 0; i < nTicks; i++) {
      if (hasForce && i === forceTick) window.__applyForce();
      window.__brain();
      tick();
      lines.push(['TICK', i, S.t.toFixed(9), S.mode, S.flag,
                   S.greenLockT.toFixed(9), S.sinceGreenT.toFixed(9),
                   PACE.s.toFixed(9), PACE.lat.toFixed(9), PACE.v.toFixed(9), PACE.state].join(' '));
      for (const c of S.cars) {
        lines.push(['CAR', c.idx, fmt(c.isPlayer),
          fmt(c.x), fmt(c.y), fmt(c.hdg), fmt(c.v),
          fmt(c.s), fmt(c.lat), c.lap, fmt(c.prog), fmt(c.done), fmt(c.finishT),
          fmt(c.wear), fmt(c.draftF), fmt(c.dirty),
          fmt(c.skill), fmt(c.aggr), fmt(c.grooveBias),
          c.passSide, fmt(c.passT),
          fmt(c.spinT), c.spinDir, fmt(c.spinCd),
          c.pit, fmt(c.pitT), fmt(c.pitReq),
          fmt(c.fuel), fmt(c.dmg), fmt(c.out), c.cautionSlot
        ].join(' '));
      }
    }
    return lines.join('\n') + '\n';
  }, { nTicks: TICKS_ARG, forceTick: FORCE_TICK_ARG, hasForce: !!FORCE_ARG });

  if (OUT_ARG) {
    fs.writeFileSync(path.join(ROOT, OUT_ARG), trace);
    console.error(`Wrote ${trace.length} bytes to ${OUT_ARG}`);
  } else {
    process.stdout.write(trace);
  }

  await browser.close();
  server.close();
})();
