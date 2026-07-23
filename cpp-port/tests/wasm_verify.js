// Phase 7 (PORT_PROGRESS.md): one-off headless verification of the
// Emscripten/WebAssembly build (build-web/lht_port.html), run manually
// against a `python3 -m http.server` instance serving build-web/. Not wired
// into ctest -- like tests/determinism/dump_js_trace.js, this needs
// Playwright/Node and a live server, not something CI-style headless test
// runners here are set up for.
//
// Checks (matching this project's established Phase 2/3c/4a screenshot
// verification technique, translated from xvfb+bgfx-screenshot-capture to
// a real browser's own screenshot API since this *is* a real browser now):
//   1. No unexpected console/page errors during load + a few seconds of run.
//   2. The canvas isn't uniformly one flat color (i.e. something is
//      actually being drawn, not just a black/blank canvas).
//   3. The canvas visibly changes between two screenshots a few seconds
//      apart (proving the frame loop is really advancing, not stuck on one
//      static frame).
//
// Phase 7b added the PWA installability wrapper (manifest.json, sw.js,
// icons, apple-touch-icon). This adds checks that those are genuinely
// correct, not just "files exist":
//   4. manifest.json fetches 200 and parses as JSON with the required
//      fields (name, icons, start_url, display) present.
//   5. The icon PNGs (icon-192, icon-512, apple-touch-icon) fetch 200 and
//      look like valid PNGs (signature check here; exact pixel dimensions
//      are checked by generate_icons.py's own output at generation time).
//   6. The service worker actually registers (navigator.serviceWorker.ready
//      resolves without throwing), checked in-page after load.
//
// Phase 4b added the menu screen -- the page now lands on the menu (no
// cars, nothing ticking) instead of an already-running pace lap, so this
// adds a real end-to-end click-through:
//   7. A screenshot immediately after load (the menu -- expected static,
//      non-blank).
//   8. A real Playwright mouse click at the Start button's known pixel
//      coordinates (computeMenuRegions()'s kRowStart row center -- see
//      src/ui/menu.cpp; not simulated X11/XTEST input, which this
//      project's own Phase 2e/3b session notes established is unreliable
//      in this container for the *native* SDL2 build specifically --
//      Playwright drives the browser's own input pipeline instead, a
//      completely different and already-proven-reliable code path here).
//   9. Two more screenshots a few seconds after the click (the original
//      frame-advances regression check, now performed post-click since
//      that's where gameplay actually renders).
// This script only captures screenshots + basic console/error checks --
// actual pixel-level verification (non-blank extrema, frame-diff) is done
// as a separate manual PIL pass over the written PNGs, same division of
// labor Phase 7/7b already used.
//
// NOT checked here, and not checkable in this container: real Safari/iOS
// "Add to Home Screen" behavior -- no macOS/iOS device exists in this
// sandbox. Manifest/icon/SW correctness is a necessary but not sufficient
// condition for that; someone with a real Apple device still needs to try
// the served build by hand.
//
// Usage: node tests/wasm_verify.js [http://localhost:8765/lht_port.html]

const { chromium } = require('playwright');

let hadFailure = false;
function fail(msg) {
  console.error('FAIL:', msg);
  hadFailure = true;
}

async function checkManifest(baseUrl) {
  const url = new URL('manifest.json', baseUrl).toString();
  const res = await fetch(url);
  if (res.status !== 200) {
    fail(`manifest.json fetch returned ${res.status} (expected 200)`);
    return;
  }
  let json;
  try {
    json = await res.json();
  } catch (e) {
    fail(`manifest.json did not parse as JSON: ${e}`);
    return;
  }
  for (const field of ['name', 'icons', 'start_url', 'display']) {
    if (!(field in json)) fail(`manifest.json missing required field "${field}"`);
  }
  if (!Array.isArray(json.icons) || json.icons.length === 0) {
    fail('manifest.json "icons" is not a non-empty array');
  }
  console.log('manifest.json: OK (fields present:', Object.keys(json).join(', '), ')');
}

