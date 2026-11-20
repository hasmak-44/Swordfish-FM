#include "config.h"
#include "i18n.h"
#include <fx.h>
#include <fxkeys.h>
#include <FXPNGIcon.h>
#include <FXPNGImage.h>
#include <FXGradientBar.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "xfedefs.h"
#include "xfeutils.h"
#include "ThemeEditor.h"
#include "BackgroundEffect.h"
#include "BackgroundImage.h"
#include "Preferences.h"
#include "XFileExplorer.h"
#include "FileList.h"
#include "TextLabel.h"
#include "ColorSelector.h"
#include "FontDialog.h"
#include "HistInputDialog.h"
#include "FileDialog.h"
#include "MessageBox.h"
#include "AppLogger.h"
#include "icons.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sys/stat.h>
#include <utility>

extern FXString execpath;

namespace
{
// Gallery thumbnails show the PNG as an image viewer would: true colours, scaled to fit,
// transparency over a checkerboard. Returns NULL if the file cannot be decoded.
FXIcon* tileThumbnail(FXApp* app, const FXString& path, FXint size)
{
    FXFileStream stream;
    FXPNGImage image(app, NULL, IMAGE_KEEP);
    if (!stream.open(path, FXStreamLoad) || !image.loadPixels(stream) || !image.getData() ||
        image.getWidth() <= 0 || image.getHeight() <= 0)
        return NULL;
    const FXint sw = image.getWidth(), sh = image.getHeight();
    const FXColor* source = image.getData();
    const double scale = FXMIN(1.0, FXMIN(static_cast<double>(size) / sw, static_cast<double>(size) / sh));
    const FXint dw = FXMAX(1, static_cast<FXint>(sw * scale + 0.5)), dh = FXMAX(1, static_cast<FXint>(sh * scale + 0.5));
    const FXint ox = (size - dw) / 2, oy = (size - dh) / 2;
    FXColor* pixels = NULL;
    if (!FXMALLOC(&pixels, FXColor, size * size)) return NULL;
    for (FXint y = 0; y < size; ++y)
        for (FXint x = 0; x < size; ++x)
            pixels[y * size + x] = ((x / 8 + y / 8) & 1) ? FXRGB(0xC8, 0xC8, 0xC8) : FXRGB(0xF4, 0xF4, 0xF4);
    for (FXint y = 0; y < dh; ++y)
    {
        FXint top = y * sh / dh, bottom = FXMAX(top + 1, (y + 1) * sh / dh);
        for (FXint x = 0; x < dw; ++x)
        {
            FXint left = x * sw / dw, right = FXMAX(left + 1, (x + 1) * sw / dw);
            double r = 0, g = 0, b = 0, a = 0;
            for (FXint sy = top; sy < bottom; ++sy)
                for (FXint sx = left; sx < right; ++sx)
                {
                    FXColor c = source[sy * sw + sx];
                    double alpha = FXALPHAVAL(c);
                    r += FXREDVAL(c) * alpha;
                    g += FXGREENVAL(c) * alpha;
                    b += FXBLUEVAL(c) * alpha;
                    a += alpha;
                }
            FXint count = (right - left) * (bottom - top);
            FXColor& out = pixels[(y + oy) * size + x + ox];
            double coverage = a / (255.0 * count);
            if (a > 0)
            {
                r /= a;
                g /= a;
                b /= a;
            }
            out = FXRGB(static_cast<FXint>(r * coverage + FXREDVAL(out) * (1 - coverage) + 0.5),
                        static_cast<FXint>(g * coverage + FXGREENVAL(out) * (1 - coverage) + 0.5),
                        static_cast<FXint>(b * coverage + FXBLUEVAL(out) * (1 - coverage) + 0.5));
        }
    }
    FXIcon* icon = new FXIcon(app, pixels, 0, IMAGE_KEEP | IMAGE_OWNED, size, size);
    icon->create();
    return icon;
}

const char* const areaKeys[] = {"main", "list", "tree", "places", "headers", "tabs", "toolbars",
                                "address", "status", "menus", "controls", "scrollbars"};
const char* const areaNames[] = {N_("Main window"), N_("File list"), N_("Folder tree"), N_("Places"),
    N_("Column headers"), N_("Tabs"), N_("Toolbars"), N_("Address bars"), N_("Status bars"),
    N_("Menus"), N_("Controls"), N_("Scrollbars")};

FXString areaName(const FXString& key)
{
    for (size_t i = 0; i < ARRAYNUMBER(areaKeys); ++i)
        if (key == areaKeys[i]) return _(areaNames[i]);
    return key;
}

FXString propertyName(const FXString& key)
{
    if (key == "background") return _("Background colour");
    if (key == "foreground") return _("Text / foreground colour");
    if (key == "field") return _("Default field colour");
    if (key == "border") return _("Border colour");
    if (key == "alternate") return _("Alternate row colour");
    if (key == "selection_background") return _("Selected background");
    if (key == "selection_foreground") return _("Selected text");
    if (key == "font") return _("Font, size and style");
    if (key == "effect") return _("Gradient, pattern and texture");
    if (key == "background_image") return _("Background image");
    if (key == "shading") return _("Bar shading");
    if (key == "opacity") return _("Window opacity");
    return key;
}

std::vector<FXString> supportedProperties(const FXString& area)
{
    std::vector<FXString> result = {"background", "foreground"};
    if (area == "main") result.push_back("field");
    if (area != "list" && area != "tree" && area != "places") result.push_back("border");
    if (area == "list") result.push_back("alternate");
    if (area == "main" || area == "list" || area == "tree" || area == "places" ||
        area == "tabs" || area == "address" || area == "menus")
    {
        result.push_back("selection_background");
        result.push_back("selection_foreground");
    }
    if (area != "scrollbars") result.push_back("font");
    if (area == "list") result.push_back("effect");
    if (area == "main")
    {
        result.push_back("background_image");
        result.push_back("shading");
        result.push_back("opacity");
    }
    return result;
}

static FXbool isWithin(const FXWindow* ancestor, const FXWindow* window)
{
    for (const FXWindow* current = window; current; current = current->getParent())
        if (current == ancestor) return true;
    return false;
}

static void includeRegion(FXRectangle& bounds, FXWindow* root, FXWindow* window)
{
    if (!window || !window->shown() || window->getWidth() <= 0 || window->getHeight() <= 0) return;
    FXint x, y;
    window->translateCoordinatesTo(x, y, root, 0, 0);
    FXRectangle rectangle(x, y, window->getWidth(), window->getHeight());
    if (bounds.w <= 0 || bounds.h <= 0) bounds = rectangle;
    else
    {
        FXint right = FXMAX(bounds.x + bounds.w, rectangle.x + rectangle.w);
        FXint bottom = FXMAX(bounds.y + bounds.h, rectangle.y + rectangle.h);
        bounds.x = FXMIN(bounds.x, rectangle.x);
        bounds.y = FXMIN(bounds.y, rectangle.y);
        bounds.w = right - bounds.x;
        bounds.h = bottom - bounds.y;
    }
}

FXString plainLabel(FXString text)
{
    text = text.before('\t');
    text.substitute("&&", "\001");
    text.substitute("&", "");
    text.substitute("\001", "&");
    return text;
}

FXbool sameTheme(const Theme& a, const Theme& b)
{
    return a == b && a.name == b.name && a.tip == b.tip;
}

// Gallery folders are the "/"-separated parents of a graphic reference ("" is the top level).
FXString referenceFolder(const FXString& reference)
{
    FXString relative;
    if (reference.empty() || !BackgroundEffect::textureRelativePath(reference, relative)) return FXString::null;
    FXint slash = relative.rfind('/');
    return slash >= 0 ? relative.left(slash) : FXString::null;
}

FXString parentFolder(const FXString& folder)
{
    FXint slash = folder.rfind('/');
    return slash >= 0 ? folder.left(slash) : FXString::null;
}

struct GalleryPreview
{
    FXint index;
    FXString name;
    FXString path;
    FXbool loaded = false;
};

struct GalleryEntry
{
    FXString reference;
    FXString name;
    FXString path;
};

FXbool collectTileFolder(const FXString& root, const FXString& folder, FXbool pattern, FXbool includeFolders,
                         std::map<FXString, FXString>& files, std::set<FXString>& subfolders, FXString& rejected)
{
    FXString path = folder.empty() ? root : root + PATHSEPSTRING + folder;
    if (!folder.empty() && xf_islink(path)) return false;
    FXDir directory;
    if (!directory.open(path))
    {
        if (xf_existfile(path))
        {
            rejected += path + ": " + strerror(errno) + "\n";
            appLog(APP_LOG_ERRORS, pattern ? "pattern-directory-unreadable" : "texture-directory-unreadable", path);
        }
        return false;
    }
    while (directory.next())
    {
        FXString filename = directory.name();
        if (filename.empty() || filename[0] == '.') continue;
        FXString file = path + PATHSEPSTRING + filename;
        if (xf_islink(file)) continue;
        if (xf_isdirectory(file))
        {
            if (includeFolders)
            {
                if (folder.empty() || folder.contains('/') < 8) subfolders.insert(filename);
                else
                {
                    rejected += filename + ": " + _("Too many nested graphic categories.") + "\n";
                    appLog(APP_LOG_ERRORS, pattern ? "pattern-category-too-deep" : "texture-category-too-deep", file);
                }
            }
            continue;
        }
        if (!xf_isfile(file) || FXPath::extension(filename).lower() != "png") continue;
        FXString name = FXPath::title(filename);
        FXString relative = folder.empty() ? name : folder + "/" + name;
        FXString reference = BackgroundEffect::textureReference(relative);
        if (reference.empty())
        {
            rejected += filename + ": " + _("Invalid graphic name or category.") + "\n";
            appLog(APP_LOG_ERRORS, pattern ? "pattern-rejected" : "texture-rejected", file);
        }
        else
        {
            auto existing = files.find(reference);
            if (existing == files.end())
                files[reference] = file;
            else if (FXPath::directory(existing->second) == path && FXPath::extension(filename) == "png")
                existing->second = file;
        }
    }
    return true;
}

FXbool collectImageFolder(const FXString& root, const FXString& folder, std::map<FXString, FXString>& files,
                          std::set<FXString>& subfolders, FXString& rejected)
{
    FXString path = folder.empty() ? root : root + PATHSEPSTRING + folder;
    if (!folder.empty() && xf_islink(path)) return false;
    FXDir directory;
    if (!directory.open(path))
    {
        if (xf_existfile(path))
        {
            rejected += path + ": " + strerror(errno) + "\n";
            appLog(APP_LOG_ERRORS, "image-directory-unreadable", path);
        }
        return false;
    }
    while (directory.next())
    {
        FXString filename = directory.name();
        if (filename.empty() || filename[0] == '.') continue;
        FXString file = path + PATHSEPSTRING + filename;
        if (xf_islink(file)) continue;
        if (xf_isdirectory(file))
        {
            if (folder.empty() || folder.contains('/') < 8) subfolders.insert(filename);
            else
            {
                rejected += filename + ": " + _("Too many nested image categories.") + "\n";
                appLog(APP_LOG_ERRORS, "image-category-too-deep", file);
            }
            continue;
        }
        if (!xf_isfile(file)) continue;
        FXString extension = FXPath::extension(filename).lower();
        if (extension != "png" && extension != "jpg" && extension != "jpeg") continue;
        FXString relative = folder.empty() ? filename : folder + "/" + filename;
        FXString reference = BackgroundEffect::textureReference(relative);
        if (!BackgroundImage::validReference(reference))
        {
            rejected += relative + ": " + _("Invalid image name or category.") + "\n";
            appLog(APP_LOG_ERRORS, "image-rejected", file);
        }
        else if (!files.count(reference))
            files[reference] = file;
    }
    return true;
}

void listFolderFiles(const std::map<FXString, FXString>& entries, const FXString& folder,
                     std::vector<std::pair<FXString, FXString>>& files)
{
    for (const auto& entry : entries)
    {
        FXString relative;
        if (!BackgroundEffect::textureRelativePath(entry.first, relative)) continue;
        if (!folder.empty())
        {
            if (relative.left(folder.length() + 1) != folder + "/") continue;
            relative = relative.mid(folder.length() + 1, relative.length());
        }
        if (relative.find('/') < 0) files.push_back(std::make_pair(entry.first, relative));
    }
}

void showLocation(FXButton* up, FXLabel* location, const FXString& top, const FXString& folder)
{
    FXString path = folder;
    path.substitute("/", " / ");
    location->setText(folder.empty() ? top : top + " / " + path);
    if (folder.empty()) up->disable();
    else up->enable();
}

void reportBatchImportFailures(FXWindow* owner, const FXString& title, FXint imported, const FXString& failures)
{
    if (failures.empty()) return;
    FXString message = imported ?
        FXString().format(_("Imported %d files. Some files could not be imported:\n"), imported) :
        _("No files were imported:\n");
    MessageBox::error(owner, BOX_OK, title.text(), "%s", (message + failures).text());
}

struct FolderGraphicFile
{
    FXString source;
    FXString relative;
};

class FolderImportProgress : public DialogBox
{
    FXLabel* current;
    FXProgressBar* progress;
    FXlong lastUpdate = 0;
public:
    FXbool cancelled = false;
    explicit FolderImportProgress(FXWindow* owner) : DialogBox(owner, _("Importing folder"))
    {
        new FXLabel(this, _("Importing your folder,\nThis may take some time depending on the size of your folder"),
                    NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        current = new FXLabel(this, _("Scanning folder..."), NULL, JUSTIFY_LEFT | LAYOUT_FIX_WIDTH | LAYOUT_FILL_X,
                              0, 0, 480, 0);
        progress = new FXProgressBar(this, NULL, 0, PROGRESSBAR_PERCENTAGE | FRAME_SUNKEN | LAYOUT_FILL_X);
        progress->setTotal(1);
        new FXButton(this, _("Cancel"), NULL, this, ID_CANCEL,
                     BUTTON_NORMAL | BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT);
        create();
        show(PLACEMENT_OWNER);
    }
    FXbool updateProgress(const FXString& text, FXuint done = 0, FXuint total = 0, FXbool force = true)
    {
        if (cancelled) return false;
        FXlong now = FXThread::time();
        if (!force && now - lastUpdate < 50000000) return true;
        lastUpdate = now;
        current->setText(text);
        progress->setTotal(FXMAX(1, total));
        progress->setProgress(done);
        layout();
        repaint();
        getApp()->runModalWhileEvents(this);
        return !cancelled;
    }
    long handle(FXObject* sender, FXSelector selector, void* data) override
    {
        if (FXSELTYPE(selector) == SEL_CLOSE ||
            (FXSELTYPE(selector) == SEL_COMMAND && FXSELID(selector) == ID_CANCEL))
        {
            cancelled = true;
            return 1;
        }
        return DialogBox::handle(sender, selector, data);
    }
};

void collectFolderGraphicFiles(const FXString& root, const FXString& directory, const FXString& relative,
                               FXint depth, FXbool images, std::vector<FolderGraphicFile>& files,
                               FXString& failures, FolderImportProgress& progress)
{
    if (!progress.updateProgress(_("Scanning folder: ") + (relative.empty() ? FXPath::name(root) : relative),
                                 0, 0, false))
        return;
    FXDir entries;
    if (!entries.open(directory))
    {
        failures += (relative.empty() ? FXPath::name(root) : relative) + ": " +
                    _("Cannot read folder.") + "\n";
        return;
    }
    while (entries.next())
    {
        FXString name = entries.name();
        if (name.empty() || name == "." || name == "..") continue;
        if (!progress.updateProgress(FXString().format(_("Scanning folder... %u supported files found"),
                                                       static_cast<FXuint>(files.size())), 0, 0, false)) return;
        FXString source = directory + PATHSEPSTRING + name;
        FXString child = relative.empty() ? name : relative + "/" + name;
        if (name[0] == '.')
            failures += child + ": " + _("Hidden files and folders are not imported.") + "\n";
        else if (xf_islink(source))
        {
            failures += child + ": " + _("Symbolic links are not imported.") + "\n";
        }
        else if (xf_isdirectory(source))
        {
            if (depth >= 7)
                failures += child + ": " + _("Too many nested graphic categories.") + "\n";
            else
                collectFolderGraphicFiles(root, source, child, depth + 1, images, files, failures, progress);
        }
        else if (!xf_isfile(source))
        {
            failures += child + ": " + _("Not a regular file.") + "\n";
        }
        else
        {
            FXString extension = FXPath::extension(name).lower();
            FXbool supported = images ? extension == "png" || extension == "jpg" || extension == "jpeg" :
                                         extension == "png";
            if (supported)
            {
                FolderGraphicFile file = {source, child};
                files.push_back(file);
            }
            else
                failures += child + ": " + _("Unsupported file type.") + "\n";
        }
        if (progress.cancelled) return;
    }
}

class FolderImportConflictDialog : public DialogBox
{
    FXDECLARE(FolderImportConflictDialog)
    FolderImportConflictDialog() {}
public:
    enum { ID_MERGE = DialogBox::ID_LAST, ID_RENAME, ID_ANOTHER };
    FolderImportConflictDialog(FXWindow* owner, const FXString& category) :
        DialogBox(owner, _("Folder already exists"))
    {
        FXString message = FXString().format(_("The category \"%s\" already exists. How should the imported folder be handled?"),
                                             category.text());
        new FXLabel(this, message, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_FILL_X | PACK_UNIFORM_WIDTH);
        new FXButton(buttons, _("Merge"), NULL, this, ID_MERGE,
                     BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT | FRAME_GROOVE);
        new FXButton(buttons, _("Rename"), NULL, this, ID_RENAME, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("Save under another folder"), NULL, this, ID_ANOTHER, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
    }
    long onChoice(FXObject*, FXSelector selector, void*)
    {
        getApp()->stopModal(this, FXSELID(selector) - ID_MERGE + 1);
        hide();
        return 1;
    }
};

FXDEFMAP(FolderImportConflictDialog) FolderImportConflictDialogMap[] = {
    FXMAPFUNCS(SEL_COMMAND, FolderImportConflictDialog::ID_MERGE,
               FolderImportConflictDialog::ID_ANOTHER, FolderImportConflictDialog::onChoice)
};
FXIMPLEMENT(FolderImportConflictDialog, DialogBox, FolderImportConflictDialogMap,
            ARRAYNUMBER(FolderImportConflictDialogMap))

FXbool folderCategoryExists(const FXString& reference, FXbool pattern, FXbool image, const FXString& assetRoot)
{
    FXString relative;
    if (!BackgroundEffect::textureRelativePath(reference, relative)) return false;
    FXString directory = image ? "images" : pattern ? "patterns" : "textures";
    FXString roots[] = {
        assetRoot.empty() ? FXString::null : assetRoot + PATHSEPSTRING + directory,
        FXPath::directory(execpath) + "/../share/swordfish/" + directory,
        xf_userdatahome() + PATHSEPSTRING + directory
    };
    for (const FXString& root : roots)
        if (!root.empty() && xf_isdirectory(root + PATHSEPSTRING + relative)) return true;
    return false;
}

FXbool resolveFolderImportCategory(FXWindow* owner, const FXString& rootName, FXbool pattern, FXbool image,
                                   const FXString& assetRoot, FXString& category)
{
    FXString name = rootName, parent;
    while (true)
    {
        category = parent.empty() ? name : parent + "/" + name;
        FXString reference = BackgroundEffect::textureReference(category);
        if (reference.empty())
        {
            MessageBox::error(owner, BOX_OK, _("Import folder"), _("The folder name or category path is invalid."));
            return false;
        }
        if (!folderCategoryExists(reference, pattern, image, assetRoot)) return true;

        FolderImportConflictDialog conflict(owner, category);
        FXuint choice = conflict.execute();
        if (choice == 1) return true;
        if (choice == 2)
        {
            InputDialog naming(owner, "", _("New root category name:"), _("Rename imported folder"));
            naming.setText(name);
            if (!naming.execute()) return false;
            FXString renamed = naming.getText().trim();
            FXString renamedPath = parent.empty() ? renamed : parent + "/" + renamed;
            if (renamed.empty() || renamed.find_first_of("/\\") >= 0 ||
                BackgroundEffect::textureReference(renamedPath).empty())
            {
                MessageBox::error(owner, BOX_OK, _("Import folder"), _("Enter a category name without a slash."));
                continue;
            }
            name = renamed;
        }
        else if (choice == 3)
        {
            InputDialog destination(owner, "", _("Parent category path (use / for subcategories):"),
                                    _("Save under another folder"));
            if (!destination.execute()) return false;
            parent = destination.getText().trim();
            if (parent.empty() || BackgroundEffect::textureReference(parent + "/" + name).empty())
            {
                MessageBox::error(owner, BOX_OK, _("Import folder"), _("Enter a valid parent category path."));
                parent.clear();
            }
        }
        else
            return false;
    }
}

enum GraphicImportKind { GRAPHIC_PATTERN, GRAPHIC_TEXTURE, GRAPHIC_IMAGE };
enum GraphicImportResult { GRAPHIC_IMPORTED, GRAPHIC_SKIPPED, GRAPHIC_CANCELLED, GRAPHIC_FAILED };

class GraphicImportConflictDialog : public DialogBox
{
    FXDECLARE(GraphicImportConflictDialog)
    GraphicImportConflictDialog() {}
    FXLabel* incomingPreview = NULL;
    FXLabel* existingPreview = NULL;
    FXTextField* renameName = NULL;
    FXString incomingPath, existingPath, extension;
    FXIcon* incomingIcon = NULL;
    FXIcon* existingIcon = NULL;
    FXint previewSize = 160;
    FXbool oneToOnePreview = false;
    FXint incomingWidth = 0, incomingHeight = 0;
    FXint existingWidth = 0, existingHeight = 0;

    void updatePreviews()
    {
        FXString error;
        FXIcon* icon = graphicPreview(getApp(), incomingPath, previewSize, oneToOnePreview,
                                      incomingWidth, incomingHeight, error);
        incomingPreview->setIcon(icon);
        incomingPreview->setText(icon ? FXString::null : _("Preview unavailable"));
        delete incomingIcon;
        incomingIcon = icon;

        error.clear();
        icon = graphicPreview(getApp(), existingPath, previewSize, oneToOnePreview,
                              existingWidth, existingHeight, error);
        existingPreview->setIcon(icon);
        existingPreview->setText(icon ? FXString::null : _("Preview unavailable"));
        delete existingIcon;
        existingIcon = icon;
        recalc();
    }

