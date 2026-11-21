# Things rain and snow hit

A connector plugin that comes along when both Weather and Physics are installed.

- Rain and snow hit physics **colliders** (box, sphere, capsule, mesh) and **character** bodies. Triggers let them through
- Things without a collider (a mesh that is only for looks) let them through
- Rain splashes into a small ring where it lands; snow sticks for a moment and then melts
- Only colliders within 80 m of the camera are considered
