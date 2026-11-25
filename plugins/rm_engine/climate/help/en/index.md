# Climate

This plugin lets one scene have cold, warm, humid and dry places. It works in both 2D and 3D games. Scripts can read the temperature and humidity at any place (to lose health in the cold, show white breath, and so on).

> Together with the Weather plugin, the connector "Weather × Climate" comes in by itself: snow falls in cold places and rain in warm ones (Weather → Weather × Climate page).

## The scene setting "Climate"

Select the top row of the hierarchy (the scene) to see it in the inspector. It is the climate **outside the climate zones**.

| Value | What it does |
|---|---|
| Temperature (°C) | The temperature outside zones. Default 15 |
| Humidity (%) | The humidity outside zones. 0 bone dry, 50 normal, 100 muggy |
| Cooling with height (°C / 100 m) | 3D only. The higher up, the colder (0.6 is about real air), so only mountain tops get cold |
| Base height (m) | 3D only. The temperature above applies at this height |

## Climate zones

Add the component "**Climate zone**" (3D) or "**Climate zone 2D**" to an entity (Add component → Climate). It follows the entity's Transform position, rotation and scale.

| Value | What it does |
|---|---|
| Shape | 3D: box / cylinder (standing up). 2D: rectangle / circle |
| Size / Extent | The size of the shape, multiplied by the Transform scale |
| Border width | Over this width inside the edge, the climate changes **gradually** from the outside to this zone's. 0 switches at the edge |
| Set temperature, Temperature | Off: the temperature is left as it is (for a zone that only changes humidity) |
| Set humidity, Humidity | Off: the humidity is left as it is |
| Priority | Where zones overlap, the higher one goes on top. Put a colder peak (priority 1) inside a cold region (priority 0). On a tie, the one lower in the hierarchy wins |

- A zone that is switched off (itself or a parent) has no effect
- 3D zones affect 3D places only, 2D zones 2D places only
- The scene view's "Shapes" switch (selected / all) shows each zone's **edge** (strong line) and the **inner edge of the border** (faint line). Inside the faint line the climate is the zone's own

## The Climate tab: paint on a map (3D)

The "**Climate**" tab, next to the Scene and Game tabs, shows the scene **seen from straight above** as a map, and you can **paint climates on it with a pen**. Where you paint gets that climate (no zones needed).

- Switch with "**Climate**" / "**Weather**" at the top. "Fit" brings the whole scene into the map
- The map: right is +X, up is +Z. **Left drag** paints, the **wheel** zooms, **right / middle drag** moves. One drag is one Ctrl+Z
- Tool buttons at the top left of the map (the same as the Scene view's left column): "**Brush**" (paint with a round brush), "**Eraser**" (erase brush paint), "**Pen**" (draw around an area) and "**Select**" (fix drawn shapes). The same tools work under "Climate" and "Weather". On the right: the brush radius and, under "Climate", the **cell size** (1 to 16 m. Smaller paints finer shapes. Changing it keeps what you painted in place)
- **Pen**: each click adds a point, and dragging with the button held traces points. Click the first point, double-click or press Enter to close it, and the inside takes that climate (Esc cancels). Shapes go on top of brush paint, later shapes on top
- **Select**: click a shape to select it, drag a point or the inside to move, right-click a point to delete it, Delete removes the shape. With a shape selected, clicking a climate type changes the shape's climate
- **Two kinds of rain**: to make it rain **over the whole scene**, raise "Precipitation" in the scene's inspector (or in "Weather of the whole scene" under "Weather"). To make it rain **only in some places**, pick the "**Rain clouds**" layer under "**Weather**", set the rain amount (%) and **draw around the area with the pen** (the brush works too). Only there, that much falls even if the scene's precipitation is 0, and the sky gets thick clouds just above. The temperature there decides rain or snow
- The "**Clouds**" layer under "Weather" draws the cloud amount. It changes the sky only and does not make rain (places without drawn clouds keep the clouds of the scene weather)
- "**Show**" at the top: "Paint" (drawn types, clouds and rain clouds) / "Temperature" / "Humidity" / "Clouds" / "Rain" (the amount from drawn rain clouds). Temperature, humidity, clouds and rain show **the value at each place as a color scale**, with everything drawn, shapes, climate zones and borders included (a scale at the bottom right; the temperature is at the base height). The value under the cursor is shown too
- **Climate types**: at first Temperate (15 °C, 50 %), Cold (-8 °C, 60 %), Desert (32 °C, 10 %) and Tropical (28 °C, 85 %). Pick one and paint. "+ Add" makes more, and the fields below change the name, color, temperature and humidity. "Delete" removes the type and what was painted with it. The color is only for the map and does not show in the game
- **Map border width**: across the edge of what you painted, the climate changes smoothly over this width (default 8 m)
- Every place you did not draw keeps the scene settings "Climate" and "Weather" from the inspector. What you drew wins where you drew it. **Climate zones go on top of the drawn climate**. "Cooling with height" applies to drawn places too (snow only on the mountain top)
- The edges of 3D climate zones are drawn on the map too
- Switch to "**Weather**" to draw clouds and rain clouds on the map. "**Weather of the whole scene**" at the bottom right (clouds, precipitation, fog, lightning, wind, templates) can be changed here too (with the Weather plugin; the same as the scene in the inspector). Whether it rains or snows depends on the temperature there (so the weather's "Snow" switch is not shown while Climate is installed)
- The map is for 3D scenes only for now. 2D scenes use "Climate zone 2D"

## Scripts

```
#include "RMClimate.h"

float t = rm::Climate::TemperatureAt(rm::Transform3D(self).WorldPosition());  // temperature (°C) where it stands
float h = rm::Climate::HumidityAt2D({x, y});            // humidity (%) at a 2D place
rm::Climate::SetTemperature(-10.0f);                     // change the temperature outside zones (at once)
```

To read: `TemperatureAt` / `HumidityAt` (3D), `TemperatureAt2D` / `HumidityAt2D` (2D), `Temperature()` / `Humidity()` / `Lapse()` (the scene setting). To change: `SetTemperature` / `SetHumidity` / `SetLapse`.