    static FXIcon* graphicPreview(FXApp* app, const FXString& path, FXint size, FXbool oneToOne,
                                  FXint& width, FXint& height, FXString& error)
    {
        std::unique_ptr<FXImage> image = BackgroundImage::load(app, path, error);
        if (!image) return NULL;
        width = image->getWidth();
        height = image->getHeight();
        FXint canvasWidth = size * 2;
        FXint canvasHeight = size;
        FXColor* pixels = NULL;
        if (!FXMALLOC(&pixels, FXColor, canvasWidth * canvasHeight))
            throw FXMemoryException(_("Unable to create graphic preview"));
        for (FXint y = 0; y < canvasHeight; ++y)
            for (FXint x = 0; x < canvasWidth; ++x)
                pixels[y * canvasWidth + x] = ((x / 10 + y / 10) & 1) ?
                    FXRGB(0xC8, 0xC8, 0xC8) : FXRGB(0xF4, 0xF4, 0xF4);
        if (oneToOne)
        {
            FXint left = (canvasWidth - image->getWidth()) / 2;
            FXint top = (canvasHeight - image->getHeight()) / 2;
            for (FXint y = 0; y < canvasHeight; ++y)
                for (FXint x = 0; x < canvasWidth; ++x)
                {
                    FXint imageX = x - left, imageY = y - top;
                    if (imageX < 0 || imageX >= image->getWidth() ||
                        imageY < 0 || imageY >= image->getHeight()) continue;
                    FXColor color = image->getPixel(imageX, imageY);
                    FXColor background = pixels[y * canvasWidth + x];
                    FXint alpha = FXALPHAVAL(color);
                    pixels[y * canvasWidth + x] = FXRGB(
                        (FXREDVAL(color) * alpha + FXREDVAL(background) * (255 - alpha) + 127) / 255,
                        (FXGREENVAL(color) * alpha + FXGREENVAL(background) * (255 - alpha) + 127) / 255,
                        (FXBLUEVAL(color) * alpha + FXBLUEVAL(background) * (255 - alpha) + 127) / 255);
                }
        }
        else
            BackgroundImage::composite(pixels, canvasWidth, canvasHeight, *image, "fit");
        FXIcon* icon = new FXIcon(app, pixels, 0, IMAGE_KEEP | IMAGE_OWNED, canvasWidth, canvasHeight);
        icon->create();
        return icon;
    }

    void addFileDetails(FXComposite* parent, const FXString& path, FXint width, FXint height)
    {
        struct stat info;
        if (stat(path.text(), &info) == 0)
        {
            FXString size = xf_humansize(FXStringVal(static_cast<FXulong>(info.st_size)).text());
            FXString timeformat = getApp()->reg().readStringEntry("SETTINGS", "time_format", DEFAULT_TIME_FORMAT);
            new FXLabel(parent, size + " - " + FXSystem::time(timeformat.text(), info.st_mtime),
                        NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        }
        if (width > 0 && height > 0)
        {
            new FXLabel(parent, FXString().format("%d x %d", width, height), NULL,
                        JUSTIFY_LEFT | LAYOUT_FILL_X);
        }
    }

public:
    enum
    {
        ID_REPLACE = DialogBox::ID_LAST,
        ID_REPLACE_ALL,
        ID_SKIP,
        ID_SKIP_ALL,
        ID_RENAME,
        ID_ZOOM_OUT,
        ID_ZOOM_IN,
        ID_ZOOM_ONE,
        ID_ZOOM_FIT
    };

    GraphicImportConflictDialog(FXWindow* owner, const FXString& incoming, const FXString& existing,
                                const FXString& proposedName, const FXString& fileExtension) :
        DialogBox(owner, _("Graphic name already exists"),
                  DECOR_TITLE | DECOR_BORDER | DECOR_STRETCHABLE | DECOR_CLOSE, 0, 0, 1000, 500),
        incomingPath(incoming), existingPath(existing), extension(fileExtension)
    {
        FXHorizontalFrame* comparison = new FXHorizontalFrame(this, LAYOUT_FILL_X);
        FXVerticalFrame* incomingColumn = new FXVerticalFrame(comparison, LAYOUT_FILL_X);
        FXVerticalFrame* zoomColumn = new FXVerticalFrame(comparison, LAYOUT_CENTER_Y);
        FXVerticalFrame* existingColumn = new FXVerticalFrame(comparison, LAYOUT_FILL_X);

        new FXLabel(incomingColumn, _("Incoming"), NULL, JUSTIFY_CENTER_X | LAYOUT_FILL_X);
        new FXLabel(incomingColumn, FXPath::name(incomingPath), NULL, JUSTIFY_CENTER_X | LAYOUT_FILL_X);
        FXTextField* incomingLocation = new FXTextField(incomingColumn, 48, NULL, 0,
                                                        TEXTFIELD_READONLY | FRAME_SUNKEN | LAYOUT_FILL_X);
        incomingLocation->setText(FXPath::directory(incomingPath));
        incomingLocation->setTipText(incomingPath);
        incomingPreview = new FXLabel(incomingColumn, FXString::null, NULL,
                                      FRAME_SUNKEN | JUSTIFY_CENTER_X | JUSTIFY_CENTER_Y | LAYOUT_FILL_X);

        new FXButton(zoomColumn, "-", NULL, this, ID_ZOOM_OUT, BUTTON_NORMAL | LAYOUT_FILL_X);
        new FXButton(zoomColumn, "+", NULL, this, ID_ZOOM_IN, BUTTON_NORMAL | LAYOUT_FILL_X);
        new FXButton(zoomColumn, "100%", NULL, this, ID_ZOOM_ONE, BUTTON_NORMAL | LAYOUT_FILL_X);
        new FXButton(zoomColumn, _("Fit"), NULL, this, ID_ZOOM_FIT, BUTTON_NORMAL | LAYOUT_FILL_X);

        new FXLabel(existingColumn, _("Existing"), NULL, JUSTIFY_CENTER_X | LAYOUT_FILL_X);
        new FXLabel(existingColumn, FXPath::name(existingPath), NULL, JUSTIFY_CENTER_X | LAYOUT_FILL_X);
        FXTextField* existingLocation = new FXTextField(existingColumn, 48, NULL, 0,
                                                        TEXTFIELD_READONLY | FRAME_SUNKEN | LAYOUT_FILL_X);
        existingLocation->setText(FXPath::directory(existingPath));
        existingLocation->setTipText(existingPath);
        existingPreview = new FXLabel(existingColumn, FXString::null, NULL,
                                      FRAME_SUNKEN | JUSTIFY_CENTER_X | JUSTIFY_CENTER_Y | LAYOUT_FILL_X);
        updatePreviews();
        addFileDetails(incomingColumn, incomingPath, incomingWidth, incomingHeight);
        addFileDetails(existingColumn, existingPath, existingWidth, existingHeight);

        FXHorizontalFrame* renameRow = new FXHorizontalFrame(this, LAYOUT_FILL_X);
        new FXLabel(renameRow, _("New name:"), NULL, JUSTIFY_LEFT);
        renameName = new FXTextField(renameRow, 32, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        renameName->setText(proposedName);
        if (!extension.empty()) new FXLabel(renameRow, "." + extension, NULL, JUSTIFY_LEFT);
        new FXButton(renameRow, _("Rename"), NULL, this, ID_RENAME, BUTTON_NORMAL);

        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_FILL_X | PACK_UNIFORM_WIDTH);
        new FXButton(buttons, _("Replace"), NULL, this, ID_REPLACE, BUTTON_DEFAULT | FRAME_GROOVE);
        new FXButton(buttons, _("Replace all"), NULL, this, ID_REPLACE_ALL, BUTTON_NORMAL);
        new FXButton(buttons, _("Skip"), NULL, this, ID_SKIP, BUTTON_NORMAL);
        new FXButton(buttons, _("Skip all"), NULL, this, ID_SKIP_ALL, BUTTON_NORMAL);
        new FXButton(buttons, _("Cancel"), NULL, this, DialogBox::ID_CANCEL, BUTTON_NORMAL);
    }

    ~GraphicImportConflictDialog()
    {
        delete incomingIcon;
        delete existingIcon;
    }

    FXString getRenameName() const { return renameName->getText().trim(); }

    long onCommand(FXObject*, FXSelector selector, void*)
    {
        FXint id = FXSELID(selector);
        if (id >= ID_REPLACE && id <= ID_RENAME)
        {
            getApp()->stopModal(this, id - ID_REPLACE + 1);
            hide();
        }
        else if (id == DialogBox::ID_CANCEL)
        {
            getApp()->stopModal(this, 0);
            hide();
        }
        else if (id == ID_ZOOM_OUT)
        {
            previewSize = FXMAX(96, previewSize - 32);
            oneToOnePreview = false;
        }
        else if (id == ID_ZOOM_IN)
        {
            previewSize = FXMIN(320, previewSize + 32);
            oneToOnePreview = false;
        }
        else if (id == ID_ZOOM_ONE)
        {
            previewSize = 240;
            oneToOnePreview = true;
        }
        else if (id == ID_ZOOM_FIT)
        {
            previewSize = 160;
            oneToOnePreview = false;
        }
        else
            return 0;
        if (id >= ID_ZOOM_OUT && id <= ID_ZOOM_FIT) updatePreviews();
        return 1;
    }
};

FXDEFMAP(GraphicImportConflictDialog) GraphicImportConflictDialogMap[] = {
    FXMAPFUNCS(SEL_COMMAND, GraphicImportConflictDialog::ID_REPLACE,
               GraphicImportConflictDialog::ID_ZOOM_FIT, GraphicImportConflictDialog::onCommand),
    FXMAPFUNC(SEL_COMMAND, DialogBox::ID_CANCEL, GraphicImportConflictDialog::onCommand)
};
FXIMPLEMENT(GraphicImportConflictDialog, DialogBox, GraphicImportConflictDialogMap,
            ARRAYNUMBER(GraphicImportConflictDialogMap))

FXString graphicKindDirectory(GraphicImportKind kind)
{
    return kind == GRAPHIC_PATTERN ? "patterns" : kind == GRAPHIC_TEXTURE ? "textures" : "images";
}

FXString graphicNameAlreadyExists(GraphicImportKind kind)
{
    return kind == GRAPHIC_PATTERN ? _("Pattern name already exists. Please use another name.") :
           kind == GRAPHIC_TEXTURE ? _("Texture name already exists. Please use another name.") :
                                     _("Image name already exists. Please use another name.");
}

FXString graphicThemeNameExists()
{
    return _("This name is already used by the current theme. Please use another name.");
}

FXString graphicPath(GraphicImportKind kind, const FXString& reference, const FXString& assetRoot = FXString::null)
{
    if (kind == GRAPHIC_PATTERN) return BackgroundEffect::patternPath(reference, assetRoot);
    if (kind == GRAPHIC_TEXTURE) return BackgroundEffect::texturePath(reference, assetRoot);
    return BackgroundImage::path(reference, assetRoot);
}

FXbool graphicDestination(GraphicImportKind kind, const FXString& reference,
                          FXString& destination, FXString& error)
{
    FXString relative;
    if (!BackgroundEffect::textureRelativePath(reference, relative))
    {
        error = _("Invalid graphic name or category.");
        return false;
    }
    FXString directory = FXPath::absolute(xf_userdatahome() + PATHSEPSTRING + graphicKindDirectory(kind));
    struct stat rootInfo;
    if (lstat(directory.text(), &rootInfo) == 0)
    {
        if (!S_ISDIR(rootInfo.st_mode) || S_ISLNK(rootInfo.st_mode))
        {
            error = _("The graphic destination is not a safe folder: ") + directory;
            return false;
        }
    }
    else if (errno != ENOENT)
    {
        error = _("Cannot inspect graphic destination: ") + directory + ": " + strerror(errno);
        return false;
    }
    FXint separator = relative.rfind('/');
    FXString parents = separator < 0 ? FXString::null : relative.left(separator);
    FXString component;
    for (FXint i = 0; i <= parents.length(); ++i)
    {
        if (i == parents.length() || parents[i] == '/')
        {
            if (!component.empty())
            {
                directory += PATHSEPSTRING + component;
                struct stat info;
                if (lstat(directory.text(), &info) == 0)
                {
                    if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode))
                    {
                        error = _("The graphic destination is not a safe folder: ") + directory;
                        return false;
                    }
                }
                else if (errno != ENOENT)
                {
                    error = _("Cannot inspect graphic destination: ") + directory + ": " + strerror(errno);
                    return false;
                }
            }
            component.clear();
        }
        else component += parents[i];
    }
    FXString filename = separator < 0 ? relative : relative.rafter('/');
    if (kind != GRAPHIC_IMAGE) filename += ".png";
    destination = FXPath::absolute(directory + PATHSEPSTRING + filename);
    struct stat info;
    if (lstat(destination.text(), &info) == 0 && S_ISLNK(info.st_mode))
    {
        error = _("The graphic destination is a symbolic link: ") + destination;
        return false;
    }
    return true;
}

FXString renamedGraphicReference(const FXString& reference, const FXString& name, GraphicImportKind kind)
{
    FXString relative;
    if (!BackgroundEffect::textureRelativePath(reference, relative)) return FXString::null;
    FXString parent = FXPath::directory(relative);
    if (parent == ".") parent.clear();
    FXString extension = kind == GRAPHIC_IMAGE ? "." + FXPath::extension(relative) : FXString::null;
    if (name.empty() || name.find_first_of("/\\") >= 0) return FXString::null;
    FXString renamed = parent.empty() ? name + extension : parent + "/" + name + extension;
    return BackgroundEffect::textureReference(renamed);
}

FXString uniqueGraphicName(const FXString& reference, GraphicImportKind kind)
{
    FXString relative;
    if (!BackgroundEffect::textureRelativePath(reference, relative)) return "Copy";
    FXString filename = FXPath::name(relative);
    FXString base = kind == GRAPHIC_IMAGE ? FXPath::title(filename) : filename;
    FXString candidate = "Copy of " + base;
    for (FXint suffix = 0; suffix < 1000; ++suffix)
    {
        FXString name = suffix == 0 ? candidate : candidate + FXString().format(" (%d)", suffix);
        FXString renamed = renamedGraphicReference(reference, name, kind), destination, error;
        if (!renamed.empty() && graphicDestination(kind, renamed, destination, error) &&
            access(destination.text(), F_OK) != 0 && errno == ENOENT)
            return name;
    }
    return candidate;
}

// Keep originals beside their destinations until the whole folder import is accepted.
class FolderImportChanges
{
    struct Change { FXString destination, backup; FXbool restoreFailed; };
    std::vector<Change> changes;
    std::set<FXString> createdFolders;
    FXbool finished = false;

    FXString restore(const Change& change)
    {
        if (!change.backup.empty())
        {
            if (::rename(change.backup.text(), change.destination.text()) != 0)
                return _("Cannot restore original file; backup kept at ") + change.backup + ": " + strerror(errno) + "\n";
        }
        else if (::unlink(change.destination.text()) != 0 && errno != ENOENT)
            return _("Cannot remove imported file: ") + change.destination + ": " + strerror(errno) + "\n";
        return FXString::null;
    }
public:
    ~FolderImportChanges()
    {
        if (!finished)
        {
            FXString errors = finish(true);
            if (!errors.empty()) appLog(APP_LOG_ERRORS, "theme-folder-import-cleanup", errors);
        }
    }
    FXbool prepare(const FXString& destination, FXbool collision, FXString& error)
    {
        Change change = {destination, FXString::null, false};
        if (collision)
        {
            FXString pattern = FXPath::directory(destination) + "/.swordfish-import-XXXXXX";
            std::vector<char> temporary(pattern.text(), pattern.text() + pattern.length() + 1);
            int fd = mkstemp(temporary.data());
            if (fd < 0)
            {
                error = _("Cannot back up existing file: ") + destination + ": " + strerror(errno);
                return false;
            }
            close(fd);
            change.backup = temporary.data();
            if (::rename(destination.text(), change.backup.text()) != 0)
            {
                error = _("Cannot back up existing file: ") + destination + ": " + strerror(errno);
                if (::unlink(change.backup.text()) != 0)
                    error += "\n" + change.backup + ": " + strerror(errno);
                return false;
            }
        }
        for (FXString parent = FXPath::directory(destination); !parent.empty() && parent != "/";
             parent = FXPath::directory(parent))
        {
            struct stat info;
            if (lstat(parent.text(), &info) == 0) break;
            if (errno != ENOENT) break; // Destination preparation reports inaccessible parents.
            createdFolders.insert(parent);
        }
        changes.push_back(change);
        return true;
    }
    void failed(FXString& error)
    {
        FXString cleanup = restore(changes.back());
        if (cleanup.empty()) changes.pop_back();
        else
        {
            changes.back().restoreFailed = true;
            error += "\n" + cleanup;
        }
    }
    FXString finish(FXbool cancelled)
    {
        FXString errors;
        for (auto change = changes.rbegin(); change != changes.rend(); ++change)
        {
            if (cancelled) errors += restore(*change);
            else if (!change->restoreFailed && !change->backup.empty() && ::unlink(change->backup.text()) != 0)
                errors += _("Cannot remove import backup: ") + change->backup + ": " + strerror(errno) + "\n";
        }
        if (cancelled)
            for (auto folder = createdFolders.rbegin(); folder != createdFolders.rend(); ++folder)
                if (::rmdir(folder->text()) != 0 && errno != ENOENT)
                    errors += _("Cannot remove imported folder: ") + *folder + ": " + strerror(errno) + "\n";
        finished = true;
        return errors;
    }
};

GraphicImportResult importGraphicWithConflict(FXWindow* owner, FXApp* app, const FXString& source,
                                              FXString& reference, GraphicImportKind kind,
                                              const FXString& assetRoot, FXbool& replaceAll,
                                              FXbool& skipAll, FXString& error,
                                              FolderImportChanges* changes = NULL)
{
    while (true)
    {
        FXString destination;
        if (!graphicDestination(kind, reference, destination, error)) return GRAPHIC_FAILED;
        FXString theme = graphicPath(kind, reference, assetRoot);
        FXString existing = graphicPath(kind, reference);
        if (!assetRoot.empty() && !theme.empty() && theme != existing)
        {
            error = graphicThemeNameExists();
            return GRAPHIC_FAILED;
        }
        if (!existing.empty() && FXPath::absolute(existing) != destination)
        {
            error = graphicNameAlreadyExists(kind);
            return GRAPHIC_FAILED;
        }
        struct stat info;
        FXbool collision = lstat(destination.text(), &info) == 0;
        if (!collision && errno != ENOENT)
        {
            error = _("Cannot inspect graphic destination: ") + destination + ": " + strerror(errno);
            return GRAPHIC_FAILED;
        }
        if (collision && !S_ISREG(info.st_mode))
        {
            error = _("The graphic destination is not a regular file: ") + destination;
            return GRAPHIC_FAILED;
        }

        FXbool replace = false;
        if (collision)
        {
            struct stat sourceInfo;
            if (FXPath::absolute(source) == destination ||
                (stat(source.text(), &sourceInfo) == 0 && sourceInfo.st_dev == info.st_dev &&
                 sourceInfo.st_ino == info.st_ino))
                return GRAPHIC_SKIPPED;
            if (skipAll) return GRAPHIC_SKIPPED;
            if (replaceAll) replace = true;
            else
            {
                FXString relative;
                BackgroundEffect::textureRelativePath(reference, relative);
                FXString extension = kind == GRAPHIC_IMAGE ? FXPath::extension(relative) : "png";
                GraphicImportConflictDialog conflict(owner, source, destination,
                                                     uniqueGraphicName(reference, kind), extension);
                FXuint choice = conflict.execute();
                if (choice == 0) return GRAPHIC_CANCELLED;
                if (choice == 2) { replaceAll = true; replace = true; }
                else if (choice == 3) return GRAPHIC_SKIPPED;
                else if (choice == 4) { skipAll = true; return GRAPHIC_SKIPPED; }
                else if (choice == 5)
                {
                    FXString renamed = renamedGraphicReference(reference, conflict.getRenameName(), kind);
                    if (renamed.empty())
                    {
                        MessageBox::error(owner, BOX_OK, _("Import graphic"),
                                          _("Enter a valid name without a slash."));
                        continue;
                    }
                    reference = renamed;
                    continue;
                }
                else replace = true;
            }
        }
        if (changes && !changes->prepare(destination, collision, error)) return GRAPHIC_FAILED;
        FXbool imported = kind == GRAPHIC_PATTERN ?
            BackgroundEffect::importPattern(app, source, reference, false, error, replace) :
            kind == GRAPHIC_TEXTURE ?
            BackgroundEffect::importTexture(app, source, reference, false, error, replace) :
            BackgroundImage::importImage(app, source, reference, error, replace);
        if (!imported && changes) changes->failed(error);
        return imported ? GRAPHIC_IMPORTED : GRAPHIC_FAILED;
    }
}

FXString importGraphicFolder(FXWindow* owner, const FXString& sourceFolder, const FXString& category,
                              GraphicImportKind kind, const FXString& assetRoot)
{
    FolderImportProgress progress(owner);
    FolderImportChanges changes;
    std::vector<FolderGraphicFile> files;
    FXString failures, successes, skipped, activeReference;
    collectFolderGraphicFiles(sourceFolder, sourceFolder, "", 0, kind == GRAPHIC_IMAGE, files, failures, progress);
    std::sort(files.begin(), files.end(), [](const FolderGraphicFile& a, const FolderGraphicFile& b)
    {
        return a.relative < b.relative;
    });
    if (!progress.cancelled && files.empty() && failures.empty())
        failures = kind == GRAPHIC_IMAGE ? _("The folder contains no supported PNG or JPEG files.\n") :
                                           _("The folder contains no supported PNG files.\n");
    FXuint imported = 0, skipCount = 0, done = 0;
    FXbool replaceAll = false, skipAll = false;
    for (const FolderGraphicFile& file : files)
    {
        if (!progress.updateProgress(FXString().format(_("Importing %u of %u: "), done + 1,
                                     static_cast<FXuint>(files.size())) + file.relative, done, files.size())) break;
        FXString relative = kind == GRAPHIC_IMAGE ? file.relative :
            file.relative.left(file.relative.length() - FXPath::extension(file.relative).length() - 1);
        FXString reference = BackgroundEffect::textureReference(category + "/" + relative), error;
        GraphicImportResult result = importGraphicWithConflict(&progress, owner->getApp(), file.source, reference,
                                                               kind, assetRoot, replaceAll, skipAll, error, &changes);
        if (result == GRAPHIC_CANCELLED) { progress.cancelled = true; break; }
        if (result == GRAPHIC_IMPORTED)
        {
            if (activeReference.empty()) activeReference = reference;
            successes += file.relative + "\n";
            ++imported;
        }
        else if (result == GRAPHIC_SKIPPED)
        {
            skipped += file.relative + "\n";
            ++skipCount;
        }
        else failures += file.relative + ": " + (error.empty() ? _("Invalid graphic name or category.") : error) + "\n";
        ++done;
    }
    if (!progress.cancelled)
        progress.updateProgress(_("Finishing import..."), files.size(), files.size());
    FXString cleanupErrors = changes.finish(progress.cancelled);
    if (!cleanupErrors.empty())
    {
        failures += cleanupErrors;
        appLog(APP_LOG_ERRORS, "theme-folder-import-cleanup", cleanupErrors);
    }
    progress.hide();
    FXString message;
    if (progress.cancelled)
    {
        activeReference.clear();
        message = cleanupErrors.empty() ?
            _("Import cancelled. Imported media and newly created folders were removed.\nExisting files were kept.\n") :
            _("Import cancelled. Some changes could not be undone; see the errors below.\n");
    }
    else
    {
        message.format(_("Imported: %u\nSkipped: %u\n"), imported, skipCount);
        if (!successes.empty()) message += "\n" + FXString(_("Successfully imported:\n")) + successes;
        if (!skipped.empty()) message += "\n" + FXString(_("Skipped:\n")) + skipped;
    }
    if (!failures.empty()) message += "\n" + FXString(_("Errors and unsupported items:\n")) + failures;
    MessageBox report(owner, _("Import status"), message, failures.empty() ? biginfoicon : bigerroricon,
                      BOX_OK, JUSTIFY_LEFT, 0, 0, true);
    report.execute(PLACEMENT_OWNER);
    return activeReference;
}

// Tiles match the file list's thumbnail view (IconItem::drawBigIcon with thumbnail tiles).
class ThemeGalleryItem : public FXIconItem
{
public:
    using FXIconItem::FXIconItem;
    FXint getHeight(const FXIconList* list) const override
    {
        return FXIconItem::getHeight(list) + 8;
    }
protected:
    FXint hitItem(const FXIconList* list, FXint x, FXint y, FXint w, FXint h) const override
    {
        FXint hit = FXIconItem::hitItem(list, x, y, w, h);
        if (hit) return hit;
        return x + w > 3 && x < list->getItemSpace() - 3 &&
               y + h > 3 && y < list->getItemHeight() - 3 ? 1 : 0;
    }
    void drawBigIcon(const FXIconList* list, FXDC& dc, FXint x, FXint y, FXint w, FXint h) const override
    {
        FXColor back = list->getBackColor();
        FXColor shade = makeShadowColor(back);
        dc.setForeground(FXRGB((FXREDVAL(back) * 7 + FXREDVAL(shade)) / 8,
                               (FXGREENVAL(back) * 7 + FXGREENVAL(shade)) / 8,
                               (FXBLUEVAL(back) * 7 + FXBLUEVAL(shade)) / 8));
        dc.fillRectangle(x + 3, y + 3, w - 6, h - 6);
        dc.setForeground(FXRGB((FXREDVAL(back) * 3 + FXREDVAL(shade)) / 4,
                               (FXGREENVAL(back) * 3 + FXGREENVAL(shade)) / 4,
                               (FXBLUEVAL(back) * 3 + FXBLUEVAL(shade)) / 4));
        dc.drawRectangle(x + 3, y + 3, w - 7, h - 7);
        FXIconItem::drawBigIcon(list, dc, x, y + 4, w, h - 8);
    }
};

class SnapshotIconList : public IconList
{
public:
    using IconList::IconList;
    std::vector<FXRectangle> positions;
    std::vector<FXint> indices;
    int getItemAt(int x, int y, int = -1) override
    {
        for (size_t i = 0; i < positions.size(); ++i)
        {
            const FXRectangle& r = positions[i];
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return indices[i];
        }
        return -1;
    }
};
}

class ThemeMockup : public FXCanvas
{
    FXDECLARE(ThemeMockup)
    friend class ThemeWorkflowRegression;
    struct Row
    {
        FXRectangle rectangle;
        FXString text;
        FXIcon* icon = NULL;
        FXbool selected = false;
        FXint depth = 0;
        FXbool expandable = false;
        FXbool expanded = false;
    };
    struct Node
    {
        enum Kind { FRAME, LABEL, BUTTON, FIELD, HEADER, TREE, LIST, SCROLLBAR, ARROW, PROGRESS };
        Kind kind = FRAME;
        FXRectangle rectangle;
        FXString area, text, font;
        FXIcon* icon = NULL;
        FXuint justify = JUSTIFY_LEFT;
        FXuint iconPosition = ICON_BEFORE_TEXT;
        FXuint frame = FRAME_NONE;
        FXbool selected = false;
        FXbool sensitive = false;
        FXbool vertical = false;
        FXbool paintImage = false;
        FXbool inFilePanel = false;
        FXbool inAnyPanel = false;
        FXint thumbPosition = 0, thumbSize = 0;
        std::vector<Row> rows;
        IconList* list = NULL;
        std::vector<FXRectangle> positions;
        std::vector<FXString> labels;
        std::vector<FXbool> selections;
        std::unique_ptr<FXuint[]> columns;
    };
    std::vector<Node> nodes;
    std::vector<FXIcon*> ownedIcons;
    std::map<FXString, FXFont*> fonts;
    BackgroundOverlay imageOverlay;
    FXString overlayKey;
    XFileExplorer* sourceWindow = NULL;
    FXRectangle fileImageRegion, panelImageRegion;
    std::unique_ptr<FXMainWindow> holder;
    FXImage* image = NULL;
    Theme theme;
    ThemeEditor* editor = NULL;
    FXint sourceWidth = 1, sourceHeight = 1;
    double scale = 1;
    FXbool fit = true, showAreas = true, privateNames = false;
    FXint previewState = 0;
    FXString hover, selectedArea = "list";
    FXbool invalid = true;
    FXString contrastEffect, contrastAssetRoot;
    FXColor contrastBase = 0, contrastAlternate = 0;
    std::vector<double> luminances;
    ThemeMockup() {}

