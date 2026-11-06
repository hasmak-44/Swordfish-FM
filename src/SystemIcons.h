#ifndef SYSTEMICONS_H
#define SYSTEMICONS_H

#include <fx.h>
#include <vector>

// A freedesktop icon theme installed on the host system
struct SystemTheme
{
    FXString id;    // Folder name of the theme
    FXString name;  // Display name from index.theme
    FXString dir;   // Full path of the theme folder
};

// List the installed freedesktop icon themes (reads folder and theme names only)
void scanSystemThemes(std::vector<SystemTheme>& themes);

// Folder where the converted copy of a system theme is stored
FXString systemThemeFolder(const FXString& id);

// Folder suffix used for converted themes
const char* systemThemeSuffix();

// Path of the bundled SVG converter, or an empty string if none is available
FXString findIconConverter();

// Named icons are resolved against the host, never the portable bundle.
// The catalog indexes filenames only; images are decoded on demand.
struct SystemActionIcon
{
    enum Category
    {
        ACTIONS, ANIMATIONS, APPLICATIONS, CATEGORIES, DEVICES, EMBLEMS,
        EMOTIONS, MIMETYPES, PLACES, STATUS, STOCK, UNCATEGORIZED
    };
    FXString name;
    FXString path;
    Category category = UNCATEGORIZED;
};
const std::vector<SystemActionIcon>& systemActionIcons(FXApp* app);
FXString resolveSystemActionIcon(FXApp* app, const FXString& name);

// Accepts theme:NAME or an image filename. SVG requires rsvg-convert.
// Returned icons belong to the caller and fit inside side x side.
FXIcon* loadCustomActionIcon(FXApp* app, const FXString& value, FXint side);

// Progress callback: done, total
typedef void (*SystemIconProgress)(int, int, void*);

// Convert a system theme to a flat Swordfish icon folder.
// Returns the number of icons taken from the theme, or -1 on failure (error is set).
int convertSystemTheme(FXApp* app, const SystemTheme& theme, const FXString& mapfile,
                       const FXString& defaultdir, const FXString& dest, FXString& error,
                       SystemIconProgress progress, void* userdata);

#endif
