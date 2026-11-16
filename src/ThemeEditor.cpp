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
    FXString contrastEffect;
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

    void capture(FXWindow* window, FXWindow* root, const FXString& parentArea)
    {
        if (!window->shown() || window->getWidth() <= 0 || window->getHeight() <= 0) return;
        Node node;
        FXint x, y;
        window->translateCoordinatesTo(x, y, root, 0, 0);
        node.rectangle = FXRectangle(x, y, window->getWidth(), window->getHeight());
        node.area = themeWidgetArea(window, parentArea);
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
        if (list->backgroundEffect != theme.effect) list->setBackgroundEffect(theme.effect);
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
    double minimumContrast(FXColor foreground)
    {
        FXColor base = theme.colorFor("list", "background"), alternate = theme.colorFor("list", "alternate");
        if (luminances.empty() || contrastEffect != theme.effect || contrastBase != base || contrastAlternate != alternate)
        {
            luminances.clear();
            luminances.push_back(themeLuminance(base));
            for (Node& node : nodes)
            {
                IconList* list = node.list;
                if (!list) continue;
                list->setBackColor(base);
                list->setHighlightColor(alternate);
                list->setBackgroundEffect(theme.effect);
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
            std::sort(luminances.begin(), luminances.end());
            luminances.erase(std::unique(luminances.begin(), luminances.end()), luminances.end());
            contrastEffect = theme.effect;
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
    FXIconList* textures = NULL;
    FXSpinner* patternSize = NULL;
    FXSpinner* strength = NULL;
    FXCheckButton* originalColors = NULL;
    FXCheckButton* rows = NULL;
    FXTextField* presetName = NULL;
    FXListBox* presets = NULL;
    FXLabel* status = NULL;
    std::vector<FXString> textureNames, presetEffects;
    std::vector<FXIcon*> icons;
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
        if (effect.pattern > 0) patterns->setCurrentItem(effect.pattern - 1);
        syncing = false;
    }
    void loadTextures()
    {
        textures->clearItems();
        textureNames.clear();
        textures->appendItem(_("No texture"));
        textureNames.push_back("");
        extern FXString execpath, xdgconfighome;
        FXString directories[2] = {FXPath::directory(execpath) + "/../share/swordfish/textures",
                                    xdgconfighome + PATHSEPSTRING XFECONFIGPATH "/textures"};
        std::set<FXString> names;
        FXString rejected;
        for (FXint folder = 0; folder < 2; ++folder)
        {
            FXDir directory;
            if (!directory.open(directories[folder]))
            {
                if (xf_existfile(directories[folder]))
                {
                    rejected += directories[folder] + ": " + strerror(errno) + "\n";
                    appLog(APP_LOG_ERRORS, "texture-directory-unreadable", directories[folder]);
                }
                continue;
            }
            while (directory.next())
            {
                FXString filename = directory.name();
                if (FXPath::extension(filename).lower() != "png") continue;
                FXString name = FXPath::title(filename);
                FXString error, path = directories[folder] + PATHSEPSTRING + filename;
                if (!BackgroundEffect::validTextureName(name))
                    error = _("Use only letters, numbers, hyphens and underscores in a texture name.");
                else if (names.count(name))
                    error = _("Texture name already exists. Please use another name.");
                else
                    BackgroundEffect::validateTexture(getApp(), path, error);
                if (!error.empty())
                {
                    rejected += filename + ": " + error + "\n";
                    appLog(APP_LOG_ERRORS, "texture-rejected", path + ": " + error);
                    continue;
                }
                names.insert(name);
                BackgroundEffect preview = effect;
                preview.texture = name;
                preview.strength = 65;
                FXIcon* icon = BackgroundEffect::preview(this, preview.serialize(),
                                                          editor->getDraft().colorFor("list", "background"));
                icons.push_back(icon);
                textures->appendItem(name, icon, icon);
                textureNames.push_back(name);
                if (name == effect.texture) textures->setCurrentItem(textureNames.size() - 1);
            }
        }
        status->setText(rejected.empty() ? _("PNG tiles: at most 1024 x 1024 pixels. Originals are never moved.") :
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
        ID_ADD_TEXTURE,
        ID_REFRESH,
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
        patterns = new FXIconList(patternPage, this, ID_LIVE,
                                   ICONLIST_BIG_ICONS | ICONLIST_BROWSESELECT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        const char* names[] = {_("Horizontal stripes"), _("Vertical stripes"), _("Diagonal stripes"), _("Checks"),
                                _("Grid"), _("Dots"), _("Diamonds"), _("Crosshatch")};
        for (FXint i = 0; i < 8; ++i)
        {
            BackgroundEffect sample = effect;
            sample.fill = "pattern";
            sample.pattern = i + 1;
            sample.texture.clear();
            sample.colors = {owner->getDraft().colorFor("list", "background"),
                               makeShadowColor(owner->getDraft().colorFor("list", "background"))};
            FXIcon* icon = BackgroundEffect::preview(this, sample.serialize(), sample.colors[0]);
            icons.push_back(icon);
            patterns->appendItem(names[i], icon, icon);
        }
        FXHorizontalFrame* patternControls = new FXHorizontalFrame(patternPage, LAYOUT_FILL_X);
        new FXLabel(patternControls, _("Pattern size (pixels):"));
        patternSize = new FXSpinner(patternControls, 4, this, ID_LIVE, FRAME_SUNKEN | FRAME_THICK);
        patternSize->setRange(4, 256);
        new FXButton(patternControls, _("First colour..."), NULL, this, ID_STOP_COLOR, BUTTON_NORMAL);
        new FXButton(patternControls, _("Second colour..."), NULL, this, ID_STOP_COLOR + 100, BUTTON_NORMAL);
        new DialogTabItem(pages, _("Texture"));
        FXVerticalFrame* texturePage = new FXVerticalFrame(pages, FRAME_NONE | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        textures = new FXIconList(texturePage, this, ID_TEXTURE,
                                   ICONLIST_BIG_ICONS | ICONLIST_BROWSESELECT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        FXHorizontalFrame* textureControls = new FXHorizontalFrame(texturePage, LAYOUT_FILL_X);
        new FXButton(textureControls, _("Add PNG..."), NULL, this, ID_ADD_TEXTURE, BUTTON_NORMAL);
        new FXButton(textureControls, _("Refresh"), NULL, this, ID_REFRESH, BUTTON_NORMAL);
        originalColors = new FXCheckButton(texturePage, _("Use original PNG colours (including transparency)"),
                                              this, ID_LIVE);
        FXHorizontalFrame* opacity = new FXHorizontalFrame(texturePage, LAYOUT_FILL_X);
        new FXLabel(opacity, _("Texture opacity / strength (%):"));
        strength = new FXSpinner(opacity, 4, this, ID_LIVE, FRAME_SUNKEN | FRAME_THICK);
        strength->setRange(0, 100);
        sync();
        loadTextures();
    }
    ~BackgroundEditor()
    {
        for (FXIcon* icon : icons) delete icon;
    }
    long onLive(FXObject* sender, FXSelector, void*)
    {
        if (syncing) return 1;
        effect.fill = fill->getCurrentItem() == 1 ? "gradient" : fill->getCurrentItem() == 2 ? "pattern" : "solid";
        effect.direction = direction->getCurrentItem() == 1 ? "radial" : "linear";
        effect.angled = effect.direction == "linear";
        effect.angle = angle->getValue();
        effect.pattern = FXMAX(0, patterns->getCurrentItem()) + 1;
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
    long onTextureFiles(FXObject*, FXSelector selector, void*)
    {
        if (FXSELID(selector) == ID_ADD_TEXTURE)
        {
            FileDialog file(this, _("Add PNG texture"));
            const char* patterns[] = {_("PNG textures"), "*.png", NULL};
            file.setPatternList(patterns);
            file.setSelectMode(SELECT_FILE_EXISTING);
            if (!file.execute()) return 1;
            FXString name = FXPath::title(file.getFilename());
            InputDialog naming(this, "", _("Name (letters, numbers, hyphens and underscores):"), _("Texture name"));
            naming.setText(name);
            if (!naming.execute()) return 1;
            name = naming.getText().trim();
            FXString error;
            if (!BackgroundEffect::importTexture(getApp(), file.getFilename(), name, false, error))
            {
                MessageBox::error(this, BOX_OK, _("Texture"), "%s", error.text());
                return 1;
            }
            effect.texture = name;
            changed();
        }
        loadTextures();
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
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_ADD_TEXTURE, BackgroundEditor::onTextureFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_REFRESH, BackgroundEditor::onTextureFiles),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_PRESET, BackgroundEditor::onPreset),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_SAVE_PRESET, BackgroundEditor::onPreset),
    FXMAPFUNC(SEL_COMMAND, BackgroundEditor::ID_RANDOM, BackgroundEditor::onPreset)
};
FXIMPLEMENT(BackgroundEditor, DialogBox, BackgroundEditorMap, ARRAYNUMBER(BackgroundEditorMap))

class ThemeDecision : public DialogBox
{
    FXDECLARE(ThemeDecision)
    ThemeDecision() {}
public:
    enum { ID_FIRST = DialogBox::ID_LAST, ID_SECOND };
    ThemeDecision(FXWindow* owner, const FXString& title, const FXString& question,
                  const FXString& first, const FXString& second) : DialogBox(owner, title)
    {
        new FXLabel(this, question, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
        FXHorizontalFrame* buttons = new FXHorizontalFrame(this, LAYOUT_FILL_X | PACK_UNIFORM_WIDTH);
        new FXButton(buttons, first, NULL, this, ID_FIRST, BUTTON_DEFAULT | BUTTON_INITIAL | LAYOUT_RIGHT | FRAME_GROOVE);
        new FXButton(buttons, second, NULL, this, ID_SECOND, BUTTON_NORMAL | LAYOUT_RIGHT);
        new FXButton(buttons, _("Cancel"), NULL, this, ID_CANCEL, BUTTON_NORMAL | LAYOUT_RIGHT);
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
    mockup = new ThemeMockup(view, source, this);
    mockup->setLayoutHints(LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXVerticalFrame* sheet = new FXVerticalFrame(workspace, LAYOUT_FIX_WIDTH | LAYOUT_FILL_Y, 0, 0, 290);
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
    new FXLabel(sheet, _("Effects are available only for file lists.\nOther elements expose supported properties only.\n"
                         "Alpha colours blend over the inherited colour.\n"
                         "Window opacity additionally requires an X11 compositor."),
                  NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    start(selected, mode);
    onPreview(NULL, 0, NULL);
}

ThemeEditor::~ThemeEditor()
{
    getApp()->removeTimeout(this, ID_READABILITY);
    delete mockup;
    mockup = NULL;
}

void ThemeEditor::start(FXint index, StartMode mode)
{
    if (mode == EDIT_CURRENT) index = 0;
    if (mode == NEW_THEME)
    {
        index = 6;
        for (size_t i = 1; i < choices.size(); ++i)
            if (choices[i].name == "Slate Blue") index = i;
    }
    index = FXCLAMP(0, index, static_cast<FXint>(choices.size()) - 1);
    original = choices[index];
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
    baseTheme->setCurrentItem(index);
    compare->setCheck(false);
    refreshProperties();
    refreshPreview();
    setTitle(FXString(_("Theme Editor")) + " - " + original.name);
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
    double contrast = area == "list" ? mockup->minimumContrast(foreground) :
                                       themeContrast(foreground, shown.colorFor(area, "background"));
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
    double black = area == "list" ? mockup->minimumContrast(FXRGB(0, 0, 0)) :
                                     themeContrast(FXRGB(0, 0, 0), shown.colorFor(area, "background"));
    double white = area == "list" ? mockup->minimumContrast(FXRGB(255, 255, 255)) :
                                     themeContrast(FXRGB(255, 255, 255), shown.colorFor(area, "background"));
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
    else if (key == "shading") description = draft.property("main", "shading") + "%";
    else if (key == "opacity") description = (draft.property("main", "opacity").empty() ?
                                               FXString("100") : draft.property("main", "opacity")) + "%";
    else description = draft.overrides(area, key) ? draft.property(area, key) : themeColorText(draft.colorFor(area, key));
    value->setText(description);
    FXbool inheritable = key != "effect" && key != "shading" && key != "opacity";
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
    if (key == "effect")
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
                                              _("Reset the whole theme to the built-in Slate Blue defaults?");
    if (MessageBox::question(this, BOX_YES_NO, _("Reset appearance"), "%s", message.text()) != BOX_CLICKED_YES) return 1;
    if (scope == ID_RESET_PROPERTY)
    {
        FXString key = currentProperty();
        if (key == "effect") draft.effect = original.effect;
        else draft.setProperty(area, key, original.property(area, key));
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
            if (theme.name == "Slate Blue") { FXString name = draft.name; draft = theme; draft.name = name; break; }
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
        "Only the mockup changes. A mixed light/dark texture may still need a gentler background.");
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
    if (copy) { choices.push_back(result); replaceIndex = choices.size() - 1; baseTheme->appendItem(result.name); }
    else choices[replaceIndex] = result;
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
