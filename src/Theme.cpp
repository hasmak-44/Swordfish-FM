#include "config.h"
#include "Theme.h"
#include "AppLogger.h"
#include "xfedefs.h"
#include "IconList.h"
#include "ComboBox.h"
#include "TextLabel.h"
#include "TabButtons.h"
#include "PathLinker.h"
#include "XFileExplorer.h"
#include "xfeutils.h"
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <array>

namespace
{
const char* const settingsKeys[NUM_COLORS] = {
    "basecolor", "bordercolor", "backcolor", "forecolor", "selbackcolor", "selforecolor",
    "listbackcolor", "listforecolor", "highlightcolor", "pbarcolor", "attentioncolor", "scrollbarcolor"
};

FXString encode(const FXString& text)
{
    FXString result;
    for (FXint i = 0; i < text.length(); ++i)
    {
        unsigned char c = text[i];
        if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == '#')
            result += c;
        else
            result += FXString().format("%%%02X", c);
    }
    return result;
}

FXbool decode(const FXString& text, FXString& result)
{
    result.clear();
    for (FXint i = 0; i < text.length(); ++i)
    {
        if (text[i] != '%')
        {
            if (!text[i] || text[i] == '\r' || text[i] == '\n') return false;
            result += text[i];
            continue;
        }
        if (i + 2 >= text.length() || !isxdigit(static_cast<unsigned char>(text[i + 1])) ||
            !isxdigit(static_cast<unsigned char>(text[i + 2]))) return false;
        unsigned long c = strtoul(text.mid(i + 1, 2).text(), NULL, 16);
        if (!c || c == '\n' || c == '\r') return false;
        result += static_cast<FXchar>(c);
        i += 2;
    }
    return true;
}

FXFont* widgetFont(FXWindow* window)
{
    if (IconList* list = dynamic_cast<IconList*>(window)) return list->getFont();
    if (FXTreeList* list = dynamic_cast<FXTreeList*>(window)) return list->getFont();
    if (FXLabel* label = dynamic_cast<FXLabel*>(window)) return label->getFont();
    if (FXMenuCaption* menu = dynamic_cast<FXMenuCaption*>(window)) return menu->getFont();
    if (FXHeader* header = dynamic_cast<FXHeader*>(window)) return header->getFont();
    if (FXTextField* field = dynamic_cast<FXTextField*>(window)) return field->getFont();
    if (TextLabel* label = dynamic_cast<TextLabel*>(window)) return label->getFont();
    return NULL;
}

void setWidgetFont(FXWindow* window, FXFont* font)
{
    if (IconList* list = dynamic_cast<IconList*>(window)) list->setFont(font);
    if (FXTreeList* list = dynamic_cast<FXTreeList*>(window)) list->setFont(font);
    if (FXLabel* label = dynamic_cast<FXLabel*>(window)) label->setFont(font);
    if (FXMenuCaption* menu = dynamic_cast<FXMenuCaption*>(window)) menu->setFont(font);
    if (FXHeader* header = dynamic_cast<FXHeader*>(window)) header->setFont(font);
    if (FXTextField* field = dynamic_cast<FXTextField*>(window)) field->setFont(font);
    if (TextLabel* label = dynamic_cast<TextLabel*>(window)) label->setFont(font);
}

