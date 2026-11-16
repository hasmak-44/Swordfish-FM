# Preferences layout redesign

Status: implemented locally; native build and focused X11 regressions pass.

## Goal

Replace the single row of ten tabs in `PreferencesBox` with a category panel
on the left and the selected category's settings on the right, split into tabs
where needed. The user supplied Double Commander's Options window and
JDownloader 2's Settings page as references.

## Agreed decisions

- **Left panel:** JDownloader style, with a large icon above each category
  name. The panel scrolls vertically when it is taller than the window.
- **Filter box:** a filter field under the left panel narrows the categories
  to those whose page contains matching setting labels.
- **Page header:** each page has a title bar naming the selected category,
  like the Double Commander "Archivers" header.
- **Pages:** each category shows its settings on the right. A category with
  several sections uses tabs inside the page.
- **Buttons:** OK, Cancel and Apply at the bottom. Apply saves without closing.
  Settings that need a restart keep the existing restart prompt.
- **Last page:** Preferences reopens on the last page viewed.
- **Themes:** the Theme tab only selects, imports, exports, renames and
  deletes themes. Colour editing moves to the separate Theme Editor (see
  [theme-editor-workflow.md](theme-editor-workflow.md)), which opens from
  Edit, New and Duplicate buttons on the Theme tab.

## Category mapping

All existing settings are kept; only their location changes.

| Category | Tabs (existing content) |
|---|---|
| General | Starting; General options; Address bar and scripts |
| Panels & Tabs | Folder panel view; Startup list; Tabs; Scrolling |
| File List | Displayed columns; Column order; Folder sizes and previews |
| File Operations | Copy suffix; Confirmations; Warnings (from the old Dialogs tab) |
| Appearance | Theme; Toolbar; Icon theme; Fonts; Screen DPI |
| Language | Language |
| Programs | Default programs; Application handling; Volume management |
| Key Bindings | Key bindings |
| Custom Actions | Button that opens the Custom Actions editor |
| System | Root mode; Automount; Logging; System integration |

## Fixed dialog and child-window colours

Dialogs and child windows no longer inherit the main window's theme colours,
which made some of them unreadable. They always use this fixed palette:

| Role | Colour |
|---|---|
| Window background | `#29353B` |
| Fields (text entries, lists, combo boxes, spinners) | `#1E242D` |
| Element borders and active tab highlight | `#0F56D9` |
| Text | `#FFFFFF` |
| Selected item | `#0F56D9` background, `#FFFFFF` text |

Hilite and shadow colours are derived from the background as they are now.

- **Scope:** all dialogs and child windows, including Preferences, Properties,
  message boxes, progress and copy dialogs, the file chooser, the text viewer
  and editor, the search window, the image viewer, Custom Actions and the
  Theme Editor's own controls. The Theme Editor mockup still shows the theme
  being edited.
- **File lists inside dialogs** (file chooser, search results) also use the
  fixed palette, with `#1E242D` as the list background.
- The main window keeps the user's theme. Dialog colours are not part of the
  Theme Editor.
- **Timing:** implemented together with the Preferences redesign.
- **Implementation approach:** shared recursive palette helpers run when
  dialogs and child windows are created/shown and when detached popups are
  constructed. Modern-control painters derive colours per widget, rather
  than sharing cached main-window colours.

## Category icons

The bundled icon themes have no suitable icons. Panels & Tabs and Language
have none at all, and several categories would share the same generic file or
gear icon. The largest bundled icons are 32 px, and they change with the
user's icon theme.

Swordfish therefore uses a fixed set of flat line icons that is independent
of the icon theme. The icons have white strokes with `#0F56D9` accents to match
the fixed dialog palette; the selected category uses an all-white variant on
`#0F56D9`. The SVG sources are in `icons/preferences/` (`pref-*.svg`). They are
approved by the user after reviewing
[`build/preferences-icons-preview.png`](../../build/preferences-icons-preview.png).
The ten SVG sources and twenty normal/selected PNGs are included in the build
and install rules. PNGs are rendered by `icons/preferences/render-icons.py`.

## Validation and delivery

The theme workflow regression checks the category icons, translated-label
filtering, actual control paint colours, Apply staying open, and Cancel
preserving an already applied baseline. The existing Custom Actions UI
regression was updated for the category layout and passes. The native build,
search and folder-expansion regressions also pass.
An isolated `DESTDIR` data installation verified all ten category SVGs,
twenty normal/selected PNGs and ten bundled textures.

The local executable includes the redesign and fixed palette. The previous
portable test archive does not; packaging and user desktop acceptance remain.