    FXIcon* copyIcon(FXIcon* icon)
    {
        if (!icon) return NULL;
        if (!icon->getData()) icon->restore();
        if (!icon->getData())
        {
            appLog(APP_LOG_ERRORS, "theme-preview-icon-unavailable", "Cannot read icon pixels");
            return NULL;
        }
        FXColor* pixels = NULL;
        if (!FXMEMDUP(&pixels, icon->getData(), FXColor, icon->getWidth() * icon->getHeight()))
            throw FXMemoryException(_("Unable to create theme preview"));
        FXIcon* copy = new FXIcon(getApp(), pixels, icon->getTransparentColor(),
                                   IMAGE_OWNED | IMAGE_KEEP, icon->getWidth(), icon->getHeight());
        copy->create();
        ownedIcons.push_back(copy);
        return copy;
    }

    FXFont* font(const FXString& description)
    {
        FXFont*& result = fonts[description];
        if (!result)
        {
            result = new FXFont(getApp(), description);
            result->create();
        }
        return result;
    }

    FXbool imageScopeAllows(const Node& node) const
    {
        FXString scope = theme.property("main", "image_scope");
        return scope == "files" ? node.inFilePanel : scope == "panels" ? node.inAnyPanel : true;
    }

    FXRectangle imageRegion() const
    {
        FXString scope = theme.property("main", "image_scope");
        if (scope == "files" && fileImageRegion.w > 0) return fileImageRegion;
        if (scope == "panels" && panelImageRegion.w > 0) return panelImageRegion;
        return FXRectangle(0, 0, sourceWidth, sourceHeight);
    }

    void captureList(IconList* source, Node& node)
    {
        node.columns.reset(new FXuint[source->nbCols]);
        std::copy(source->idCol, source->idCol + source->nbCols, node.columns.get());
        node.list = new SnapshotIconList(holder.get(), node.columns.get(), source->nbCols, NULL, 0,
                                  source->getListStyle() | LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT |
                                  LAYOUT_FIX_X | LAYOUT_FIX_Y, 0, 0, source->getWidth(), source->getHeight());
        IconList* list = node.list;
        for (FXint column = 0; column < source->getNumHeaders(); ++column)
            list->appendHeader(source->getHeaderText(column), NULL, source->getHeaderSize(column));
        list->inTrash = source->inTrash;
        list->numsortheader = source->numsortheader;
        list->viewIconSize = source->viewIconSize;
        list->detailIconSize = source->detailIconSize;
        list->thumbnailTiles = source->thumbnailTiles;
        list->font = font(source->getFont()->getFont());
        list->itemWidth = source->itemWidth;
        list->itemHeight = source->itemHeight;
        list->itemSpace = source->itemSpace;
        list->ncols = source->ncols;
        list->nrows = source->nrows;
        list->setPosition(source->getXPosition(), source->getYPosition());
        for (FXint i = 0; i < source->getNumItems(); ++i)
        {
            if (!source->isItemVisible(i)) continue;
            IconItem* original = source->items[i];
            IconItem* copy = new IconItem(getApp(), original->label, copyIcon(original->bigIcon),
                                          copyIcon(original->miniIcon));
            copy->state = original->state & ~(IconItem::BIGICONOWNED | IconItem::MINIICONOWNED);
            copy->bigIconBadge = copyIcon(original->bigIconBadge);
            copy->miniIconBadge = copyIcon(original->miniIconBadge);
            copy->treeDepth = original->treeDepth;
            copy->treeMark = original->treeMark;
            list->appendItem(copy);
            node.labels.push_back(original->label);
            node.selections.push_back(original->isSelected());
            FXint x = source->getXPosition(), y = source->getYPosition() + source->header->getDefaultHeight();
            FXint width = source->getWidth();
            if (source->options & (ICONLIST_BIG_ICONS | ICONLIST_MINI_ICONS))
            {
                FXint row = (source->options & ICONLIST_COLUMNS) ? i / source->ncols : i % source->nrows;
                FXint column = (source->options & ICONLIST_COLUMNS) ? i % source->ncols : i / source->nrows;
                x += column * source->itemSpace;
                y = source->getYPosition() + row * source->itemHeight;
                width = source->itemSpace;
            }
            else
                y += i * source->itemHeight;
            node.positions.push_back(FXRectangle(x, y, width, source->itemHeight));
            SnapshotIconList* snapshot = static_cast<SnapshotIconList*>(list);
            snapshot->positions.push_back(node.positions.back());
            snapshot->indices.push_back(i);
        }
    }

    void captureTree(FXTreeList* list, Node& node)
    {
        node.font = list->getFont()->getFont();
        FXint y = list->getYPosition();
        FXTreeItem* item = list->getFirstItem();
        while (item)
        {
            FXint height = list->getItemHeight(item), depth = 0;
            for (FXTreeItem* parent = item->getParent(); parent; parent = parent->getParent()) ++depth;
            if (y + height > 0 && y < list->getHeight())
            {
                Row row;
                row.rectangle = FXRectangle(0, y, list->getWidth(), height);
                row.text = item->getText();
                row.icon = copyIcon(item->isOpened() ? item->getOpenIcon() : item->getClosedIcon());
                row.selected = item->isSelected();
                row.depth = depth;
                row.expandable = item->hasItems();
                row.expanded = item->isExpanded();
                node.rows.push_back(row);
            }
            y += height;
            if (item->isExpanded() && item->getFirst()) item = item->getFirst();
            else
            {
                while (item && !item->getNext()) item = item->getParent();
                if (item) item = item->getNext();
            }
        }
    }

    void capture(FXWindow* window, FXWindow* root, const FXString& parentArea)
    {
        if (!window->shown() || window->getWidth() <= 0 || window->getHeight() <= 0) return;
        Node node;
        FXint x, y;
        window->translateCoordinatesTo(x, y, root, 0, 0);
        node.rectangle = FXRectangle(x, y, window->getWidth(), window->getHeight());
        node.area = themeWidgetArea(window, parentArea);
        // Matches the live widgets that show the window image (see drawBackgroundImage callers)
        node.paintImage = dynamic_cast<FXPacker*>(window) || dynamic_cast<FXHeader*>(window) ||
                          dynamic_cast<FXTreeList*>(window) || dynamic_cast<FXToolBarGrip*>(window) ||
                          (dynamic_cast<FXLabel*>(window) && (!dynamic_cast<FXButton*>(window) ||
                           (static_cast<FXButton*>(window)->getButtonStyle() & BUTTON_TOOLBAR))) ||
                          (FXString(window->getClassName()) == "FXFrame");
        node.inFilePanel = isWithin(sourceWindow->getLeftPanel(), window) ||
                          isWithin(sourceWindow->getRightPanel(), window);
        node.inAnyPanel = node.inFilePanel || isWithin(sourceWindow->getDirPanel(), window);
        for (FXWindow* owner = window; owner && node.paintImage; owner = owner->getParent())
            if (BackgroundImageOwner* imageOwner = dynamic_cast<BackgroundImageOwner*>(owner))
                node.paintImage = imageOwner->allowsBackgroundImage(window);
        node.font = getApp()->getNormalFont()->getFont();
        if (FXFrame* frame = dynamic_cast<FXFrame*>(window)) node.frame = frame->getFrameStyle();
        if (IconList* list = dynamic_cast<IconList*>(window))
        {
            node.kind = Node::LIST;
            captureList(list, node);
        }
        else if (FXTreeList* list = dynamic_cast<FXTreeList*>(window))
        {
            node.kind = Node::TREE;
            captureTree(list, node);
        }
        else if (FXHeader* header = dynamic_cast<FXHeader*>(window))
        {
            node.kind = Node::HEADER;
            node.font = header->getFont()->getFont();
            FXint columnX = 0;
            for (FXint i = 0; i < header->getNumItems(); ++i)
            {
                Row row;
                row.rectangle = FXRectangle(columnX, 0, header->getItemSize(i), header->getHeight());
                row.text = header->getItemText(i);
                row.icon = copyIcon(header->getItemIcon(i));
                node.rows.push_back(row);
                columnX += header->getItemSize(i);
            }
        }
        else if (FXScrollBar* scrollbar = dynamic_cast<FXScrollBar*>(window))
        {
            node.kind = Node::SCROLLBAR;
            node.vertical = scrollbar->getScrollBarStyle() & SCROLLBAR_VERTICAL;
            FXint length = node.vertical ? node.rectangle.h : node.rectangle.w;
            FXint breadth = node.vertical ? node.rectangle.w : node.rectangle.h;
            FXint range = FXMAX(1, scrollbar->getRange());
            node.thumbSize = FXMAX(breadth, (length - 2 * breadth) * scrollbar->getPage() / range);
            node.thumbPosition = breadth + (length - 2 * breadth - node.thumbSize) * scrollbar->getPosition() /
                                             FXMAX(1, range - scrollbar->getPage());
        }
        else if (FXLabel* label = dynamic_cast<FXLabel*>(window))
        {
            node.kind = dynamic_cast<FXButton*>(window) ? Node::BUTTON : Node::LABEL;
            node.text = plainLabel(label->getText());
            node.icon = copyIcon(label->getIcon());
            node.font = label->getFont()->getFont();
            node.justify = label->getJustify();
            node.iconPosition = label->getIconPosition();
            if (FXButton* button = dynamic_cast<FXButton*>(window)) node.selected = button->getState() != STATE_UP;
        }
        else if (FXMenuCaption* menu = dynamic_cast<FXMenuCaption*>(window))
        {
            node.kind = Node::LABEL;
            node.text = plainLabel(menu->getText());
            node.icon = copyIcon(menu->getIcon());
            node.font = menu->getFont()->getFont();
        }
        else if (FXTextField* field = dynamic_cast<FXTextField*>(window))
        {
            node.kind = Node::FIELD;
            node.text = field->getText();
            node.font = field->getFont()->getFont();
            node.sensitive = true;
        }
        else if (TextLabel* field = dynamic_cast<TextLabel*>(window))
        {
            node.kind = Node::FIELD;
            node.text = field->getText();
            node.font = field->getFont()->getFont();
            node.sensitive = true;
        }
        else if (dynamic_cast<FXArrowButton*>(window))
            node.kind = Node::ARROW;
        FXString area = node.area;
        nodes.push_back(std::move(node));
        for (FXWindow* child = window->getFirst(); child; child = child->getNext())
            capture(child, root, area);
    }

    void text(FXDC& dc, const FXString& label, FXFont* f, FXint x, FXint y, FXint width, FXint height,
              FXuint justification = JUSTIFY_LEFT)
    {
        FXString value = label;
        while (!value.empty() && f->getTextWidth(value.text(), value.length()) > width - 8)
            value.trunc(value.dec(value.length()));
        FXint textWidth = f->getTextWidth(value.text(), value.length());
        if (justification & JUSTIFY_RIGHT) x += width - textWidth - 4;
        else if (!(justification & JUSTIFY_LEFT)) x += (width - textWidth) / 2;
        else x += 4;
        dc.setFont(f);
        dc.drawText(x, y + (height - f->getFontHeight()) / 2 + f->getFontAscent(), value.text(), value.length());
    }

    std::unique_ptr<FXImage> drawList(Node& node)
    {
        IconList* list = node.list;
        list->setBackColor(theme.colorFor("list", "background"));
        list->setTextColor(theme.colorFor("list", "foreground"));
        list->setHighlightColor(theme.colorFor("list", "alternate"));
        list->setSelBackColor(theme.colorFor("list", "selection_background"));
        list->setSelTextColor(theme.colorFor("list", "selection_foreground"));
        if (list->backgroundEffect != theme.effect || list->effectAssetRoot != theme.assetRoot)
            list->setBackgroundEffect(theme.effect, theme.assetRoot);
        list->previewOverlay = imageOverlay.pixels.empty() || !imageScopeAllows(node) ? NULL : &imageOverlay;
        list->previewOverlayX = node.rectangle.x;
        list->previewOverlayY = node.rectangle.y;
        list->clearBackgroundEffect();
        list->font = font(theme.fontFor("list", getApp()->getNormalFont()->getFont()));
        std::unique_ptr<FXImage> layer(new FXImage(getApp(), NULL, IMAGE_KEEP, node.rectangle.w, node.rectangle.h));
        layer->create();
        {
            FXDCWindow content(layer.get());
            content.setClipRectangle(0, 0, node.rectangle.w, node.rectangle.h);
            list->fillBackground(content, 0, 0, node.rectangle.w, node.rectangle.h);
            content.setFont(list->font);
            for (FXint i = 0; i < list->items.no(); ++i)
            {
                IconItem* item = list->items[i];
                item->label = node.labels[i];
                if (privateNames)
                {
                    item->label.clear();
                    for (FXint column = 0; column <= node.labels[i].contains('\t'); ++column)
                    {
                        if (column) item->label += '\t';
                        FXString field = node.labels[i].section('\t', column);
                        item->label += column == 0 ? _("Item ") + FXStringVal(i + 1) :
                            field.find('/') >= 0 ? FXString(_("Private path")) : field;
                    }
                }
                item->setSelected(previewState == 1 ? i == 0 : node.selections[i]);
                item->setEnabled(previewState != 3);
                const FXRectangle& r = node.positions[i];
                item->draw(list, content, r.x, r.y, r.w, r.h);
            }
        }
        return layer;
    }

    void renderOverlay()
    {
        FXString reference = theme.property("main", "background_image");
        FXString mode = theme.property("main", "image_mode");
        FXRectangle region = imageRegion();
        FXint opacity = backgroundImageOpacity(theme.property("main", "image_opacity"));
        FXString key = reference + "|" + mode + "|" + theme.property("main", "image_scope") + "|" +
                       FXStringVal(region.x) + "," + FXStringVal(region.y) + "," +
                       FXStringVal(region.w) + "," + FXStringVal(region.h) + "|" +
                       FXStringVal(opacity) + "|" + theme.assetRoot + "|" +
                       FXStringVal(sourceWidth) + "x" + FXStringVal(sourceHeight);
        if (key == overlayKey) return;
        overlayKey = key;
        imageOverlay = BackgroundOverlay();
        if (reference.empty() || opacity <= 0) return;
        FXString path = BackgroundImage::path(reference, theme.assetRoot), error;
        std::unique_ptr<FXImage> source;
        if (!path.empty()) source = BackgroundImage::load(getApp(), path, error);
        if (!source)
        {
            appLog(APP_LOG_ERRORS, "theme-preview-image-unavailable", reference + ": " +
                   (error.empty() ? _("Image was not found in the theme or its library.") : error));
            return;
        }
        imageOverlay.render(*source, sourceWidth, sourceHeight, mode.empty() ? FXString("fit") : mode, opacity,
                            region.x, region.y, region.w, region.h);
    }

