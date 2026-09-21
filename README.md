# hypr-cube

A Hyprland plugin that turns relative workspace switching into a Compiz-style rotating
cube: the current and target workspaces are captured as textures and drawn on adjacent
faces of a cube that spins to the new face. Supports both a fixed-duration animated switch
and a free mouse drag that snaps to the nearest face on release (or aborts back to the
starting face on Escape).

The renderer draws with its own GLES program through Hyprland's render pass, and never
calls `createFunctionHook`. Most Hyprland render plugins hook internal renderer functions,
which silently does nothing on non-x86_64 systems (the hooking library only supports
x86_64). Because this plugin uses no function hooks, it works the same way on aarch64 as
on x86_64.

## Supported Hyprland version

Built and tested against **Hyprland v0.56.2, commit
`efb50993780079460b0cbed1363e2166a2de1d9f`**. A compiled plugin binary is valid for exactly
one Hyprland commit — Hyprland's plugin ABI is not stable across commits, and the plugin
refuses to load (with a notification) if the running Hyprland's API hash does not match the
hash it was built against.

## Install

```sh
hyprpm update
hyprpm add <this-repository-url-or-path> <commit-hash>
hyprpm enable hypr-cube
hyprpm reload
```

`hyprpm update` builds `hyprpm`'s own Hyprland header tree the first time it runs on a
machine (it clones Hyprland's source) and can take several minutes; pass `--verbose` if it
seems stuck. Pass a commit hash, not a branch name, as the `hyprpm add` revision — a branch
name reliably fails with `fatal: ambiguous argument '<branch>': unknown revision or path
not in the working tree`. If `hyprpm`'s state gets wedged, `hyprpm purge-cache` is the
recovery hatch.

## Configuration

All values live under `plugin:hypr-cube` and hot-reload on `hyprctl reload`.

| value | type | default | effective range | out-of-range behaviour |
|---|---|---|---|---|
| `faces` | int | 4 | 3 – 16 | clamped, notification |
| `duration_ms` | int | 300 | 16 – 5000 | clamped **silently, no notification** |
| `fov` | float | 45 | 20 – 120 | clamped, notification |
| `drag_zoom` | float | 0.6 | 0.2 – 1.0 | clamped, notification |
| `drag_sensitivity` | float | 1.0 | > 0 | any value ≤ 0 **silently** becomes 1.0 |
| `background` | color | `rgba(000000ff)` | — | — |

`duration_ms` and `drag_sensitivity` fail closed with no feedback at all: set
`duration_ms = 10000` and you get 5000ms with nothing telling you why; set
`drag_sensitivity = 0` or a negative number and you get the default of `1.0`, again with no
notification. The other three values notify on screen when clamped.

Lua config example:

```lua
hl.config({
    plugin = {
        hypr_cube = {
            faces = 6,
            duration_ms = 400,
            fov = 50,
            drag_zoom = 0.5,
            drag_sensitivity = 1.2,
            background = "rgba(101010ff)",
        }
    }
})
```

## Dispatchers

Two dispatchers do the work:

- `cube:workspace next` / `cube:workspace prev` — animate to the adjacent workspace on the
  focused monitor. If the focused workspace isn't one of the cube's faces, this falls back
  to a plain relative workspace switch with no cube animation.
- `cube:drag` — start a free drag: the cube follows the mouse until the button is released
  (snaps to the nearest face) or Escape is pressed (aborts back to the starting face).

There is also `cube:stop`, which force-ends whatever session is active (mid-rotation or
mid-drag) without waiting for it to finish.

### Lua config

`hl.dsp` is a closed set, so a Lua config cannot name a plugin dispatcher directly. Instead
the plugin exposes Lua entry points at `hl.plugin.cube.next`, `.prev`, `.stop`, and `.drag`.
These are the binds live on this machine:

```lua
hl.bind("SUPER + ALT + mouse:272", function() hl.plugin.cube.drag() end)
hl.bind("SUPER + ALT + right", function() hl.plugin.cube.next() end)
hl.bind("SUPER + ALT + left",  function() hl.plugin.cube.prev() end)
```

Modifiers join with `" + "`. **A malformed chord fails silently** — `hyprctl reload` still
reports `ok` even if the bind never registered — so after editing a bind, confirm it with
`hyprctl binds` rather than trusting the reload result.

### Legacy `.conf` config

On a `.conf`-based Hyprland config, the dispatchers can be bound directly by name:

```
bind  = SUPER ALT, right, cube:workspace, next
bind  = SUPER ALT, left,  cube:workspace, prev
bindm = SUPER ALT, mouse:272, cube:drag
```

## Known limitations

- Faces are **snapshots** taken at the start of a turn, not a live view. A video playing on
  a background face freezes for the duration of the turn. Every face is captured, so a turn
  costs `faces` off-screen workspace renders regardless of how far the cube travels.
- A face whose workspace does not exist yet shows the bare desktop — wallpaper and layer
  surfaces, no windows.
- The mouse cursor stays visible over the cube while dragging; it is not hidden or
  replaced with a grab cursor.
