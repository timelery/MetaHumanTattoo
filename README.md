# MetaHuman Tattoo

Tattoos for MetaHuman Characters in Unreal Engine 5.8. Load an SVG design, click on the character in the **MetaHuman Character editor** to place it, scale and turn it, pick black or colored ink, and bake it into the character's skin.

<p>
  <img src="Examples/NauticalStar.svg" width="140" alt="Nautical star example">
  &nbsp;&nbsp;
  <img src="Examples/Heart.svg" width="140" alt="Heart example">
</p>

*The two example designs in [`Examples/`](Examples): a one-color star and a two-color heart.*

- **Any SVG.** Filled shapes and strokes, in one ink color (black-work) or in the SVG's own colors.
- **Place it on the character.** Click on the skin in the editor's viewport, drag to slide it, Ctrl + mouse wheel to scale, Shift + mouse wheel to turn.
- **Live preview.** The viewport shows the tattoo while you place and tune it.
- **Part of the skin.** *Apply to Skin* bakes the tattoos into the character's skin color textures. Assembly, LODs and lighting then treat the ink as skin, and skin detail (pores, freckles, skin tone) shows through it like a healed tattoo.
- **Non-destructive.** Your original textures are kept. Change, hide or remove tattoos at any time and apply again.

## Requirements

