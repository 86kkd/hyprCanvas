#!/usr/bin/env bash
# Local Hyprland layout-switch helper tracked in this repository for reference.
# Supports both legacy and Lua config providers.
# Lua keybinds are installed by the Lua config and refreshed after layout changes.
# Load examples/canvas-layout.lua after user_keybinds.lua to replace native
# per-monitor workspace shortcuts with global canvas switching.
# Source path on the maintainer machine:
#   ~/.config/hypr/scripts/ChangeLayout.sh

set -euo pipefail

notif="$HOME/.config/swaync/images/ja.png"
current_layout=$(hyprctl -j getoption general:layout | jq -r '.str')
show_notification=true

if [ "${1:-}" = "init" ]; then
  show_notification=false
fi

config_provider() {
  hyprctl status 2>/dev/null | awk -F': ' '/configProvider/ {print $2; exit}' || true
}

CONFIG_PROVIDER="$(config_provider)"

# Validate the binding module before changing the live layout. Lua is the
# supported provider for the current canvas plugin; the legacy branch below
# remains available for older Hyprland installations.
bindings_path="$HOME/.config/hypr/lua/canvas-layout.lua"
if [ "$CONFIG_PROVIDER" = "lua" ] && [ ! -f "$bindings_path" ]; then
  printf 'Missing canvas layout bindings: %s\n' "$bindings_path" >&2
  exit 1
fi


is_lua_provider() {
  [ "${CONFIG_PROVIDER:-}" = "lua" ]
}

trim() {
  local value="$*"
  value="${value#"${value%%[![:space:]]*}"}"
  value="${value%"${value##*[![:space:]]}"}"
  printf '%s' "$value"
}

lua_quote() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  value="${value//$'\n'/\\n}"
  printf '"%s"' "$value"
}

old_keyseq_to_lua() {
  local mods
  local key
  local part
  local result=""
  mods="$(trim "${1:-}")"
  key="$(trim "${2:-}")"
  mods="${mods//+/ }"

  for part in $mods; do
    if [ -n "$result" ]; then
      result+=" + "
    fi
    result+="$part"
  done

  if [ -n "$result" ]; then
    result+=" + "
  fi
  result+="$key"

  printf '%s' "$result"
}

unbind_compat() {
  local spec="$1"

  if is_lua_provider; then
    local mods
    local key
    local keyseq
    IFS=, read -r mods key <<< "$spec"
    keyseq="$(old_keyseq_to_lua "$mods" "$key")"
    hyprctl eval "hl.unbind($(lua_quote "$keyseq"))" >/dev/null || true
  else
    hyprctl keyword unbind "$spec" || true
  fi
}

bindd_compat() {
  local spec="$1"

  if is_lua_provider; then
    local mods
    local key
    local desc
    local dispatcher
    local arg
    local keyseq
    local dispatch_expr

    IFS=, read -r mods key desc dispatcher arg <<< "$spec"
    mods="$(trim "$mods")"
    key="$(trim "$key")"
    desc="$(trim "$desc")"
    dispatcher="$(trim "$dispatcher")"
    arg="$(trim "${arg:-}")"
    keyseq="$(old_keyseq_to_lua "$mods" "$key")"

    if [ "$dispatcher" = "exec" ]; then
      dispatch_expr="hl.dsp.exec_cmd($(lua_quote "$arg"))"
    elif [ "$dispatcher" = "movefocus" ]; then
      dispatch_expr="hl.dsp.focus({ direction = $(lua_quote "$arg") })"
    elif [ "$dispatcher" = "canvas:movefocus" ]; then
      dispatch_expr="function() hl.plugin.canvas.move_focus($(lua_quote "$arg")) end"
    elif [ "$dispatcher" = "movewindow" ]; then
      dispatch_expr="hl.dsp.window.move({ direction = $(lua_quote "$arg") })"
    elif [ "$dispatcher" = "canvas:movewindow" ]; then
      dispatch_expr="function() hl.plugin.canvas.move_window($(lua_quote "$arg")) end"
    elif [ "$dispatcher" = "canvas:focusmonitor" ]; then
      dispatch_expr="function() hl.plugin.canvas.focus_monitor($(lua_quote "$arg")) end"
    elif [ "$dispatcher" = "canvas:togglefullscreen" ]; then
      dispatch_expr='function() hl.plugin.canvas.toggle_fullscreen() end'
    elif [ "$dispatcher" = "canvas:toggleoverview" ]; then
      dispatch_expr='function() hl.plugin.canvas.toggle_overview() end'
    elif [ "$dispatcher" = "fullscreenstate" ]; then
      dispatch_expr='hl.dsp.window.fullscreen_state({ internal = 2, client = 0, action = "toggle" })'
    elif [ "$dispatcher" = "layoutmsg" ]; then
      dispatch_expr="hl.dsp.layout($(lua_quote "$arg"))"
    else
      printf 'Unsupported Lua dispatcher: %s %s\n' "$dispatcher" "$arg" >&2
      return 1
    fi

    hyprctl eval "hl.bind($(lua_quote "$keyseq"), $dispatch_expr, { description = $(lua_quote "$desc") })" >/dev/null
  else
    hyprctl keyword bindd "$spec"
  fi
}