    void rebuild()
    {
        delete image;
        image = new FXImage(getApp(), NULL, IMAGE_KEEP, sourceWidth, sourceHeight);
        image->create();
        renderOverlay();
        std::map<const Node*, std::unique_ptr<FXImage>> listLayers;
        for (Node& node : nodes)
            if (node.list) listLayers[&node] = drawList(node);
        {
            FXDCWindow dc(image);
            dc.setForeground(theme.colorFor("main", "background"));
            dc.fillRectangle(0, 0, sourceWidth, sourceHeight);
            for (Node& node : nodes)
            {
                const FXRectangle& r = node.rectangle;
                FXColor background = theme.colorFor(node.area, "background");
                if (node.kind == Node::FIELD && node.area == "address" && !theme.overrides("address", "background"))
                    background = theme.colorFor("main", "field");
                FXColor foreground = theme.colorFor(node.area, "foreground");
                if (previewState == 3) foreground = themeBlend(background, foreground, 0.5);
                FXColor border = theme.colorFor(node.area, "border");
                FXFont* f = font(theme.fontFor(node.area, node.font));
                dc.setClipRectangle(r);
                dc.setForeground(background);
                dc.fillRectangle(r.x, r.y, r.w, r.h);
                if (node.paintImage && imageScopeAllows(node) && !node.selected &&
                    !imageOverlay.pixels.empty() && r.w > 0 && r.h > 0)
                {
                    FXColor* pixels = NULL;
                    if (!FXMALLOC(&pixels, FXColor, static_cast<size_t>(r.w) * r.h))
                        throw FXMemoryException(_("Unable to create theme preview"));
                    std::fill(pixels, pixels + static_cast<size_t>(r.w) * r.h, background);
                    imageOverlay.blend(pixels, r.w, r.h, r.x, r.y);
                    FXImage part(getApp(), pixels, IMAGE_OWNED, r.w, r.h);
                    part.create();
                    dc.drawImage(&part, r.x, r.y);
                }
                if (node.frame || node.kind == Node::FIELD || node.kind == Node::BUTTON)
                {
                    dc.setForeground(border);
                    dc.drawRectangle(r.x, r.y, FXMAX(0, r.w - 1), FXMAX(0, r.h - 1));
                }
                dc.setForeground(foreground);
                if (node.kind == Node::LIST) dc.drawImage(listLayers[&node].get(), r.x, r.y);
                else if (node.kind == Node::TREE || node.kind == Node::HEADER)
                {
                    for (size_t i = 0; i < node.rows.size(); ++i)
                    {
                        const Row& row = node.rows[i];
                        FXRectangle rectangle(r.x + row.rectangle.x, r.y + row.rectangle.y,
                                               row.rectangle.w, row.rectangle.h);
                        dc.setClipRectangle(r);
                        FXbool selected = previewState == 1 ? i == 0 : row.selected;
                        if (selected)
                        {
                            dc.setForeground(theme.colorFor(node.area, "selection_background"));
                            dc.fillRectangle(rectangle.x, rectangle.y, rectangle.w, rectangle.h);
                        }
                        if (node.kind == Node::HEADER)
                        {
                            dc.setForeground(border);
                            dc.drawRectangle(rectangle.x, rectangle.y, rectangle.w - 1, rectangle.h - 1);
                        }
                        FXint indent = node.kind == Node::TREE ? 18 + row.depth * 14 : 2;
                        if (row.expandable)
                        {
                            dc.setForeground(foreground);
                            dc.drawRectangle(rectangle.x + indent - 14, rectangle.y + rectangle.h / 2 - 4, 8, 8);
                            dc.drawLine(rectangle.x + indent - 12, rectangle.y + rectangle.h / 2,
                                         rectangle.x + indent - 8, rectangle.y + rectangle.h / 2);
                            if (!row.expanded) dc.drawLine(rectangle.x + indent - 10, rectangle.y + rectangle.h / 2 - 2,
                                                            rectangle.x + indent - 10, rectangle.y + rectangle.h / 2 + 2);
                        }
                        if (row.icon)
                        {
                            dc.drawIcon(row.icon, rectangle.x + indent,
                                         rectangle.y + (rectangle.h - row.icon->getHeight()) / 2);
                            indent += row.icon->getWidth() + 4;
                        }
                        dc.setForeground(selected ? theme.colorFor(node.area, "selection_foreground") : foreground);
                        FXString label = privateNames && node.kind == Node::TREE ?
                            _("Folder ") + FXStringVal(static_cast<FXint>(i) + 1) : row.text;
                        text(dc, label, f, rectangle.x + indent, rectangle.y, rectangle.w - indent, rectangle.h);
                    }
                }
                else if (node.kind == Node::SCROLLBAR)
                {
                    dc.setForeground(border);
                    FXRectangle thumb = node.vertical ?
                        FXRectangle(r.x + 2, r.y + node.thumbPosition, r.w - 4, node.thumbSize) :
                        FXRectangle(r.x + node.thumbPosition, r.y + 2, node.thumbSize, r.h - 4);
                    dc.fillRectangle(thumb.x, thumb.y, thumb.w, thumb.h);
                }
                else
                {
                    if (node.selected || (previewState == 2 && node.area == selectedArea))
                    {
                        dc.setForeground(theme.colorFor(node.area, "selection_background"));
                        dc.fillRectangle(r.x + 1, r.y + 1, FXMAX(0, r.w - 2), FXMAX(0, r.h - 2));
                        dc.setForeground(theme.colorFor(node.area, "selection_foreground"));
                    }
                    FXint indent = 0;
                    if (node.icon)
                    {
                        FXint iconX = node.text.empty() ? r.x + (r.w - node.icon->getWidth()) / 2 : r.x + 4;
                        dc.drawIcon(node.icon, iconX, r.y + (r.h - node.icon->getHeight()) / 2);
                        indent = node.icon->getWidth() + 4;
                    }
                    if (node.kind == Node::ARROW)
                    {
                        FXPoint points[3] = {FXPoint(r.x + r.w / 2 - 3, r.y + r.h / 2 - 2),
                                              FXPoint(r.x + r.w / 2 + 3, r.y + r.h / 2 - 2),
                                              FXPoint(r.x + r.w / 2, r.y + r.h / 2 + 2)};
                        dc.fillPolygon(points, 3);
                    }
                    if (!node.text.empty())
                    {
                        FXString label = privateNames && (node.sensitive || node.area == "tabs" ||
                            node.area == "address" || node.area == "status") ? _("Private view") : node.text;
                        text(dc, label, f, r.x + indent, r.y, r.w - indent, r.h, node.justify);
                    }
                }
            }
            dc.clearClipRectangle();
            if (previewState == 4)
            {
                FXint x = 10, y = 28, width = 250, height = 130;
                dc.setForeground(theme.colorFor("menus", "background"));
                dc.fillRectangle(x, y, width, height);
                dc.setForeground(theme.colorFor("menus", "border"));
                dc.drawRectangle(x, y, width, height);
                dc.setForeground(theme.colorFor("menus", "foreground"));
                FXFont* menuFont = font(theme.fontFor("menus", getApp()->getNormalFont()->getFont()));
                const char* labels[] = {_("Open"), _("Copy"), _("Paste"), _("Properties")};
                for (FXint i = 0; i < 4; ++i) text(dc, labels[i], menuFont, x + 8, y + i * 30, width - 16, 30);
            }
            if (previewState == 5 || previewState == 6)
            {
                FXint x = FXMAX(0, (sourceWidth - 420) / 2), y = FXMAX(0, (sourceHeight - 170) / 2);
                dc.setForeground(DialogColors::background);
                dc.fillRectangle(x, y, 420, 170);
                dc.setForeground(DialogColors::accent);
                dc.drawRectangle(x, y, 420, 170);
                dc.setForeground(DialogColors::text);
                FXFont* fixed = font(getApp()->getNormalFont()->getFont());
                text(dc, _("Fixed dialog palette (not theme editable)"), fixed, x + 12, y + 10, 396, 32);
                dc.setForeground(DialogColors::field);
                dc.fillRectangle(x + 16, y + 60, 388, 36);
                if (previewState == 6)
                {
                    dc.setForeground(DialogColors::accent);
                    dc.fillRectangle(x + 16, y + 60, 232, 36);
                }
                dc.setForeground(DialogColors::text);
                text(dc, previewState == 6 ? _("Copying... 60%") : _("Readable field text"), fixed,
                       x + 20, y + 60, 380, 36);
                dc.setForeground(DialogColors::accent);
                dc.drawRectangle(x + 300, y + 120, 100, 30);
                dc.setForeground(DialogColors::text);
                text(dc, _("Cancel"), fixed, x + 300, y + 120, 100, 30, JUSTIFY_CENTER_X);
            }
        }
        image->restore();
        invalid = false;
    }

public:
    ThemeMockup(FXComposite* parent, XFileExplorer* source, ThemeEditor* target) :
        FXCanvas(parent, NULL, 0, FRAME_SUNKEN | FRAME_THICK), sourceWindow(source), editor(target)
    {
        holder.reset(new FXMainWindow(getApp(), "Theme preview resources", NULL, NULL, DECOR_NONE));
        sourceWidth = FXMAX(1, source->getWidth());
        sourceHeight = FXMAX(1, source->getHeight());
        includeRegion(fileImageRegion, source, source->getLeftPanel());
        includeRegion(fileImageRegion, source, source->getRightPanel());
        panelImageRegion = fileImageRegion;
        includeRegion(panelImageRegion, source, source->getDirPanel());
        capture(source, source, "main");
        setDefaultCursor(getApp()->getDefaultCursor(DEF_CROSSHAIR_CURSOR));
    }
    ~ThemeMockup()
    {
        delete image;
        holder.reset();
        for (FXIcon* icon : ownedIcons) delete icon;
        for (const auto& entry : fonts) delete entry.second;
    }
    void setTheme(const Theme& appearance)
    {
        theme = appearance;
        invalid = true;
        update();
    }
    void setPreview(double zoom, FXbool outlines, FXbool privacy, FXint state, const FXString& area)
    {
        fit = zoom == 0;
        if (!fit)
        {
            scale = zoom;
            resize(FXMAX(1, static_cast<FXint>(sourceWidth * scale)),
                     FXMAX(1, static_cast<FXint>(sourceHeight * scale)));
        }
        showAreas = outlines;
        privateNames = privacy;
        previewState = state;
        selectedArea = area;
        invalid = true;
        recalc();
        update();
    }
    FXint getDefaultWidth() override { return fit ? 500 : static_cast<FXint>(sourceWidth * scale); }
    FXint getDefaultHeight() override { return fit ? 360 : static_cast<FXint>(sourceHeight * scale); }
    FXString hit(FXint x, FXint y) const
    {
        FXint sourceX = static_cast<FXint>(x / scale), sourceY = static_cast<FXint>(y / scale);
        for (auto node = nodes.rbegin(); node != nodes.rend(); ++node)
        {
            const FXRectangle& r = node->rectangle;
            if (sourceX >= r.x && sourceX < r.x + r.w && sourceY >= r.y && sourceY < r.y + r.h)
                return node->area;
        }
        return "main";
    }
    long onPaint(FXObject*, FXSelector, void* pointer)
    {
        if (invalid) rebuild();
        if (fit) scale = FXMIN(getWidth() / static_cast<double>(sourceWidth), getHeight() / static_cast<double>(sourceHeight));
        FXDCWindow dc(this, static_cast<FXEvent*>(pointer));
        dc.setForeground(DialogColors::field);
        dc.fillRectangle(0, 0, getWidth(), getHeight());
        if (image)
        {
            FXImage scaled(getApp(), image->getData(), IMAGE_KEEP, sourceWidth, sourceHeight);
            scaled.scale(FXMAX(1, static_cast<FXint>(sourceWidth * scale)),
                           FXMAX(1, static_cast<FXint>(sourceHeight * scale)), 1);
            scaled.create();
            dc.drawImage(&scaled, 0, 0);
        }
        dc.setForeground(DialogColors::accent);
        for (const Node& node : nodes)
        {
            if (node.kind == Node::FRAME || node.rectangle.w < 8 || node.rectangle.h < 8) continue;
            if (!showAreas && node.area != hover && node.area != selectedArea) continue;
            const FXRectangle& r = node.rectangle;
            dc.setLineWidth(node.area == hover || node.area == selectedArea ? 2 : 1);
            dc.drawRectangle(static_cast<FXint>(r.x * scale), static_cast<FXint>(r.y * scale),
                               FXMAX(1, static_cast<FXint>(r.w * scale) - 1),
                               FXMAX(1, static_cast<FXint>(r.h * scale) - 1));
        }
        return 1;
    }
    long onClick(FXObject*, FXSelector, void* pointer)
    {
        FXEvent* event = static_cast<FXEvent*>(pointer);
        if (previewState >= 5) return 1;
        editor->selectElement(hit(event->win_x, event->win_y));
        return 1;
    }
    long onMotion(FXObject*, FXSelector, void* pointer)
    {
        FXEvent* event = static_cast<FXEvent*>(pointer);
        FXString area = hit(event->win_x, event->win_y);
        if (hover != area)
        {
            hover = area;
            update();
        }
        return 1;
    }
    long onQueryTip(FXObject* sender, FXSelector, void*)
    {
        FXString tip = areaName(hover);
        sender->handle(this, FXSEL(SEL_COMMAND, FXWindow::ID_SETSTRINGVALUE), &tip);
        return 1;
    }
    double minimumContrast(FXColor foreground, const FXString& area)
    {
        if (invalid) rebuild();
        FXColor base = theme.colorFor(area, "background"), alternate = theme.colorFor("list", "alternate");
        FXString effect = area + "|" + theme.effect + "|" + overlayKey;
        if (luminances.empty() || contrastEffect != effect || contrastAssetRoot != theme.assetRoot ||
            contrastBase != base || contrastAlternate != alternate)
        {
            luminances.clear();
            luminances.push_back(themeLuminance(base));
            if (area == "list")
            {
                for (Node& node : nodes)
                {
                    IconList* list = node.list;
                    if (!list) continue;
                    list->setBackColor(base);
                    list->setHighlightColor(alternate);
                    list->setBackgroundEffect(theme.effect, theme.assetRoot);
                    list->previewOverlay = imageOverlay.pixels.empty() ? NULL : &imageOverlay;
                    list->previewOverlayX = node.rectangle.x;
                    list->previewOverlayY = node.rectangle.y;
                    list->buildBackgroundEffect();
                    FXbool details = !(list->getListStyle() & (ICONLIST_BIG_ICONS | ICONLIST_MINI_ICONS));
                    for (FXint layer = 0; layer < 4; ++layer)
                    {
                        if ((layer & 1) && (!details || !list->effectRows)) continue;
                        if ((layer & 2) && (!details || list->numsortheader < 0)) continue;
                        FXImage* pixels = list->getEffectLayer(layer);
                        if (pixels && !pixels->getData()) pixels->restore();
                        if (!pixels || !pixels->getData())
                        {
                            appLog(APP_LOG_ERRORS, "theme-preview-readback-failed", "Cannot read rendered background pixels");
                            luminances.clear();
                            return -1;
                        }
                        for (FXint i = 0; i < pixels->getWidth() * pixels->getHeight(); ++i)
                            luminances.push_back(themeLuminance(pixels->getData()[i]));
                        pixels->release();
                    }
                }
            }
            else if (!imageOverlay.pixels.empty())
            {
                for (const Node& node : nodes)
                {
                    const FXRectangle& r = node.rectangle;
                    if (node.area != area || !node.paintImage || !imageScopeAllows(node) ||
                        node.selected || r.w <= 0 || r.h <= 0) continue;
                    std::vector<FXColor> pixels(static_cast<size_t>(r.w) * r.h, base);
                    imageOverlay.blend(pixels.data(), r.w, r.h, r.x, r.y);
                    for (FXColor pixel : pixels) luminances.push_back(themeLuminance(pixel));
                }
            }
            std::sort(luminances.begin(), luminances.end());
            luminances.erase(std::unique(luminances.begin(), luminances.end()), luminances.end());
            contrastEffect = effect;
            contrastAssetRoot = theme.assetRoot;
            contrastBase = base;
            contrastAlternate = alternate;
        }
        double luminance = themeLuminance(foreground);
        auto nearest = std::lower_bound(luminances.begin(), luminances.end(), luminance);
        double minimum = 21;
        if (nearest != luminances.end()) minimum = (*nearest + 0.05) / (luminance + 0.05);
        if (nearest != luminances.begin()) minimum = FXMIN(minimum, (luminance + 0.05) / (*(nearest - 1) + 0.05));
        return minimum;
    }
};

FXDEFMAP(ThemeMockup) ThemeMockupMap[] = {
    FXMAPFUNC(SEL_PAINT, 0, ThemeMockup::onPaint),
    FXMAPFUNC(SEL_LEFTBUTTONPRESS, 0, ThemeMockup::onClick),
    FXMAPFUNC(SEL_LEFTBUTTONRELEASE, 0, ThemeMockup::onClick),
    FXMAPFUNC(SEL_MOTION, 0, ThemeMockup::onMotion),
    FXMAPFUNC(SEL_QUERY_TIP, 0, ThemeMockup::onQueryTip)
};
FXIMPLEMENT(ThemeMockup, FXCanvas, ThemeMockupMap, ARRAYNUMBER(ThemeMockupMap))

// Thumbnail chooser shared by the pattern, texture and background-image pickers: a grid of tiles
// that stretches to the full width, with a ".." item and folders listed before the graphics.
class ThemeGallery : public FXIconList
{
    FXDECLARE(ThemeGallery)
    static constexpr FXint minimumTile = 108;
    static constexpr FXint iconSize = 64;
    std::vector<FXString> folders;
    FXIcon* folderIcon = NULL;
    FXIcon* upIcon = NULL;
    FXIcon* loadingIcon = NULL;
    FXbool keyboard = false;
    FXbool swallowRepeat = false;
    ThemeGallery() {}
    FXIcon* scaled(FXIcon* source, FXIcon*& cache)
    {
        if (cache || !source) return cache ? cache : source;
        if (!source->getData()) source->restore();
        FXColor* pixels = NULL;
        if (!source->getData() ||
            !FXMEMDUP(&pixels, source->getData(), FXColor, source->getWidth() * source->getHeight()))
            return source;
        cache = new FXIcon(getApp(), pixels, source->getTransparentColor(), IMAGE_OWNED | IMAGE_KEEP,
                           source->getWidth(), source->getHeight());
        FXint longest = FXMAX(source->getWidth(), source->getHeight());
        cache->scale(FXMAX(1, source->getWidth() * iconSize / longest),
                     FXMAX(1, source->getHeight() * iconSize / longest), 1);
        cache->create();
        return cache;
    }
protected:
    FXIconItem* createItem(const FXString& text, FXIcon* big, FXIcon* mini, void* data) override
    {
        return new ThemeGalleryItem(text, big, mini, data);
    }
public:
    enum { FOLDER = 1, UP = 2 };
    ThemeGallery(FXComposite* parent, FXObject* target, FXSelector selector) :
        FXIconList(parent, target, selector,
                   ICONLIST_BIG_ICONS | ICONLIST_BROWSESELECT | ICONLIST_COLUMNS | LAYOUT_FILL_X | LAYOUT_FILL_Y)
    {
        setItemSpace(minimumTile);
        FXColor* pixels = NULL;
        if (FXMALLOC(&pixels, FXColor, iconSize * iconSize))
        {
            for (FXint y = 0; y < iconSize; ++y)
                for (FXint x = 0; x < iconSize; ++x)
                    pixels[y * iconSize + x] = ((x / 8 + y / 8) & 1) ?
                        FXRGB(0xC8, 0xC8, 0xC8) : FXRGB(0xF4, 0xF4, 0xF4);
            loadingIcon = new FXIcon(getApp(), pixels, 0, IMAGE_OWNED | IMAGE_KEEP, iconSize, iconSize);
            loadingIcon->create();
        }
    }
    ~ThemeGallery()
    {
        clearItems();
        delete folderIcon;
        delete upIcon;
        delete loadingIcon;
    }
    void reset()
    {
        clearItems();
        folders.clear();
    }
    void addUp(const FXString& parent)
    {
        appendItem("..", scaled(bigfolderupicon, upIcon), NULL, reinterpret_cast<void*>(UP));
        folders.push_back(parent);
    }
    void addFolder(const FXString& name, const FXString& path)
    {
        appendItem(name, scaled(bigfoldericon, folderIcon), NULL, reinterpret_cast<void*>(FOLDER));
        folders.push_back(path);
    }
    void addChoice(const FXString& label, FXIcon* icon)
    {
        appendItem(label, icon, icon);
        folders.push_back(FXString::null);
    }
    FXint addPendingChoice(const FXString& label)
    {
        appendItem(label, loadingIcon, loadingIcon);
        folders.push_back(FXString::null);
        return getNumItems() - 1;
    }
    FXbool isFolder(FXint index) const
    {
        return 0 <= index && index < getNumItems() && getItemData(index);
    }
    // Arrow keys and type-ahead only move the cursor; clicks, Enter and Backspace open folders.
    FXbool opens(FXint index) const
    {
        return isFolder(index) && !keyboard;
    }
    const FXString& folderAt(FXint index) const
    {
        return folders[index];
    }
    // Only the applied graphic is highlighted; FOX's browse mode would otherwise select the first folder.
    void settle()
    {
        if (getCurrentItem() < 0 || isFolder(getCurrentItem())) killSelection();
    }
    // The second click of a double-click must not act on the tile that replaced the opened folder.
    void opened()
    {
        swallowRepeat = true;
        if (getNumItems()) makeItemVisible(FXMAX(0, getCurrentItem()));
    }
    void layout() override
    {
        // Always reserve the scrollbar so the column count cannot oscillate as it appears.
        FXint usable = width - vertical->getDefaultWidth();
        FXint columns = FXMAX(1, usable / minimumTile);
        FXint space = FXMAX(minimumTile, usable / columns);
        if (space != itemSpace)
        {
            itemSpace = space;
            flags |= FLAG_RECALC;
        }
        FXIconList::layout();
    }
    long onGalleryKey(FXObject* sender, FXSelector selector, void* data)
    {
        FXEvent* event = static_cast<FXEvent*>(data);
        keyboard = event->code != KEY_Return && event->code != KEY_KP_Enter;
        if (event->code == KEY_BackSpace && isEnabled() && getNumItems() && getItemData(0) == reinterpret_cast<void*>(UP))
        {
            keyboard = false;
            setCurrentItem(0);
            if (target) target->tryHandle(this, FXSEL(SEL_COMMAND, message), reinterpret_cast<void*>(0));
            return 1;
        }
        return FXIconList::onKeyPress(sender, selector, data);
    }
    long onGalleryPress(FXObject* sender, FXSelector selector, void* data)
    {
        FXEvent* event = static_cast<FXEvent*>(data);
        keyboard = false;
        if (event->click_count > 1 && swallowRepeat)
        {
            handle(this, FXSEL(SEL_FOCUS_SELF, 0), data);
            return 1;
        }
        swallowRepeat = false;
        return FXIconList::onLeftBtnPress(sender, selector, data);
    }
};

FXDEFMAP(ThemeGallery) ThemeGalleryMap[] = {
    FXMAPFUNC(SEL_KEYPRESS, 0, ThemeGallery::onGalleryKey),
    FXMAPFUNC(SEL_LEFTBUTTONPRESS, 0, ThemeGallery::onGalleryPress)
};
FXIMPLEMENT(ThemeGallery, FXIconList, ThemeGalleryMap, ARRAYNUMBER(ThemeGalleryMap))

class BackgroundEditor : public DialogBox
{
    FXDECLARE(BackgroundEditor)
    ThemeEditor* editor = NULL;
    BackgroundEffect effect;
    FXListBox* fill = NULL;
    FXListBox* direction = NULL;
    FXSpinner* angle = NULL;
    FXGradientBar* gradient = NULL;
    FXList* stops = NULL;
    FXRealSpinner* position = NULL;
    ThemeGallery* patterns = NULL;
    FXButton* patternUp = NULL;
    FXLabel* patternLocation = NULL;
    FXTextField* patternTargetCategory = NULL;
    ThemeGallery* textures = NULL;
    FXButton* textureUp = NULL;
    FXLabel* textureLocation = NULL;
    FXTextField* textureTargetCategory = NULL;
    FXString patternFolder, textureFolder;
    FXSpinner* patternSize = NULL;
    FXSpinner* strength = NULL;
    FXCheckButton* originalColors = NULL;
    FXCheckButton* patternOriginalColors = NULL;
    FXCheckButton* rows = NULL;
    FXTextField* presetName = NULL;
    FXListBox* presets = NULL;
    FXLabel* status = NULL;
    FXLabel* patternStatus = NULL;
    FXLabel* textureStatus = NULL;
    std::vector<FXString> patternNames;
    std::vector<FXint> patternNumbers;
    std::vector<FXString> textureNames, presetEffects;
    std::vector<FXIcon*> patternIcons;
    std::vector<FXIcon*> textureIcons;
    std::vector<GalleryPreview> patternPreviews, texturePreviews;
    std::vector<GalleryEntry> patternEntries, textureEntries;
    size_t patternEntryIndex = 0, textureEntryIndex = 0;
    FXString patternRejected, textureRejected;
    FXString gradientBefore;
    FXbool syncing = false;
    BackgroundEditor() {}
    void changed(FXbool stop = false, const FXString& previous = "")
    {
        editor->previewEffect(effect.serialize(), stop, previous);
    }
    void sync()
    {
        syncing = true;
        if (effect.colors.empty()) effect.colors.push_back(editor->getDraft().colorFor("list", "background"));
        if (effect.colors.size() < 2) effect.colors.push_back(makeShadowColor(effect.colors[0]));
        FXbool validPositions = effect.positions.size() == effect.colors.size() &&
                                effect.positions.front() == 0 && effect.positions.back() == 1;
        for (size_t i = 1; validPositions && i < effect.positions.size(); ++i)
            validPositions = effect.positions[i] - effect.positions[i - 1] >= 0.000001;
        if (!validPositions)
        {
            effect.positions.clear();
            for (size_t i = 0; i < effect.colors.size(); ++i)
                effect.positions.push_back(i / static_cast<double>(effect.colors.size() - 1));
        }
        if (effect.midpoints.size() + 1 != effect.colors.size())
            effect.midpoints.assign(effect.colors.size() - 1, 0.5);
        std::vector<FXGradient> segments;
        for (size_t i = 0; i + 1 < effect.colors.size(); ++i)
        {
            FXGradient segment;
            segment.lower = effect.positions[i];
            segment.upper = effect.positions[i + 1];
            segment.middle = segment.lower + effect.midpoints[i] * (segment.upper - segment.lower);
            segment.lowerColor = effect.colors[i];
            segment.upperColor = effect.colors[i + 1];
            segment.blend = GRADIENT_BLEND_LINEAR;
            segments.push_back(segment);
        }
        gradient->setGradients(segments.data(), segments.size());
        FXint current = FXCLAMP(0, stops->getCurrentItem(), static_cast<FXint>(effect.colors.size()) - 1);
        stops->clearItems();
        for (size_t i = 0; i < effect.colors.size(); ++i)
            stops->appendItem(FXString().format("%.1f%%  ", effect.positions[i] * 100) +
                               themeColorText(effect.colors[i], true));
        stops->setCurrentItem(current);
        stops->selectItem(current);
        position->setValue(effect.positions[current] * 100);
        direction->setCurrentItem(effect.direction == "radial" ? 1 : 0);
        angle->setValue(effect.direction == "horizontal" ? 0 : effect.direction == "diagonal" ? 45 :
                         effect.angled ? static_cast<FXint>(effect.angle) : 90);
        fill->setCurrentItem(effect.fill == "gradient" ? 1 : effect.fill == "pattern" ? 2 : 0);
        patternSize->setValue(effect.size);
        strength->setValue(effect.strength);
        originalColors->setCheck(effect.originalColors);
        patternOriginalColors->setCheck(effect.patternOriginalColors);
        rows->setCheck(effect.rows);
        for (FXint i = 0; i < static_cast<FXint>(patternNames.size()); ++i)
            if (!patterns->isFolder(i) &&
                ((effect.fill != "pattern" && patternNames[i].empty() && patternNumbers[i] == 0) ||
                 (effect.fill == "pattern" && ((!effect.patternImage.empty() && effect.patternImage == patternNames[i]) ||
                  (effect.patternImage.empty() && patternNames[i].empty() && effect.pattern == patternNumbers[i])))))
                patterns->setCurrentItem(i);
        syncing = false;
    }
    // Theme colours made the samples nearly invisible on dark themes, so samples use a fixed neutral pair.
    static constexpr FXColor previewLight = FXRGB(0xD5, 0xDA, 0xDF);
    static constexpr FXColor previewDark = FXRGB(0x6E, 0x77, 0x80);
    static constexpr FXint previewSize = 64;

