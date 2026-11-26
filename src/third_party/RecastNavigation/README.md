# Recast Navigation (third_party)

- Source: https://github.com/recastnavigation/recastnavigation
- Version: v1.6.0 (tag `v1.6.0`, 2023-05-21)
- License: zlib (`License.txt`, kept unchanged next to the code)
- Taken: `Recast/`, `Detour/`, `DetourCrowd/`, `DetourTileCache/` (`Include/` and `Source/` only). The demo app, tests and `DebugUtils` are not included.
- The files are not changed. Built as one static library: `src/RMRecast.vcxproj` (warnings off for these files only).
- `DT_POLYREF64` is defined for every project (`src/RM.Common.props`).
- Wrapped by the plugin `plugins/rm_engine/navigation` (the Recast / Detour types appear only in its `.cpp` files).