void apply(FXWindow* window, const Theme& theme, const FXString& parentArea,
           std::map<FXString, FXFont*>& fonts)
{
    FXString area = themeWidgetArea(window, parentArea);
    FXColor background = theme.colorFor(area, "background");
    FXColor foreground = theme.colorFor(area, "foreground");
    FXColor border = theme.colorFor(area, "border");
    FXColor selected = theme.colorFor(area, "selection_background");
    FXColor selectedText = theme.colorFor(area, "selection_foreground");
    FXColor field = area == "address" && !theme.overrides(area, "background") ?
                        theme.colorFor("main", "field") : background;
    xf_widgetcolors(window, background, field, foreground, border, selected, selectedText);
    if (DialogTabItem* item = dynamic_cast<DialogTabItem*>(window)) item->setHighlightColor(selected);
    if (IconList* list = dynamic_cast<IconList*>(window))
    {
        list->setHighlightColor(theme.colorFor("list", "alternate"));
        list->setBackgroundEffect(theme.effect, theme.assetRoot);
    }
    if (FXScrollBar* scrollbar = dynamic_cast<FXScrollBar*>(window))
        scrollbar->setBackColor(theme.colorFor("scrollbars", "background"));
    if (FXFont* current = widgetFont(window))
    {
        FXString description = theme.fontFor(area, window->getApp()->getNormalFont()->getFont());
        if (description != current->getFont() || theme.overrides(area, "font") || theme.overrides("main", "font"))
        {
            FXFont*& font = fonts[description];
            if (!font)
            {
                font = new FXFont(window->getApp(), description);
                font->create();
            }
            setWidgetFont(window, font);
        }
    }
    for (FXWindow* child = window->getFirst(); child; child = child->getNext())
        apply(child, theme, area, fonts);
    if (FXString(window->getClassName()) == "TabButtons")
        window->handle(window, FXSEL(SEL_COMMAND, TabButtons::ID_THEME), const_cast<Theme*>(&theme));
    if (FXString(window->getClassName()) == "PathLinker")
        window->handle(window, FXSEL(SEL_COMMAND, PathLinker::ID_THEME), NULL);
    FXPopup* popup = NULL;
    if (FXMenuButton* button = dynamic_cast<FXMenuButton*>(window)) popup = button->getMenu();
    if (FXMenuTitle* title = dynamic_cast<FXMenuTitle*>(window)) popup = title->getMenu();
    if (FXMenuCascade* cascade = dynamic_cast<FXMenuCascade*>(window)) popup = cascade->getMenu();
    if (popup) apply(popup, theme, "menus", fonts);
    window->update();
}
}

Theme::Theme(const char* n, const char* t, FXColor base, FXColor border, FXColor background, FXColor foreground,
             FXColor selection, FXColor selectedText, FXColor list, FXColor listText, FXColor alternate,
             FXColor progress, FXColor attention, FXColor scrollbar) : name(n), tip(t)
{
    const FXColor colors[NUM_COLORS] = {base, border, background, foreground, selection, selectedText,
                                       list, listText, alternate, progress, attention, scrollbar};
    for (FXint i = 0; i < NUM_COLORS; ++i) color[i] = colors[i];
}

FXbool Theme::operator!=(const Theme& other) const
{
    for (FXint i = 0; i < NUM_COLORS; ++i)
        if (color[i] != other.color[i]) return true;
    return effect != other.effect || assetRoot != other.assetRoot || properties != other.properties;
}

FXString Theme::property(const FXString& area, const FXString& key) const
{
    auto found = properties.find(area + "." + key);
    return found == properties.end() ? FXString::null : found->second;
}

FXbool Theme::overrides(const FXString& area, const FXString& key) const
{
    return properties.find(area + "." + key) != properties.end();
}

void Theme::setProperty(const FXString& area, const FXString& key, const FXString& value)
{
    if (value.empty())
        properties.erase(area + "." + key);
    else
        properties[area + "." + key] = value;
}

void Theme::resetElement(const FXString& area)
{
    FXString prefix = area + ".";
    for (auto item = properties.begin(); item != properties.end(); )
    {
        if (item->first.left(prefix.length()) == prefix)
            item = properties.erase(item);
        else
            ++item;
    }
    if (area == "list") effect.clear();
}

