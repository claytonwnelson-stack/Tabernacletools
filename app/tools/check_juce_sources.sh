#!/usr/bin/env bash
# Syntax/semantic check of the JUCE UI + processor sources on Linux (no X11 needed) by compiling them for
# x86_64-windows-gnu with zig's clang (-fsyntax-only). Catches C++/JUCE API errors before the Mac/CI build.
# Checks the shared core UI (core/ui), the Guitar app (apps/guitar) and the Core Test Harness (apps/coretest).
# Usage: JUCE=/path/to/JUCE tools/check_juce_sources.sh
set -euo pipefail
cd "$(dirname "$0")/.."
JUCE="${JUCE:-/workspace/vst/worship-keys/JUCE}"
ZIG="${ZIG:-python3 -m ziglang}"
COMPAT=/workspace/vst/worship-keys/compat/mingw
BASE="-target x86_64-windows-gnu -std=c++17 -O0 -DUNICODE -D_UNICODE -DWR_APP_GROUP=\"group.test\"
  -Itools/jucecheck -I$JUCE/modules -Icore -Icore/ui -I$COMPAT -include $COMPAT/wk_mingw_compat.h -Wall -Wno-unknown-pragmas -Wno-deprecated-declarations"
rc=0
check() { local f=$1; shift; echo "== $f"; out=$($ZIG c++ $BASE "$@" -c "$f" -o /tmp/wr_check.o 2>&1) || rc=1
  echo "$out" | grep -E "error|warning" | grep -vE "^In file included|MinGW|warnings? generated" | head -20 || true; }
for f in core/ui/*.cpp core/ui/store/StoreBridge_other.cpp; do check "$f"; done
for f in core/ui/CoreProcessor.cpp core/ui/CoreEditor.cpp core/ui/SeatSession.cpp core/ui/TeamPanel.cpp core/ui/PaywallView.cpp core/ui/SetlistView.cpp; do echo "   (WR_STORE_BUILD)"; check "$f" -DWR_STORE_BUILD=1; done   # v0.11 store-only code paths
for f in apps/guitar/*.cpp; do check "$f" -Iguitar -Iapps/guitar; done
for f in apps/coretest/*.cpp; do check "$f" -Iapps/coretest -DWR_DATA_FOLDER=\"TabernacleCoreTest\"; done
exit $rc
