#!/usr/bin/env bash
# Usage: ./patch_linux_mac.sh "path/to/Melee 1.02.iso"
DIR=$(cd "$(dirname "$0")" && pwd)
if command -v xdelta3 >/dev/null; then XD=xdelta3
elif command -v xdelta >/dev/null; then XD=xdelta
else echo "ERROR: install xdelta3 (e.g. 'brew install xdelta' or 'sudo apt install xdelta3')."; exit 1; fi
if [[ ! -f "$1" ]]; then echo "Usage: $0 \"path/to/Melee 1.02.iso\""; exit 1; fi
if ! "$XD" -f -d -s "$1" "$DIR/patch.xdelta" "$DIR/BuildAMelee.iso"; then
    rm -f "$DIR/BuildAMelee.iso"
    echo "ERROR: '$1' is not a clean NTSC v1.02 Melee ISO."; exit 1
fi
echo "Done! $DIR/BuildAMelee.iso"