    FXbool addTileEntries(FXbool pattern, FXuint& budget)
    {
        std::vector<GalleryEntry>& entries = pattern ? patternEntries : textureEntries;
        size_t& next = pattern ? patternEntryIndex : textureEntryIndex;
        ThemeGallery* gallery = pattern ? patterns : textures;
        FXbool added = false;
        while (budget && next < entries.size())
        {
            const GalleryEntry& entry = entries[next++];
            FXint index = gallery->addPendingChoice(entry.name);
            gallery->disableItem(index);
            if (pattern)
            {
                patternNames.push_back(entry.reference);
                patternNumbers.push_back(0);
                if (effect.fill == "pattern" && entry.reference == effect.patternImage)
                {
                    gallery->setCurrentItem(index);
                    gallery->makeItemVisible(index);
                }
                patternPreviews.push_back({index, entry.name, entry.path});
            }
            else
            {
                textureNames.push_back(entry.reference);
                if (entry.reference == effect.texture)
                {
                    gallery->setCurrentItem(index);
                    gallery->makeItemVisible(index);
                }
                texturePreviews.push_back({index, entry.name, entry.path});
            }
            --budget;
            added = true;
        }
        return added;
    }

    FXbool loadTilePreviews(FXbool pattern, FXuint& budget)
    {
        ThemeGallery* gallery = pattern ? patterns : textures;
        std::vector<GalleryPreview>& pending = pattern ? patternPreviews : texturePreviews;
        std::vector<FXIcon*>& icons = pattern ? patternIcons : textureIcons;
        FXString& rejected = pattern ? patternRejected : textureRejected;
        FXLabel* label = pattern ? patternStatus : textureStatus;
        FXbool remaining = false;
        for (GalleryPreview& preview : pending)
        {
            if (preview.loaded) continue;
            remaining = true;
            if (!budget || !gallery->isItemVisible(preview.index)) continue;
            preview.loaded = true;
            --budget;
            FXString error;
            FXIcon* icon = (pattern ? BackgroundEffect::validatePatternHeader(preview.path, error) :
                                      BackgroundEffect::validateTextureHeader(preview.path, error)) ?
                           tileThumbnail(getApp(), preview.path, previewSize) : NULL;
            if (!icon)
            {
                if (error.empty()) error = _("The PNG image could not be decoded.");
                rejected += preview.name + ": " + error + "\n";
                appLog(APP_LOG_ERRORS, pattern ? "pattern-rejected" : "texture-rejected",
                       preview.path + ": " + error);
                gallery->disableItem(preview.index);
                label->setText(_("Some graphics were excluded:\n") + rejected);
                continue;
            }
            icons.push_back(icon);
            gallery->setItemBigIcon(preview.index, icon);
            gallery->setItemMiniIcon(preview.index, icon);
            gallery->enableItem(preview.index);
        }
        return remaining;
    }

    void scheduleTilePreviews()
    {
        getApp()->removeTimeout(this, ID_PREVIEWS);
        if (patternEntryIndex < patternEntries.size() || textureEntryIndex < textureEntries.size())
        {
            getApp()->addTimeout(this, ID_PREVIEWS, 1);
            return;
        }
        for (const GalleryPreview& preview : patternPreviews)
            if (!preview.loaded) { getApp()->addTimeout(this, ID_PREVIEWS, 10); return; }
        for (const GalleryPreview& preview : texturePreviews)
            if (!preview.loaded) { getApp()->addTimeout(this, ID_PREVIEWS, 10); return; }
    }

    void loadPatterns()
    {
        getApp()->removeTimeout(this, ID_PREVIEWS);
        patterns->reset();
        for (FXIcon* icon : patternIcons) delete icon;
        patternIcons.clear();
        patternPreviews.clear();
        patternEntries.clear();
        patternEntryIndex = 0;
        patternNames.clear();
        patternNumbers.clear();
        patternRejected.clear();
        extern FXString execpath;
        FXString directories[] = {editor->getDraft().assetRoot.empty() ? FXString::null :
                                      editor->getDraft().assetRoot + PATHSEPSTRING "patterns",
                                    FXPath::directory(execpath) + "/../share/swordfish/patterns",
                                    xf_userdatahome() + PATHSEPSTRING "patterns"};
        std::map<FXString, FXString> entries;
        FXString rejected;
        std::set<FXString> subfolders;
        std::vector<std::pair<FXString, FXString>> files;
        FXbool found = false;
        for (const FXString& directory : directories)
            if (!directory.empty())
                found = collectTileFolder(directory, patternFolder, true, true, entries, subfolders, rejected) || found;
        if (!found && !patternFolder.empty())
        {
            patternFolder.clear();
            entries.clear();
            subfolders.clear();
            rejected.clear();
            for (const FXString& directory : directories)
                if (!directory.empty())
                    collectTileFolder(directory, patternFolder, true, true, entries, subfolders, rejected);
            patternTargetCategory->setText(patternFolder);
        }
        listFolderFiles(entries, patternFolder, files);
        showLocation(patternUp, patternLocation, _("Patterns"), patternFolder);
        if (!patternFolder.empty()) patterns->addUp(parentFolder(patternFolder));
        for (const FXString& name : subfolders)
            patterns->addFolder(name, patternFolder.empty() ? name : patternFolder + "/" + name);
        patternNames.assign(patterns->getNumItems(), FXString::null);
        patternNumbers.assign(patterns->getNumItems(), 0);

        const char* names[] = {_("Horizontal stripes"), _("Vertical stripes"), _("Diagonal stripes"), _("Checks"),
                               _("Grid"), _("Dots"), _("Diamonds"), _("Crosshatch")};
        if (patternFolder.empty())
        {
            patterns->addChoice(_("No pattern"), NULL);
            patternNames.push_back("");
            patternNumbers.push_back(0);
            if (effect.fill != "pattern" || (effect.patternImage.empty() && effect.pattern == 0))
                patterns->setCurrentItem(patternNames.size() - 1);
            for (FXint i = 0; i < static_cast<FXint>(ARRAYNUMBER(names)); ++i)
            {
                BackgroundEffect sample = effect;
                sample.fill = "pattern";
                sample.pattern = i + 1;
                sample.patternImage.clear();
                sample.texture.clear();
                sample.size = 12;
                sample.rows = false;
                sample.colors = {previewLight, previewDark};
                FXIcon* icon = BackgroundEffect::preview(this, sample.serialize(), previewLight, previewSize);
                patternIcons.push_back(icon);
                patterns->addChoice(names[i], icon);
                patternNames.push_back("");
                patternNumbers.push_back(i + 1);
                if (effect.fill == "pattern" && effect.patternImage.empty() && effect.pattern == i + 1)
                    patterns->setCurrentItem(patternNames.size() - 1);
            }
        }
        for (const auto& file : files)
            patternEntries.push_back({file.first, file.second, entries[file.first]});
        patterns->settle();
        patternRejected = rejected;
        patternStatus->setText(rejected.empty() ? _("PNG patterns: at most 1024 x 1024 pixels. Originals are never moved.") :
                                                  _("Some patterns were excluded:\n") + rejected);
        scheduleTilePreviews();
    }

    void loadTextures()
    {
        getApp()->removeTimeout(this, ID_PREVIEWS);
        textures->reset();
        for (FXIcon* icon : textureIcons) delete icon;
        textureIcons.clear();
        texturePreviews.clear();
        textureEntries.clear();
        textureEntryIndex = 0;
        textureNames.clear();
        textureRejected.clear();
        extern FXString execpath, xdgconfighome;
        FXString directories[] = {editor->getDraft().assetRoot.empty() ? FXString::null :
                                      editor->getDraft().assetRoot + PATHSEPSTRING "textures",
                                    editor->getDraft().assetRoot,
                                    FXPath::directory(execpath) + "/../share/swordfish/textures",
                                    xf_userdatahome() + PATHSEPSTRING "textures",
                                    xdgconfighome + PATHSEPSTRING XFECONFIGPATH "/textures"};
        std::map<FXString, FXString> entries;
        FXString rejected;
        std::set<FXString> subfolders;
        FXbool found = false;
        for (FXint folder = 0; folder < static_cast<FXint>(ARRAYNUMBER(directories)); ++folder)
        {
            if (directories[folder].empty()) continue;
            if (folder == 1 && !textureFolder.empty()) continue;
            found = collectTileFolder(directories[folder], textureFolder, false, folder != 1,
                                      entries, subfolders, rejected) || found;
        }
        std::vector<std::pair<FXString, FXString>> files;
        if (!found && !textureFolder.empty())
        {
            textureFolder.clear();
            entries.clear();
            subfolders.clear();
            rejected.clear();
            for (FXint folder = 0; folder < static_cast<FXint>(ARRAYNUMBER(directories)); ++folder)
                if (!directories[folder].empty())
                    collectTileFolder(directories[folder], textureFolder, false, folder != 1,
                                      entries, subfolders, rejected);
            textureTargetCategory->setText(textureFolder);
        }
        listFolderFiles(entries, textureFolder, files);
        showLocation(textureUp, textureLocation, _("Textures"), textureFolder);
        if (!textureFolder.empty()) textures->addUp(parentFolder(textureFolder));
        for (const FXString& name : subfolders)
            textures->addFolder(name, textureFolder.empty() ? name : textureFolder + "/" + name);
        textureNames.assign(textures->getNumItems(), FXString::null);
        if (textureFolder.empty())
        {
            textures->addChoice(_("No texture"), NULL);
            textureNames.push_back("");
            if (effect.texture.empty()) textures->setCurrentItem(textureNames.size() - 1);
        }
        for (const auto& file : files)
            textureEntries.push_back({file.first, file.second, entries[file.first]});
        textures->settle();
        textureRejected = rejected;
        textureStatus->setText(rejected.empty() ? _("PNG tiles: at most 1024 x 1024 pixels. Originals are never moved.") :
                                                  _("Some textures were excluded:\n") + rejected);
        scheduleTilePreviews();
    }

