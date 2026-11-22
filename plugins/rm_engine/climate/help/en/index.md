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

## Scripts

```
#include "RMClimate.h"

float t = rm::Climate::TemperatureAt(rm::Transform3D(self).WorldPosition());  // temperature (°C) where it stands
float h = rm::Climate::HumidityAt2D({x, y});            // humidity (%) at a 2D place
rm::Climate::SetTemperature(-10.0f);                     // change the temperature outside zones (at once)
```

To read: `TemperatureAt` / `HumidityAt` (3D), `TemperatureAt2D` / `HumidityAt2D` (2D), `Temperature()` / `Humidity()` / `Lapse()` (the scene setting). To change: `SetTemperature` / `SetHumidity` / `SetLapse`.
