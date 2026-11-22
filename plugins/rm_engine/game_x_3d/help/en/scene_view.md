# Scene view (3D)

The panel where you look at a 3D scene and select and move things. Coordinates are **Y-up, left-handed** (right, up and forward are +).

## Moving the camera
| Input | What it does |
|---|---|
| Right drag | Look around. While held, W A S D move forward / left / back / right, Space or E up, Ctrl or Q down. Shift is faster |
| Middle drag | Pan |
| Wheel | Move closer / farther |
| Alt + left drag | Orbit around the selection |
| F | Frame the selection |

## Tools (the column on the left)
| Button | Key | What it does |
|---|---|---|
| Grid | | Click to turn on / off. While on, the gizmo always moves in grid steps (Ctrl for smooth). Right-click to set the steps (move, rotate, scale) |
| Select | Q | Only select by clicking, no gizmo |
| Move | W | Drag the arrows to move |
| Rotate | E | Drag the rings to rotate. Drag inside the sphere to rotate freely |
| Scale | R | Drag the boxes to resize |

## Also
- Click: select. With Ctrl: add / remove
- X: local / world, Z: each / center
- With Grid off, hold Ctrl while dragging the gizmo to move in grid steps
- Esc while dragging: cancel and put things back

> Entities with a Transform show up here as markers. Add a mesh (3D model) component to draw its shape or model.
