# On-device checklist for the Xteink X3

Run each section on a physical X3 with the fork's firmware flashed. Record the
result and the firmware version next to each item. Nothing here has been run on
hardware yet: "build-verified" is not "hardware-verified".

## Input, zoom and menu (t14)

Settings used: **Settings > Controls**. Defaults: Zoom Button (Reader) = Up,
Side Button Layout (Reader) = Prev/Next.

### Key map (expected)

| Logical button | Reader | Zoom mode | Reader menu |
|----------------|--------|-----------|-------------|
| Left (front) | previous page | smaller text | previous row |
| Right (front) | next page | larger text | next row |
| Confirm (front) | open reader menu | apply the size and leave zoom | select |
| Back (front) | back / leave book (existing behaviour) | cancel, keep the old size | close menu |
| Zoom key (side, default Up) | enter zoom mode | apply the size and leave zoom | n/a |
| Other side key (Down) | next page (Prev/Next layout) | larger text | next row |

### Checks

1. Verify which physical key is "top" on the X3 and that it is the one the
   firmware calls Up. If it is not, set **Zoom Button (Reader)** to Down in
   Settings > Controls and note the result here. No rebuild should be needed.
2. In a book, Left/Right turn pages back/forward. Confirm opens the reader
   menu. Back leaves the reader as before.
3. Press the zoom key: a panel appears at the bottom of the page listing the
   available point sizes with the current one highlighted. The page text
   behind the panel does not change yet.
4. With the built-in font, the panel shows 12 14 16 18. With a vector SD font it
   shows 8 to 22. With an installed `.cpfont` family it shows only the sizes
   that family ships.
5. Right moves the highlight to a larger size and Left to a smaller one, one
   step per press, stopping at the ends. Each press redraws only the panel (no
   reflow, no long wait).
6. Press the zoom key again (or Confirm): the book reflows once at the chosen
   size. The same passage is on screen (compare the first words of the page
   before entering zoom and after leaving it). Repeat at the start, middle and
   end of a chapter and across a chapter boundary.
7. Enter zoom, change the size, press Back: the panel closes, the size is the
   one from before and the page is unchanged (no reflow).
8. Enter zoom and leave without changing the size: no reflow happens.
9. Zoom in then zoom out again (e.g. 14 to 18 and back to 14): the reading
   position is still the same passage.
10. After a commit, the new size is what Settings > Reader Font Size shows, and
    survives a power cycle.
11. Rotate the screen (inverted or landscape) and repeat 3 to 6: Right still
    means larger text on the side of the screen the page-forward key is on.
12. The zoom key no longer turns pages. In Settings, set Zoom Button to Off:
    both side keys turn pages again and the zoom key does nothing.
13. Reader menu (Confirm): **Select Chapter** opens the chapter list and jumps
    there. **Go to %** opens the percent dialog (front Left/Right = 1%, side
    keys = 10%) and jumps. **Zoom** starts zoom mode.
14. Toolbar menu style (Settings > Reader > Reader Menu Style = Toolbar): the
    More panel lists **Zoom** and it starts zoom mode.
15. Check for e-ink ghosting around the zoom panel after several steps and after
    closing it. A grayscale text page may need a full refresh to look clean;
    note whether the firmware does that on its own.
16. Zoom mode does nothing in an XTC book (pre-rendered pages cannot reflow).
