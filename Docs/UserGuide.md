# MetaHuman Tattoo: user guide

This guide covers every control and setting, what the plugin writes, how a tattoo becomes part of the skin, scripting, and troubleshooting. To install the plugin and try it in five minutes, see the [README](../README.md).

## Contents

- [The workflow](#the-workflow)
- [The Tattoos panel](#the-tattoos-panel)
- [Tattoo settings](#tattoo-settings)
- [What Apply to Skin writes](#what-apply-to-skin-writes)
- [Which skin textures take tattoos](#which-skin-textures-take-tattoos)
- [Tattoos, assembly and tools that copy skin materials](#tattoos-assembly-and-tools-that-copy-skin-materials)
- [Limits](#limits)
- [Scripting](#scripting)
- [Troubleshooting](#troubleshooting)
- [How it works](#how-it-works)

## The workflow

1. **Open the character.** Double-click a MetaHuman Character asset. The MetaHuman Character editor opens.
2. **Open the panel.** Click **Tattoos** on the editor's toolbar. The Tattoos tab docks next to Preview Settings.
3. **Show the skin.** Tattoos are visible only on the **Skin** preview material. If the viewport shows Topology or Clay, the panel shows a yellow bar with a **Show Skin** button.
4. **Add a design.** Click **Add SVG...** and choose one or more `.svg` files. Each becomes a tattoo in the list, the last one is selected, and **Place on Skin** switches on.
5. **Place it.** Click on the character's skin. The centre of the design goes where you click, upright as the camera sees it. Hold the left mouse button and drag to slide it. **Ctrl + mouse wheel** scales it, **Shift + mouse wheel** turns it, and **Esc** stops placing.
6. **Tune it** in the settings below the list. The viewport updates as you change values.
7. **Apply to Skin.** The tattoos are baked into the skin textures, the character's texture overrides point at the results, and everything is saved.

The status at the top of the panel says **not applied yet** while the list differs from what is baked into the skin.

## The Tattoos panel

| Control | What it does |
|---|---|
| **Add SVG...** | Adds one tattoo per chosen file and switches Place on Skin on. |
| **Duplicate** | Copies the selected tattoo, offset half its width to the side. Tick **Mirror** on the copy for a matching tattoo on the other side. |
| **Delete** | Removes the selected tattoo from the list. Apply to Skin then takes it off the skin. |
| **Up / Down** | Drawing order: a later tattoo is drawn over an earlier one where they overlap. |
| List checkbox | Shows or hides a tattoo without deleting it. Hidden tattoos are not baked. |
| List size column | The tattoo's size, or *not placed* if it has not been put on the skin yet. |
| **Place on Skin** | While on, a left click on the character places the selected tattoo and dragging slides it. A click that misses the character still orbits the camera; Alt + left drag always orbits. |
| **Apply to Skin** | Bakes every visible, placed tattoo, sets the character's texture overrides, and saves the character, the textures and the tattoo list. |
| **Discard Changes** | Returns the list to how it was when it was last applied. |
| **Remove All Tattoos** | Deletes every tattoo and restores the skin textures the character had before. |

Ctrl+Z does not undo tattoo edits. Use **Discard Changes**.

## Tattoo settings

Every setting also has a tooltip in the editor.

| Setting | Default | Range | What it does and when to change it |
|---|---|---|---|
| **Name** | file name | | Shown in the list. |
| **Visible** | on | | Untick to keep the tattoo but leave it off the skin. |
| **SVG File** | | `.svg` | The design. Choosing a file copies its text into the tattoo, so the tattoo keeps working if the file moves or changes. Pick the file again to reload it. |
| **SVG Aspect** | from the file | read-only | Height ÷ width of the SVG canvas. The tattoo's height is **Width** × this. |
| **Ink Mode** | Single Ink Color | | **Single Ink Color** draws every shape in Ink Color, for black-work and one-color tattoos. **Colors From SVG** keeps each shape's fill and stroke color from the file, for colored tattoos. |
| **Ink Color** | (0.012, 0.016, 0.020) | linear color | Used in Single Ink Color mode only. The default is the blue-black of healed black ink; pure black (0, 0, 0) looks painted on. |
| **Opacity** | 0.9 | 0–1 | 1 is fresh, saturated ink. 0.75–0.9 is a healed tattoo, 0.3–0.5 an old, faded one, and 0 hides it. |
| **Blend Mode** | Healed Ink (Multiply) | | **Healed Ink** multiplies skin by ink, so pores, freckles and skin tone show through. **Opaque** replaces the skin color in proportion to Opacity. Use Opaque for white or very light ink, which Healed Ink cannot show on darker skin. |
| **Ink Spread** | 0.3 mm | 0–5 mm | Blurs the edges by this distance on the skin, like ink spreading as a tattoo heals. 0 is razor sharp; 0.2–0.5 is healed; 1–2 is old and blown out. |
| **Width** | 10 cm | 0.5–200 cm | Width on the skin. Typical: 3–6 cm wrist or finger, 8–15 cm forearm or upper arm, 20–40 cm chest or back. |
| **Rotation** | 0° | −360–360° | Turns the tattoo clockwise as seen looking at the skin. 0 keeps the top pointing the way the camera's up pointed when it was placed. |
| **Mirror** | off | | Flips the design left to right. |
| **Target** | Head and Body | | **Head and Body** lets a tattoo cross the neck seam. **Body Only** or **Head Only** keeps it on one mesh, for example a chest piece whose top edge must not reach the jaw. |
| **Projection Depth** | 0 (automatic) | 0–100 cm | How far the projection reaches in front of and behind the tattoo centre. Automatic is half the tattoo's larger side, at least 2 cm. Lower it if ink also lands on a nearby surface, such as the torso under an arm tattoo. Raise it if a tattoo on a strongly curved surface is cut off. |
| **Max Surface Angle** | 70° | 10–89° | Skin that turns away from the projection direction by more than this gets no ink, fading out over the last 10°. This stops the design smearing round the sides of a limb. Raise it to wrap further; lower it if the edges look stretched. |
| **Placement** | | read-only | Centre, outward skin direction and up direction, in the character's rest-pose (A-pose) mesh space, in cm. Set it by clicking. |

**SVG support:** filled shapes and strokes, even-odd and non-zero fills, groups and transforms. Text, embedded images, gradients and filters are not drawn. Convert text to paths in your vector editor first.

## What Apply to Skin writes

Everything goes into a `Tattoos` folder next to the character asset:

| Asset | What it is |
|---|---|
| `TS_<Character>` | The **tattoo set**: the tattoo list with each SVG's text embedded, the list as last applied, and the bookkeeping that makes baking repeatable and reversible. That bookkeeping records which texture each baked texture was made from, which textures were written, and any inactive override entries set aside when tattoos switched texture overrides on. |
| `T_<Character>_Tattoo_<Slot>` | One **baked texture** per skin texture a tattoo reaches. It has the same size, sRGB setting, compression and texture group as the texture it was made from, with virtual texturing off. |

Apply then sets the character's **Skin > Texture & Material Overrides** to use the baked textures. It changes nothing else there: material overrides and other texture slots are left as they are.

**Baking always starts from the original texture** of each slot, never from a baked one, so applying again does not stack ink. If something else later replaces an override that pointed at a baked texture, such as a re-import of the character's skin, the next Apply treats the new texture as the original.

## Which skin textures take tattoos

| Texture | Mesh | Baked when |
|---|---|---|
| Face **Base Color** | head (UV tile 1001) | always: the override texture, or the face texture MetaHuman Creator synthesizes |
| **Body Base Color** | body (UV tile 1002) | the slot has a plain color texture override |
| **Chest Base Color** | head, lower neck | the slot has a plain color texture override |

The plugin leaves these textures untouched:

- **The body and chest textures MetaHuman Creator generates.** They are desaturated, tone-neutral maps that the skin material colors with the character's skin-tone settings. Ink drawn into them would only come out as a darker skin tone. Apply skips them and says so in its report.
- **The animated face color maps (CM1–CM3).** These are differences the material adds on top of the base color while the face moves, so the tattoo stays visible through expressions without them.
- **Normal, cavity and roughness maps.** Skin detail shows through the ink.
- **Underwear textures.** Underwear is drawn over the skin and should cover a tattoo.

A character whose texture overrides are **off** and whose body uses the generated texture is refused with an explanation. Switching overrides on for the tattoos would change how its generated body skin renders. To tattoo such a character, give it a **Body Base Color** override (and a Face one if you like) in the Texture & Material Overrides tool first.

## Tattoos, assembly and tools that copy skin materials

The tattoo is part of the skin's base color, so anything that shades skin treats the ink like the skin around it. Assembling the MetaHuman builds from the character's skin settings, texture overrides included, and wet or sweaty skin darkens and glosses the inked area as it does bare skin.

Some tools keep **copies** of a character's skin material instances, holding the textures as they were when the copies were made. Sweat and wetness systems such as [MetaHuman Sweat](https://github.com/timelery/MetaHumanSweat) do this. For those, the order matters:

1. **Apply to Skin.**
2. **Assemble** the character.
3. **Set up the tool** on the assembled character. For MetaHuman Sweat, delete the character's old `MI_Sweat_*` copies first, because existing copies are not refreshed.

Do step 3 again whenever the tattoos change.

## Limits

- **Projection is planar**, like a stencil held against the skin. For a band that wraps fully round a limb, use two or three tattoos placed around it.
- **On the lowest part of the neck,** the head material blends in the chest texture. If that texture is MetaHuman Creator's generated one, a tattoo there can be partly hidden.
- **Live preview:**
  - It shows only on the Skin preview material.
  - It does not run when the character's skin uses virtual textures; the panel says so, and Apply to Skin still works.
  - It uses copies of up to 2048 pixels, so it is slightly softer than the bake.
- **Skin changes:** if you change the skin tone or skin texture after applying, the panel shows a bar. Apply again so the tattoos sit on the new skin.
- **Another project:** copy the character's `Tattoos` folder with it. The baked textures and overrides work without the plugin; the `TS_` set needs it.
- **Tested** on Unreal Engine 5.8 on Windows only.

## Scripting

Everything in the panel is available from Python through `unreal.MetaHumanTattooLibrary`, and from Blueprint (category *MetaHuman | Tattoo*). The functions work whether or not the character's editor is open. Positions are in the character's rest-pose (A-pose) mesh space, in centimetres. A MetaHuman faces +Y, its right side is −X, and Z is up.

| Python call | Returns |
|---|---|
| `get_tattoo_set(character, create_if_missing)` | the `MetaHumanTattooSet`, or None |
| `add_tattoo_from_svg_file(character, svg_file, width_cm=10.0)` | the new tattoo's index, or −1 (see the Output Log) |
| `get_tattoo(character, index)` | a `MetaHumanTattooLayer`, or None |
| `set_tattoo(character, index, tattoo)` | True on success |
| `remove_tattoo(character, index)` | True on success |
| `place_tattoo_along_ray(character, index, ray_origin, ray_direction, up_hint)` | True when the ray hits the skin; the tattoo is centred there, with its top towards `up_hint` |
| `get_rest_bone_location(character, bone_name)` | a `Vector`, or None; useful for aiming rays |
| `apply_tattoos(character)` | `(report, ok)` |
| `remove_all_tattoos(character)` | `(report, ok)` |
| `open_tattoo_panel(character)` | True when the editor and its Tattoos tab are open |
| `simulate_viewport_click(character, unreal.Vector2D(x, y))` | `(status, hit)`. Acts as a Place on Skin click at a point of the open viewport (0–1 across and down). |
| `get_tattoo_panel_status(character)` | a status line for the open panel's live preview |
| `discard_tattoo_changes(character)` | True on success |
| `probe_character(character)` | a diagnostic report: skin sections, UV tiles, skin material parameters, overrides |
| `compare_textures(a, b, png_path)` | how many texels differ between two textures, optionally with a side-by-side PNG of the changed region |

`MetaHumanTattooLayer` fields in Python:
- `name`, `visible`, `svg_file`, `svg_data`, `svg_aspect`
- `ink_mode`: `unreal.MetaHumanTattooInkMode.SINGLE_INK` or `SVG_COLORS`
- `ink_color`, `opacity`
- `blend_mode`: `unreal.MetaHumanTattooBlendMode.HEALED_INK` or `OPAQUE`
- `ink_spread_mm`, `width_cm`, `rotation_degrees`, `mirror`
- `target`: `unreal.MetaHumanTattooTarget.HEAD_AND_BODY`, `BODY_ONLY` or `HEAD_ONLY`
- `projection_depth_cm`, `max_surface_angle_degrees`
- `placement`, with `placed`, `location`, `normal` and `up`

```python
import unreal

lib = unreal.MetaHumanTattooLibrary
character = unreal.load_asset('/Game/MyCharacters/MHC_Example')

# A red heart on the outside of the left upper arm.
index = lib.add_tattoo_from_svg_file(character, 'C:/Designs/Heart.svg', 7.0)
tattoo = lib.get_tattoo(character, index)
tattoo.set_editor_property('ink_mode', unreal.MetaHumanTattooInkMode.SVG_COLORS)
lib.set_tattoo(character, index, tattoo)

shoulder = lib.get_rest_bone_location(character, 'upperarm_l')
elbow = lib.get_rest_bone_location(character, 'lowerarm_l')
centre = shoulder + (elbow - shoulder) * 0.5
outward = unreal.Vector(1.0, 0.0, 0.0)          # the character's left side is +X
lib.place_tattoo_along_ray(character, index, centre + outward * 40.0, outward * -1.0, shoulder - elbow)

report, ok = lib.apply_tattoos(character)
print(ok, report)
```

**Two tips for scripts:**
- Registering a character for editing and saving assets both let the editor tick. If your script runs from `register_slate_post_tick_callback`, guard the callback against running inside itself.
- `UnrealEditor.exe -ExecutePythonScript=<file>` quits the editor when the script returns. Use `-ExecCmds="py <file>"` to keep it open.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| No **Tattoos** button | The plugin is not enabled or not built. | Edit > Plugins > MetaHuman Tattoo, then restart and build. From Python, `open_tattoo_panel(character)` also opens the tab. |
| Clicking does nothing | Place on Skin is off, no tattoo is selected, or the click missed the character. | Select a tattoo, switch Place on Skin on, and click on the skin. |
| No live preview | The viewport shows Topology or Clay, or the skin uses virtual textures. | Click **Show Skin**. With virtual textures, Apply to Skin to see the result. |
| "Not baked: … tone-encoded" | That slot still uses MetaHuman Creator's generated texture. | Give the slot a base color texture override. |
| "Not applied: … texture overrides are off" | A character with a generated body texture and overrides off. | Add a Body Base Color override, then apply. |
| Ink also lands on the torso under an arm tattoo | The projection reaches it. | Lower **Projection Depth**, or set **Target** to Body Only. |
| Edges stretched round a limb | The design wraps too far round a curve. | Lower **Max Surface Angle**, or split the design into two tattoos. |
| The tattoo disappears when the character sweats or is wet | The sweat or wetness tool was set up before the tattoos. | Set it up again after applying; see [above](#tattoos-assembly-and-tools-that-copy-skin-materials). |
| A rebuild fails with "cannot open file UnrealEditor-MetaHumanTattooEditor.dll" | An editor still has the plugin loaded. | Close every editor on the project, then build. |

## How it works

| Piece | Source | Job |
|---|---|---|
| Editor integration | `MetaHumanTattooEditorModule.cpp` | Adds the Tattoos button to the MetaHuman Character editor's toolbar, registers the Tattoos tab on that editor's own tab manager, and docks it next to Preview Settings. |
| Panel | `SMetaHumanTattooPanel.*` | The tattoo list, Place on Skin, the settings (a details view on the selected tattoo), and Apply / Discard / Remove. |
| Session | `MetaHumanTattooSession.*` | Clicks and the live preview. Details below. |
| Viewport input | `MetaHumanTattooInputProcessor.*` | A Slate input pre-processor for left click and drag, Ctrl and Shift wheel, and Esc over the MetaHuman viewport, active only while Place on Skin is on. |
| Meshes | `MetaHumanTattooMesh.*` | Reads LOD 0 of the head and body edit meshes: rest positions, normals, UVs, skin weights and the skin-section triangles. Also skins them and raycasts against them. |
| SVG | `MetaHumanTattooSvg.*` | Rasterizes with the engine's nanosvg into premultiplied linear color (about 10 texels per mm for baking, 4 for the preview), cached, with a Gaussian blur for Ink Spread. |
| Baker | `MetaHumanTattooBaker.*` | Chooses the slots and their original textures, projects in UV space, fills gutters, writes the textures, sets overrides, commits and saves. Details below. |
| Script API | `MetaHumanTattooLibrary.*` | The functions above. |

**Session.** It finds the editor's preview actor and the viewport client showing it. A click becomes a camera ray. The edit meshes are skinned with the preview components' bone matrices and hit-tested against their skin triangles, so a posed character can be clicked too. The hit is stored in rest pose. For the live preview, unapplied tattoos are drawn into preview copies of the base color textures, which are bound to the editor's skin material instances. The original textures are put back afterwards.

**Baker.** For every skin triangle in a texture's UV tile, it rasterizes the triangle in texture space. Each texel's rest-pose position is projected into the tattoo's frame: a depth box plus a fade with skin angle. The ink is composited in linear color, as a healed-ink multiply or as paint-over. Changed texels are also spread into the empty gutter texels round UV islands, so seams show no light line.