    void openFolder(FXbool pattern, const FXString& folder)
    {
        (pattern ? patternFolder : textureFolder) = folder;
        (pattern ? patternTargetCategory : textureTargetCategory)->setText(folder);
        if (pattern) loadPatterns();
        else loadTextures();
        (pattern ? patterns : textures)->opened();
    }
public:
    enum
    {
        ID_LIVE = DialogBox::ID_LAST,
        ID_GRADIENT,
        ID_STOP,
        ID_POSITION,
        ID_STOP_ADD,
        ID_STOP_REMOVE,
        ID_STOP_COLOR,
        ID_COLOR_CHANGED,
        ID_TEXTURE,
        ID_ADD_PATTERN,
        ID_IMPORT_PATTERN_FOLDER,
        ID_REFRESH_PATTERN,
        ID_PATTERN_UP,
        ID_ADD_TEXTURE,
        ID_IMPORT_TEXTURE_FOLDER,
        ID_REFRESH,
        ID_TEXTURE_UP,
        ID_PRESET,
        ID_SAVE_PRESET,
        ID_RANDOM,
        ID_PREVIEWS,
        ID_LAST
    };
    BackgroundEditor(ThemeEditor* owner) :
        DialogBox(owner, _("File-list background"), DECOR_ALL, 0, 0, 760, 660), editor(owner),
        effect(BackgroundEffect::parse(owner->getDraft().effect))
    {
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXButton(buttons, _("Apply to mockup"), NULL, this, ID_ACCEPT,
                       BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT | FRAME_GROOVE);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("User Manual"), NULL, editor, ThemeEditor::ID_MANUAL, BUTTON_NORMAL | LAYOUT_LEFT);
        status = new FXLabel(this, "", NULL, JUSTIFY_LEFT | LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        FXHorizontalFrame* common = new FXHorizontalFrame(this, LAYOUT_FILL_X);
        new FXLabel(common, _("Fill:"));
        fill = new FXListBox(common, this, ID_LIVE, FRAME_SUNKEN | FRAME_THICK);
        fill->appendItem(_("Solid"));
        fill->appendItem(_("Gradient"));
        fill->appendItem(_("Pattern"));
        fill->setNumVisible(3);
        new FXButton(common, _("Fill colour..."), NULL, this, ID_STOP_COLOR, BUTTON_NORMAL);
        rows = new FXCheckButton(common, _("Alternate row tint"), this, ID_LIVE);
        FXTabBook* pages = new FXTabBook(this, NULL, 0, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        new DialogTabItem(pages, _("Gradient"));
        FXVerticalFrame* colors = new FXVerticalFrame(pages, FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        gradient = new FXGradientBar(colors, this, ID_GRADIENT,
                                      FRAME_SUNKEN | FRAME_THICK | GRADIENTBAR_CONTROLS_BOTTOM |
                                      LAYOUT_FILL_X | LAYOUT_FIX_HEIGHT,
                                      0, 0, 0, 65);
        new FXLabel(colors, _("Select a stop to edit its colour or position. Each stop change can be undone separately."),
                      NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        stops = new FXList(colors, this, ID_STOP, LIST_BROWSESELECT | LAYOUT_FILL_X | FRAME_SUNKEN | FRAME_THICK);
        stops->setNumVisible(4);
        FXHorizontalFrame* stopControls = new FXHorizontalFrame(colors, LAYOUT_FILL_X);
        new FXLabel(stopControls, _("Position (%):"));
        position = new FXRealSpinner(stopControls, 7, this, ID_POSITION, FRAME_SUNKEN | FRAME_THICK);
        position->setRange(0, 100);
        position->setIncrement(1);
        new FXButton(stopControls, _("Colour..."), NULL, this, ID_STOP_COLOR, BUTTON_NORMAL);
        new FXButton(stopControls, _("Add stop"), NULL, this, ID_STOP_ADD, BUTTON_NORMAL);
        new FXButton(stopControls, _("Remove stop"), NULL, this, ID_STOP_REMOVE, BUTTON_NORMAL);
        FXHorizontalFrame* directions = new FXHorizontalFrame(colors, LAYOUT_FILL_X);
        new FXLabel(directions, _("Type:"));
        direction = new FXListBox(directions, this, ID_LIVE, FRAME_SUNKEN | FRAME_THICK);
        direction->appendItem(_("Linear"));
        direction->appendItem(_("Radial"));
        direction->setNumVisible(2);
        new FXLabel(directions, _("Angle:"));
        angle = new FXSpinner(directions, 4, this, ID_LIVE, FRAME_SUNKEN | FRAME_THICK);
        angle->setRange(0, 359);
        new FXButton(directions, _("Random gradient"), NULL, this, ID_RANDOM, BUTTON_NORMAL);
        FXHorizontalFrame* presetControls = new FXHorizontalFrame(colors, LAYOUT_FILL_X);
        presetName = new FXTextField(presetControls, 18, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        presetName->setTipText(_("Optional gradient name; saved inside this theme"));
        new FXButton(presetControls, _("Name gradient"), NULL, this, ID_SAVE_PRESET, BUTTON_NORMAL);
        presets = new FXListBox(colors, this, ID_PRESET, FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X);
        presets->appendItem(_("Named / recent gradients"));
        presetEffects.push_back("");
        for (const auto& entry : owner->getDraft().properties)
        {
            if (entry.first.left(9) != "gradient.") continue;
            presets->appendItem(entry.first.mid(9, entry.first.length() - 9));
            presetEffects.push_back(entry.second);
        }
        for (FXint i = 0; i < 12; ++i)
        {
            FXString recent = getApp()->reg().readStringEntry("COLOR_PICKER", ("gradient" + FXStringVal(i)).text(), "");
            if (recent.empty()) continue;
            presets->appendItem(_("Recent gradient ") + FXStringVal(i + 1));
            presetEffects.push_back(recent);
        }
        presets->setNumVisible(8);
        new DialogTabItem(pages, _("Pattern"));
        FXVerticalFrame* patternPage = new FXVerticalFrame(pages, FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        FXHorizontalFrame* patternNavigation = new FXHorizontalFrame(patternPage, LAYOUT_FILL_X);
        patternUp = new FXButton(patternNavigation, _("Up") + FXString("\t") + _("Go to the parent folder (Backspace)"),
                                 minifolderupicon, this, ID_PATTERN_UP, BUTTON_NORMAL | ICON_BEFORE_TEXT);
        new FXLabel(patternNavigation, _("Location:"));
        patternLocation = new FXLabel(patternNavigation, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        patterns = new ThemeGallery(patternPage, this, ID_LIVE);
        FXHorizontalFrame* patternControls = new FXHorizontalFrame(patternPage, LAYOUT_FILL_X);
        new FXLabel(patternControls, _("Pattern size (pixels):"));
        patternSize = new FXSpinner(patternControls, 4, this, ID_LIVE, FRAME_SUNKEN | FRAME_THICK);
        patternSize->setRange(4, 256);
        new FXButton(patternControls, _("First colour..."), NULL, this, ID_STOP_COLOR, BUTTON_NORMAL);
        new FXButton(patternControls, _("Second colour..."), NULL, this, ID_STOP_COLOR + 100, BUTTON_NORMAL);
        patternOriginalColors = new FXCheckButton(patternPage,
                                                  _("Use original PNG colours (off: recolour with First/Second colour)"),
                                                  this, ID_LIVE);
        FXHorizontalFrame* patternFiles = new FXHorizontalFrame(patternPage, LAYOUT_FILL_X);
        new FXButton(patternFiles, _("Add PNG..."), NULL, this, ID_ADD_PATTERN, BUTTON_NORMAL);
        new FXButton(patternFiles, _("Add folder..."), NULL, this, ID_IMPORT_PATTERN_FOLDER, BUTTON_NORMAL);
        new FXButton(patternFiles, _("Refresh"), NULL, this, ID_REFRESH_PATTERN, BUTTON_NORMAL);
        new FXLabel(patternFiles, _("Save in category:"));
        patternTargetCategory = new FXTextField(patternFiles, 14, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        patternTargetCategory->setTipText(_("Optional category; use / for subcategories."));
        new FXLabel(patternPage, _("User patterns are stored in:") + FXString("\n") +
                                  xf_userdatahome() + PATHSEPSTRING "patterns", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        patternStatus = new FXLabel(patternPage, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        new DialogTabItem(pages, _("Texture"));
        FXVerticalFrame* texturePage = new FXVerticalFrame(pages, FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        FXHorizontalFrame* textureNavigation = new FXHorizontalFrame(texturePage, LAYOUT_FILL_X);
        textureUp = new FXButton(textureNavigation, _("Up") + FXString("\t") + _("Go to the parent folder (Backspace)"),
                                 minifolderupicon, this, ID_TEXTURE_UP, BUTTON_NORMAL | ICON_BEFORE_TEXT);
        new FXLabel(textureNavigation, _("Location:"));
        textureLocation = new FXLabel(textureNavigation, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        textures = new ThemeGallery(texturePage, this, ID_TEXTURE);
        FXHorizontalFrame* textureControls = new FXHorizontalFrame(texturePage, LAYOUT_FILL_X);
        new FXButton(textureControls, _("Add PNG..."), NULL, this, ID_ADD_TEXTURE, BUTTON_NORMAL);
        new FXButton(textureControls, _("Add folder..."), NULL, this, ID_IMPORT_TEXTURE_FOLDER, BUTTON_NORMAL);
        new FXButton(textureControls, _("Refresh"), NULL, this, ID_REFRESH, BUTTON_NORMAL);
        new FXLabel(textureControls, _("Save in category:"));
        textureTargetCategory = new FXTextField(textureControls, 14, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        textureTargetCategory->setTipText(_("Optional category; use / for subcategories."));
        new FXLabel(texturePage, _("User textures are stored in:") + FXString("\n") +
                                  xf_userdatahome() + PATHSEPSTRING "textures",
                    NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        textureStatus = new FXLabel(texturePage, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        originalColors = new FXCheckButton(texturePage, _("Use original PNG colours (including transparency)"),
                                              this, ID_LIVE);
        FXHorizontalFrame* opacity = new FXHorizontalFrame(texturePage, LAYOUT_FILL_X);
        new FXLabel(opacity, _("Texture opacity / strength (%):"));
        strength = new FXSpinner(opacity, 4, this, ID_LIVE, FRAME_SUNKEN | FRAME_THICK);
        strength->setRange(0, 100);
        patternFolder = referenceFolder(effect.patternImage);
        textureFolder = referenceFolder(effect.texture);
        patternTargetCategory->setText(patternFolder);
        textureTargetCategory->setText(textureFolder);
        loadPatterns();
        sync();
        loadTextures();
    }
    ~BackgroundEditor()
    {
        getApp()->removeTimeout(this, ID_PREVIEWS);
        for (FXIcon* icon : textureIcons) delete icon;
        for (FXIcon* icon : patternIcons) delete icon;
    }
    long onPreviewTimeout(FXObject*, FXSelector, void*)
    {
        FXuint entryBudget = 32;
        FXbool added = addTileEntries(true, entryBudget);
        added = addTileEntries(false, entryBudget) || added;
        FXuint budget = 4;
        FXbool remaining = loadTilePreviews(true, budget);
        remaining = loadTilePreviews(false, budget) || remaining;
        FXbool moreEntries = patternEntryIndex < patternEntries.size() ||
                             textureEntryIndex < textureEntries.size();
        if (moreEntries || (remaining && (added || budget < 4)))
            getApp()->addTimeout(this, ID_PREVIEWS, 1);
        else if (remaining)
            getApp()->addTimeout(this, ID_PREVIEWS, 100);
        return 1;
    }
    long onLive(FXObject* sender, FXSelector, void*)
    {
        if (syncing) return 1;
        if (sender == patterns && patterns->isFolder(patterns->getCurrentItem()))
        {
            if (patterns->opens(patterns->getCurrentItem()))
                openFolder(true, patterns->folderAt(patterns->getCurrentItem()));
            return 1;
        }
        if (sender == patterns)
        {
            FXint index = patterns->getCurrentItem();
            if (index < 0 || index >= static_cast<FXint>(patternNames.size()) ||
                patterns->isFolder(index) || !patterns->isItemEnabled(index)) return 1;
        }
        effect.fill = fill->getCurrentItem() == 1 ? "gradient" : fill->getCurrentItem() == 2 ? "pattern" : "solid";
        effect.direction = direction->getCurrentItem() == 1 ? "radial" : "linear";
        effect.angled = effect.direction == "linear";
        effect.angle = angle->getValue();
        if (sender == patterns ||
            (sender == fill && effect.fill == "pattern" && effect.pattern == 0 && effect.patternImage.empty()))
        {
            FXint index = patterns->getCurrentItem();
            if (sender == fill && (index < 0 || index >= static_cast<FXint>(patternNumbers.size()) ||
                patterns->isFolder(index) ||
                (patternNumbers[index] == 0 && patternNames[index].empty())))
            {
                index = -1;
                while (++index < patterns->getNumItems() &&
                       (patterns->isFolder(index) || !patterns->isItemEnabled(index) ||
                        (patternNumbers[index] == 0 && patternNames[index].empty()))) {}
            }
            if (index < 0 || index >= static_cast<FXint>(patternNumbers.size()) || patterns->isFolder(index))
            {
                if (sender == patterns) return 1;
                effect.pattern = 1;
                effect.patternImage.clear();
            }
            else
            {
                effect.pattern = patternNumbers[index];
                effect.patternImage = patternNames[index];
            }
        }
        effect.size = patternSize->getValue();
        effect.strength = strength->getValue();
        effect.originalColors = originalColors->getCheck();
        effect.patternOriginalColors = patternOriginalColors->getCheck();
        effect.rows = rows->getCheck();
        if (sender == patterns)
        {
            if (effect.pattern == 0 && effect.patternImage.empty())
            {
                if (effect.fill == "pattern") effect.fill = "solid";
                fill->setCurrentItem(effect.fill == "gradient" ? 1 : 0);
            }
            else
            {
                effect.fill = "pattern";
                fill->setCurrentItem(2);
            }
        }
        changed();
        if (sender == fill) sync();
        return 1;
    }
    long onStop(FXObject*, FXSelector, void*)
    {
        FXint index = stops->getCurrentItem();
        if (index >= 0) position->setValue(effect.positions[index] * 100);
        return 1;
    }
    long onStopChange(FXObject*, FXSelector selector, void*)
    {
        FXint index = FXCLAMP(0, stops->getCurrentItem(), static_cast<FXint>(effect.colors.size()) - 1);
        FXString previous = effect.serialize();
        if (FXSELID(selector) == ID_POSITION)
        {
            if (index == 0 || index + 1 == static_cast<FXint>(effect.colors.size()))
            {
                position->setValue(index == 0 ? 0 : 100);
                return 1;
            }
            effect.positions[index] = FXCLAMP(effect.positions[index - 1] + 0.001, position->getValue() / 100,
                                               effect.positions[index + 1] - 0.001);
        }
        else if (FXSELID(selector) == ID_STOP_ADD)
        {
            if (effect.colors.size() >= 16)
            {
                MessageBox::warning(this, BOX_OK, _("Gradient"), _("A gradient supports at most 16 stops."));
                return 1;
            }
            if (index + 1 == static_cast<FXint>(effect.colors.size())) --index;
            double midpoint = (effect.positions[index] + effect.positions[index + 1]) / 2;
            effect.positions.insert(effect.positions.begin() + index + 1, midpoint);
            FXColor lower = effect.colors[index], upper = effect.colors[index + 1];
            effect.colors.insert(effect.colors.begin() + index + 1,
                                   (themeBlend(lower, upper, 0.5) & FXRGBA(255, 255, 255, 0)) |
                                   FXRGBA(0, 0, 0, (FXALPHAVAL(lower) + FXALPHAVAL(upper)) / 2));
            effect.midpoints[index] = 0.5;
            effect.midpoints.insert(effect.midpoints.begin() + index + 1, 0.5);
        }
        else
        {
            if (effect.colors.size() <= 2 || index == 0 || index + 1 == static_cast<FXint>(effect.colors.size()))
            {
                MessageBox::warning(this, BOX_OK, _("Gradient"), _("Keep the first and last stops. Select an interior stop to remove."));
                return 1;
            }
            effect.positions.erase(effect.positions.begin() + index);
            effect.colors.erase(effect.colors.begin() + index);
            effect.midpoints[index - 1] = 0.5;
            effect.midpoints.erase(effect.midpoints.begin() + index);
        }
        effect.fill = "gradient";
        sync();
        changed(true, previous);
        return 1;
    }
    long onColor(FXObject*, FXSelector, void* pointer)
    {
        FXint index = FXCLAMP(0, stops->getCurrentItem(), static_cast<FXint>(effect.colors.size()) - 1);
        effect.colors[index] = static_cast<FXColor>(reinterpret_cast<FXuval>(pointer));
        changed();
        return 1;
    }
    long onEditColor(FXObject*, FXSelector selector, void*)
    {
        if (FXSELID(selector) == ID_STOP_COLOR + 100) stops->setCurrentItem(1);
        FXint index = FXCLAMP(0, stops->getCurrentItem(), static_cast<FXint>(effect.colors.size()) - 1);
        FXString previous = effect.serialize();
        ColorDialog picker(this, _("Background colour"));
        picker.addHelpButton(editor, ThemeEditor::ID_MANUAL);
        picker.setRGBA(effect.colors[index]);
        picker.setTheme(editor->paletteTheme());
        picker.setOpaqueOnly(false);
        picker.setTarget(this);
        picker.setSelector(ID_COLOR_CHANGED);
        if (picker.execute(PLACEMENT_OWNER))
        {
            effect.colors[index] = picker.getRGBA();
            changed(true, previous);
        }
        else
        {
            effect = BackgroundEffect::parse(previous);
            changed();
        }
        sync();
        return 1;
    }
    long onGradient(FXObject*, FXSelector selector, void*)
    {
        if (syncing) return 1;
        FXbool valid = gradient->getNumSegments() > 0;
        for (FXint segment = 0; valid && segment < gradient->getNumSegments(); ++segment)
        {
            double lower = gradient->getSegmentLower(segment), upper = gradient->getSegmentUpper(segment);
            double middle = gradient->getSegmentMiddle(segment);
            valid = std::isfinite(lower) && std::isfinite(upper) && std::isfinite(middle) &&
                    lower >= 0 && upper <= 1 && upper > lower && middle >= lower && middle <= upper;
        }
        if (!valid)
        {
            appLog(APP_LOG_ERRORS, "theme-gradient-invalid", "Invalid native gradient segment");
            if (!gradientBefore.empty()) changed(true, gradientBefore);
            gradientBefore.clear();
            sync();
            status->setText(_("Invalid gradient segment; the last valid positions were kept."));
            return 1;
        }
        if (gradientBefore.empty()) gradientBefore = effect.serialize();
        effect.colors.clear();
        effect.positions.clear();
        effect.midpoints.clear();
        for (FXint segment = 0; segment < gradient->getNumSegments(); ++segment)
        {
            if (!segment)
            {
                effect.colors.push_back(gradient->getSegmentLowerColor(segment));
                effect.positions.push_back(0);
            }
            effect.colors.push_back(gradient->getSegmentUpperColor(segment));
            effect.positions.push_back(gradient->getSegmentUpper(segment));
            double lower = gradient->getSegmentLower(segment), upper = gradient->getSegmentUpper(segment);
            effect.midpoints.push_back(FXCLAMP(0.001, (gradient->getSegmentMiddle(segment) - lower) / (upper - lower), 0.999));
        }
        effect.positions.back() = 1;
        effect.fill = "gradient";
        changed(FXSELTYPE(selector) == SEL_COMMAND, gradientBefore);
        if (FXSELTYPE(selector) == SEL_COMMAND) { gradientBefore.clear(); sync(); }
        return 1;
    }
    long onTexture(FXObject*, FXSelector, void*)
    {
        FXint index = textures->getCurrentItem();
        if (textures->isFolder(index))
        {
            if (textures->opens(index)) openFolder(false, textures->folderAt(index));
        }
        else if (index >= 0 && index < static_cast<FXint>(textureNames.size()))
        {
            if (!textures->isItemEnabled(index)) return 1;
            effect.texture = textureNames[index];
            changed();
        }
        return 1;
    }
    long onTileFiles(FXObject*, FXSelector selector, void*)
    {
        FXint id = FXSELID(selector);
        FXbool pattern = id == ID_ADD_PATTERN || id == ID_IMPORT_PATTERN_FOLDER ||
                         id == ID_REFRESH_PATTERN || id == ID_PATTERN_UP;
        FXTextField* target = pattern ? patternTargetCategory : textureTargetCategory;
        if (id == ID_PATTERN_UP || id == ID_TEXTURE_UP)
        {
            openFolder(pattern, parentFolder(pattern ? patternFolder : textureFolder));
            return 1;
        }
        if (id == ID_IMPORT_PATTERN_FOLDER || id == ID_IMPORT_TEXTURE_FOLDER)
        {
            FXString sourceFolder = FileDialog::getOpenDirectory(
                this, pattern ? _("Import pattern folder") : _("Import texture folder"), FXString::null);
            if (sourceFolder.empty()) return 1;
            if (xf_islink(sourceFolder))
            {
                MessageBox::error(this, BOX_OK, pattern ? _("Pattern") : _("Texture"),
                                  _("Symbolic-link folders are not imported."));
                return 1;
            }
            FXString destinationCategory;
            if (!resolveFolderImportCategory(this, FXPath::name(sourceFolder), pattern, false,
                                             editor->getDraft().assetRoot, destinationCategory)) return 1;
            FXString activeReference = importGraphicFolder(this, sourceFolder, destinationCategory,
                pattern ? GRAPHIC_PATTERN : GRAPHIC_TEXTURE, editor->getDraft().assetRoot);
            if (!activeReference.empty())
            {
                if (pattern)
                {
                    effect.pattern = 0;
                    effect.patternImage = activeReference;
                    effect.fill = "pattern";
                    fill->setCurrentItem(2);
                }
                else effect.texture = activeReference;
                changed();
                (pattern ? patternFolder : textureFolder) = referenceFolder(activeReference);
            }
            if (pattern) loadPatterns();
            else loadTextures();
            return 1;
        }
        if (id == ID_ADD_PATTERN || id == ID_ADD_TEXTURE)
        {
            FileDialog file(this, pattern ? _("Add PNG pattern") : _("Add PNG texture"));
            const char* filters[] = {pattern ? _("PNG patterns") : _("PNG textures"), "*.png", NULL};
            file.setPatternList(filters);
            file.setSelectMode(SELECT_FILE_MULTIPLE);
            file.showThumbnails(true, true);
            if (!file.execute()) return 1;
            std::unique_ptr<FXString[]> selected(file.getFilenames());
            if (!selected) return 1;
            FXint count = 0;
            while (!selected[count].empty()) ++count;
            if (!count) return 1;
            FXString singleName;
            if (count == 1)
            {
                InputDialog naming(this, "", pattern ? _("Pattern name (without a slash):") :
                                                       _("Texture name (without a slash):"),
                                   pattern ? _("Pattern name") : _("Texture name"));
                naming.addHelpButton(editor, ThemeEditor::ID_MANUAL);
                naming.setText(FXPath::title(selected[0]));
                if (!naming.execute()) return 1;
                singleName = naming.getText().trim();
            }
            FXString category = target->getText().trim();
            FXString activeReference, failures;
            FXint importedCount = 0;
            FXbool replaceAll = false, skipAll = false;
            for (FXint i = 0; i < count; ++i)
            {
                FXString name = count == 1 ? singleName : FXPath::title(selected[i]);
                FXString reference = name.find_first_of("/\\") >= 0 ? FXString::null :
                    BackgroundEffect::textureReference(category.empty() ? name : category + "/" + name);
                FXString error;
                GraphicImportResult result = importGraphicWithConflict(
                    this, getApp(), selected[i], reference, pattern ? GRAPHIC_PATTERN : GRAPHIC_TEXTURE,
                    editor->getDraft().assetRoot, replaceAll, skipAll, error);
                if (result == GRAPHIC_CANCELLED) break;
                if (result == GRAPHIC_IMPORTED)
                {
                    if (activeReference.empty()) activeReference = reference;
                    ++importedCount;
                }
                else if (result == GRAPHIC_FAILED)
                    failures += FXPath::name(selected[i]) + ": " +
                                (error.empty() ? _("Invalid graphic name or category.") : error) + "\n";
            }
            if (!activeReference.empty())
            {
                if (pattern)
                {
                    effect.pattern = 0;
                    effect.patternImage = activeReference;
                    effect.fill = "pattern";
                    fill->setCurrentItem(2);
                }
                else effect.texture = activeReference;
                changed();
                (pattern ? patternFolder : textureFolder) = referenceFolder(activeReference);
            }
            if (pattern) loadPatterns();
            else loadTextures();
            reportBatchImportFailures(this, pattern ? _("Pattern") : _("Texture"), importedCount, failures);
            return 1;
        }
        if (pattern) loadPatterns();
        else loadTextures();
        return 1;
    }
    long onPreset(FXObject*, FXSelector selector, void*)
    {
        if (FXSELID(selector) == ID_RANDOM)
        {
            FXString previous = effect.serialize();
            effect.fill = "gradient";
            FXuint seed = static_cast<FXuint>(FXThread::time());
            for (FXColor& color : effect.colors)
            {
                FXuint red = fxrandom(seed) & 255, green = fxrandom(seed) & 255, blue = fxrandom(seed) & 255;
                color = FXRGB(red, green, blue);
            }
            changed(true, previous);
            sync();
        }
        else if (FXSELID(selector) == ID_SAVE_PRESET)
        {
            FXString name = presetName->getText().trim();
            if (name.empty() || name.find_first_of(";\r\n") >= 0)
            {
                MessageBox::error(this, BOX_OK, _("Gradient"), _("Enter a gradient name without semicolons or newlines."));
                return 1;
            }
            editor->setGradientPreset(name, effect.serialize());
            presets->appendItem(name);
            presetEffects.push_back(effect.serialize());
        }
        else
        {
            FXint index = presets->getCurrentItem();
            if (index <= 0 || index >= static_cast<FXint>(presetEffects.size())) return 1;
            FXString previous = effect.serialize();
            effect = BackgroundEffect::parse(presetEffects[index]);
            changed(true, previous);
            sync();
        }
        return 1;
    }
    FXString specification() const { return effect.serialize(); }
};

FXDEFMAP(BackgroundEditor) BackgroundEditorMap[] = {
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_LIVE, BackgroundEditor::onLive),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_STOP, BackgroundEditor::onStop),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_POSITION, BackgroundEditor::onStopChange),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_STOP_ADD, BackgroundEditor::onStopChange),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_STOP_REMOVE, BackgroundEditor::onStopChange),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_STOP_COLOR, BackgroundEditor::onEditColor),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_STOP_COLOR + 100, BackgroundEditor::onEditColor),
    FXMAPFUNC(SEL_CHANGED, BackgroundEditor::ID_COLOR_CHANGED, BackgroundEditor::onColor),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_COLOR_CHANGED, BackgroundEditor::onColor),
    FXMAPFUNC(SEL_CHANGED, BackgroundEditor::ID_GRADIENT, BackgroundEditor::onGradient),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_GRADIENT, BackgroundEditor::onGradient),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_TEXTURE, BackgroundEditor::onTexture),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_ADD_PATTERN, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_IMPORT_PATTERN_FOLDER, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_REFRESH_PATTERN, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_PATTERN_UP, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_ADD_TEXTURE, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_IMPORT_TEXTURE_FOLDER, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_REFRESH, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_TEXTURE_UP, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_PRESET, BackgroundEditor::onPreset),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_SAVE_PRESET, BackgroundEditor::onPreset),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_RANDOM, BackgroundEditor::onPreset),
    FXMAPFUNC(SEL_TIMEOUT, BackgroundEditor::ID_PREVIEWS, BackgroundEditor::onPreviewTimeout)
};
FXIMPLEMENT(BackgroundEditor, DialogBox, BackgroundEditorMap, ARRAYNUMBER(BackgroundEditorMap))

class BackgroundImageEditor : public DialogBox
{
    FXDECLARE(BackgroundImageEditor)
    ThemeEditor* editor = NULL;
    FXString area, reference, folder;
    FXButton* up = NULL;
    FXLabel* location = NULL;
    FXListBox* placement = NULL;
    FXListBox* scope = NULL;
    FXSpinner* opacity = NULL;
    FXTextField* targetCategory = NULL;
    ThemeGallery* gallery = NULL;
    FXLabel* status = NULL;
    std::vector<FXString> names;
    std::vector<FXIcon*> icons;
    std::vector<GalleryPreview> previews;
    std::vector<GalleryEntry> entriesToAdd;
    size_t entryIndex = 0;
    FXString rejected;
    BackgroundImageEditor() {}

    FXString mode() const
    {
        return placement->getCurrentItem() == 1 ? "fill" : placement->getCurrentItem() == 2 ? "tile" : "fit";
    }

    void changed()
    {
        editor->previewImage(reference, mode(), opacity->getValue(),
                             scope->getCurrentItem() == 1 ? "files" :
                             scope->getCurrentItem() == 2 ? "panels" : "window");
    }

    void loadVisiblePreviews()
    {
        FXuint entryBudget = 32;
        FXbool added = false;
        while (entryBudget && entryIndex < entriesToAdd.size())
        {
            const GalleryEntry& entry = entriesToAdd[entryIndex++];
            FXint index = gallery->addPendingChoice(entry.name);
            gallery->disableItem(index);
            names.push_back(entry.reference);
            if (entry.reference == reference)
            {
                gallery->setCurrentItem(index);
                gallery->makeItemVisible(index);
            }
            previews.push_back({index, entry.name, entry.path});
            --entryBudget;
            added = true;
        }
        FXuint budget = 4;
        FXbool remaining = false;
        for (GalleryPreview& preview : previews)
        {
            if (preview.loaded) continue;
            remaining = true;
            if (!budget || !gallery->isItemVisible(preview.index)) continue;
            preview.loaded = true;
            --budget;
            FXString error;
            FXIcon* icon = BackgroundImage::preview(getApp(), preview.path,
                                                     FXRGB(0xD5, 0xDA, 0xDF), 64, error);
            if (!icon)
            {
                rejected += preview.name + ": " + error + "\n";
                appLog(APP_LOG_ERRORS, "image-rejected", preview.path + ": " + error);
                gallery->disableItem(preview.index);
                status->setText(_("Some images were excluded:\n") + rejected);
                continue;
            }
            icons.push_back(icon);
            gallery->setItemBigIcon(preview.index, icon);
            gallery->setItemMiniIcon(preview.index, icon);
            gallery->enableItem(preview.index);
        }
        FXbool moreEntries = entryIndex < entriesToAdd.size();
        if (moreEntries || (remaining && (added || budget < 4)))
            getApp()->addTimeout(this, ID_PREVIEWS, 1);
        else if (remaining)
            getApp()->addTimeout(this, ID_PREVIEWS, 100);
    }

