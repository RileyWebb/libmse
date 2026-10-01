# Drop-in overlays

Every `.lua` file in this directory is run at startup, in name order, and can
register overlays that draw on the game picture:

```lua
local ig      = require("mse.imgui")
local overlay = require("mse.overlay")

overlay.register {
    id    = "my.crosshair",
    title = "Crosshair",
    group = "Mine",
    draw  = function(view)
        view:line(0, view.pixels_y / 2, view.pixels_x, view.pixels_y / 2,
            ig.U32(1, 0, 0, 0.6))
    end,
}
```

`view` gives you the picture's screen rect (`x`, `y`, `w`, `h`), its own
resolution (`pixels_x`, `pixels_y`), the `scale` between them, and helpers that
take **game** pixels so a box lands on a sprite at any window size: `view:pos`,
`view:box`, `view:filled`, `view:line`, `view:text`.

Turn one on from the View > Overlays menu, or from the console with
`overlay <id>`; `overlays` lists what is loaded.

Reaching the emulator is the backend's business, not the frontend's. For cNES,
`require("ui.debug")` gives you the debug API and `require("games.smb")` the
Super Mario Bros. addresses the bundled overlays use. See
`frontend/lua/README.md` and `cnes/data/lua/overlays/` for worked examples.

A script that throws is reported once and disabled; the frame carries on.
