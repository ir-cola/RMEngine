# Tilemap

Build the ground, walls and background of a 2D game by **laying small square pictures (tiles) on a grid**. Prepare one image cut into tiles of the same size (a tileset), then paint with it in the Scene view like a brush.

## How to make one

1. Right-click in the Assets panel and choose Create ▸ Tileset. A `.rmtileset` file appears
2. Right-click in the Hierarchy and choose Add ▸ 2D Object ▸ Tilemap. You get the grid "Tilemap" with a layer "Ground" below it
3. Select "Ground" and put the `.rmtileset` from step 1 in the Inspector's Tileset field
4. In the **Tiles** tab (next to the Scene and Game tabs; if it isn't there, open it from the Window menu), go to the Tileset page, press Add source image, and set the image with the tiles laid out, plus the tile width, height, margin and spacing
5. On the Palette page, click a tile (drag over a rectangle for a stamp), then paint in the Scene view

## Components

| Component | What it does |
|---|---|
| Tile grid | The cell size (1 x 1 unit by default = 1 m in physics). Every layer below follows this grid. Moving or turning it with its Transform 2D takes the layers along |
| Tile layer | The tileset, the order (in front or behind, in the same line as sprites' order) and a color. One layer = one entity, so reordering in the Hierarchy, on / off and Prefabs work as they are |

## Tools in the Scene view

Select a tile layer and the tools appear in the bar at the Scene view's top left.

| Tool | Key | What it does |
|---|---|---|
| Brush | B | Click or drag to paint. Shift to erase, Alt to pick |
| Rectangle | U | Fill the dragged rectangle. Shift for just the outline |
| Eraser | | Click or drag to empty cells |
| Picker | I | Take the tile in a cell. Drag over a rectangle to take a stamp |
| (back to Move) | W / Esc | |

> One drag is one Undo step (Ctrl+Z). You can't paint while playing.

## Colliders, tags and custom data

On the Tiles tab, go to Tileset ▸ Tiles and paint collision shapes over the picture (the shape bar: Select, None, All, Top, Bottom, Left, Right, four slopes, Draw). Select a tile with Select to get its name, collider, tags and custom data fields. Add custom data fields (a name and a type) on the Custom data fields page.

With the Physics plugin, layers with Make colliders on collide in physics (connected colliders are merged into large boxes). The Scene view's Shapes: Selected / All switch shows their outlines.

## Saving and exporting

The painted cells go in the scene file as text per 32 x 32 chunk (a run of the same tile is written "value*count"). Chunks with nothing painted don't appear in the file. An exported game also gets the tileset and its images.
