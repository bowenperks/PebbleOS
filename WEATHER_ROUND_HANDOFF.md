# Handoff — Weather app, gabbro (round) report page

## Context

I'm building a custom Weather app inside PebbleOS firmware at `~/Downloads/pebbleos`,
branch `weather-tested-base`.

**All app state is UNCOMMITTED working-tree changes. Never `git reset`, `stash`, `clean`,
or `checkout` over that repo.** Do not work in `/Volumes/code/pebble weather` — that's the
pre-firmware original, kept only as a design reference.

Two boards:
- **emery / obelix** = rect 200×228. **FROZEN — do not change one pixel.**
- **gabbro / getafix** = round 260×260 circular glass, r=130, centre (130,130).

Nearly all shared code lives in `src/fw/apps/system/weather/weather_app_layout.c`, which
hosts the SELECT screen ("the weather report") for **both** shapes. `weather_report.c`'s two
arms are near-identical clones — the visual difference is all in `weather_app_layout.c`.

## What was just finished

The round report page was rebuilt to carry emery's full content set:
crown-free white field → day name (caps, G28_BOLD) → hero temp (LECO_36) → hi/lo →
condition phrase → wrapping prose sentence → UV bar with glass-concentric arc ends →
45-dot rule → stacked footer (`TOMORROW` bold over `24°/14°`, coloured 35px icon disc) →
grey chin band with black chevron.

Everything is round-only via `#if PBL_ROUND` / `PBL_IF_ROUND_ELSE`.
**Note `PBL_IF_RECT_ELSE(a,b)` = a on RECT; `PBL_IF_ROUND_ELSE(a,b)` = a on ROUND — opposite, easy to confuse.**

## Outstanding / unverified

1. **The fin** (week's-end marker) on the final day — never seen. It draws in the same
   footer zone the stacked footer and chin band now occupy. The chevron is gated on
   `layout->next_forecast`, the exact inverse of the fin's condition, so they should swap
   cleanly — unconfirmed.
2. **Day-flip disc animations** — the hero disc lerp and the travelling footer disc were
   both fixed by construction but never frame-by-frame verified (see "QEMU limits" below).
3. **Emery re-verification** — last confirmed pixel-identical several edits ago. Every
   change since has been round-gated, but re-run the check before shipping.
4. **Nothing is pushed.** I said "hold off on pushing" when this port began and never
   lifted it. Ask me before pushing to the fork.

## Build + verify recipe

```bash
cd ~/Downloads/pebbleos && source .venv/bin/activate
./waf configure --board qemu_gabbro   # or qemu_emery
./waf build && ./waf qemu_image_micro && ./waf qemu_image_spi
```

Launch (**`-kernel` + `-drive if=mtd` — NOT `-pflash`, that silently fails on these machines**):

```bash
DYLD_LIBRARY_PATH=/Users/bowham/pebbleos-sdk-0.1.6/qemu/lib \
/Users/bowham/pebbleos-sdk-0.1.6/qemu/bin/qemu-pebble \
  -rtc base=localtime -serial null \
  -serial tcp::63771,server=on,wait=off -serial tcp::63772,server=on,wait=off \
  -kernel build/qemu_micro_flash.bin -gdb tcp::63773,server=on,wait=off \
  -monitor tcp::63774,server=on,wait=off -qmp tcp::63775,server=on,wait=off \
  -machine pebble-gabbro -cpu cortex-m33 \
  -drive if=mtd,format=raw,file=build/qemu_spi_flash.bin -display cocoa &
```

- Buttons: `pebble emu-button --qemu localhost:63771 click {up|down|select|back}`
- Screenshot: `python3 /tmp/qshot.py NAME` (monitor `screendump` → PPM → PNG)
- Burst: `python3 /tmp/qburst.py <button> <n> <tag>`
- Navigation to the app: launcher retains scroll position — **screenshot between steps,
  don't blind-step.** From a fresh boot: down ×6, up ×1, select, then select again for
  the report page.

**Etiquette:** headless (`-display none`) is fine while iterating, but at handoff kill
everything and leave exactly ONE visible (`-display cocoa`) instance — I'm the judge.

## Hard-won traps (please don't rediscover these)

- **Verifying "emery is untouched" — compare RENDERED PIXELS, not hashes.** The firmware
  embeds a build stamp so `.bin` hashes always differ, AND QEMU's screendump gamma varies
  between captures so every pixel can differ while the page is identical. The real test:
  build a colour map from old→new and assert it's **bijective** (each old colour maps to
  exactly one new colour — a uniform brightness scale). That proves structure identical.
