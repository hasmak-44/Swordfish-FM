#include "config.h"
#include "i18n.h"
#include <fx.h>
#include <fxkeys.h>
#include <FXPNGIcon.h>
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
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace
{
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
    if (area == "list" || area == "tree" || area == "places" || area == "tabs" || area == "toolbars")
        result.push_back("background_image");
    if (area == "list") result.push_back("effect");
    if (area == "main")
    {
        result.push_back("shading");
        result.push_back("opacity");
    }
    return result;
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
        FXint imageOwner = -1;
        FXbool paintImage = false;
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
    std::map<FXint, std::unique_ptr<FXImage>> imageLayers;
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

    void capture(FXWindow* window, FXWindow* root, const FXString& parentArea,
                 FXint imageOwner = -1, BackgroundImageOwner* imageWidget = NULL,
                 const FXString& imageArea = FXString::null)
    {
        if (!window->shown() || window->getWidth() <= 0 || window->getHeight() <= 0) return;
        Node node;
        FXint x, y;
        window->translateCoordinatesTo(x, y, root, 0, 0);
        node.rectangle = FXRectangle(x, y, window->getWidth(), window->getHeight());
        node.area = themeWidgetArea(window, parentArea);
        FXString ownerArea = imageArea;
        if (BackgroundImageOwner* owner = dynamic_cast<BackgroundImageOwner*>(window))
        {
            imageOwner = nodes.size();
            imageWidget = owner;
            ownerArea = node.area;
        }
        node.imageOwner = imageOwner;
        node.paintImage = imageWidget && node.area == ownerArea && imageWidget->allowsBackgroundImage(window);
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
            capture(child, root, area, imageOwner, imageWidget, ownerArea);
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
        list->setBackgroundImage(theme.property("list", "background_image"), theme.property("list", "image_mode"));
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

    void rebuild()
    {
        delete image;
        image = new FXImage(getApp(), NULL, IMAGE_KEEP, sourceWidth, sourceHeight);
        image->create();
        imageLayers.clear();
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const Node& host = nodes[i];
            if (host.imageOwner != static_cast<FXint>(i)) continue;
            FXString reference = theme.property(host.area, "background_image");
            if (reference.empty()) continue;
            FXString path = BackgroundImage::path(reference, theme.assetRoot), error;
            std::unique_ptr<FXImage> source;
            if (!path.empty()) source = BackgroundImage::load(getApp(), path, error);
            if (!source)
            {
                appLog(APP_LOG_ERRORS, "theme-preview-image-unavailable", reference + ": " +
                       (error.empty() ? _("Image was not found in the theme or its library.") : error));
                continue;
            }
            FXint width = host.rectangle.w, height = host.rectangle.h;
            FXColor* pixels = NULL;
            if (!FXMALLOC(&pixels, FXColor, static_cast<size_t>(width) * height))
                throw FXMemoryException(_("Unable to create theme preview"));
            std::fill(pixels, pixels + static_cast<size_t>(width) * height,
                      theme.colorFor(host.area, "background"));
            BackgroundImage::composite(pixels, width, height, *source, theme.property(host.area, "image_mode"));
            std::unique_ptr<FXImage> layer(new FXImage(getApp(), pixels, IMAGE_KEEP | IMAGE_OWNED, width, height));
            layer->create();
            imageLayers[static_cast<FXint>(i)] = std::move(layer);
        }
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
                auto backgroundLayer = imageLayers.find(node.imageOwner);
                if (node.paintImage && !node.selected && backgroundLayer != imageLayers.end())
                {
                    const FXRectangle& host = nodes[node.imageOwner].rectangle;
                    FXint x = FXMAX(r.x, host.x), y = FXMAX(r.y, host.y);
                    FXint right = FXMIN(r.x + r.w, host.x + host.w), bottom = FXMIN(r.y + r.h, host.y + host.h);
                    if (right > x && bottom > y)
                        dc.drawArea(backgroundLayer->second.get(), x - host.x, y - host.y,
                                    right - x, bottom - y, x, y);
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
        FXCanvas(parent, NULL, 0, FRAME_SUNKEN | FRAME_THICK), editor(target)
    {
        holder.reset(new FXMainWindow(getApp(), "Theme preview resources", NULL, NULL, DECOR_NONE));
        sourceWidth = FXMAX(1, source->getWidth());
        sourceHeight = FXMAX(1, source->getHeight());
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
        FXString effect = area + "|" + theme.effect + "|" + theme.property(area, "background_image") +
                          "|" + theme.property(area, "image_mode");
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
                    list->setBackgroundImage(theme.property(area, "background_image"), theme.property(area, "image_mode"));
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
            else if (!theme.property(area, "background_image").empty())
            {
                FXbool found = false;
                for (const auto& layer : imageLayers)
                {
                    if (nodes[layer.first].area != area) continue;
                    found = true;
                    FXImage* pixels = layer.second.get();
                    for (FXint i = 0; i < pixels->getWidth() * pixels->getHeight(); ++i)
                        luminances.push_back(themeLuminance(pixels->getData()[i]));
                }
                if (!found)
                {
                    luminances.clear();
                    return -1;
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
    FXIconList* patterns = NULL;
    FXListBox* patternCategory = NULL;
    FXTextField* patternTargetCategory = NULL;
    FXIconList* textures = NULL;
    FXListBox* textureCategory = NULL;
    FXTextField* textureTargetCategory = NULL;
    FXSpinner* patternSize = NULL;
    FXSpinner* strength = NULL;
    FXCheckButton* originalColors = NULL;
    FXCheckButton* rows = NULL;
    FXTextField* presetName = NULL;
    FXListBox* presets = NULL;
    FXLabel* status = NULL;
    FXLabel* patternStatus = NULL;
    FXLabel* textureStatus = NULL;
    std::vector<FXString> patternNames, patternCategories;
    std::vector<FXint> patternNumbers;
    std::vector<FXString> textureNames, presetEffects;
    std::vector<FXString> textureCategories;
    std::vector<FXIcon*> patternIcons;
    std::vector<FXIcon*> textureIcons;
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
        rows->setCheck(effect.rows);
        for (FXint i = 0; i < static_cast<FXint>(patternNames.size()); ++i)
            if ((!effect.patternImage.empty() && effect.patternImage == patternNames[i]) ||
                (effect.patternImage.empty() && effect.pattern > 0 && effect.pattern == patternNumbers[i]))
                patterns->setCurrentItem(i);
        syncing = false;
    }
    // Theme colours made the samples nearly invisible on dark themes, so samples use a fixed neutral pair.
    static constexpr FXColor previewLight = FXRGB(0xD5, 0xDA, 0xDF);
    static constexpr FXColor previewDark = FXRGB(0x6E, 0x77, 0x80);
    static constexpr FXint previewSize = 64;

    void collectTiles(const FXString& root, const FXString& category, FXbool recursive,
                      std::map<FXString, FXString>& entries, FXString& rejected, FXbool pattern)
    {
        FXString path = category.empty() ? root : root + PATHSEPSTRING + category;
        FXDir directory;
        if (!directory.open(path))
        {
            if (xf_existfile(path))
            {
                rejected += path + ": " + strerror(errno) + "\n";
                appLog(APP_LOG_ERRORS, pattern ? "pattern-directory-unreadable" : "texture-directory-unreadable", path);
            }
            return;
        }
        while (directory.next())
        {
            FXString filename = directory.name();
            if (filename.empty() || filename[0] == '.') continue;
            FXString file = path + PATHSEPSTRING + filename;
            FXString relative = category.empty() ? filename : category + "/" + filename;
            if (recursive && xf_isdirectory(file) && !xf_islink(file))
            {
                if (relative.contains('/') < 8) collectTiles(root, relative, true, entries, rejected, pattern);
                else
                {
                    rejected += relative + ": " + _("Too many nested graphic categories.") + "\n";
                    appLog(APP_LOG_ERRORS, pattern ? "pattern-category-too-deep" : "texture-category-too-deep", file);
                }
            }
            else if (FXPath::extension(filename).lower() == "png" && xf_isfile(file))
            {
                FXString title = category.empty() ? FXPath::title(filename) : category + "/" + FXPath::title(filename);
                FXString reference = BackgroundEffect::textureReference(title);
                if (reference.empty())
                {
                    rejected += relative + ": " + _("Invalid graphic name or category.") + "\n";
                    appLog(APP_LOG_ERRORS, pattern ? "pattern-rejected" : "texture-rejected", file);
                }
                else if (!entries.count(reference))
                    entries[reference] = file;
                else if (FXPath::directory(entries[reference]) == path && FXPath::extension(filename) == "png")
                    entries[reference] = file;
            }
        }
    }

    void loadPatterns()
    {
        FXint previous = patternCategory->getCurrentItem();
        FXString selected = previous >= 3 && previous < static_cast<FXint>(patternCategories.size()) ?
                            patternCategories[previous] : FXString::null;
        patterns->clearItems();
        for (FXIcon* icon : patternIcons) delete icon;
        patternIcons.clear();
        patternNames.clear();
        patternNumbers.clear();
        extern FXString execpath;
        FXString directories[] = {editor->getDraft().assetRoot.empty() ? FXString::null :
                                      editor->getDraft().assetRoot + PATHSEPSTRING "patterns",
                                    FXPath::directory(execpath) + "/../share/swordfish/patterns",
                                    xf_userdatahome() + PATHSEPSTRING "patterns"};
        std::map<FXString, FXString> entries;
        FXString rejected;
        for (const FXString& directory : directories)
            if (!directory.empty()) collectTiles(directory, "", true, entries, rejected, true);

        std::set<FXString> categories;
        for (const auto& entry : entries)
        {
            FXString relative;
            if (!BackgroundEffect::textureRelativePath(entry.first, relative)) continue;
            FXint slash = relative.rfind('/');
            if (slash >= 0) categories.insert(relative.left(slash));
        }
        patternCategory->clearItems();
        patternCategories.clear();
        patternCategory->appendItem(_("All categories"));
        patternCategories.push_back("");
        patternCategory->appendItem(_("Built-in shapes"));
        patternCategories.push_back("");
        patternCategory->appendItem(_("Uncategorized"));
        patternCategories.push_back("");
        FXint categoryIndex = previous == 1 || previous == 2 ? previous : 0;
        for (const FXString& category : categories)
        {
            patternCategory->appendItem(category);
            patternCategories.push_back(category);
            if (previous >= 3 && category == selected) categoryIndex = patternCategories.size() - 1;
        }
        patternCategory->setCurrentItem(categoryIndex);

        const char* names[] = {_("Horizontal stripes"), _("Vertical stripes"), _("Diagonal stripes"), _("Checks"),
                               _("Grid"), _("Dots"), _("Diamonds"), _("Crosshatch")};
        if (categoryIndex <= 1)
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
                patterns->appendItem(names[i], icon, icon);
                patternNames.push_back("");
                patternNumbers.push_back(i + 1);
                if (effect.patternImage.empty() && effect.pattern == i + 1) patterns->setCurrentItem(i);
            }
        for (const auto& entry : entries)
        {
            FXString relative;
            if (!BackgroundEffect::textureRelativePath(entry.first, relative)) continue;
            FXint slash = relative.rfind('/');
            FXString category = slash >= 0 ? relative.left(slash) : FXString::null;
            if (categoryIndex == 1 || (categoryIndex > 1 && category != patternCategories[categoryIndex])) continue;
            FXString error;
            if (!BackgroundEffect::validatePattern(getApp(), entry.second, error))
            {
                rejected += relative + ": " + error + "\n";
                appLog(APP_LOG_ERRORS, "pattern-rejected", entry.second + ": " + error);
                continue;
            }
            BackgroundEffect sample = effect;
            sample.fill = "pattern";
            sample.pattern = 0;
            sample.patternImage = entry.first;
            sample.texture.clear();
            sample.size = 64;
            sample.rows = false;
            sample.colors = {previewLight, previewDark};
            FXIcon* icon = BackgroundEffect::preview(this, sample.serialize(), previewLight, previewSize,
                                                      editor->getDraft().assetRoot);
            patternIcons.push_back(icon);
            FXString label = slash < 0 ? relative : relative.rafter('/');
            if (categoryIndex == 0 && !category.empty()) label += " (" + category + ")";
            patterns->appendItem(label, icon, icon);
            patternNames.push_back(entry.first);
            patternNumbers.push_back(0);
            if (entry.first == effect.patternImage) patterns->setCurrentItem(patternNames.size() - 1);
        }
        patternStatus->setText(rejected.empty() ? _("PNG patterns: at most 1024 x 1024 pixels. Originals are never moved.") :
                                                  _("Some patterns were excluded:\n") + rejected);
    }

    void loadTextures()
    {
        FXint previousCategory = textureCategory->getCurrentItem();
        FXbool all = previousCategory <= 0 || previousCategory >= static_cast<FXint>(textureCategories.size());
        FXString selectedCategory = all ? FXString::null : textureCategories[previousCategory];
        textures->clearItems();
        for (FXIcon* icon : textureIcons) delete icon;
        textureIcons.clear();
        textureNames.clear();
        textures->appendItem(_("No texture"));
        textureNames.push_back("");
        extern FXString execpath, xdgconfighome;
        FXString directories[] = {editor->getDraft().assetRoot.empty() ? FXString::null :
                                      editor->getDraft().assetRoot + PATHSEPSTRING "textures",
                                    editor->getDraft().assetRoot,
                                    FXPath::directory(execpath) + "/../share/swordfish/textures",
                                    xf_userdatahome() + PATHSEPSTRING "textures",
                                    xdgconfighome + PATHSEPSTRING XFECONFIGPATH "/textures"};
        std::map<FXString, FXString> entries;
        FXString rejected;
        for (FXint folder = 0; folder < static_cast<FXint>(ARRAYNUMBER(directories)); ++folder)
        {
            if (directories[folder].empty()) continue;
            collectTiles(directories[folder], "", folder != 1, entries, rejected, false);
        }

        std::set<FXString> categories;
        for (const auto& entry : entries)
        {
            FXString relative;
            if (!BackgroundEffect::textureRelativePath(entry.first, relative)) continue;
            FXint slash = relative.rfind('/');
            if (slash >= 0) categories.insert(relative.left(slash));
        }
        textureCategory->clearItems();
        textureCategories.clear();
        textureCategory->appendItem(_("All categories"));
        textureCategories.push_back("");
        textureCategory->appendItem(_("Uncategorized"));
        textureCategories.push_back("");
        FXint categoryIndex = all ? 0 : 1;
        for (const FXString& category : categories)
        {
            textureCategory->appendItem(category);
            textureCategories.push_back(category);
            if (!all && category == selectedCategory) categoryIndex = textureCategories.size() - 1;
        }
        textureCategory->setCurrentItem(categoryIndex);

        for (const auto& entry : entries)
        {
            FXString relative;
            if (!BackgroundEffect::textureRelativePath(entry.first, relative)) continue;
            FXint slash = relative.rfind('/');
            FXString category = slash >= 0 ? relative.left(slash) : FXString::null;
            if (categoryIndex != 0 && category != textureCategories[categoryIndex]) continue;
            FXString error;
            if (!BackgroundEffect::validateTexture(getApp(), entry.second, error))
            {
                rejected += relative + ": " + error + "\n";
                appLog(APP_LOG_ERRORS, "texture-rejected", entry.second + ": " + error);
                continue;
            }
            BackgroundEffect preview = effect;
            preview.fill = "solid";
            preview.colors = {previewLight};
            preview.texture = entry.first;
            preview.strength = 80;
            preview.rows = false;
            FXIcon* icon = BackgroundEffect::preview(this, preview.serialize(), previewLight, previewSize,
                                                      editor->getDraft().assetRoot);
            textureIcons.push_back(icon);
            FXString label = slash < 0 ? relative : relative.rafter('/');
            if (categoryIndex == 0 && !category.empty()) label += " (" + category + ")";
            textures->appendItem(label, icon, icon);
            textureNames.push_back(entry.first);
            if (entry.first == effect.texture) textures->setCurrentItem(textureNames.size() - 1);
        }
        textureStatus->setText(rejected.empty() ? _("PNG tiles: at most 1024 x 1024 pixels. Originals are never moved.") :
                                                  _("Some textures were excluded:\n") + rejected);
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
        ID_REFRESH_PATTERN,
        ID_PATTERN_CATEGORY,
        ID_ADD_TEXTURE,
        ID_REFRESH,
        ID_TEXTURE_CATEGORY,
        ID_PRESET,
        ID_SAVE_PRESET,
        ID_RANDOM,
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
        FXHorizontalFrame* patternCategoryControls = new FXHorizontalFrame(patternPage, LAYOUT_FILL_X);
        new FXLabel(patternCategoryControls, _("Category:"));
        patternCategory = new FXListBox(patternCategoryControls, this, ID_PATTERN_CATEGORY,
                                         FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X);
        patternCategory->setNumVisible(10);
        patterns = new FXIconList(patternPage, this, ID_LIVE,
                                   ICONLIST_BIG_ICONS | ICONLIST_BROWSESELECT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        FXHorizontalFrame* patternControls = new FXHorizontalFrame(patternPage, LAYOUT_FILL_X);
        new FXLabel(patternControls, _("Pattern size (pixels):"));
        patternSize = new FXSpinner(patternControls, 4, this, ID_LIVE, FRAME_SUNKEN | FRAME_THICK);
        patternSize->setRange(4, 256);
        new FXButton(patternControls, _("First colour..."), NULL, this, ID_STOP_COLOR, BUTTON_NORMAL);
        new FXButton(patternControls, _("Second colour..."), NULL, this, ID_STOP_COLOR + 100, BUTTON_NORMAL);
        FXHorizontalFrame* patternFiles = new FXHorizontalFrame(patternPage, LAYOUT_FILL_X);
        new FXButton(patternFiles, _("Add PNG..."), NULL, this, ID_ADD_PATTERN, BUTTON_NORMAL);
        new FXButton(patternFiles, _("Refresh"), NULL, this, ID_REFRESH_PATTERN, BUTTON_NORMAL);
        new FXLabel(patternFiles, _("Save in category:"));
        patternTargetCategory = new FXTextField(patternFiles, 14, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        patternTargetCategory->setTipText(_("Optional category; use / for subcategories."));
        new FXLabel(patternPage, _("User patterns are stored in:") + FXString("\n") +
                                  xf_userdatahome() + PATHSEPSTRING "patterns", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        patternStatus = new FXLabel(patternPage, "", NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        new DialogTabItem(pages, _("Texture"));
        FXVerticalFrame* texturePage = new FXVerticalFrame(pages, FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        FXHorizontalFrame* categoryControls = new FXHorizontalFrame(texturePage, LAYOUT_FILL_X);
        new FXLabel(categoryControls, _("Category:"));
        textureCategory = new FXListBox(categoryControls, this, ID_TEXTURE_CATEGORY,
                                         FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X);
        textureCategory->setNumVisible(10);
        textures = new FXIconList(texturePage, this, ID_TEXTURE,
                                   ICONLIST_BIG_ICONS | ICONLIST_BROWSESELECT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        FXHorizontalFrame* textureControls = new FXHorizontalFrame(texturePage, LAYOUT_FILL_X);
        new FXButton(textureControls, _("Add PNG..."), NULL, this, ID_ADD_TEXTURE, BUTTON_NORMAL);
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
        loadPatterns();
        sync();
        loadTextures();
    }
    ~BackgroundEditor()
    {
        for (FXIcon* icon : textureIcons) delete icon;
        for (FXIcon* icon : patternIcons) delete icon;
    }
    long onLive(FXObject* sender, FXSelector, void*)
    {
        if (syncing) return 1;
        effect.fill = fill->getCurrentItem() == 1 ? "gradient" : fill->getCurrentItem() == 2 ? "pattern" : "solid";
        effect.direction = direction->getCurrentItem() == 1 ? "radial" : "linear";
        effect.angled = effect.direction == "linear";
        effect.angle = angle->getValue();
        if (sender == patterns ||
            (sender == fill && effect.fill == "pattern" && effect.pattern == 0 && effect.patternImage.empty()))
        {
            FXint index = patterns->getCurrentItem();
            if (index < 0 && patterns->getNumItems() > 0) index = 0;
            if (index < 0 || index >= static_cast<FXint>(patternNumbers.size()))
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
        effect.rows = rows->getCheck();
        if (sender == patterns) { effect.fill = "pattern"; fill->setCurrentItem(2); }
        changed();
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
        if (index >= 0 && index < static_cast<FXint>(textureNames.size()))
        {
            effect.texture = textureNames[index];
            changed();
        }
        return 1;
    }
    long onTileFiles(FXObject*, FXSelector selector, void*)
    {
        FXint id = FXSELID(selector);
        FXbool pattern = id == ID_ADD_PATTERN || id == ID_REFRESH_PATTERN || id == ID_PATTERN_CATEGORY;
        FXListBox* categories = pattern ? patternCategory : textureCategory;
        FXTextField* target = pattern ? patternTargetCategory : textureTargetCategory;
        const std::vector<FXString>& names = pattern ? patternCategories : textureCategories;
        if (id == ID_PATTERN_CATEGORY || id == ID_TEXTURE_CATEGORY)
        {
            FXint index = categories->getCurrentItem();
            target->setText(index > 0 && index < static_cast<FXint>(names.size()) ? names[index] : FXString::null);
        }
        if (id == ID_ADD_PATTERN || id == ID_ADD_TEXTURE)
        {
            FileDialog file(this, pattern ? _("Add PNG pattern") : _("Add PNG texture"));
            const char* filters[] = {pattern ? _("PNG patterns") : _("PNG textures"), "*.png", NULL};
            file.setPatternList(filters);
            file.setSelectMode(SELECT_FILE_EXISTING);
            if (!file.execute()) return 1;
            FXString name = FXPath::title(file.getFilename());
            InputDialog naming(this, "", pattern ? _("Pattern name (without a slash):") :
                                                   _("Texture name (without a slash):"),
                               pattern ? _("Pattern name") : _("Texture name"));
            naming.addHelpButton(editor, ThemeEditor::ID_MANUAL);
            naming.setText(name);
            if (!naming.execute()) return 1;
            name = naming.getText().trim();
            FXString category = target->getText().trim();
            FXString reference = name.find_first_of("/\\") >= 0 ? FXString::null :
                BackgroundEffect::textureReference(category.empty() ? name : category + "/" + name);
            FXString error;
            FXString local = pattern ? BackgroundEffect::patternPath(reference, editor->getDraft().assetRoot) :
                                       BackgroundEffect::texturePath(reference, editor->getDraft().assetRoot);
            FXString shared = pattern ? BackgroundEffect::patternPath(reference) : BackgroundEffect::texturePath(reference);
            if (!editor->getDraft().assetRoot.empty() && !local.empty() &&
                local != shared)
            {
                MessageBox::error(this, BOX_OK, pattern ? _("Pattern") : _("Texture"),
                                  _("This name is already used by the current theme. Please use another name."));
                return 1;
            }
            FXbool imported = !reference.empty() &&
                (pattern ? BackgroundEffect::importPattern(getApp(), file.getFilename(), reference, false, error) :
                           BackgroundEffect::importTexture(getApp(), file.getFilename(), reference, false, error));
            if (!imported)
            {
                MessageBox::error(this, BOX_OK, pattern ? _("Pattern") : _("Texture"), "%s",
                                  error.empty() ? _("Invalid graphic name or category.") : error.text());
                return 1;
            }
            if (pattern)
            {
                effect.pattern = 0;
                effect.patternImage = reference;
                effect.fill = "pattern";
                fill->setCurrentItem(2);
            }
            else effect.texture = reference;
            changed();
            categories->setCurrentItem(0);
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
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_REFRESH_PATTERN, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_PATTERN_CATEGORY, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_ADD_TEXTURE, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_REFRESH, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_TEXTURE_CATEGORY, BackgroundEditor::onTileFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_PRESET, BackgroundEditor::onPreset),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_SAVE_PRESET, BackgroundEditor::onPreset),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_RANDOM, BackgroundEditor::onPreset)
};
FXIMPLEMENT(BackgroundEditor, DialogBox, BackgroundEditorMap, ARRAYNUMBER(BackgroundEditorMap))

class BackgroundImageEditor : public DialogBox
{
    FXDECLARE(BackgroundImageEditor)
    ThemeEditor* editor = NULL;
    FXString area, reference;
    FXListBox* category = NULL;
    FXListBox* placement = NULL;
    FXTextField* targetCategory = NULL;
    FXIconList* gallery = NULL;
    FXLabel* status = NULL;
    std::vector<FXString> categories, names;
    std::vector<FXIcon*> icons;
    BackgroundImageEditor() {}

    FXString mode() const
    {
        return placement->getCurrentItem() == 1 ? "fill" : placement->getCurrentItem() == 2 ? "tile" : "fit";
    }

    void changed()
    {
        editor->previewImage(area, reference, mode());
    }

    void collect(const FXString& root, const FXString& parent, std::map<FXString, FXString>& entries,
                 FXString& rejected)
    {
        FXString path = parent.empty() ? root : root + PATHSEPSTRING + parent;
        if (xf_islink(path)) return;
        FXDir directory;
        if (!directory.open(path))
        {
            if (xf_existfile(path))
            {
                rejected += path + ": " + strerror(errno) + "\n";
                appLog(APP_LOG_ERRORS, "image-directory-unreadable", path);
            }
            return;
        }
        while (directory.next())
        {
            FXString filename = directory.name();
            if (filename.empty() || filename[0] == '.') continue;
            FXString file = path + PATHSEPSTRING + filename;
            if (xf_islink(file)) continue;
            FXString relative = parent.empty() ? filename : parent + "/" + filename;
            if (xf_isdirectory(file))
            {
                if (relative.contains('/') < 8) collect(root, relative, entries, rejected);
                else
                {
                    rejected += relative + ": " + _("Too many nested image categories.") + "\n";
                    appLog(APP_LOG_ERRORS, "image-category-too-deep", file);
                }
            }
            else if (xf_isfile(file))
            {
                FXString extension = FXPath::extension(filename).lower();
                if (extension != "png" && extension != "jpg" && extension != "jpeg") continue;
                FXString key = BackgroundEffect::textureReference(relative);
                if (!BackgroundImage::validReference(key))
                {
                    rejected += relative + ": " + _("Invalid image name or category.") + "\n";
                    appLog(APP_LOG_ERRORS, "image-rejected", file);
                }
                else if (!entries.count(key)) entries[key] = file;
            }
        }
    }

    void refresh()
    {
        FXint previous = category->getCurrentItem();
        FXString selected = previous > 0 && previous < static_cast<FXint>(categories.size()) ?
                            categories[previous] : FXString::null;
        gallery->clearItems();
        for (FXIcon* icon : icons) delete icon;
        icons.clear();
        names.clear();
        extern FXString execpath;
        FXString roots[] = {
            editor->getDraft().assetRoot.empty() ? FXString::null :
                editor->getDraft().assetRoot + PATHSEPSTRING "images",
            FXPath::directory(execpath) + "/../share/swordfish/images",
            xf_userdatahome() + PATHSEPSTRING "images"
        };
        std::map<FXString, FXString> entries;
        FXString rejected;
        for (const FXString& root : roots)
            if (!root.empty()) collect(root, "", entries, rejected);

        std::set<FXString> found;
        for (const auto& entry : entries)
        {
            FXString relative;
            if (!BackgroundEffect::textureRelativePath(entry.first, relative)) continue;
            FXint slash = relative.rfind('/');
            if (slash >= 0) found.insert(relative.left(slash));
        }
        category->clearItems();
        categories.clear();
        category->appendItem(_("All categories"));
        categories.push_back("");
        category->appendItem(_("Uncategorized"));
        categories.push_back("");
        FXint current = previous > 0 ? 1 : 0;
        for (const FXString& name : found)
        {
            category->appendItem(name);
            categories.push_back(name);
            if (previous > 1 && name == selected) current = categories.size() - 1;
        }
        category->setCurrentItem(current);
        gallery->appendItem(_("No image"));
        names.push_back("");
        for (const auto& entry : entries)
        {
            FXString relative;
            if (!BackgroundEffect::textureRelativePath(entry.first, relative)) continue;
            FXint slash = relative.rfind('/');
            FXString group = slash >= 0 ? relative.left(slash) : FXString::null;
            if (current != 0 && group != categories[current]) continue;
            FXString error;
            FXIcon* icon = BackgroundImage::preview(getApp(), entry.second, FXRGB(0xD5, 0xDA, 0xDF), 64, error);
            if (!icon)
            {
                rejected += relative + ": " + error + "\n";
                appLog(APP_LOG_ERRORS, "image-rejected", entry.second + ": " + error);
                continue;
            }
            icons.push_back(icon);
            FXString label = slash < 0 ? relative : relative.rafter('/');
            if (current == 0 && !group.empty()) label += " (" + group + ")";
            gallery->appendItem(label, icon, icon);
            names.push_back(entry.first);
            if (entry.first == reference) gallery->setCurrentItem(names.size() - 1);
        }
        if (reference.empty()) gallery->setCurrentItem(0);
        status->setText(rejected.empty() ?
            _("PNG or JPEG, up to 32 MB and 32 megapixels. Originals are never moved.") :
            _("Some images were excluded:\n") + rejected);
    }

public:
    enum { ID_SELECT = DialogBox::ID_LAST, ID_PLACEMENT, ID_CATEGORY, ID_IMPORT, ID_REFRESH };
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
        new FXLabel(controls, _("Category:"));
        category = new FXListBox(controls, this, ID_CATEGORY, FRAME_SUNKEN | FRAME_THICK | LAYOUT_FILL_X);
        category->setNumVisible(10);
        new FXLabel(controls, _("Placement:"));
        placement = new FXListBox(controls, this, ID_PLACEMENT, FRAME_SUNKEN | FRAME_THICK);
        placement->appendItem(_("Fit (show whole image)"));
        placement->appendItem(_("Fill (crop to cover)"));
        placement->appendItem(_("Tile (original size)"));
        placement->setNumVisible(3);
        FXString currentMode = owner->getDraft().property(area, "image_mode");
        placement->setCurrentItem(currentMode == "fill" ? 1 : currentMode == "tile" ? 2 : 0);
        gallery = new FXIconList(this, this, ID_SELECT,
                                 ICONLIST_BIG_ICONS | ICONLIST_BROWSESELECT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        FXHorizontalFrame* files = new FXHorizontalFrame(this, LAYOUT_FILL_X);
        new FXButton(files, _("Add image..."), NULL, this, ID_IMPORT, BUTTON_NORMAL);
        new FXButton(files, _("Refresh"), NULL, this, ID_REFRESH, BUTTON_NORMAL);
        new FXLabel(files, _("Save in category:"));
        targetCategory = new FXTextField(files, 16, NULL, 0, TEXTFIELD_NORMAL | LAYOUT_FILL_X);
        targetCategory->setTipText(_("Optional category; use / for subcategories."));
        refresh();
    }
    ~BackgroundImageEditor()
    {
        for (FXIcon* icon : icons) delete icon;
    }
    long onSelect(FXObject*, FXSelector, void*)
    {
        FXint index = gallery->getCurrentItem();
        if (index >= 0 && index < static_cast<FXint>(names.size()))
        {
            reference = names[index];
            changed();
        }
        return 1;
    }
    long onPlacement(FXObject*, FXSelector, void*)
    {
        if (!reference.empty()) changed();
        return 1;
    }
    long onFiles(FXObject*, FXSelector selector, void*)
    {
        FXint id = FXSELID(selector);
        if (id == ID_CATEGORY)
        {
            FXint index = category->getCurrentItem();
            targetCategory->setText(index > 0 && index < static_cast<FXint>(categories.size()) ?
                                    categories[index] : FXString::null);
        }
        else if (id == ID_IMPORT)
        {
            FileDialog file(this, _("Add background image"));
            const char* filters[] = {_("PNG and JPEG images"), "*.png,*.jpg,*.jpeg", _("All files"), "*", NULL};
            file.setPatternList(filters);
            file.setSelectMode(SELECT_FILE_EXISTING);
            if (!file.execute()) return 1;
            InputDialog naming(this, "", _("Image name (without extension or a slash):"), _("Image name"));
            naming.addHelpButton(editor, ThemeEditor::ID_MANUAL);
            naming.setText(FXPath::title(file.getFilename()));
            if (!naming.execute()) return 1;
            FXString name = naming.getText().trim(), group = targetCategory->getText().trim();
            FXString extension = FXPath::extension(file.getFilename()).lower();
            FXString relative = group.empty() ? name + "." + extension : group + "/" + name + "." + extension;
            FXString candidate = name.empty() || name.find_first_of("/\\") >= 0 ?
                                 FXString::null : BackgroundEffect::textureReference(relative);
            FXString error;
            FXString local = BackgroundImage::path(candidate, editor->getDraft().assetRoot);
            FXString shared = BackgroundImage::path(candidate);
            if (!editor->getDraft().assetRoot.empty() && !local.empty() && local != shared)
                error = _("This name is already used by the current theme. Please use another name.");
            else if (!BackgroundImage::importImage(getApp(), file.getFilename(), candidate, error) && error.empty())
                error = _("Invalid image name or category.");
            if (!error.empty())
            {
                MessageBox::error(this, BOX_OK, _("Background image"), "%s", error.text());
                return 1;
            }
            reference = candidate;
            changed();
            category->setCurrentItem(0);
        }
        refresh();
        return 1;
    }
};

FXDEFMAP(BackgroundImageEditor) BackgroundImageEditorMap[] = {
    FXMAPFUNC(SEL_COMMAND, BackgroundImageEditor::ID_SELECT, BackgroundImageEditor::onSelect),
    FXMAPFUNC(SEL_COMMAND, BackgroundImageEditor::ID_PLACEMENT, BackgroundImageEditor::onPlacement),
    FXMAPFUNCS(SEL_COMMAND, BackgroundImageEditor::ID_CATEGORY, BackgroundImageEditor::ID_REFRESH, BackgroundImageEditor::onFiles)
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
            description = relative + " (" + (mode.empty() ? FXString("fit") : mode) + ")";
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

void ThemeEditor::previewImage(const FXString& selectedArea, const FXString& reference, const FXString& mode)
{
    draft.setProperty(selectedArea, "background_image", reference);
    draft.setProperty(selectedArea, "image_mode", reference.empty() ? FXString::null : mode);
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
                       scope == ID_RESET_ELEMENT ? _("Reset only this element to its original values?") :
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
                draft.setProperty(area, "image_mode", original.property(area, "image_mode"));
        }
    }
    else if (scope == ID_RESET_ELEMENT)
    {
        draft.resetElement(area);
        for (const auto& entry : original.properties)
            if (entry.first.left(area.length() + 1) == area + ".") draft.properties.insert(entry);
        if (area == "list") draft.effect = original.effect;
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
    if (!preferences->saveEditedTheme(result, copy ? -1 : replaceIndex)) return false;
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
