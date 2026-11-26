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

## States and rules (the Animation tab)
Select an animator, open the "Animation" tab and press "Create states and rules". (The Animator's "States and rules" field then points at the new .rmanim file.)

- **Parameters**: values your script sets (number, integer, bool, trigger)
- **States**: name, clip, loop, speed, exit (Free / Own only: only rows whose From is this state, and ⚡ rows / Until end: own rows until the clip ends)
- **Rules**: checked **from the top** every frame; the **first row** whose From (empty: anywhere) matches and whose condition is true moves to its To state with a fade. Put "(always) → Idle" at the bottom to idle when nothing else matches
- Conditions are written like C++: `speed > 0.1`, `jump`, `grounded && !attack`, `t > 0.5` (progress in the current state), `ended` (a one-shot clip finished / a loop went around), `abs` `min` `max`
- During Play the current state and the matching row light up, and you can try parameter values in the "Now" fields

```cpp
rm::Animator anim(self);
anim.SetFloat("speed", 2.0f);   anim.SetBool("grounded", true);   anim.SetTrigger("jump");
std::string s = anim.State();   anim.Play("Attack", 0.1f);   // skip the rules and go to that state
```

## Blending and layers
- **Blending**: choose a state's type: [Clip | 1D | 2D]. 1D uses one parameter (such as `speed`), 2D uses two (`moveX`, `moveY`). Place points (clips) on the diagram under the state and drag them around, or type clip, x and y in the table. Move the ▼ / + marker to see the weights at that value. Blended clips keep their feet in step
- **Layers**: "+ Layer" adds a layer tab. A layer has its own states and rules and stacks on top of the body. Set its weight (a number or a parameter), mode (override / additive) and mask (a bone and the bones below it)
