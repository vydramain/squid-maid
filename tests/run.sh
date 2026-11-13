#!/usr/bin/env bash
# Host unit tests: builds every tests/test-*.cpp against src/ and the PDK headers and runs it.
# The disc build compiles only src/*.cpp, so nothing here reaches the disc. PDK headers: $PDK, the console checkout this disc sits in.
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
pdk=${PDK:?set PDK to the 3dmppc console checkout}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
rc=0
for t in "$here"/tests/test-*.cpp; do
    name=$(basename "$t" .cpp)
    if g++ -std=c++23 -O1 -Wall -Wextra -I "$here/src" -I "$pdk/pdk/include" -I "$pdk/pdk/lib/include" \
        -I "$pdk/pdk/kit/include" "$t" -o "$tmp/$name" && "$tmp/$name"; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        rc=1
    fi
done
exit $rc