set_layout_compat() {
  local layout="$1"

  if is_lua_provider; then
    hyprctl eval "hl.config({ general = { layout = $(lua_quote "$layout") } })" >/dev/null
  else
    hyprctl keyword general:layout "$layout"
  fi
}

set_focus_binds() {
  local layout="${1:-}"

  unbind_compat SUPER,h
  unbind_compat SUPER,j
  unbind_compat SUPER,k
  unbind_compat SUPER,l
  unbind_compat SUPER,n
  unbind_compat SUPER,m
  unbind_compat SUPER,comma
  unbind_compat SUPER,period
  unbind_compat SUPER,F
  unbind_compat SUPER,I

  case "$layout" in
    canvas)
      bindd_compat "SUPER,h,focus left (canvas),canvas:movefocus,l"
      bindd_compat "SUPER,j,focus down (canvas),canvas:movefocus,d"
      bindd_compat "SUPER,k,focus up (canvas),canvas:movefocus,u"
      bindd_compat "SUPER,l,focus right (canvas),canvas:movefocus,r"
      bindd_compat "SUPER,n,monitor left (canvas),canvas:focusmonitor,l"
      bindd_compat "SUPER,m,monitor down (canvas),canvas:focusmonitor,d"
      bindd_compat "SUPER,comma,monitor up (canvas),canvas:focusmonitor,u"
      bindd_compat "SUPER,period,monitor right (canvas),canvas:focusmonitor,r"
      bindd_compat "SUPER,F,fullscreen (canvas),canvas:togglefullscreen"
      bindd_compat "SUPER,I,overview (canvas),canvas:toggleoverview"
      ;;
    *)
      bindd_compat "SUPER,h,focus left,movefocus,l"
      bindd_compat "SUPER,j,focus down,movefocus,d"
      bindd_compat "SUPER,k,focus up,movefocus,u"
      bindd_compat "SUPER,l,focus right,movefocus,r"
      bindd_compat "SUPER,F,special fullscreenstate,fullscreenstate,2 0"
      bindd_compat "SUPER,I,add master,layoutmsg,addmaster"
      ;;
  esac
}

set_move_binds() {
  local layout="${1:-}"

  unbind_compat "SUPER CTRL,h"
  unbind_compat "SUPER CTRL,j"
  unbind_compat "SUPER CTRL,k"
  unbind_compat "SUPER CTRL,l"

  case "$layout" in
    canvas)
      bindd_compat "SUPER CTRL,h,move window left (canvas),canvas:movewindow,l"
      bindd_compat "SUPER CTRL,j,move window down (canvas),canvas:movewindow,d"
      bindd_compat "SUPER CTRL,k,move window up (canvas),canvas:movewindow,u"
      bindd_compat "SUPER CTRL,l,move window right (canvas),canvas:movewindow,r"
      ;;
    *)
      bindd_compat "SUPER CTRL,h,move window left,movewindow,l"
      bindd_compat "SUPER CTRL,j,move window down,movewindow,d"
      bindd_compat "SUPER CTRL,k,move window up,movewindow,u"
      bindd_compat "SUPER CTRL,l,move window right,movewindow,r"
      ;;
  esac
}

if [ "${1:-}" = "init" ]; then
  target_layout="$current_layout"
else
  case "$current_layout" in
    master) target_layout="dwindle" ;;
    dwindle) target_layout="canvas" ;;
    canvas) target_layout="master" ;;
    *) target_layout="master" ;;
  esac
fi

case "$target_layout" in
  master)
    set_layout_compat master
    if ! is_lua_provider; then
      set_focus_binds master
      set_move_binds master
    fi
    if [ "$show_notification" = true ]; then
      notify-send -e -u low -i "$notif" " Master Layout"
    fi
    ;;
  dwindle)
    set_layout_compat dwindle
    if ! is_lua_provider; then
      set_focus_binds dwindle
      set_move_binds dwindle
    fi
    if [ "$show_notification" = true ]; then
      notify-send -e -u low -i "$notif" " Dwindle Layout"
    fi
    ;;
  canvas)
    set_layout_compat canvas
    if ! is_lua_provider; then
      set_focus_binds canvas
      set_move_binds canvas
    fi
    if [ "$show_notification" = true ]; then
      notify-send -e -u low -i "$notif" " Canvas Layout"
    fi
    ;;
  *)
    if ! is_lua_provider; then
      set_focus_binds "$target_layout"
      set_move_binds "$target_layout"
    fi
    ;;
esac

# Reinstall after every layout change, including init. The module removes
# conflicting defaults in canvas and restores native actions in other layouts.
if is_lua_provider; then
  bindings_path="$HOME/.config/hypr/lua/canvas-layout.lua"
  if [ ! -f "$bindings_path" ]; then
    printf 'Missing canvas layout bindings: %s\n' "$bindings_path" >&2
    exit 1
  fi
  bindings_result=$(hyprctl eval "dofile($(lua_quote "$bindings_path"))")
  if [ "$bindings_result" != "ok" ]; then
    printf '%s\n' "$bindings_result" >&2
    exit 1
  fi
fi
