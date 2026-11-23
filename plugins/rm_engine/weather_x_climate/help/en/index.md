# Weather × Climate

A connector that comes in by itself when both "Weather" and "Climate" are installed. **The temperature of each place decides between rain and snow**.

- **+2 °C or warmer** rains, **-1 °C or colder** snows. In between, raindrops and snowflakes fall together (sleet)
- **Humidity** changes how much falls. 50 % as it is, 0 % nothing (a desert), 100 % one and a half times
- Each raindrop and snowflake decides by where it is, so across a climate zone's border the rain turns into snow little by little right in front of you
- Fog colour (in Play) follows what falls **where the camera is**, changing over about 2 seconds after crossing a border
- **Wetness and lying snow are kept per place** (in Play). Snow piles up only where it snows, only rainy places get wet, and rain melts the snow. Across a border the lying snow thins out smoothly. Places are split within 128 m around the camera (a 2 m grid); farther away looks the same as the nearest place
- The weather's "**Snow**" checkbox is not used (the temperature decides). How stormy the sky is (clouds, precipitation, wind, lightning) is set in Weather as before

## Try it

1. In a 3D scene, set the weather's precipitation to about 60 (the scene is 15 °C: rain)
2. Add a "Climate zone" to an empty entity: box, size 40, border width 10, temperature -5 °C
3. Set the Shapes switch to "all" to see the outline and move the camera into the zone: snow starts mixing in around the faint line, and in the middle only snow falls
