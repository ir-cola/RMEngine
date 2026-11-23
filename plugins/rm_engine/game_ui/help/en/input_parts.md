# Input parts

Parts for typing text and choosing values on the game screen: the **input field**, **slider**, **toggle** and **scroll view**.

## Making them
Select a canvas and right-click → "Add" → "UI" → "Input field", "Slider", "Toggle" or "Scroll view".

The input field, slider and toggle are selected through the "Button (UI)" on the same entity (hover and press looks, key and pad selection, where to move next, sounds, interactable). When you add only the part, add a button too.

## Input field
| Action | What it does |
|---|---|
| Click / select and submit | Start typing (the caret blinks) |
| Japanese input (IME) | Text being converted is underlined inside the field; candidates appear below it |
| ← → Home End | Move the caret (Shift selects, Ctrl moves by word) |
| Ctrl+A / C / X / V | Select all / copy / cut / paste |
| Enter | Confirm ("UISubmitted" to scripts) |
| Esc / pad B | Go back to the text from before typing |
| Click elsewhere | Stop typing |

The size, color, font and alignment of the text come from the "Text (UI)" on the same entity. While typing, arrows and Enter do not select or press buttons.

- **Allowed characters**: integer, number (with decimals), letters and digits (full-width letters and digits become half-width)
- **Max characters**: each visible character counts as 1
- **Password**: hidden with ●, and cannot be copied
- One line only (line breaks become spaces)

## Slider
Click or drag the handle or the track to change the value. While it is selected, ← → (↑ ↓ for a vertical slider) move it, and the other directions move to other buttons. With a "Value step" of 1 it takes whole numbers only.

## Toggle
Each press switches it on and off. The look is a "Check box" or a "Switch". Toggles with the same "Group" name have only one on in a window (radio buttons).

## Scroll view
Clips its children to its frame and scrolls with the wheel, by dragging an empty part of the content, or with the scrollbar. Selecting a button inside with keys or a pad scrolls until it is visible. Putting a tall "Content" inside and lining things up in it is the easy way. In the UI view, things outside the frame are shown faintly.

## Scripts
When the value changes, the scripts on that entity get "UIChanged" in OnEvent, and "UISubmitted" when Enter is pressed in an input field. For the functions that read and set values, see the comments in `RMUI.h`.