FXColor Theme::colorFor(const FXString& area, const FXString& key) const
{
    FXint role = 0;
    if (key == "foreground") role = area == "list" || area == "tree" || area == "places" ? 7 : 3;
    else if (key == "border") role = 1;
    else if (key == "selection_background") role = 4;
    else if (key == "selection_foreground") role = 5;
    else if (key == "alternate") role = 8;
    else if (key == "progress") role = 9;
    else if (key == "attention") role = 10;
    else if (key == "field") role = 2;
    else if (key == "background")
    {
        if (area == "list" || area == "tree" || area == "places") role = 6;
        else if (area == "scrollbars") role = 11;
    }
    FXColor inherited = color[role];
    FXColor value;
    FXString parentKey;
    if (role == 0) parentKey = "background";
    else if (role == 1) parentKey = "border";
    else if (role == 2) parentKey = "field";
    else if (role == 3) parentKey = "foreground";
    else if (role == 4) parentKey = "selection_background";
    else if (role == 5) parentKey = "selection_foreground";
    if (area != "main" && !parentKey.empty() && themeColorParse(property("main", parentKey), value))
        inherited = themeBlend(inherited, value, FXALPHAVAL(value) / 255.0);
    if (key == "background" && (area == "toolbars" || area == "status" || area == "address" || area == "tabs"))
    {
        FXint level = area == "tabs" ? 2 : 1;
        FXint percent = property("main", "shading").empty() ? 7 : FXCLAMP(0, FXIntVal(property("main", "shading")), 20);
        FXColor tint = (FXREDVAL(inherited) * 299 + FXGREENVAL(inherited) * 587 + FXBLUEVAL(inherited) * 114) / 1000 >= 96 ?
                           FXRGB(0, 0, 0) : FXRGB(255, 255, 255);
        inherited = themeBlend(inherited, tint, FXMIN(60, level * percent) / 100.0);
    }
    FXString text = property(area, key);
    if (!text.empty() && themeColorParse(text, value))
        return themeBlend(inherited, value, FXALPHAVAL(value) / 255.0);
    return inherited;
}

FXString Theme::fontFor(const FXString& area, const FXString& fallback) const
{
    FXString font = property(area, "font");
    if (font.empty()) font = property("main", "font");
    return font.empty() ? fallback : font;
}

FXString Theme::encodeProperties() const
{
    FXString result;
    for (const auto& entry : properties)
    {
        if (!result.empty()) result += '&';
        result += encode(entry.first) + "=" + encode(entry.second);
    }
    return result;
}

FXbool Theme::decodeProperties(const FXString& text, FXString& error)
{
    std::map<FXString, FXString> parsed;
    for (FXint i = 0; i <= text.contains('&') && !text.empty(); ++i)
    {
        FXString pair = text.section('&', i), key, value;
        if (pair.find('=') < 0 || !decode(pair.before('='), key) || !decode(pair.after('='), value) ||
            key.empty() || key.find('.') < 0 || key.find_first_of(";=\t\r\n") >= 0)
        {
            error = "Invalid theme properties";
            return false;
        }
        FXString area = key.before('.'), property = key.after('.');
        FXbool valid = false;
        if (area == "palette" || area == "gradient") valid = !property.empty() && value.length() <= 32768;
        else
        {
            const char* areas[] = {"main", "list", "tree", "places", "headers", "tabs", "toolbars",
                                     "address", "status", "menus", "controls", "scrollbars"};
            for (const char* name : areas) if (area == name) valid = true;
            if (property == "font") valid = valid && !value.empty() && value.length() <= 512;
            else if (area == "main" && (property == "shading" || property == "opacity"))
            {
                char* end = NULL;
                long number = strtol(value.text(), &end, 10);
                valid = valid && !value.empty() && end && !*end &&
                        number >= (property == "opacity" ? 20 : 0) && number <= (property == "opacity" ? 100 : 20);
            }
            else
            {
                FXColor color;
                const char* colors[] = {"background", "foreground", "field", "border", "selection_background",
                                           "selection_foreground", "alternate"};
                FXbool known = false;
                for (const char* name : colors) if (property == name) known = true;
                valid = valid && known && themeColorParse(value, color);
            }
        }
        if (!valid)
        {
            error = "Invalid theme property: " + key;
            return false;
        }
        parsed[key] = value;
    }
    properties.swap(parsed);
    return true;
}

FXString Theme::serialize() const
{
    FXString result = name + ";" + tip + ";";
    for (FXint i = 0; i < NUM_COLORS; ++i) result += FXStringVal(color[i]) + ";";
    return result + effect + ";" + encodeProperties() + ";";
}

