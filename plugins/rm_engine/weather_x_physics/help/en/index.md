# Things that leave footprints in snow

A connector plugin that comes along when both Weather and Physics are installed.

- Rain and snow hit the **meshes you can see** (shapes and models). Colliders do not matter: places with only an invisible collider let them through
- This plugin tells Weather which things **move**: **characters** and **rigid bodies** that are not static. Those things (and the meshes of their children) leave footprints in lying snow