- **Unreal Engine 5.8** (tested on 5.8.3, Windows) with Epic's **MetaHuman Character** plugin, which ships with the engine. Not tested on other engine versions.
- **No compiler** for the prebuilt UE 5.8 Windows download. To build from source, a C++ toolchain (Visual Studio 2022 with the *Game development with C++* workload).
- **A character whose skin uses texture overrides.** Its *Texture & Material Overrides* need **Face Base Color** and **Body Base Color** textures, as characters with custom skin textures have. The skin textures MetaHuman Creator generates for the body hold skin-tone data rather than color, so ink cannot be drawn into them. The plugin tells you when a character cannot take tattoos; see [What to know](#what-to-know).

## Install

### Prebuilt (UE 5.8 on Windows, no compiler needed)

1. Close the Unreal Editor.
2. Download **`MetaHumanTattoo-1.0.0-UE5.8-Win64.zip`** from the [latest release](https://github.com/timelery/MetaHumanTattoo/releases/latest).
3. Unzip it into your project's `Plugins` folder, so that you have `<YourProject>/Plugins/MetaHumanTattoo/MetaHumanTattoo.uplugin`. Create the `Plugins` folder if the project has none.
4. Open the project and check that **Edit > Plugins > MetaHuman Tattoo** is enabled (it is by default).

### From source

1. Close the Unreal Editor.
2. Put the plugin in your project's `Plugins` folder, in a folder named `MetaHumanTattoo`:

   ```
   cd <YourProject>/Plugins
   git clone https://github.com/timelery/MetaHumanTattoo.git MetaHumanTattoo
   ```

   Or, on GitHub, use **Code > Download ZIP**, unzip it into `<YourProject>/Plugins`, and rename the folder to `MetaHumanTattoo`.
3. Open the project. When Unreal says the *MetaHumanTattooEditor* module is missing, click **Yes** to build it. You can also build the project from your IDE.
4. Check that **Edit > Plugins > MetaHuman Tattoo** is enabled (it is by default).

If your project is Blueprint-only and the build fails, make it a C++ project once with **Tools > New C++ Class**, then build again.

## Quick start

1. Double-click a **MetaHuman Character** asset to open it in the MetaHuman Character editor.
2. Click **Tattoos** on the editor's toolbar. The Tattoos tab opens next to Preview Settings.
3. If a yellow bar says the viewport shows the Topology or Clay material, click **Show Skin**.
4. Click **Add SVG...** and choose `Plugins/MetaHumanTattoo/Examples/NauticalStar.svg`. *Place on Skin* switches on.
5. **Click on the upper arm.** The star appears on the skin.
   - Hold the left mouse button and drag to slide it.
   - **Ctrl + mouse wheel** scales it.
   - **Shift + mouse wheel** turns it.
   - **Esc** stops placing.
6. Tune it in the settings under the list: **Opacity**, **Ink Color**, **Width**, **Rotation**, **Mirror** and so on.
7. Click **Apply to Skin**. The tattoo is baked into the skin, and the character and its new textures are saved.

For a colored tattoo, add `Examples/Heart.svg` and set **Ink Mode** to **Colors From SVG**.

## Controls

| Where | Control | What it does |
|---|---|---|
| Viewport, with Place on Skin on | Left click on the skin | Places the selected tattoo there |
| | Left drag | Slides it over the skin |
| | Ctrl + mouse wheel | Scales it |
| | Shift + mouse wheel | Turns it in 5° steps |
| | Esc | Switches Place on Skin off |
| | Alt + left drag, or clicking beside the character | Orbits the camera as usual |
| Tattoos panel | Add SVG..., Duplicate, Delete, Up, Down | Edit the list. Later tattoos are drawn over earlier ones. |
| | List checkbox | Hides a tattoo without deleting it |
| | Apply to Skin | Bakes every visible, placed tattoo into the skin and saves |
| | Discard Changes | Returns to the last applied state |
| | Remove All Tattoos | Deletes all tattoos and restores the original skin textures |

### Tattoo settings

| Setting | Default | What it does |
|---|---|---|
| Ink Mode | Single Ink Color | *Single Ink Color* draws every shape in **Ink Color**. *Colors From SVG* keeps the file's colors. |
| Ink Color | blue-black | Healed black ink reads as a very dark blue-green; pure black looks painted on. |
| Opacity | 0.9 | 1 is fresh ink, 0.75–0.9 healed, 0.3–0.5 old and faded. |
| Blend Mode | Healed Ink | *Healed Ink* multiplies, so skin shows through. *Opaque* paints over; use it for white or light ink. |
| Ink Spread | 0.3 mm | Softens the edges like ink spreading in the skin (0 is razor sharp). |
| Width | 10 cm | The height follows the SVG's proportions. |
| Rotation, Mirror | 0°, off | Turn the design clockwise as seen on the skin; flip it left to right. |
| Target | Head and Body | Lets a tattoo cross the neck seam. Use *Body Only* or *Head Only* to keep it on one mesh. |
| Projection Depth, Max Surface Angle | automatic, 70° | Stop the ink reaching nearby surfaces, or smearing round the sides of a limb. |

Every setting has a full tooltip in the editor. Ranges, recommended values and troubleshooting are in the **[User Guide](Docs/UserGuide.md)**.

## What Apply to Skin does

Apply writes into a `Tattoos` folder next to the character:

- `TS_<Character>`: the tattoo list, with the SVGs embedded.
- `T_<Character>_Tattoo_<Slot>`: one baked texture per skin texture a tattoo reaches.

It then points the character's **Texture & Material Overrides** at the baked textures. The textures the character had before are never modified: *Remove All Tattoos* puts them back, and each Apply starts from them again, so ink never stacks.

## What to know

- **Skin textures.** Tattoos go into the face base color and into Body and Chest base colors that are plain color textures.
  - Body and chest textures generated by MetaHuman Creator are skipped, with a message.
  - A character with texture overrides off and a generated body is refused. Switching overrides on would change how its body renders.
  - To tattoo a body, give the character a **Body Base Color** texture override first.
- **Other tools that copy skin materials,** such as a sweat or wetness system like [MetaHuman Sweat](https://github.com/timelery/MetaHumanSweat), keep the textures the skin had when they were set up. Apply tattoos first, then assemble the character, then set those tools up. Redo that last step whenever the tattoos change.
- **Expressions:** the tattoo stays visible while the face moves.
- **Underwear** shown on the character covers tattoos under it.
- **Projection is planar,** like a stencil. For a band all the way round a limb, place two or three tattoos around it.
- **Live preview** needs the Skin preview material, and does not run when the skin uses virtual textures (Apply still works). It is a little softer than the final bake.
- **Skin changes:** if you change the skin tone or skin texture after applying, the panel asks you to apply again.
- **Undo:** use **Discard Changes**; Ctrl+Z does not undo tattoo edits.
- **Moving a character to another project:** copy its `Tattoos` folder with it. The baked textures work without the plugin; the `TS_` list needs it.

## Scripting

Everything is also available from Python as `unreal.MetaHumanTattooLibrary`:

```python
import unreal

lib = unreal.MetaHumanTattooLibrary
character = unreal.load_asset('/Game/MyCharacters/MHC_Example')

index = lib.add_tattoo_from_svg_file(character, 'C:/Designs/Star.svg', 8.0)    # width in cm; -1 on failure

shoulder = lib.get_rest_bone_location(character, 'upperarm_r')                # a Vector (None if no such bone)
elbow = lib.get_rest_bone_location(character, 'lowerarm_r')
centre = shoulder + (elbow - shoulder) * 0.5
outward = unreal.Vector(-1.0, 0.0, 0.0)                                        # the character's right side is -X
lib.place_tattoo_along_ray(character, index, centre + outward * 40.0, outward * -1.0, shoulder - elbow)

report, ok = lib.apply_tattoos(character)
print(ok, report)
```

See the [User Guide](Docs/UserGuide.md#scripting) for every function.

## Tested

| Engine | Built | Bake | Editor panel |
|---|---|---|---|
| Unreal Engine 5.8.3, Windows | yes, including Epic's `BuildPlugin -StrictIncludes`; the prebuilt download is that build | yes: textures compared before and after | yes: placement driven by the plugin's automation API |
| Other versions, Linux, macOS | not tested | | |

Tested with a character that uses custom face and body skin textures. Not yet tested:
- assembling a tattooed character;
- a sweat system running on one;
- placement by hand, with mouse and keyboard.

## Licence

MIT. See [LICENSE](LICENSE).
