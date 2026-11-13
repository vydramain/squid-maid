#!/usr/bin/env bash
# Checks that the disc burns and its manifest carries [touch_pad] with exactly the groups dpad, face, gyro.
# The burn runs on a copy in a temp dir, so the worktree stays clean; the temp dir is removed on exit.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BURNER="${MPPCBURNER:-${PDK:?set PDK to the 3dmppc console checkout}/build-dev/sdk/bin/mppcburner}"
WANT="dpad face gyro"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cp -r "$ROOT" "$TMP/disc"
rm -rf "$TMP/disc/.git"

if ! "$BURNER" build "$TMP/disc" -o "$TMP/squid-maid.mppcdisc" >"$TMP/build.log" 2>&1; then
    echo "ERROR: mppcburner build failed"
    cat "$TMP/build.log"
    exit 1
fi

if ! "$BURNER" inspect "$TMP/squid-maid.mppcdisc" >"$TMP/inspect.txt" 2>"$TMP/inspect.err"; then
    echo "ERROR: mppcburner inspect failed"
    cat "$TMP/inspect.err"
    exit 1
fi

# The section runs from its header to the next header at column 0.
SECTION="$(awk '/^\[touch_pad\]$/ {f = 1; next} /^\[/ {f = 0} f' "$TMP/inspect.txt")"

if ! grep -q '^\[touch_pad\]$' "$TMP/inspect.txt"; then
    echo "FAIL: no [touch_pad] section in inspect output"
    echo "section headers in the manifest:"
    grep -n '^\[' "$TMP/inspect.txt" || echo "(none)"
    exit 1
fi

if ! printf '%s\n' "$SECTION" | grep -q '^show = \['; then
    echo "FAIL: [touch_pad] has no show list"
    printf '%s\n' "$SECTION"
    exit 1
fi

GOT="$(printf '%s\n' "$SECTION" | { grep -o '"[^"]*"' || true; } | tr -d '"' | sort | paste -sd' ' -)"
if [ "$GOT" != "$WANT" ]; then
    echo "FAIL: [touch_pad] show is '$GOT', want '$WANT'"
    printf '%s\n' "$SECTION"
    exit 1
fi

echo "OK: [touch_pad] show = $GOT"
