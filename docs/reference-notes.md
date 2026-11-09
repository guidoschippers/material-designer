# Reference notes

Observations from reference photos supplied by the project owner. They guide the look of the
built-in materials. (The photos themselves are not stored in the repository.)

## Asphalt

- Exposed-aggregate road surface: densely packed, **rounded** gravel (river gravel, not angular
  crushed stone), roughly 4-8 mm; very little black binder visible between the stones.
- Multicoloured stones: mostly grey, plus beige/tan, white quartz, light brown, a few reddish and
  dark grey ones. Small grit and sand between the larger stones.
- Cracks: a single long **meandering** crack (~1 cm wide) across the surface, with crumbled edges
  where stones have broken out. Not a polygonal network.
- Edge against a kerb: a darker, binder-rich band where stones have come loose (crumbling edge).
  Implemented as the `asphalt_edge` overlay (decided: overlays, not baked into the base texture).

## Concrete

- Light warm grey, broom-finished or lightly brushed surface: subtle parallel striations in one
  direction.
- Sparse exposed fine aggregate (~2-5 mm, perhaps 10-20 % coverage): orange/brown/tan, dark grey and
  white specks. Small dark pores/pits.
- Joints: thin, straight saw-cut lines, dark, with dirt and a bit of moss in them.
- Slight large-scale tonal variation (cloudy).

## Road markings (for asphalt and concrete)

- White paint, rounded corners, slightly worn: the paint follows the surface texture, is thinner
  on stone tops and partly worn away in places, showing the surface underneath.
- Implemented as the `road_marking` overlay.

## Grass (from the grass next to the concrete)

- Mixed lawn: bright yellow-green blades on top, darker green deeper in the turf, dark shadows at
  the base.
- Clumping; blades in varying directions (not combed).
- Some dry straw-coloured blades mixed in, a few broadleaf weeds/clover, occasional fallen brown leaves.

## Wood

Two bases, for floors and beams:

1. **Oak, stained honey/orange**: fine straight grain with prominent elongated pores and ray flecks;
   pores filled with whitish residue (limed / worn finish). Fine scratches and a dented, chipped spot.
2. **Pine planks**: light pine with pronounced annual rings (narrow dark orange-brown latewood lines),
   cathedral patterns, small dark knots with rings bending around them. Planks of different widths
   with tight joints and slight colour differences between planks.
3. **Weathered, stained board**: one wide board with a dark grey-brown stain that has eroded in
   streaks along the grain, exposing lighter tan wood; a blue-grey haze where the finish is thin.
   Coarse, fibrous grain: ragged latewood lines broken into short dark streaks, tall narrow cathedral
   arches, a faint knot, raised grain, a few white scratches and chalky spots.
   Parameters: `stain` + `stain_color`, `fibrous`, `ring_irregularity`, `figure` and the wear
   parameter `weathering`. Example: `--set plank_width=1 --set plank_length=1 --set plank_gap=0
   --set grain_scale=0.7 --set ring_density=0.35 --set figure=1 --set fibrous=0.7 --set stain=0.9
   --set ring_contrast=0.7 --set gloss=0.2 --set weathering=0.5 --set scratches=0.15 --wear 1`.

## Cardboard

For the moving box in the attic of the Racecars main menu (no photo; described by the owner):
the outside of an old brown corrugated shipping box, single-wall board with a kraft liner.

- Kraft brown liner, slightly mottled, with fine light and dark paper fibres (lying mostly across
  the flutes, the paper's machine direction), a few dark woody shives and small recycled specks.
- The flutes faintly show through the liner as subtle parallel ridges, only in relief (normal map),
  hardly in colour. **The flutes run along V**: each ridge is a vertical line of constant u, repeating
  across U (`flute_spacing`, rounded so the texture tiles).
- Matte paper roughness with slight variation.
- Wear of a box that has spent years in an attic: abraded scuffs (lighter, fuzzy), fold creases
  (groove, crushed flutes, a light cracked line) and soft dents, water stains with darker tide-mark
  rings, greyish dust collecting in low areas, faint yellowed tape residue with torn paper.
  Example for an old, worn moving box: `--seed 1998 --wear 0.8 --set scuffs=0.6 --set creases=0.6
  --set dents=0.4 --set water_stains=0.5 --set dirt=0.6 --set tape_residue=0.3`.

## Plastic

For the late-90s electronics in the attic (a PS1-style console, a VCR, a game controller); no
photo, described by the owner.

- Moulded ABS / polystyrene in one uniform colour: light warm grey for the console, near-black for
  the VCR, dark grey for the controller.
- Housings mostly have the fine spark-eroded (EDM) matte texture of the mould: densely overlapping
  shallow craters, visible in the normal map and as a matte roughness (`texture`, `texture_scale`,
  `texture_depth`, `roughness_min` / `roughness_max`). Glossy parts use `texture` 0.
- Optional moulding defects: faint wavy flow lines (gloss bands) and soft sink marks.
- Wear: fine scratches and scuffed patches (stress-whitened, lighter on dark plastic), areas polished
  glossy by hands, a light grey dust film, dark grime in the texture, UV yellowing of light plastic
  (`yellowing`, `yellowing_tint`) and small dings with a whitened rim.
  Examples: grey console `--set yellowing=0.5 --wear 0.6`; black VCR `--set color=#1c1c1e --set dust=0.8
  --wear 0.7`; controller `--set color=#3c3d40 --set hand_polish=0.8 --wear 0.8`.

## CRT glass

For the old TV in the attic; no photo, described by the owner. The glass layer of the screen, seen
switched off or over a picture that comes from elsewhere.

- Near-black, slightly grey-green, very smooth glass: the roughness and normal maps matter most, the
  albedo is dark. An optional anti-glare coating makes it matte with a fine micro-texture.
- Optionally a faint phosphor pattern behind the glass: a delta shadow mask (RGB dots, most TVs) or an
  aperture grille (RGB stripes, Trinitron), with `mask_pitch` as the distance between phosphors of the
  same colour. Below about two pixels per phosphor the pattern fades out instead of aliasing.
- Wear: a light dust film with specks (the film keeps a blurred reflection, specks are fully matte),
  greasy fingerprints with whorl and loop ridges and smeared smudges (roughness blotches), fine
  circular wipe streaks from a cloth.
- No opacity map: base materials are opaque in this tool. For a transparent glass layer over a
  picture, use a constant alpha in the shader (or the wear mask, which marks the dust and grease).
  Example: `--wear 0.8 --set dust=0.6 --set fingerprints=0.45 --set wipe_streaks=0.3`.