Theme Theme::current(FXApp* app)
{
    Theme theme;
    const FXColor defaults[NUM_COLORS] = {app->getBaseColor(), app->getBorderColor(), app->getBackColor(),
        app->getForeColor(), app->getSelbackColor(), app->getSelforeColor(), app->getBackColor(),
        app->getForeColor(), app->getBackColor(), app->getSelbackColor(), FXRGB(255, 0, 0), app->getBaseColor()};
    for (FXint i = 0; i < NUM_COLORS; ++i)
        theme.color[i] = app->reg().readColorEntry("SETTINGS", settingsKeys[i], defaults[i]);
    theme.effect = app->reg().readStringEntry("SETTINGS", "list_background_effect",
                                               "fill=gradient colors=#135563,#0A2A33,#05141A direction=vertical");
    FXString folder = app->reg().readStringEntry("SETTINGS", "theme_asset_folder", "");
    if (!folder.empty())
    {
        FXString root = FXPath::absolute(xf_userdatahome() + PATHSEPSTRING "themes" PATHSEPSTRING
                                         "colors" PATHSEPSTRING + folder);
        if (folder != "." && folder != ".." && folder.find_first_of("/\\\r\n") < 0 &&
            !xf_islink(root) && xf_isdirectory(root))
            theme.assetRoot = root;
        else
            appLog(APP_LOG_ERRORS, "theme-asset-folder-invalid", folder);
    }
    FXString error;
    if (!theme.decodeProperties(app->reg().readStringEntry("SETTINGS", "theme_details", ""), error))
        appLog(APP_LOG_ERRORS, "theme-properties-invalid", error);
    if (theme.property("main", "shading").empty())
        theme.setProperty("main", "shading", FXStringVal(app->reg().readUnsignedEntry("SETTINGS", "bar_shading", 7)));
    if (theme.property("main", "font").empty())
        theme.setProperty("main", "font", app->getNormalFont()->getFont());
    return theme;
}

void Theme::writeActive(FXSettings& settings) const
{
    for (FXint i = 0; i < NUM_COLORS; ++i) settings.writeColorEntry("SETTINGS", settingsKeys[i], color[i]);
    settings.writeColorEntry("SETTINGS", "hilitecolor", makeHiliteColor(color[0]));
    settings.writeColorEntry("SETTINGS", "shadowcolor", makeShadowColor(color[0]));
    settings.writeStringEntry("SETTINGS", "list_background_effect", effect.text());
    settings.writeStringEntry("SETTINGS", "theme_details", encodeProperties().text());
    FXString library = FXPath::absolute(xf_userdatahome() + PATHSEPSTRING "themes" PATHSEPSTRING "colors");
    if (!assetRoot.empty() && FXPath::directory(assetRoot) == library && !xf_islink(assetRoot))
        settings.writeStringEntry("SETTINGS", "theme_asset_folder", FXPath::name(assetRoot).text());
    else
        settings.deleteEntry("SETTINGS", "theme_asset_folder");
    FXString font = property("main", "font");
    if (!font.empty()) settings.writeStringEntry("SETTINGS", "font", font.text());
    FXString shading = property("main", "shading");
    if (!shading.empty()) settings.writeUnsignedEntry("SETTINGS", "bar_shading", FXUIntVal(shading));
}

FXString themeColorText(FXColor color, FXbool alpha)
{
    FXString text;
    text.format("#%02X%02X%02X", FXREDVAL(color), FXGREENVAL(color), FXBLUEVAL(color));
    if (alpha) text += FXString().format("%02X", FXALPHAVAL(color));
    return text;
}

FXbool themeColorParse(const FXString& text, FXColor& color)
{
    FXString value = FXString(text).trim();
    if ((value.length() != 7 && value.length() != 9) || value[0] != '#') return false;
    for (FXint i = 1; i < value.length(); ++i)
        if (!isxdigit(static_cast<unsigned char>(value[i]))) return false;
    unsigned long number = strtoul(value.text() + 1, NULL, 16);
    if (value.length() == 7)
        color = FXRGB((number >> 16) & 255, (number >> 8) & 255, number & 255);
    else
        color = FXRGBA((number >> 24) & 255, (number >> 16) & 255, (number >> 8) & 255, number & 255);
    return true;
}

