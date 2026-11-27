# Load with: source /path/to/hyprcanvas/scripts/completions/hsctl.fish
# Or copy to ~/.config/fish/completions/hsctl.fish for automatic loading.

# Return only positional tokens, skipping the global instance option and value.
# The current, unfinished token is deliberately excluded.
function __hsctl_positionals
    set -l tokens (commandline -opc)
    set -e tokens[1]
    while test (count $tokens) -gt 0
        switch $tokens[1]
            case -i --instance
                if test (count $tokens) -lt 2
                    return 1
                end
                set -e tokens[1..2]
            case --
                set -e tokens[1]
                break
            case '-*'
                return 1
            case '*'
                break
        end
    end
    if test (count $tokens) -gt 0
        printf '%s\n' $tokens
    end
end

function __hsctl_at_command
    set -l args (__hsctl_positionals)
    or return 1
    test (count $args) -eq 0
end

function __hsctl_argument
    set -l args (__hsctl_positionals)
    or return 1
    test (count $args) -eq 1; and contains -- $args[1] $argv
end

# Option argument completion must stay active while -i is awaiting its value.
function __hsctl_option_scope
    set -l tokens (commandline -opc)
    set -e tokens[1]
    while test (count $tokens) -gt 0
        switch $tokens[1]
            case -i --instance
                if test (count $tokens) -eq 1
                    return 0
                end
                set -e tokens[1..2]
            case '*'
                return 1
        end
    end
    return 0
end

function __hsctl_instances
    command -q hyprctl; and command -q jq; or return
    command hyprctl instances -j 2>/dev/null | command jq -r '.[] | .instance + "\t" + .wl_socket' 2>/dev/null
end

complete -c hsctl -f
complete -c hsctl -n __hsctl_option_scope -s i -l instance -r -a '(__hsctl_instances)' -d 'Target Hyprland instance'
complete -c hsctl -n __hsctl_option_scope -s h -l help -d 'Show help'

for entry in \
    'focus:Move window focus' 'move:Move window' 'swap:Swap adjacent windows' \
    'move-workspace:Move window to adjacent canvas and follow' \
    'move-monitor:Move window to adjacent monitor and follow' \
    'monitor:Focus monitor or overview canvas' 'focusmonitor:Alias for monitor' \
    'workspace:Switch canvas workspace' 'canvas:Alias for workspace' \
    'marks:Manage window marks' 'mark:Alias for marks' \
    'size:Set visible stack count' 'mode:Set layout orientation' \
    'fullscreen:Expand or restore stack' 'overview:Toggle overview' 'ov:Alias for overview' \
    'exec:Run a command in the nested session' \
    'kitty:Open kitty' 'foot:Open foot' 'alacritty:Open alacritty' 'ghostty:Open ghostty' \
    'clients:List windows' 'monitors:List monitors' 'activeworkspace:Show active workspace' \
    'activewindow:Show active window' 'workspaces:List workspaces' 'plugins:List plugins' \
    'eval:Evaluate Lua' 'raw:Pass arguments to hyprctl' 'which:Show target instance' \
    'exit:Exit target session' 'help:Show help'
    set -l parts (string split -m 1 : -- $entry)
    complete -c hsctl -n __hsctl_at_command -a $parts[1] -d $parts[2]
end

complete -c hsctl -n '__hsctl_argument focus move move-monitor move-workspace swap monitor focusmonitor workspace canvas' -a 'l r u d left right up down dn'
complete -c hsctl -n '__hsctl_argument focus move' -a 'begin b beginning end e'
complete -c hsctl -n '__hsctl_argument workspace canvas' -a '1 2 3 4 5 6 7 8 9 10'
complete -c hsctl -n '__hsctl_argument size' -a '1 2 3 4 5 6 7 8'
complete -c hsctl -n '__hsctl_argument mode' -a 'row col'
complete -c hsctl -n '__hsctl_argument overview ov' -a 'accept cancel confirm enter abort close'
complete -c hsctl -n '__hsctl_argument marks mark' -a 'add visit delete reset'
complete -c hsctl -n '__hsctl_argument exec' -a '(__fish_complete_command)'
