# Animator

A component that moves a 3D model with bones (the bones, skin and animations of glTF / FBX) using one of the model's **clips**. It moves the model of the same entity's Mesh (3D model).

## Adding one
1. Put a model with bones and animations (`.gltf` / `.glb` / `.fbx`) into the Assets panel
2. Put the model into the Model field of the entity's Mesh (3D model)
3. In the Inspector: "+ Add component" → "3D" → "Animator"

## Fields
| Field | What it does |
|---|---|
| Clip | The clip to play. Click it to pick one of the model's clips (name and length). "(first clip)" is the model's first one. A name the model does not have shows "(missing)" and the model is drawn in its bind pose (the shape before the bones move it) |
| Loop | When off, it plays to the end and stops |
| Speed | How fast it plays. 2 is twice as fast; a negative number plays backwards |
| Play on start | When off, it stays on the first frame in the game |

## Watching it in the scene view (no Play needed)
Select an entity with an Animator: its clip plays in the scene view and controls appear at the bottom left of the picture.

- ▶ / ‖ plays and pauses. A clip that does not loop stops at its end; ▶ starts it again
- Dragging the time slider stops it at that moment's pose
- Things you have not selected stay on the first frame of their clip

## Play and the exported game
Every Animator with "Play on start" plays. The exported game gets the bones and clips together with the model.

> Not yet: switching clips with a crossfade (the script's `RMAnim.h`), events, attaching to bones, the animation tab and its rules (stages 2 and 3 of docs/ANIMATION.md).
