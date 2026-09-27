#!/usr/bin/env bash
# ddnet-background-module - installer (Linux / macOS)
#
#   ./apply.sh /path/to/client            # install (supported base: DDNet official 20.1)
#   ./apply.sh /path/to/client --check    # only test whether it applies
#   ./apply.sh /path/to/client --revert   # remove the feature again
#   ./apply.sh /path/to/client --reject   # apply what fits, leave .rej for the rest
set -euo pipefail

module_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
patch="$module_dir/patch/ddnet-20.1.patch"
target="${1:-.}"
mode="${2:-install}"

[ -f "$patch" ] || { echo "patch not found: $patch" >&2; exit 1; }
[ -d "$target" ] || { echo "target not found: $target" >&2; exit 1; }
target="$(cd "$target" && pwd)"

[ -f "$target/src/game/client/components/menus.cpp" ] || {
    echo "'$target' does not look like a DDNet-family client source tree" >&2; exit 1; }

if [ -f "$target/src/engine/shared/config_variables_tclient.h" ]; then
    echo "Detected base: TClient / other fork"
    [ "$mode" = "install" ] && echo "[i] this module ships a patch for DDNet official 20.1 only; use --reject on other bases"
else
    echo "Detected base: DDNet official (20.1 is the supported base)"
fi

has_feature=0
[ -f "$target/src/game/client/components/custom_background.cpp" ] && has_feature=1

case "$mode" in
    --check)  args=(apply --check --verbose --whitespace=nowarn) ;;
    --revert) args=(apply -R --3way --verbose --whitespace=nowarn) ;;
    --reject) args=(apply --reject --verbose --whitespace=nowarn) ;;
    install)
        if [ "$has_feature" = 1 ]; then
            echo "[!] this tree already contains the background feature; nothing to do (use --revert to remove)."
            exit 0
        fi
        args=(apply --verbose --whitespace=nowarn) ;;
    *) echo "unknown mode: $mode (use --check / --revert / --reject)" >&2; exit 1 ;;
esac

echo "> git ${args[*]} $patch"
set +e
( cd "$target" && git "${args[@]}" "$patch" )
code=$?
set -e

if [ $code -ne 0 ]; then
    if [ "$mode" = "--reject" ]; then
        echo "[!] partial apply. Rejected hunks were written to .rej files:"
        find "$target" -name '*.rej' -print
        echo "    Fix them by hand using the hunk context in patch/ddnet-20.1.patch."
        exit 0
    fi
    echo "[x] git apply failed (exit $code). Re-run with --reject to see which hunks clash." >&2
    exit $code
fi

case "$mode" in
    --check)  echo "[ok] patch applies cleanly."; exit 0 ;;
    --revert) echo "[ok] feature removed."; exit 0 ;;
    --reject) echo "[ok] applied (nothing rejected)."; exit 0 ;;
esac

cat <<'EOF'
[ok] background module installed.

Next steps
  1) FFmpeg (video only): replace the ddnet-libs FFmpeg with the BtbN 8.1 shared build
     (libavcodec 62, libavformat 62, libavutil 60, libswresample 6, libswscale 9);
     cmake/FindFFMPEG.cmake is already patched to those names.
  2) Build: cmake -DVULKAN=OFF -DDOWNLOAD_GTEST=OFF . && cmake --build . --config Release --target game-client
  3) Run, then Settings -> Background; put your files into <save dir>/Background
EOF