async function checkIcons(baseUrl) {
  const icons = ['icons/icon-192.png', 'icons/icon-512.png', 'icons/apple-touch-icon.png'];
  const pngSig = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  for (const rel of icons) {
    const url = new URL(rel, baseUrl).toString();
    const res = await fetch(url);
    if (res.status !== 200) {
      fail(`${rel} fetch returned ${res.status} (expected 200)`);
      continue;
    }
    const buf = Buffer.from(await res.arrayBuffer());
    if (buf.length < 8 || !buf.subarray(0, 8).equals(pngSig)) {
      fail(`${rel} does not look like a valid PNG (bad signature)`);
      continue;
    }
    console.log(`${rel}: OK (200, ${buf.length} bytes, valid PNG signature)`);
  }
}

async function main() {
  const url = process.argv[2] || 'http://localhost:8765/lht_port.html';

  await checkManifest(url);
  await checkIcons(url);

  const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium' });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });

  const consoleErrors = [];
  const pageErrors = [];
  page.on('console', (msg) => {
    if (msg.type() === 'error') consoleErrors.push(msg.text());
  });
  page.on('pageerror', (err) => pageErrors.push(String(err)));

  console.log(`Navigating to ${url} ...`);
  await page.goto(url, { waitUntil: 'load' });

  // Phase 7b: service worker registration check.
  let swResult;
  try {
    swResult = await page.evaluate(async () => {
      if (!('serviceWorker' in navigator)) return { ok: false, reason: 'serviceWorker not in navigator' };
      const reg = await navigator.serviceWorker.ready;
      return { ok: true, scope: reg.scope };
    });
  } catch (e) {
    swResult = { ok: false, reason: String(e) };
  }
  if (!swResult.ok) {
    fail(`service worker did not become ready: ${swResult.reason}`);
  } else {
    console.log('service worker: OK (ready, scope =', swResult.scope, ')');
  }

  const fs = require('fs');
  const path = require('path');
  const outDir = process.argv[3] || '/tmp/claude-0/-home-user-Boardwalk/d3ca08ad-c098-513a-a356-d82aaf81bd44/scratchpad';

  // Give the wasm module time to instantiate and render the menu (Phase 4b:
  // the page now lands here first, not an already-running pace lap).
  await page.waitForTimeout(2000);
  const menuShot = await page.screenshot();
  fs.writeFileSync(path.join(outDir, 'wasm_menu.png'), menuShot);

  // Click the Start button. Fixed pixel coordinates matching
  // computeMenuRegions()'s startBtn row exactly (src/ui/menu.cpp: kCol=1,
  // kCellW=8, kCellH=16, kRowStart=11, kStartColsWide=24 -> x in
  // [8, 8+24*8)=[8,200), y in [11*16, 11*16+16)=[176,192)); this is a real
  // click through Playwright's own input pipeline, not simulated X11/XTEST
  // input (see this function's header comment for why that distinction
  // matters in this container).
  await page.mouse.click(104, 184);

  // Give gridStart()'s new car field time to actually appear and the pace
  // phase to advance a few real seconds before the first post-click shot.
  await page.waitForTimeout(4000);
  const shot1 = await page.screenshot();

  await page.waitForTimeout(2000);
  const shot2 = await page.screenshot();

  await browser.close();

  fs.writeFileSync(path.join(outDir, 'wasm_shot1.png'), shot1);
  fs.writeFileSync(path.join(outDir, 'wasm_shot2.png'), shot2);

  console.log(`Console errors: ${consoleErrors.length}`);
  consoleErrors.forEach((e) => console.log('  console error:', e));
  console.log(`Page errors: ${pageErrors.length}`);
  pageErrors.forEach((e) => console.log('  page error:', e));
  console.log('Screenshots written to', outDir,
              '(wasm_menu.png before the click, wasm_shot1.png/wasm_shot2.png after)');

  if (consoleErrors.length > 0 || pageErrors.length > 0) {
    fail('console/page errors occurred during load+run (see above)');
  }
  if (hadFailure) {
    process.exitCode = 1;
  }
}

main().catch((err) => {
  console.error('wasm_verify.js failed:', err);
  process.exit(1);
});
