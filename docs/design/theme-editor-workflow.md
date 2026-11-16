# Theme editor workflow

Status: **implemented locally; native build and focused X11 regressions pass**.

This document records the agreed workflow for the separate, interactive
Swordfish Theme Editor. Open **Preferences > Appearance > Theme**, then choose
**Edit**, **New theme** or **Duplicate and edit**.

The editor covers supported colours, fonts and other properties of the main
window's lists, tree, Places, headers, tabs, toolbars, address bar, status bar,
menus, controls and scrollbars. Gradients, patterns and textures are supported
on file lists; unsupported properties are hidden. Whole-window opacity requires
an X11 compositor. Dialog controls retain their fixed palette.

The current local executable includes this work. The earlier portable test
archive does not; a replacement portable bundle has not been built.

## 1. Start the editor

Open a separate Theme Editor window. Its startup choices are:

- **Edit Current** — edit the theme currently in use.
- **New Theme** — start from the built-in Swordfish default theme (Slate Blue).
- **Duplicate and Edit** — choose an existing theme and edit a copy.

The selected starting theme controls the mockup's appearance. In every case,
the mockup uses the user's current file-manager view: its pane layout, folders,
files, visible columns, tabs, tree and other current content. Choosing a base
theme does not replace that view with sample content.

## 2. Select an element

After startup, show a full-scale mockup and the instruction:

> Please select the element you want to edit.

The mockup should reflect the current view while preventing file operations.
It is a snapshot for editing appearance, not another active file-manager
window. Clicking an element selects it for theme editing; it must not open,
rename, move or delete files, or change folders.

An optional privacy switch masks file and folder names, including paths, in
the mockup for screen sharing. The normal view shows the real names; privacy
mode changes the preview only, not the files or the saved theme.

Make editable areas easy to discover without permanently cluttering the view:

- Highlight an element and identify it on pointer hover.
- Provide a **Show editable areas** toggle that outlines all editable elements.
- Provide an element tree/list as another way to select small, obscured or
  temporarily invisible elements. Organize nested parts, such as a file list's
  background, alternate rows, sorted column, text and selection.

The eventual design target is every area that can reasonably be themed:
file list, folder tree and Places, column headers, tabs, toolbars, controls,
menus and other relevant interface elements of the main window. Dialogs and
child windows are excluded: they use a fixed readable palette (see
[preferences-layout.md](preferences-layout.md)). A phased implementation
is a delivery plan, not a reduction in that target.

## 3. Edit element properties

Selecting an element opens its property sheet. The available properties depend
on the element. Only supported properties and effects are available for
editing; unsupported ones are hidden or shown disabled (greyed out). Supported
properties can include:

- foreground and background colours, including gradients;
- textures and patterns;
- transparency/opacity where supported;
- borders;
- text/font properties;
- sub-elements and their own properties.

Choosing a property opens its appropriate editor:

- **Colour:** colour chooser, with gradient controls where supported.
- **Texture or pattern:** visual chooser, with intensity/size and other
  relevant controls. When combining a texture with a colour or gradient, let
  the user choose theme-tinted or original PNG colours and preview each option.
- **Text:** font manager for family, size, bold, italic and other supported
  font attributes.
- **Border:** supported colour, width and style controls.

Show whether a value is inherited from a parent or is an explicit override.
Allow an individual value to be unlinked and set separately.

## 4. Preview, Apply and undo

Changes should preview immediately in the mockup as controls are adjusted.
**Apply** commits the current property edit, keeps its effect in the cumulative
mockup, and normally records one undo step. Individual gradient-stop changes
must remain separately undoable, even when several were made before the same
Apply. Apply affects only the mockup: it does not save the theme, change the
running file manager or restart the application. **Cancel** discards the
in-progress property edit and returns it to its last applied value. Later edits
build on earlier applied edits.

Provide:

- multi-step **Undo** and **Redo**;
- **Reset Property**, **Reset Element** and **Reset Whole Theme**;
- **Restore Original**, which returns to the theme as it was when this editing
  session began;
- **Restore Defaults**, which returns to the built-in Swordfish default;
- **Compare**, which toggles between the original and edited appearance.

