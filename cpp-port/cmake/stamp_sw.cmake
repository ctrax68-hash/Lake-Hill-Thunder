# L13 (NT2003/2004 fidelity pass): stamps the service worker's CACHE_NAME at
# BUILD time, from a hash of the actual built lht_port.wasm.
#
# This exists because the previous approach -- execute_process(git rev-parse)
# + configure_file() in CMakeLists.txt -- ran at CONFIGURE time. The stamp
# therefore froze at whenever cmake last reconfigured, and a plain rebuild
# never refreshed it: HEAD sat three commits ahead of the value actually
# shipped in build-web/sw.js. Since the worker's activate handler only deletes
# caches whose name DIFFERS from the current one, a stamp that never changes
# means a browser cache that is never invalidated -- so an installed PWA kept
# serving the first build it ever cached, no matter how many real fixes were
# deployed afterwards.
#
# Hashing the wasm rather than using the git SHA is deliberate: it changes
# whenever the build's actual content changes, which covers local builds with
# no commit behind them (exactly the case during development, when checking
# whether a fix reached the browser matters most).
#
# Invoked via `cmake -P` from a POST_BUILD step, so it re-runs on every build.
#   -DWASM=<path to lht_port.wasm> -DTEMPLATE=<sw.js.in> -DOUT=<sw.js>

if(NOT EXISTS "${WASM}")
  # Nothing built yet (or a layout change): fall back to a timestamp rather
  # than failing the build, matching the old behaviour when git was absent.
  string(TIMESTAMP LHT_CACHE_VERSION "%Y%m%d%H%M%S")
else()
  file(SHA256 "${WASM}" _wasm_hash)
  string(SUBSTRING "${_wasm_hash}" 0 12 LHT_CACHE_VERSION)
endif()

configure_file("${TEMPLATE}" "${OUT}" @ONLY)
message(STATUS "Service worker CACHE_NAME stamped: lht-${LHT_CACHE_VERSION}")