    void refresh()
    {
        getApp()->removeTimeout(this, ID_PREVIEWS);
        gallery->reset();
        for (FXIcon* icon : icons) delete icon;
        icons.clear();
        previews.clear();
        entriesToAdd.clear();
        entryIndex = 0;
        names.clear();
        rejected.clear();
        extern FXString execpath;
        FXString roots[] = {
            editor->getDraft().assetRoot.empty() ? FXString::null :
                editor->getDraft().assetRoot + PATHSEPSTRING "images",
            FXPath::directory(execpath) + "/../share/swordfish/images",
            xf_userdatahome() + PATHSEPSTRING "images"
        };
        std::map<FXString, FXString> entries;
        std::set<FXString> subfolders;
        FXbool found = false;
        for (const FXString& root : roots)
            if (!root.empty())
                found = collectImageFolder(root, folder, entries, subfolders, rejected) || found;
        if (!found && !folder.empty())
        {
            folder.clear();
            entries.clear();
            subfolders.clear();
            rejected.clear();
            for (const FXString& root : roots)
                if (!root.empty())
                    collectImageFolder(root, folder, entries, subfolders, rejected);
            targetCategory->setText(folder);
        }
        std::vector<std::pair<FXString, FXString>> files;
        listFolderFiles(entries, folder, files);
        showLocation(up, location, _("Images"), folder);
        if (!folder.empty()) gallery->addUp(parentFolder(folder));
        for (const FXString& name : subfolders)
            gallery->addFolder(name, folder.empty() ? name : folder + "/" + name);
        names.assign(gallery->getNumItems(), FXString::null);
        if (folder.empty())
        {
            gallery->addChoice(_("No image"), NULL);
            names.push_back("");
            if (reference.empty()) gallery->setCurrentItem(names.size() - 1);
        }
        for (const auto& file : files)
            entriesToAdd.push_back({file.first, file.second, entries[file.first]});
        gallery->settle();
        status->setText(rejected.empty() ?
            _("PNG or JPEG, up to 32 MB and 32 megapixels. Originals are never moved.") :
            _("Some images were excluded:\n") + rejected);
        if (!entriesToAdd.empty()) getApp()->addTimeout(this, ID_PREVIEWS, 1);
    }

public:
    enum { ID_SELECT = DialogBox::ID_LAST, ID_PLACEMENT, ID_SCOPE, ID_UP, ID_IMPORT, ID_IMPORT_FOLDER,
           ID_REFRESH, ID_PREVIEWS };
    BackgroundImageEditor(ThemeEditor* owner, const FXString& selectedArea) :
        DialogBox(owner, _("Background image"), DECOR_ALL, 0, 0, 720, 600),
        editor(owner), area(selectedArea), reference(owner->getDraft().property(selectedArea, "background_image"))
    {
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXButton(buttons, _("Apply to mockup"), NULL, this, ID_ACCEPT,
                     BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT | FRAME_GROOVE);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("User Manual"), NULL, editor, ThemeEditor::ID_MANUAL, BUTTON_NORMAL | LAYOUT_LEFT);
        status = new FXLabel(this, "", NULL, JUSTIFY_LEFT | LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXLabel(this, _("User images are stored in:") + FXString("\n") +
                          xf_userdatahome() + PATHSEPSTRING "images", NULL, JUSTIFY_LEFT | LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        FXHorizontalFrame* controls = new FXHorizontalFrame(this, LAYOUT_FILL_X);
        up = new FXButton(controls, _("Up") + FXString("\t") + _("Go to the parent folder (Backspace)"),
                          minifolderupicon, this, ID_UP, BUTTON_NORMAL | ICON_BEFORE_TEXT);
        new FXLabel(controls, _("Location:"));
        location = new FXLabel(controls, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        new FXLabel(controls, _("Placement:"));
        placement = new FXListBox(controls, this, ID_PLACEMENT, FRAME_SUNKEN | FRAME_THICK);
        placement->appendItem(_("Fit (show whole image)"));
        placement->appendItem(_("Fill (crop to cover)"));
        placement->appendItem(_("Tile (original size)"));
        placement->setNumVisible(3);
        FXString currentMode = owner->getDraft().property(area, "image_mode");
        placement->setCurrentItem(currentMode == "fill" ? 1 : currentMode == "tile" ? 2 : 0);
        new FXLabel(controls, _("Area:"));
        scope = new FXListBox(controls, this, ID_SCOPE, FRAME_SUNKEN | FRAME_THICK);
        scope->appendItem(_("Whole UI"));
        scope->appendItem(_("File panels"));
        scope->appendItem(_("All panels"));
        scope->setNumVisible(3);
        FXString currentScope = owner->getDraft().property(area, "image_scope");
        scope->setCurrentItem(currentScope == "files" ? 1 : currentScope == "panels" ? 2 : 0);
        scope->setTipText(_("Fit or Fill scales one image across the selected area; Tile repeats at original size."));
        new FXLabel(controls, _("Opacity %:"));
        opacity = new FXSpinner(controls, 3, this, ID_PLACEMENT, FRAME_SUNKEN | FRAME_THICK);
        opacity->setRange(5, 100);
        opacity->setIncrement(5);
        opacity->setValue(FXMAX(5, backgroundImageOpacity(owner->getDraft().property(area, "image_opacity"))));
        opacity->setTipText(_("How strongly the image shows through every part of the window"));
        // Bottom rows are packed before the gallery, which takes the remaining space.
        FXHorizontalFrame* files = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
        new FXButton(files, _("Add image..."), NULL, this, ID_IMPORT, BUTTON_NORMAL);
        new FXButton(files, _("Add folder..."), NULL, this, ID_IMPORT_FOLDER, BUTTON_NORMAL);
        new FXButton(files, _("Refresh"), NULL, this, ID_REFRESH, BUTTON_NORMAL);
        new FXLabel(files, _("Save in category:"));
        targetCategory = new FXTextField(files, 16, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        targetCategory->setTipText(_("Optional category; use / for subcategories."));
        gallery = new ThemeGallery(this, this, ID_SELECT);
        folder = referenceFolder(reference);
        targetCategory->setText(folder);
        refresh();
    }
    ~BackgroundImageEditor()
    {
        getApp()->removeTimeout(this, ID_PREVIEWS);
        for (FXIcon* icon : icons) delete icon;
    }
    long onPreviewTimeout(FXObject*, FXSelector, void*)
    {
        loadVisiblePreviews();
        return 1;
    }
    long onSelect(FXObject*, FXSelector, void*)
    {
        FXint index = gallery->getCurrentItem();
        if (index >= 0 && !gallery->isItemEnabled(index)) return 1;
        if (gallery->isFolder(index))
        {
            if (gallery->opens(index)) openFolder(gallery->folderAt(index));
        }
        else if (index >= 0 && index < static_cast<FXint>(names.size()))
        {
            reference = names[index];
            changed();
        }
        return 1;
    }
    void openFolder(const FXString& path)
    {
        folder = path;
        targetCategory->setText(folder);
        refresh();
        gallery->opened();
    }
    long onPlacement(FXObject*, FXSelector, void*)
    {
        if (!reference.empty()) changed();
        return 1;
    }
    long onFiles(FXObject*, FXSelector selector, void*)
    {
        FXint id = FXSELID(selector);
        if (id == ID_UP)
        {
            openFolder(parentFolder(folder));
            return 1;
        }
        else if (id == ID_IMPORT_FOLDER)
        {
            FXString sourceFolder = FileDialog::getOpenDirectory(
                this, _("Import background-image folder"), FXString::null);
            if (sourceFolder.empty()) return 1;
            if (xf_islink(sourceFolder))
            {
                MessageBox::error(this, BOX_OK, _("Background image"), _("Symbolic-link folders are not imported."));
                return 1;
            }
            FXString destinationCategory;
            if (!resolveFolderImportCategory(this, FXPath::name(sourceFolder), false, true,
                                             editor->getDraft().assetRoot, destinationCategory)) return 1;
            FXString firstReference = importGraphicFolder(this, sourceFolder, destinationCategory, GRAPHIC_IMAGE,
                                                         editor->getDraft().assetRoot);
            if (!firstReference.empty())
            {
                reference = firstReference;
                changed();
                folder = referenceFolder(reference);
            }
            refresh();
            return 1;
        }
        else if (id == ID_IMPORT)
        {
            FileDialog file(this, _("Add background image"));
            const char* filters[] = {_("PNG and JPEG images"), "*.png,*.jpg,*.jpeg", _("All files"), "*", NULL};
            file.setPatternList(filters);
            file.setSelectMode(SELECT_FILE_MULTIPLE);
            file.showThumbnails(true, true);
            if (!file.execute()) return 1;
            std::unique_ptr<FXString[]> selected(file.getFilenames());
            if (!selected) return 1;
            FXint count = 0;
            while (!selected[count].empty()) ++count;
            if (!count) return 1;
            FXString singleName;
            if (count == 1)
            {
                InputDialog naming(this, "", _("Image name (without extension or a slash):"), _("Image name"));
                naming.addHelpButton(editor, ThemeEditor::ID_MANUAL);
                naming.setText(FXPath::title(selected[0]));
                if (!naming.execute()) return 1;
                singleName = naming.getText().trim();
            }
            FXString group = targetCategory->getText().trim(), firstReference, failures;
            FXint importedCount = 0;
            FXbool replaceAll = false, skipAll = false;
            for (FXint i = 0; i < count; ++i)
            {
                FXString sourceName = FXPath::name(selected[i]);
                FXString extension = FXPath::extension(selected[i]);
                FXString name = count == 1 ? singleName : FXPath::title(sourceName);
                FXString relative = group.empty() ? name + "." + extension : group + "/" + name + "." + extension;
                FXString candidate = name.empty() || name.find_first_of("/\\") >= 0 ?
                                     FXString::null : BackgroundEffect::textureReference(relative);
                FXString error;
                GraphicImportResult result = importGraphicWithConflict(
                    this, getApp(), selected[i], candidate, GRAPHIC_IMAGE, editor->getDraft().assetRoot,
                    replaceAll, skipAll, error);
                if (result == GRAPHIC_CANCELLED) break;
                if (result == GRAPHIC_IMPORTED)
                {
                    if (firstReference.empty()) firstReference = candidate;
                    ++importedCount;
                }
                else if (result == GRAPHIC_FAILED)
                    failures += sourceName + ": " +
                                (error.empty() ? _("Invalid image name or category.") : error) + "\n";
            }
            if (!firstReference.empty())
            {
                reference = firstReference;
                changed();
                folder = referenceFolder(reference);
            }
            refresh();
            reportBatchImportFailures(this, _("Background image"), importedCount, failures);
            return 1;
        }
        refresh();
        return 1;
    }
};

FXDEFMAP(BackgroundImageEditor) BackgroundImageEditorMap[] = {
    FXMAPFUNC(SEL_COMMAND, BackgroundImageEditor::ID_SELECT, BackgroundImageEditor::onSelect),
    FXMAPFUNC(SEL_COMMAND, BackgroundImageEditor::ID_PLACEMENT, BackgroundImageEditor::onPlacement),
    FXMAPFUNC(SEL_COMMAND, BackgroundImageEditor::ID_SCOPE, BackgroundImageEditor::onPlacement),
    FXMAPFUNC(SEL_TIMEOUT, BackgroundImageEditor::ID_PREVIEWS, BackgroundImageEditor::onPreviewTimeout),
    FXMAPFUNCS(SEL_COMMAND, BackgroundImageEditor::ID_UP, BackgroundImageEditor::ID_REFRESH, BackgroundImageEditor::onFiles)
};
FXIMPLEMENT(BackgroundImageEditor, DialogBox, BackgroundImageEditorMap, ARRAYNUMBER(BackgroundImageEditorMap))

class ThemeDecision : public DialogBox
{
    FXDECLARE(ThemeDecision)
    ThemeDecision() {}
public:
    enum { ID_FIRST = DialogBox::ID_LAST, ID_SECOND };
    ThemeDecision(ThemeEditor* owner, const FXString& title, const FXString& question,
                  const FXString& first, const FXString& second) : DialogBox(owner, title)
    {
        new FXLabel(this, question, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_FILL_X | PACK_UNIFORM_WIDTH);
        new FXButton(buttons, first, NULL, this, ID_FIRST, BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT | FRAME_GROOVE);
        new FXButton(buttons, second, NULL, this, ID_SECOND, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("User Manual"), NULL, owner, ThemeEditor::ID_MANUAL, BUTTON_NORMAL | LAYOUT_LEFT);
    }
    long onChoice(FXObject*, FXSelector selector, void*)
    {
        getApp()->stopModal(this, FXSELID(selector) == ID_FIRST ? 1 : 2);
        hide();
        return 1;
    }
};
FXDEFMAP(ThemeDecision) ThemeDecisionMap[] = {
    FXMAPFUNC(SEL_COMMAND, ThemeDecision::ID_FIRST, ThemeDecision::onChoice),
    FXMAPFUNC(SEL_COMMAND, ThemeDecision::ID_SECOND, ThemeDecision::onChoice)
};
FXIMPLEMENT(ThemeDecision, DialogBox, ThemeDecisionMap, ARRAYNUMBER(ThemeDecisionMap))

namespace
{
FXbool compositorAvailable(FXApp* app)
{
    Display* display = static_cast<Display*>(app->getDisplay());
    FXString selection = "_NET_WM_CM_S" + FXStringVal(DefaultScreen(display));
    return XGetSelectionOwner(display, XInternAtom(display, selection.text(), False)) != None;
}
}

FXDEFMAP(ThemeEditor) ThemeEditorMap[] = {
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_START_CURRENT, ThemeEditor::onStart),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_START_NEW, ThemeEditor::onStart),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_START_DUPLICATE, ThemeEditor::onStart),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_ELEMENT, ThemeEditor::onElement),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_PROPERTY, ThemeEditor::onProperty),
    FXMAPFUNC(SEL_DOUBLECLICKED, ThemeEditor::ID_PROPERTY, ThemeEditor::onEditProperty),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_EDIT_PROPERTY, ThemeEditor::onEditProperty),
    FXMAPFUNC(SEL_CHANGED, ThemeEditor::ID_COLOR, ThemeEditor::onColor),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_COLOR, ThemeEditor::onColor),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_INHERIT, ThemeEditor::onInherit),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_APPLY, ThemeEditor::onApply),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_DISCARD, ThemeEditor::onDiscard),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_UNDO, ThemeEditor::onUndo),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_REDO, ThemeEditor::onRedo),
    FXMAPFUNCS(SEL_COMMAND, ThemeEditor::ID_RESET_PROPERTY, ThemeEditor::ID_DEFAULTS, ThemeEditor::onReset),
    FXMAPFUNCS(SEL_COMMAND, ThemeEditor::ID_COMPARE, ThemeEditor::ID_STATE, ThemeEditor::onPreview),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_FIX_CONTRAST, ThemeEditor::onFixContrast),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_SAVE, ThemeEditor::onSave),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_SAVE_AS, ThemeEditor::onSave),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_CLOSE, ThemeEditor::onCloseEditor),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_CANCEL, ThemeEditor::onCloseEditor),
    FXMAPFUNC(SEL_CLOSE, 0, ThemeEditor::onCloseEditor),
    FXMAPFUNC(SEL_TIMEOUT, ThemeEditor::ID_READABILITY, ThemeEditor::onReadability),
    FXMAPFUNC(SEL_COMMAND, ThemeEditor::ID_MANUAL, ThemeEditor::onManual),
    FXMAPFUNCS(SEL_UPDATE, ThemeEditor::ID_EDIT_PROPERTY, ThemeEditor::ID_REDO, ThemeEditor::onUpdate)
};
FXIMPLEMENT(ThemeEditor, DialogBox, ThemeEditorMap, ARRAYNUMBER(ThemeEditorMap))

