# Joose Clip app icon

**Mark:** a lowercase "j" — thick white stem with a sweeping hook — with a blue tittle,
plus a column of muted film-sprocket dots on the left edge, on a dark navy rounded tile.
Drawn from scratch for the Joose Clip fork (no FilmCraft/ArtCraft artwork used as reference).

## Palette

| Colour | Hex | Used for |
|---|---|---|
| Ink navy (icon field) | `#182747` | the full-bleed rounded tile |
| Paper white | `#eef2f7` | the j glyph |
| Joose blue (accent) | `#4da3ff` | the tittle |
| Sprocket slate | `#7d8db0` | film-strip dots |

## Geometry

- 512-unit viewBox tile, rounded square `rx=112`, field colour full bleed.
- The j sits right of center; the sprocket dots balance it on the left.
- macOS renders use Apple's grid (824 of 1024 px, transparent margin); other targets use the
  full-bleed tile.

## Files

| File | What |
|---|---|
| `joose-clip.svg` | canonical artwork |
| `joose-clip-small.svg` | lighter single-path variant |
| `joose-clip-1024.png` | 1024 px render |
| `joose-clip-macos-512.png` | runtime Dock icon on macOS (embedded by `apps/filmcraft/src/main.rs`) |
| `joose-clip.ico` | Windows icon, 16–256 px (embedded by `apps/filmcraft/build.rs`) |
| `hicolor/<size>/apps/com.jooselabs.jooseclip.png`, `hicolor/scalable/…svg` | Linux icon theme |

The web app's `apps/filmcraft-web/web/favicon.png` (128 px) comes from the same artwork.

## Regenerate

Renders were made with cairosvg from `joose-clip.svg`; the `.ico` was packed with
`cargo xtask ico`. Every derived file keeps its `.attribution` sidecar and a row in
`ATTRIBUTION.md`.
