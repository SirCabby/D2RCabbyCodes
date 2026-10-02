#!/usr/bin/env bash
# Launch D2RLoader (and, through it, Diablo II: Resurrected) inside the Battle.net
# Proton prefix with umu - the same runner ~/.local/bin/battlenet uses. Extra
# arguments go to D2RLoader (for example: -mod D2RMM -txt, or -debug_mode). The
# "run" verb shares the prefix with a Battle.net that is already open; the
# "waitforexitandrun" verb the Battle.net script uses would wait for it to close.
set -euo pipefail

export GAMEID="${GAMEID:-umu-0}"
export PROTONPATH="${PROTONPATH:-$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton11-1}"
export WINEPREFIX="${WINEPREFIX:-$HOME/Games/battlenet}"
export PROTON_VERB="${PROTON_VERB:-run}"

UMU="${UMU:-$HOME/.local/share/lutris/runtime/umu/umu-run}"
GAME_DIR="${GAME_DIR:-$WINEPREFIX/drive_c/Program Files (x86)/Diablo II Resurrected}"
EXE="$GAME_DIR/D2RLoader.exe"

if [[ ! -f "$EXE" ]]; then
  echo "d2r-loader: '$EXE' not found - install D2RLoader into the game folder first" >&2
  exit 1
fi

cd "$GAME_DIR"
exec "$UMU" "$EXE" "$@"
