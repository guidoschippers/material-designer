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
