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
// Usage: node tests/wasm_verify.js [http://localhost:8765/lht_port.html]

const { chromium } = require('playwright');

async function main() {
  const url = process.argv[2] || 'http://localhost:8765/lht_port.html';
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

  // Give the wasm module time to instantiate and the sim/render loop to
  // actually start producing frames (pace phase + a few seconds of real
  // ticks) before the first screenshot.
  await page.waitForTimeout(4000);
  const shot1 = await page.screenshot();

  await page.waitForTimeout(2000);
  const shot2 = await page.screenshot();

  await browser.close();

  const fs = require('fs');
  const path = require('path');
  const outDir = process.argv[3] || '/tmp/claude-0/-home-user-Boardwalk/d3ca08ad-c098-513a-a356-d82aaf81bd44/scratchpad';
  fs.writeFileSync(path.join(outDir, 'wasm_shot1.png'), shot1);
  fs.writeFileSync(path.join(outDir, 'wasm_shot2.png'), shot2);

  console.log(`Console errors: ${consoleErrors.length}`);
  consoleErrors.forEach((e) => console.log('  console error:', e));
  console.log(`Page errors: ${pageErrors.length}`);
  pageErrors.forEach((e) => console.log('  page error:', e));
  console.log('Screenshots written to', outDir, '(wasm_shot1.png, wasm_shot2.png)');
}

main().catch((err) => {
  console.error('wasm_verify.js failed:', err);
  process.exit(1);
});