ThemeEditor::ThemeEditor(PreferencesBox* owner, XFileExplorer* source, const vector_Theme& themes,
                         FXint selected, StartMode mode) :
    DialogBox(owner, _("Theme Editor"), DECOR_ALL, 0, 0, 1240, 820), preferences(owner), choices(themes)
{
    FXHorizontalFrame* footer = new FXHorizontalFrame(this, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X);
    new FXButton(footer, _("Close"), NULL, this, ID_CLOSE, BUTTON_NORMAL | LAYOUT_RIGHT);
    new FXButton(footer, _("Save As..."), NULL, this, ID_SAVE_AS, BUTTON_NORMAL | LAYOUT_RIGHT);
    new FXButton(footer, _("Save"), NULL, this, ID_SAVE, BUTTON_DEFAULT | LAYOUT_RIGHT | FRAME_GROOVE);
    new FXButton(footer, _("User Manual"), NULL, this, ID_MANUAL, BUTTON_NORMAL | LAYOUT_LEFT);
    new FXLabel(footer, _("Apply changes the mockup only. Save activates all cumulative changes."),
                  NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    FXHorizontalFrame* startup = new FXHorizontalFrame(this, LAYOUT_FILL_X);
    new FXButton(startup, _("Edit current"), NULL, this, ID_START_CURRENT, BUTTON_NORMAL);
    new FXButton(startup, _("New theme"), NULL, this, ID_START_NEW, BUTTON_NORMAL);
    new FXButton(startup, _("Duplicate and edit"), NULL, this, ID_START_DUPLICATE, BUTTON_NORMAL);
    baseTheme = new FXListBox(startup, NULL, 0, FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X);
    for (const Theme& theme : choices) baseTheme->appendItem(theme.name);
    baseTheme->setNumVisible(12);
    baseTheme->setCurrentItem(FXCLAMP(0, selected, static_cast<FXint>(choices.size()) - 1));
    FXHorizontalFrame* tools = new FXHorizontalFrame(this, LAYOUT_FILL_X);
    new FXButton(tools, _("Undo"), NULL, this, ID_UNDO, BUTTON_NORMAL);
    new FXButton(tools, _("Redo"), NULL, this, ID_REDO, BUTTON_NORMAL);
    new FXButton(tools, _("Restore original"), NULL, this, ID_ORIGINAL, BUTTON_NORMAL);
    new FXButton(tools, _("Restore defaults"), NULL, this, ID_DEFAULTS, BUTTON_NORMAL);
    compare = new FXCheckButton(tools, _("Compare with original"), this, ID_COMPARE);
    new FXLabel(tools, _("Zoom:"));
    zoom = new FXListBox(tools, this, ID_ZOOM, FRAME_SUNKEN | FRAME_THICK);
    const char* zoomLabels[] = {_("Fit"), "50%", "75%", "100%", "125%", "150%", "200%"};
    for (const char* label : zoomLabels) zoom->appendItem(label);
    zoom->setNumVisible(7);
    zoom->setCurrentItem(3);
    FXHorizontalFrame* previewTools = new FXHorizontalFrame(this, LAYOUT_FILL_X);
    outlines = new FXCheckButton(previewTools, _("Show editable areas"), this, ID_OUTLINES);
    outlines->setCheck(true);
    privacy = new FXCheckButton(previewTools, _("Hide private names and paths"), this, ID_PRIVACY);
    new FXLabel(previewTools, _("State preview:"));
    state = new FXListBox(previewTools, this, ID_STATE, FRAME_SUNKEN | FRAME_THICK);
    const char* stateNames[] = {_("Current view"), _("Selected"), _("Hovered"), _("Disabled"), _("Menu open"),
                                  _("Fixed dialog"), _("Progress dialog")};
    for (const char* name : stateNames) state->appendItem(name);
    state->setNumVisible(7);
    new FXLabel(this, _("Please select the element you want to edit."), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    FXHorizontalFrame* workspace = new FXHorizontalFrame(this, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXVerticalFrame* left = new FXVerticalFrame(workspace, LAYOUT_FIX_WIDTH | LAYOUT_FILL_Y, 0, 0, 190);
    new FXLabel(left, _("Elements"), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    elements = new FXTreeList(left, this, ID_ELEMENT,
                               TREELIST_BROWSESELECT | TREELIST_SHOWS_BOXES | TREELIST_SHOWS_LINES |
                               FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X | LAYOUT_FILL_Y);
    for (size_t i = 0; i < ARRAYNUMBER(areaKeys); ++i)
    {
        FXTreeItem* root = elements->appendItem(NULL, _(areaNames[i]), NULL, NULL,
                                                reinterpret_cast<void*>(static_cast<FXival>(i + 1)));
        std::vector<FXString> keys = supportedProperties(areaKeys[i]);
        for (size_t property = 0; property < keys.size(); ++property)
            elements->appendItem(root, propertyName(keys[property]), NULL, NULL,
                reinterpret_cast<void*>(static_cast<FXival>(100 + i * 32 + property)));
    }
    FXScrollWindow* view = new FXScrollWindow(workspace, LAYOUT_FILL_X | LAYOUT_FILL_Y | FRAME_SUNKEN | FRAME_THICK);
    xf_readablescrolling(view);
    mockup = new ThemeMockup(view, source, this);
    mockup->setLayoutHints(LAYOUT_FILL_X | LAYOUT_FILL_Y);
    // The property column scrolls vertically so short or small windows never truncate its controls.
    FXScrollWindow* sheetScroll = new FXScrollWindow(workspace, HSCROLLING_OFF | LAYOUT_FIX_WIDTH | LAYOUT_FILL_Y,
                                                     0, 0, 300 + getApp()->getScrollBarSize());
    xf_readablescrolling(sheetScroll);
    FXVerticalFrame* sheet = new FXVerticalFrame(sheetScroll, LAYOUT_FIX_WIDTH | LAYOUT_FILL_Y, 0, 0, 300);
    instructions = new FXLabel(sheet, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    properties = new FXList(sheet, this, ID_PROPERTY,
                              LIST_BROWSESELECT | FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X);
    properties->setNumVisible(9);
    value = new FXLabel(sheet, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    inheritance = new FXCheckButton(sheet, _("Inherit from theme defaults"), this, ID_INHERIT);
    edit = new FXButton(sheet, _("Edit selected property..."), NULL, this, ID_EDIT_PROPERTY, BUTTON_NORMAL | LAYOUT_FILL_X);
    new FXButton(sheet, _("Reset selected property"), NULL, this, ID_RESET_PROPERTY, BUTTON_NORMAL | LAYOUT_FILL_X);
    new FXButton(sheet, _("Reset this element"), NULL, this, ID_RESET_ELEMENT, BUTTON_NORMAL | LAYOUT_FILL_X);
    new FXButton(sheet, _("Reset whole theme overrides"), NULL, this, ID_RESET_THEME, BUTTON_NORMAL | LAYOUT_FILL_X);
    FXHorizontalFrame* apply = new FXHorizontalFrame(sheet, LAYOUT_FILL_X);
    new FXButton(apply, _("Apply to mockup"), NULL, this, ID_APPLY, BUTTON_NORMAL | LAYOUT_FILL_X);
    new FXButton(apply, _("Discard pending"), NULL, this, ID_DISCARD, BUTTON_NORMAL);
    readability = new FXLabel(sheet, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    contrastFix = new FXButton(sheet, _("Use suggested text colour"), NULL, this, ID_FIX_CONTRAST, BUTTON_NORMAL | LAYOUT_FILL_X);
    new FXLabel(sheet, _("Effects are available only for file\nlists. Other elements expose\nsupported properties only.\n"
                         "Alpha colours blend over the\ninherited colour. Window opacity\n"
                         "also requires an X11 compositor."),
                  NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    ready = start(selected, mode);
    if (ready) onPreview(NULL, 0, NULL);
}

ThemeEditor::~ThemeEditor()
{
    getApp()->removeTimeout(this, ID_READABILITY);
    delete mockup;
    mockup = NULL;
    for (const FXString& name : savedThemesToClean)
        preferences->cleanupSavedThemeGraphics(name);
}

FXbool ThemeEditor::start(FXint index, StartMode mode)
{
    if (mode == EDIT_CURRENT) index = 0;
    if (mode == NEW_THEME)
    {
        index = 1;
        for (size_t i = 1; i < choices.size(); ++i)
            if (choices[i].name == "Ocean Depth") index = i;
    }
    index = FXCLAMP(0, index, static_cast<FXint>(choices.size()) - 1);
    Theme selected;
    if (!preferences->resolveTheme(choices[index], selected))
    {
        baseTheme->setCurrentItem(currentBase);
        return false;
    }
    original = selected;
    newTheme = mode != EDIT_CURRENT;
    replaceIndex = -1;
    if (mode == EDIT_CURRENT)
    {
        FXString active = getApp()->reg().readStringEntry("SETTINGS", "theme_name", "");
        for (size_t i = NUM_DEFAULT_THEMES; i < choices.size(); ++i)
            if (choices[i].name == active) { original.name = active; replaceIndex = i; }
        newTheme = replaceIndex < 0;
        if (newTheme) original.name = _("My theme");
    }
    else if (mode == NEW_THEME) original.name = _("New theme");
    else original.name += _(" copy");
    if (original.property("main", "shading").empty())
        original.setProperty("main", "shading", FXStringVal(getApp()->reg().readUnsignedEntry("SETTINGS", "bar_shading", 7)));
    applied = draft = saved = original;
    undoStates.clear();
    redoStates.clear();
    pendingSteps.clear();
    currentBase = index;
    baseTheme->setCurrentItem(index);
    compare->setCheck(false);
    refreshProperties();
    refreshPreview();
    setTitle(FXString(_("Theme Editor")) + " - " + original.name);
    return true;
}

FXString ThemeEditor::currentProperty() const
{
    FXint index = properties->getCurrentItem();
    return index >= 0 && index < static_cast<FXint>(propertyKeys.size()) ? propertyKeys[index] : FXString::null;
}

FXbool ThemeEditor::dirty() const
{
    return !sameTheme(draft, saved);
}

FXbool ThemeEditor::settleProperty()
{
    if (sameTheme(draft, applied)) return true;
    ThemeDecision choice(this, _("Pending preview changes"),
                          _("Apply the pending changes to the cumulative mockup, or discard them?"),
                          _("Apply to mockup"), _("Discard pending"));
    FXint result = choice.execute(PLACEMENT_OWNER);
    if (result == 1) applyDraft();
    else if (result == 2) { draft = applied; pendingSteps.clear(); }
    else return false;
    return true;
}

void ThemeEditor::applyDraft()
{
    if (sameTheme(draft, applied)) { pendingSteps.clear(); return; }
    if (undoStates.empty() || !sameTheme(undoStates.back(), applied)) undoStates.push_back(applied);
    for (const Theme& step : pendingSteps)
        if (!sameTheme(undoStates.back(), step)) undoStates.push_back(step);
    applied = draft;
    pendingSteps.clear();
    redoStates.clear();
}

void ThemeEditor::refreshProperties()
{
    FXString previous = currentProperty();
    propertyKeys = supportedProperties(area);
    properties->clearItems();
    FXint selected = 0;
    for (size_t i = 0; i < propertyKeys.size(); ++i)
    {
        properties->appendItem(propertyName(propertyKeys[i]));
        if (propertyKeys[i] == previous) selected = i;
        if (propertyKeys[i] == "opacity" && !compositorAvailable(getApp())) properties->disableItem(i);
    }
    properties->setCurrentItem(selected);
    properties->selectItem(selected);
    instructions->setText(areaName(area));
    onProperty(NULL, 0, NULL);
}

void ThemeEditor::refreshPreview()
{
    mockup->setTheme(compare->getCheck() ? original : draft);
    double factors[] = {0, 0.5, 0.75, 1, 1.25, 1.5, 2};
    mockup->setPreview(factors[zoom->getCurrentItem()], outlines->getCheck(), privacy->getCheck(),
                         state->getCurrentItem(), area);
    getApp()->removeTimeout(this, ID_READABILITY);
    getApp()->addTimeout(this, ID_READABILITY, 150);
    onProperty(NULL, 0, NULL);
}

long ThemeEditor::onReadability(FXObject*, FXSelector, void*)
{
    const Theme& shown = compare->getCheck() ? original : draft;
    FXColor foreground = shown.colorFor(area, "foreground");
    double contrast = mockup->minimumContrast(foreground, area);
    if (contrast < 0)
    {
        readability->setText(_("Readability unavailable: cannot read the rendered preview."));
        contrastFix->disable();
        return 1;
    }
    if (compare->getCheck()) contrastFix->disable(); else contrastFix->enable();
    double selectionContrast = themeContrast(shown.colorFor(area, "selection_foreground"),
                                               shown.colorFor(area, "selection_background"));
    FXString grade = contrast >= 7 ? "AAA" : contrast >= 4.5 ? "AA" : contrast >= 3 ? _("Large text only") : _("Low contrast");
    readability->setText(FXString().format(_("Readability: %.2f:1 (%s)\nSelection: %.2f:1\n"
                                           "AA: 4.5:1 normal / 3:1 large; AAA: 7:1"), contrast,
                                           grade.text(), selectionContrast));
    double black = mockup->minimumContrast(FXRGB(0, 0, 0), area);
    double white = mockup->minimumContrast(FXRGB(255, 255, 255), area);
    suggestedColor = black >= white ? FXRGB(0, 0, 0) : FXRGB(255, 255, 255);
    return 1;
}

void ThemeEditor::selectElement(const FXString& selected)
{
    if (selected == area) return;
    if (!settleProperty()) return;
    area = selected;
    for (FXTreeItem* item = elements->getFirstItem(); item; item = item->getNext())
    {
        FXint index = static_cast<FXint>(reinterpret_cast<FXival>(item->getData())) - 1;
        if (index >= 0 && index < static_cast<FXint>(ARRAYNUMBER(areaKeys)) && selected == areaKeys[index])
        {
            elements->setCurrentItem(item);
            elements->selectItem(item);
            elements->makeItemVisible(item);
            break;
        }
    }
    refreshProperties();
    refreshPreview();
}

long ThemeEditor::onElement(FXObject*, FXSelector, void* pointer)
{
    FXTreeItem* item = static_cast<FXTreeItem*>(pointer);
    if (!item) return 1;
    FXival data = reinterpret_cast<FXival>(item->getData());
    if (data >= 100)
    {
        FXint index = (data - 100) / 32, property = (data - 100) % 32;
        selectElement(areaKeys[index]);
        if (area != areaKeys[index]) return 1;
        properties->setCurrentItem(property);
        properties->selectItem(property);
        onProperty(NULL, 0, NULL);
    }
    else if (data > 0) selectElement(areaKeys[data - 1]);
    return 1;
}

long ThemeEditor::onProperty(FXObject*, FXSelector, void*)
{
    FXString key = currentProperty();
    FXString description;
    if (key == "font") description = draft.fontFor(area, getApp()->getNormalFont()->getFont());
    else if (key == "effect") description = _("Gradient / pattern / PNG texture");
    else if (key == "background_image")
    {
        FXString reference = draft.property(area, key), relative;
        if (reference.empty()) description = _("No image");
        else
        {
            BackgroundEffect::textureRelativePath(reference, relative);
            FXString mode = draft.property(area, "image_mode");
            FXString scope = draft.property(area, "image_scope");
            FXString scopeName = scope == "files" ? _("File panels") :
                                scope == "panels" ? _("All panels") : _("Whole UI");
            description = relative + " (" + (mode.empty() ? FXString("fit") : mode) + ", " + scopeName + ", " +
                          FXStringVal(backgroundImageOpacity(draft.property(area, "image_opacity"))) + "%)";
        }
    }
    else if (key == "shading") description = draft.property("main", "shading") + "%";
    else if (key == "opacity") description = (draft.property("main", "opacity").empty() ?
                                               FXString("100") : draft.property("main", "opacity")) + "%";
    else description = draft.overrides(area, key) ? draft.property(area, key) : themeColorText(draft.colorFor(area, key));
    value->setText(description);
    FXbool inheritable = key != "effect" && key != "background_image" && key != "shading" && key != "opacity";
    if (inheritable) inheritance->show(); else inheritance->hide();
    inheritance->setCheck(!draft.overrides(area, key));
    if (compare->getCheck() || (key == "opacity" && !compositorAvailable(getApp()))) edit->disable();
    else edit->enable();
    return 1;
}

long ThemeEditor::onStart(FXObject*, FXSelector selector, void*)
{
    if (dirty())
    {
        ThemeDecision decision(this, _("Unsaved theme"), _("Save this theme before starting another one?"),
                                 _("Save"), _("Discard"));
        FXint result = decision.execute(PLACEMENT_OWNER);
        if (!result || (result == 1 && !save(false))) return 1;
    }
    start(baseTheme->getCurrentItem(), FXSELID(selector) == ID_START_NEW ? NEW_THEME :
           FXSELID(selector) == ID_START_DUPLICATE ? DUPLICATE : EDIT_CURRENT);
    return 1;
}

long ThemeEditor::onEditProperty(FXObject*, FXSelector, void*)
{
    if (compare->getCheck()) return 1;
    FXString key = currentProperty();
    Theme before = draft;
    std::vector<Theme> stepsBefore = pendingSteps;
    if (key == "background_image")
    {
        BackgroundImageEditor dialog(this, area);
        if (dialog.execute(PLACEMENT_OWNER)) applyDraft();
        else { draft = before; pendingSteps = stepsBefore; }
    }
    else if (key == "effect")
    {
        BackgroundEditor dialog(this);
        if (dialog.execute(PLACEMENT_OWNER))
        {
            draft.effect = dialog.specification();
            applyDraft();
            if (BackgroundEffect::parse(draft.effect).fill == "gradient")
            {
                for (FXint i = 11; i > 0; --i)
                    getApp()->reg().writeStringEntry("COLOR_PICKER", ("gradient" + FXStringVal(i)).text(),
                        getApp()->reg().readStringEntry("COLOR_PICKER", ("gradient" + FXStringVal(i - 1)).text(), ""));
                getApp()->reg().writeStringEntry("COLOR_PICKER", "gradient0", draft.effect.text());
            }
        }
        else { draft = before; pendingSteps = stepsBefore; }
    }
    else if (key == "font")
    {
        FXFont candidate(getApp(), draft.fontFor(area, getApp()->getNormalFont()->getFont()));
        FXFontDesc description;
        candidate.getFontDesc(description);
        FontDialog dialog(this, _("Element font"));
        dialog.addHelpButton(this, ID_MANUAL);
        dialog.setFontSelection(description);
        if (dialog.execute(PLACEMENT_OWNER))
        {
            dialog.getFontSelection(description);
            candidate.setFontDesc(description);
            draft.setProperty(area, key, candidate.getFont());
        }
    }
    else if (key == "shading" || key == "opacity")
    {
        if (key == "opacity" && !compositorAvailable(getApp()))
        {
            MessageBox::warning(this, BOX_OK, _("Window opacity"),
                                  _("Window transparency is unavailable without an X11 compositor.\n"
                                    "Alpha colours and PNG transparency are still supported."));
            return 1;
        }
        InputDialog dialog(this, "", key == "opacity" ? _("Window opacity (20-100%):") :
                                                          _("Bar shading (0-20%):"), propertyName(key));
        dialog.addHelpButton(this, ID_MANUAL);
        FXString initial = draft.property("main", key);
        dialog.setText(initial.empty() ? (key == "opacity" ? "100" : "0") : initial);
        if (dialog.execute(PLACEMENT_OWNER))
        {
            FXString text = dialog.getText().trim();
            char* end = NULL;
            long number = strtol(text.text(), &end, 10);
            if (text.empty() || !end || *end || number < (key == "opacity" ? 20 : 0) ||
                number > (key == "opacity" ? 100 : 20))
                MessageBox::error(this, BOX_OK, _("Invalid percentage"), _("Enter a whole number in the displayed range."));
            else draft.setProperty("main", key, FXStringVal(static_cast<FXint>(number)));
        }
    }
    else
    {
        ColorDialog picker(this, propertyName(key));
        picker.addHelpButton(this, ID_MANUAL);
        FXColor color = draft.colorFor(area, key);
        if (draft.overrides(area, key)) themeColorParse(draft.property(area, key), color);
        picker.setRGBA(color);
        picker.setTheme(&draft);
        picker.setOpaqueOnly(false);
        picker.setTarget(this);
        picker.setSelector(ID_COLOR);
        if (picker.execute(PLACEMENT_OWNER)) draft.setProperty(area, key, themeColorText(picker.getRGBA(), true));
        else draft = before;
    }
    refreshPreview();
    return 1;
}

long ThemeEditor::onManual(FXObject* sender, FXSelector, void*)
{
    XFileExplorer* window = dynamic_cast<XFileExplorer*>(preferences->getOwner());
    if (!window)
    {
        MessageBox::error(this, BOX_OK, _("User Manual"), _("The main window is unavailable."));
        return 0;
    }
    return window->handle(sender, FXSEL(SEL_COMMAND, XFileExplorer::ID_THEME_MANUAL), NULL);
}

long ThemeEditor::onColor(FXObject*, FXSelector, void* pointer)
{
    draft.setProperty(area, currentProperty(), themeColorText(static_cast<FXColor>(reinterpret_cast<FXuval>(pointer)), true));
    refreshPreview();
    return 1;
}

void ThemeEditor::previewEffect(const FXString& spec, FXbool stopEdit, const FXString& previous)
{
    if (stopEdit)
    {
        Theme before = draft;
        if (!previous.empty()) before.effect = previous;
        if (before.effect != spec && (pendingSteps.empty() || !sameTheme(pendingSteps.back(), before)))
            pendingSteps.push_back(before);
    }
    draft.effect = spec;
    refreshPreview();
}

void ThemeEditor::previewImage(const FXString& reference, const FXString& mode, FXint opacity,
                               const FXString& scope)
{
    draft.setProperty("main", "background_image", reference);
    draft.setProperty("main", "image_mode", reference.empty() ? FXString::null : mode);
    draft.setProperty("main", "image_opacity", reference.empty() || opacity >= 100 ? FXString::null : FXStringVal(opacity));
    draft.setProperty("main", "image_scope", reference.empty() || scope == "window" ? FXString::null : scope);
    refreshPreview();
}

void ThemeEditor::setGradientPreset(const FXString& name, const FXString& effect)
{
    draft.properties["gradient." + name] = effect;
}

long ThemeEditor::onInherit(FXObject*, FXSelector, void*)
{
    FXString key = currentProperty();
    if (inheritance->getCheck()) draft.setProperty(area, key, "");
    else if (key == "font") draft.setProperty(area, key, draft.fontFor(area, getApp()->getNormalFont()->getFont()));
    else draft.setProperty(area, key, themeColorText(draft.colorFor(area, key)));
    refreshPreview();
    return 1;
}

long ThemeEditor::onApply(FXObject*, FXSelector, void*)
{
    applyDraft();
    refreshPreview();
    return 1;
}

long ThemeEditor::onDiscard(FXObject*, FXSelector, void*)
{
    draft = applied;
    pendingSteps.clear();
    refreshPreview();
    return 1;
}

long ThemeEditor::onUndo(FXObject*, FXSelector, void*)
{
    if (!settleProperty() || undoStates.empty()) return 1;
    redoStates.push_back(applied);
    applied = draft = undoStates.back();
    undoStates.pop_back();
    refreshPreview();
    return 1;
}

long ThemeEditor::onRedo(FXObject*, FXSelector, void*)
{
    if (!settleProperty() || redoStates.empty()) return 1;
    undoStates.push_back(applied);
    applied = draft = redoStates.back();
    redoStates.pop_back();
    refreshPreview();
    return 1;
}

long ThemeEditor::onReset(FXObject*, FXSelector selector, void*)
{
    FXint scope = FXSELID(selector);
    FXString message = scope == ID_RESET_PROPERTY ? _("Reset only the selected property to its original value?") :
                       scope == ID_RESET_ELEMENT ? _("Reset only this element to inherited defaults?") :
                       scope == ID_RESET_THEME ? _("Reset every element to inherited colours and remove all effects and overrides?") :
                       scope == ID_ORIGINAL ? _("Discard all edits and restore this theme's original appearance?") :
                                              _("Reset the whole theme to the built-in Ocean Depth defaults?");
    if (MessageBox::question(this, BOX_YES_NO, _("Reset appearance"), "%s", message.text()) != BOX_CLICKED_YES) return 1;
    if (scope == ID_RESET_PROPERTY)
    {
        FXString key = currentProperty();
        if (key == "effect") draft.effect = original.effect;
        else
        {
            draft.setProperty(area, key, original.property(area, key));
            if (key == "background_image")
            {
                draft.setProperty(area, "image_mode", original.property(area, "image_mode"));
                draft.setProperty(area, "image_opacity", original.property(area, "image_opacity"));
                draft.setProperty(area, "image_scope", original.property(area, "image_scope"));
            }
        }
    }
    else if (scope == ID_RESET_ELEMENT)
    {
        draft.resetElement(area);
    }
    else if (scope == ID_ORIGINAL) draft = original;
    else if (scope == ID_RESET_THEME)
    {
        draft.properties.clear();
        draft.effect.clear();
        for (FXint i = 0; i < NUM_COLORS; ++i) draft.color[i] = original.color[i];
    }
    else
    {
        for (const Theme& theme : choices)
            if (theme.name == "Ocean Depth") { FXString name = draft.name; draft = theme; draft.name = name; break; }
    }
    pendingSteps.clear();
    applyDraft();
    refreshProperties();
    refreshPreview();
    return 1;
}

long ThemeEditor::onPreview(FXObject*, FXSelector, void*)
{
    mockup->setLayoutHints(zoom->getCurrentItem() == 0 ? LAYOUT_FILL_X | LAYOUT_FILL_Y : LAYOUT_FIX_WIDTH | LAYOUT_FIX_HEIGHT);
    refreshPreview();
    return 1;
}

long ThemeEditor::onFixContrast(FXObject*, FXSelector, void*)
{
    FXString message = _("Use ") + themeColorText(suggestedColor) + _(" for this element's text?\n"
        "Only the mockup changes. A mixed light/dark texture or image may still need a gentler background.");
    if (MessageBox::question(this, BOX_YES_NO, _("Suggested contrast fix"), "%s", message.text()) == BOX_CLICKED_YES)
    {
        draft.setProperty(area, "foreground", themeColorText(suggestedColor));
        refreshPreview();
    }
    return 1;
}

FXbool ThemeEditor::save(FXbool copy)
{
    applyDraft();
    if (!copy && !newTheme && replaceIndex >= NUM_DEFAULT_THEMES)
    {
        ThemeDecision decision(this, _("Save theme"), _("Overwrite the saved custom theme, or save a separate copy?"),
                                 _("Overwrite"), _("Save a copy"));
        FXint result = decision.execute(PLACEMENT_OWNER);
        if (!result) return false;
        copy = result == 2;
    }
    Theme result = applied;
    if (copy || newTheme || replaceIndex < NUM_DEFAULT_THEMES)
    {
        InputDialog naming(this, "", _("Theme name:"), _("Save theme"));
        naming.addHelpButton(this, ID_MANUAL);
        naming.setText(result.name + (copy ? _(" copy") : ""));
        if (!naming.execute(PLACEMENT_OWNER)) return false;
        result.name = naming.getText().trim();
        if (result.name.empty() || result.name.find_first_of(";\r\n") >= 0)
        {
            MessageBox::error(this, BOX_OK, _("Theme name"), _("Enter a non-empty name without semicolons or newlines."));
            return false;
        }
        for (const Theme& theme : choices)
        {
            if (theme.name == result.name)
            {
                MessageBox::error(this, BOX_OK, _("Theme name"), _("Theme name already exists. Please use another name."));
                return false;
            }
        }
        copy = true;
    }
    std::vector<Theme> retained = undoStates;
    retained.insert(retained.end(), redoStates.begin(), redoStates.end());
    retained.push_back(original);
    if (!preferences->saveEditedTheme(result, copy ? -1 : replaceIndex, retained)) return false;
    if (std::find(savedThemesToClean.begin(), savedThemesToClean.end(), result.name) == savedThemesToClean.end())
        savedThemesToClean.push_back(result.name);
    if (copy) { choices.push_back(result.libraryReference()); replaceIndex = choices.size() - 1; baseTheme->appendItem(result.name); }
    else choices[replaceIndex] = result.libraryReference();
    newTheme = false;
    applied = draft = saved = result;
    setTitle(FXString(_("Theme Editor")) + " - " + result.name);
    refreshPreview();
    return true;
}

long ThemeEditor::onSave(FXObject*, FXSelector selector, void*)
{
    save(FXSELID(selector) == ID_SAVE_AS);
    return 1;
}

long ThemeEditor::onCloseEditor(FXObject*, FXSelector, void*)
{
    if (dirty())
    {
        ThemeDecision decision(this, _("Unsaved theme"), _("Save all cumulative and pending changes before closing?"),
                                 _("Save"), _("Discard"));
        FXint result = decision.execute(PLACEMENT_OWNER);
        if (!result || (result == 1 && !save(false))) return 1;
    }
    getApp()->stopModal(this, 1);
    hide();
    return 1;
}

long ThemeEditor::onUpdate(FXObject* sender, FXSelector selector, void*)
{
    FXint id = FXSELID(selector);
    FXbool enabled = true;
    if (id == ID_UNDO) enabled = !undoStates.empty();
    else if (id == ID_REDO) enabled = !redoStates.empty();
    else if (id == ID_APPLY || id == ID_DISCARD) enabled = !sameTheme(draft, applied);
    else if (id == ID_EDIT_PROPERTY)
        enabled = !compare->getCheck() && (currentProperty() != "opacity" || compositorAvailable(getApp()));
    sender->handle(this, FXSEL(SEL_COMMAND, enabled ? FXWindow::ID_ENABLE : FXWindow::ID_DISABLE), NULL);
    return 1;
}
