#!/bin/sh
# Saves a PNG of one tmux pane as shown in Windows Terminal (WSL host only).
# usage: capture_pane.sh <out.png> <tmux pane, e.g. dev-1:1.2>
set -eu
out=${1:?usage: $0 <out.png> <pane>}
pane=${2:?usage: $0 <out.png> <pane>}
ps=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe

# Rows include the tmux status line.
set -- $(tmux display -p -t "$pane" '#{window_width} #{window_height} #{pane_left} #{pane_top} #{pane_width} #{pane_height}')
rows=$(($2 + $(tmux show -gv status 2>/dev/null | grep -qx on && echo 1 || echo 0)))

touch "$out"
"$ps" -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$(dirname "$0")/capture_pane.ps1")" \
  -Out "$(wslpath -w "$(realpath "$out")")" -Cols "$1" -Rows "$rows" -Left "$3" -Top "$4" -Width "$5" -Height "$6" | tr -d '\r'
