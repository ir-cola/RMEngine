# Attach to bone

A component that puts the entity on a **bone** of the model moved by the Animator of its parent (or one above it), every frame. Put a sword in a hand, a hat on a head, an effect or a camera on a bone. The attached thing stays an ordinary entity (it shows in the hierarchy).

## Adding it
1. Create an entity as a **child** of the entity with the Animator (the model with bones) by dragging it in the hierarchy
2. In the inspector: "+ Add component" → "3D" → "Attach to bone"
3. Click the "Bone" field and pick one of the model's bones

## Fields
| Field | What it does |
|---|---|
| Bone | The bone to attach to, picked from the bones of the parent's (or an ancestor's) Animator model |
| Position offset | Position seen from the bone (along its axes) |
| Rotation offset (degrees) | Rotation seen from the bone |
| Follow rotation | When off, only the position follows the bone (the facing stays as it is) |

- It never changes the scale (the bone's scale is not copied)
- It moves only during Play and in the exported game. In the stopped editor it stays where you put it, and ■ puts it back
- With no Animator above it, or a bone name the model lacks, the console says so once
- To only read a bone's place from a script, use `rm::Animator(self).BonePosition("bone name")` ([Animator](animator.md))