- **Never use `prv_draw_text` for round rows.** It measures into a 1000px-tall box and
  WRAPS; a long phone-supplied phrase spills a second line through the row below. Round's
  grid is fixed, so use explicit `graphics_draw_text` with a height-capped box +
  `GTextOverflowModeTrailingEllipsis`.
- **Check BOTH edges of every element against the chord at ITS OWN y.** The circle narrows
  downward — checking only an element's top row has bitten us twice.
- **`prv_draw_weather_background` paints a WHITE HALO 4px proud of the disc.** It exists so
  the hero icon can erase the rule mid-flight. Anywhere else it bites white gaps out of
  whatever it passes (it ate the dotted rule). Use plain `graphics_fill_circle` unless you
  specifically want the erase.
- **The PDC art can't be resized by changing its layer.** `gdraw_command_frame_draw` ignores
  `sequence->size` and draws at the native 75×75. The only way to scale is
  `gdraw_command_list_scale` on the command lists at init — legal only because the sequence
  is cloned into RAM (the original is mmap'd read-only from flash and writing faults on
  launch). It scales toward the origin, so `ROUND_PDC_INSET` re-centres it.
- **Animation constants must derive from `s_today_icon_size`, not literals.** A baked-in 50
  produced a 250px disc flash when round's icon became 75.
- **`layer_insert_below_sibling(x, NULL)` hard-faults** (no NULL guard in layer.c).
- **Draw order ≠ z-order.** Being last in `prv_render_layout` only puts you on top of the
  *content layer*. `current_weather_escape_layer` and `fin_layer` are later ROOT children
  and paint over it. This is why the (since-removed) city label needed its own root layer.
- **-Werror is on for both boards.** Un-gating a function while gating out its only caller
  = `-Wunused-function`; orphaning locals = `-Wunused-variable`; a buffer the same size as
  its snprintf source = `-Wformat-truncation`.

## Key round-only constants (all in `weather_app_layout.c`, near the top)

```
ROUND_TEXT_RAIL / ROUND_RAIL_MIN / _PIVOT / _K   left rail curve: rail = MIN + dy²/K,
                                                  solved at each row's INK TOP
ROUND_LABEL_Y / _W, ROUND_TEMP_Y, ROUND_HIGHLOW_Y, ROUND_PHRASE_Y / _W,
ROUND_DESC_Y / _W / _H                            fixed row grid (gaps even: 6,6,6,5,5)
ROUND_UV_BOX_Y / _H / _ARC_R_OUT / _ARC_R_IN      UV bar; ends are arcs concentric w/ glass
ROUND_UV_SUN_DY / _HDR_DY / _SQ_DY / _NUM_X       UV internals
DISC_RATIO_NUM = PBL_IF_ROUND_ELSE(111, 140)      hero disc, hundredths (75 → 83px)
ROUND_PDC_NATIVE 75 / ROUND_PDC_ART 65 / _INSET   hero art scaled down inside its layer
ROUND_FOOT_DISC 35                                footer icon disc (plain circle, no halo)
ROUND_FOOT_LABEL_DY / _TEMP_DY                    stacked footer lines
ROUND_CHIN_Y / _CHEV_W / _CHEV_Y / _CHEV_H        grey chin band + chevron
```

The hero disc at 1.11x is near its ceiling: bounded simultaneously by the glass, the day
label's tail (the halo would erase it), and the description's first ink row.

## Backup

`~/Downloads/weather-backup-2026-07-27-round-report-preFooter` — all 25 weather sources,
taken just before the footer was restacked and the city label removed.

⚠️ Disk is at 100% (~1.3 GB free). Check `df -h ~` before large builds.

## How I like to work

Show me the result in the emulator and let me judge it. When a change can't be done as
asked, say so and tell me why plus the options — don't silently do something adjacent.
Measure the render rather than asserting from the constants where you can, and tell me when
a measurement was unreliable.
