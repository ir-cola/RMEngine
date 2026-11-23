# Snow and snow cover

## Snow depth
- "Snow depth (cm)" is a whole number from 0 to 1000. The default is **10 cm** (deep enough to see footprints). 0 means no snow cover
- Above about 30 cm, steps and small things start to get buried

## How fast it piles up
- **It depends on how much snow falls.** At precipitation 100 it grows 2.5 cm a minute, at 50 1.25 cm, at 10 0.25 cm. It stops at the snow depth
- When it stops snowing it melts about 1 cm a minute, faster in rain
- In the editor (before Play and after Stop) nothing has piled up. Press Play and it piles up as the snow falls
- With the "Climate" plugin it piles up **per place**: only where it is cold enough, and it melts where it rains (within 128 m around the camera. See the help of the "Weather × Climate" connector)

## Where it lies
- Snow with height lies on surfaces you can see from straight above (the top surface). It ends along the edge of each object
- Slanted snow also whitens the edges under roofs (no height). It fades out smoothly toward the inside

## Footprints (only in Play)
- Where moving things (characters, rigid bodies that are not "static") touch the snow, it is pressed down and stays that way
- While it snows, the dents slowly fill in at the same speed as the snow piles up