The two restore actions are distinct. Reset controls should make their scope
clear before discarding changes.

Provide preview-state controls for states not visible in the user's current
view, such as selected, hover, disabled, open menu/dialog and progress. State
previews change what the mockup displays, not the underlying file-manager view.

## 5. Readability and saving

Show advisory readability warnings when text is difficult to distinguish from
its background, especially over strong effects or busy textures. Warn below
the WCAG AA minimum (4.5:1 for normal text, 3:1 for large text) and also show
the stricter AAA rating. Offer a suggested fix, but change the selected
appearance only if the user chooses it.

Provide **Save** and **Save As**, and warn before closing with unapplied or
unsaved work. **Save** saves and applies all cumulative changes to the running
file manager, restarting Swordfish if needed. When saving edits to an existing
theme, ask each time whether to overwrite it or save a copy. Exporting a theme
that uses custom textures should include those texture files; importing it
should make them available to the theme. If an imported or added texture name
already exists, show:

> Texture name already exists. Please use another name.

The texture palette accepts PNG tiles only and is extensible with user-provided
tiles in Swordfish's user texture folder. A tile may be at most 1024 pixels
wide and 1024 pixels high; report an error if it exceeds either limit. When a
user selects a PNG from another folder, copy it into the user texture folder
and leave the original in place.

The editor mockup must preview changes without restarting Swordfish. Saving
should avoid a restart where the affected widgets support live updates, and
identify clearly when a restart is needed.

## 6. Colour picker

The user chose [keshavbhatt/ColorPicker](https://github.com/keshavbhatt/ColorPicker)
as the model for the colour picker, with all features. ColorPicker (MIT,
archived) and its successor ColorSmith (GPLv3) are Qt applications. A Qt widget
cannot run inside a FOX window, and neither app returns a picked colour to its
caller. The picker is therefore built natively in FOX by extending Swordfish's
own copy of the FOX selector (`src/ColorSelector.*`), following ColorPicker's
design and ColorSmith's feature list without copying their code:

- HSV colour plane with a brightness slider (the wheel remains available);
- HEX, HexRGBA, RGB, HSV/HSB and CMYK fields, copyable, with paste detection;
- screen eyedropper with a magnified preview; if unavailable, explain this and
  keep manual colour selection available;
- original and new colour side by side;
- named palettes that can be saved, imported and exported, with both shared
  and theme-specific palettes;
- optional names for individual colours and gradients, and recently used
  colours and gradients;
- tint/shade generator;
- gradient editor (stops, linear/radial, angle), based on `FXGradientBar`;
- WCAG contrast check, which also supplies the editor's readability warnings;
- random colour and gradient;
- palette extraction from an image.

Every change updates the mockup live. No new library dependency is added.

## 7. Layout and implementation phases

The mockup should offer **Fit to window** and zoom so the full-scale view remains
usable on smaller displays.

Implementation can add area support in phases while retaining the complete
scope above:

1. Theme Editor window, startup choices, mockup selection, property editing,
   undo/reset/save workflow and file-list effects.
2. Folder tree and Places.
3. Tabs and column headers.
4. Toolbars, controls and remaining supported main-window areas.

The editor should reuse real Swordfish/FOX widgets where practical so the
mockup tracks actual rendering. Its snapshot of current content and layout
must remain safe and display-only. Gradients, textures and patterns on each
widget require drawing support in that widget; the file-list prototype does
not make them automatically available everywhere.

## 8. Validation and remaining acceptance checks

The native build and `bash tests/run-theme-workflow-regression.sh` pass.
The regression checks real painted pixels, the safe snapshot, supported
properties, preview-only Apply, gradient midpoint rendering and separate undo,
cumulative Save and disk reload, fixed chooser colours, colour parsing, and
shared versus draft-only theme palettes. Cancelling Preferences preserves an
explicit editor Save while discarding unrelated pending preferences.

The existing search, folder-expansion and Custom Actions model/UI regressions
also pass. These are isolated Xvfb checks, not real-desktop acceptance.
Compositor opacity, interactive screen picking, palette/image import and
texture export confirmation still need manual desktop testing. No new library
dependency was added.
