# Materials

A material is the **look** of a 3D thing (color, pictures, metallic, roughness, bumps, emission, transparency) kept in one file (`.rmmat`). Many things can use the same material, and changing the file changes all of them.

## Making one
Right-click in the Assets panel → "Create" → "Material". With nothing selected it goes in `Assets/Materials`; with a folder selected it goes in that folder. "New Material.rmmat" appears and you can type its name right away.

## Editing
Select the material in the Assets panel and its fields appear in the Inspector. Changes show in the scene view at once and Ctrl+Z undoes them (the file is written when you finish editing).

| Field | What it does |
|---|---|
| Color | The surface color. It looks like the color you pick. With a color picture it multiplies the picture |
| Color Picture | The image put on the surface. Drag it from the Assets panel, or click to pick one |
| Metallic / Roughness | 0 to 1. Metal reflects its surroundings; lower roughness is smoother |
| Metallic / Roughness Picture | Blue is metallic, green is roughness (as in glTF) |
| Normal Picture / Normal Strength | Fine bumps (a normal map, green is up) |
| Emission Color / Strength / Picture | A color that glows by itself. Strength above 1 glows brighter |
| Transparency | Opaque / Cutout (a hole where alpha is below the threshold) / Transparent (see-through) |
| Picture Tiling / Offset | How many times the picture repeats and how far it shifts. 10, 10 on the 10 x 10 plane gives one picture per meter |

## Putting it on a thing
Drag it from the Assets panel into the "**Material**" slots of the thing's "Mesh (3D model)" part (you can also add a slot with + and click to pick).

- A shape (box, sphere, cylinder, capsule, plane) is painted with the **first** slot. Without slots it stays the usual gray
- For a model, the **i-th** slot replaces the model's i-th material. An empty slot keeps the model's own. A model without materials (only an OBJ, for example) can be painted with the first slot too

> An exported game contains the materials it uses and their pictures (the pictures are baked into GPU form, like model pictures).