FXColor themeBlend(FXColor background, FXColor foreground, double alpha)
{
    return FXRGB(static_cast<int>(FXREDVAL(background) * (1 - alpha) + FXREDVAL(foreground) * alpha + 0.5),
                 static_cast<int>(FXGREENVAL(background) * (1 - alpha) + FXGREENVAL(foreground) * alpha + 0.5),
                 static_cast<int>(FXBLUEVAL(background) * (1 - alpha) + FXBLUEVAL(foreground) * alpha + 0.5));
}

double themeLuminance(FXColor color)
{
    static const std::array<double, 256> values = [] {
        std::array<double, 256> result;
        for (size_t i = 0; i < result.size(); ++i)
        {
            double normalized = i / 255.0;
            result[i] = normalized <= 0.04045 ? normalized / 12.92 : pow((normalized + 0.055) / 1.055, 2.4);
        }
        return result;
    }();
    return 0.2126 * values[FXREDVAL(color)] + 0.7152 * values[FXGREENVAL(color)] + 0.0722 * values[FXBLUEVAL(color)];
}

double themeContrast(FXColor foreground, FXColor background)
{
    if (FXALPHAVAL(foreground) != 255) foreground = themeBlend(background, foreground, FXALPHAVAL(foreground) / 255.0);
    double a = themeLuminance(foreground), b = themeLuminance(background);
    return (FXMAX(a, b) + 0.05) / (FXMIN(a, b) + 0.05);
}

FXString themeWidgetArea(FXWindow* window, const FXString& parent)
{
    FXString name = window->getClassName();
    if (dynamic_cast<FXScrollBar*>(window)) return "scrollbars";
    if (dynamic_cast<IconList*>(window)) return "list";
    if (name == "Places" || name == "SidebarTree") return "places";
    if (dynamic_cast<FXTreeList*>(window)) return "tree";
    if (dynamic_cast<FXHeader*>(window)) return "headers";
    if (name == "TabButtons" || name == "TabDragButton" || name == "TabCloseButton") return "tabs";
    if (dynamic_cast<FXTabItem*>(window) || dynamic_cast<FXTabBook*>(window)) return "tabs";
    if (name == "PathLinker" || dynamic_cast<ComboBox*>(window)) return "address";
    if (dynamic_cast<FXStatusBar*>(window) || dynamic_cast<FXStatusLine*>(window)) return "status";
    if (dynamic_cast<FXMenuCaption*>(window) || dynamic_cast<FXMenuPane*>(window) ||
        dynamic_cast<FXMenuBar*>(window)) return "menus";
    if (dynamic_cast<FXToolBar*>(window)) return "toolbars";
    if (dynamic_cast<FXTextField*>(window) || dynamic_cast<TextLabel*>(window))
        return parent == "toolbars" ? FXString("address") : parent;
    if (dynamic_cast<FXButton*>(window) && parent == "main") return "controls";
    for (FXWindow* ancestor = window->getParent(); ancestor; ancestor = ancestor->getParent())
        if (FXString(ancestor->getClassName()) == "FilePanel")
        {
            FXString area = static_cast<FilePanel*>(ancestor)->themeArea(window);
            if (!area.empty()) return area;
            break;
        }
    return parent;
}

void applyThemeWidgets(FXWindow* window, const Theme& theme, std::vector<FXFont*>& ownedFonts)
{
    std::map<FXString, FXFont*> fonts;
    for (FXFont* font : ownedFonts) fonts[font->getFont()] = font;
    apply(window, theme, "main", fonts);
    ownedFonts.clear();
    for (const auto& entry : fonts) ownedFonts.push_back(entry.second);
}

void xf_windowcolors(FXWindow* window)
{
    FXWindow* shell = window->getShell();
    while (dynamic_cast<FXPopup*>(shell))
    {
        FXWindow* owner = shell->getOwner();
        if (!owner || owner->getShell() == shell) break;
        shell = owner->getShell();
    }
    if (FXString(shell->getClassName()) == "XFileExplorer")
        shell->handle(window, FXSEL(SEL_COMMAND, XFileExplorer::ID_THEME_REQUEST), window);
    else
        xf_dialogcolors(window);
}
