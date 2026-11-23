# Saving the World

Save where things are and their state, and bring them back later. It goes into the same file (slot) as the saved global variables.

## Choosing what to keep
- Add the component "**Save State**" to the things you want to keep (Add Component ▸ Game). Things without it are not kept
- "Position, rotation and scale": the 3D Transform or Transform 2D values, and the parent
- "Active": whether it is active
- "All component values": the values of every component (scripts too) and the name, tag and layer. Components added during the game are added again
- Put it on a prefab and things made during the game (`rm::Spawn`) are kept too, and made again when loading

## Saving and loading
- From a script: `rm::Save::WriteWorld(1)` saves, `rm::Save::ReadWorld(1)` loads, and `rm::Save::HasWorld(1)` tells whether the slot has a world
- Loading happens after that frame. The saved scene is read again, then the saved state is put on top of it (as with switching scenes, scripts get EndPlay and BeginPlay)
- "Save State" things destroyed during the game stay destroyed after loading
- Where: Play in the editor uses `<project>/Library/Saves/`, an exported game uses `%LOCALAPPDATA%\<exe name>\Saves\`

## Not kept
- The order among siblings, physics velocities, particles and sounds part way through
- Components removed during the game (they come back when loading)
- A broken save does not crash the game. Parts it cannot read are skipped and written to the console
