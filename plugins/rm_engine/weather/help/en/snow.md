# Snow and snow cover

## Snow depth
- "Snow depth (cm)" is a whole number from 0 to 1000. The default is **10 cm** (deep enough to see footprints). 0 means no snow cover
- Above about 30 cm, steps and small things start to get buried

## How fast it piles up
- Set it with "**Snow speed (cm / min)**" (default 20). It is the speed at precipitation 100 and is proportional to how much snow falls: with 20 it grows 20 cm a minute at precipitation 100, 10 cm at 50, 2 cm at 10. It stops at the snow depth
- When it stops snowing it melts about 1 cm a minute, faster in rain
- "**Already lying at the start**" (on by default): the snow already lies at the full depth when Play starts, and the editor's scene view shows the same. Turn it off and, in Play, it piles up from bare ground as the snow falls
- With the "Climate" plugin it piles up **per place**: only where it is cold enough, and it melts where it rains (within 128 m around the camera. See the help of the "Weather × Climate" connector)

## Where it lies
- Snow with height lies on surfaces you can see from straight above (the top surface). It ends along the edge of each object
- Slanted snow also whitens the edges under roofs (no height). It fades out smoothly toward the inside

## Footprints (only in Play)
- Where moving things (characters, rigid bodies that are not "static") touch the snow, it is pressed down and stays that way
- While it snows, the dents slowly fill in at the same speed as the snow piles up
