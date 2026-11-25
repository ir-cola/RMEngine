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

Changing "Clip" in the inspector during Play starts that clip from its beginning.

## Driving it from a script
```cpp
#include "RMAnim.h"
rm::Animator anim(self);
anim.PlayClip("Run", 0.2f);   // Run from the start, moving from the current pose over 0.2 s (crossfade)
anim.Pause();  anim.Resume();  anim.SetSpeed(1.5f);
if (anim.Ended()) anim.PlayClip("Idle", 0.3f);   // a clip that does not loop has finished
rm::Vec3 hand = anim.BonePosition("RightHand");    // the bone's world position (BoneRotation for its rotation)
```
`PlayClip` and `SetSpeed` write the component's fields (you see them in the inspector during Play).

## Clip events
Select the model in the asset panel and place events (time, name, number, text) on a clip's strip in the inspector's "Clips and events". When playback passes that time, the event reaches the entity's script `OnEvent` (sent by the engine).
```cpp
void OnEvent(rm::Entity self, const rm::Event& e) { if (e.Is("Footstep")) { /* e.number, e.text */ } }
```
It counts across the loop seam and when playing backwards. The exported game has them too.

## Attaching to bones
To make a sword or a hat follow a bone, add the "Attach to bone" component to a child entity ([Attach to bone](bone_attach.md)).

> Not yet: the animation tab and its rules (stage 3 of docs/ANIMATION.md), blending and layers (stage 4).
