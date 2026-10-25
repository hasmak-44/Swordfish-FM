// Message box. Taken from the FOX library and slightly modified for translation purpose.
// Also added a SU button

#include "config.h"
#include "i18n.h"

#include <fx.h>
#include <fxkeys.h>
#include <FXPNGIcon.h>

#include <stdlib.h>
#include <string.h>

#include "icons.h"
#include "xfedefs.h"
#include "xfeutils.h"
#include "startupnotification.h"
#include "XFileExplorer.h"
#include "MessageBox.h"


// Main window
extern FXMainWindow* mainWindow;


// Padding for message box buttons
#define HORZ_PAD    30
#define VERT_PAD    2

#define BOX_BUTTON_MASK    (BOX_OK | BOX_OK_CANCEL | BOX_YES_NO | BOX_YES_NO_CANCEL | BOX_QUIT_CANCEL | \
                            BOX_QUIT_SAVE_CANCEL | BOX_OK_SU | BOX_YES_NO_ALL_CANCEL)


// Map
FXDEFMAP(MessageBox) MessageBoxMap[] =
{
    FXMAPFUNC(SEL_COMMAND, MessageBox::ID_CANCEL, MessageBox::onCmdCancel),
    FXMAPFUNCS(SEL_COMMAND, MessageBox::ID_CLICKED_YES, MessageBox::ID_CLICKED_ALL, MessageBox::onCmdClicked),
    FXMAPFUNC(SEL_COMMAND, MessageBox::ID_CLICKED_SU, MessageBox::onCmdSu),
};



FXDEFMAP(LinkText) LinkTextMap[] =
{
    FXMAPFUNC(SEL_MOTION, 0, LinkText::onMotion),
    FXMAPFUNC(SEL_LEFTBUTTONRELEASE, 0, LinkText::onLeftBtnRelease),
};

FXIMPLEMENT(LinkText, FXText, LinkTextMap, ARRAYNUMBER(LinkTextMap))


static std::vector<FXString> namedLinkWords;
static std::vector<FXString> namedLinkUrls;


void LinkText::addNamedLink(const FXString& words, const FXString& url)
{
    namedLinkWords.push_back(words);
    namedLinkUrls.push_back(url);
}


FXColor LinkText::linkColor(FXColor back)
{
    FXuint luma = (FXREDVAL(back) * 299 + FXGREENVAL(back) * 587 + FXBLUEVAL(back) * 114) / 1000;
    return luma < 128 ? FXRGB(120, 190, 255) : FXRGB(0, 70, 170);
}


LinkText::LinkText(FXComposite* p, FXObject* tgt, FXSelector sel, FXuint opts) :
    FXText(p, tgt, sel, opts)
{
    memset(&linkstyle, 0, sizeof(linkstyle));
}


void LinkText::setText(const FXchar* str, FXint n, FXbool notify)
{
    FXText::setText(str, n, notify);
    findLinks();
}


void LinkText::setText(const FXString& str, FXbool notify)
{
    FXText::setText(str, notify);
    findLinks();
}


// Underline every http(s) address and remember where it is
void LinkText::findLinks()
{
    linkstart.clear();
    linkend.clear();
    linkurl.clear();
    FXString str = getText();
    FXint pos = 0;
    while (true)
    {
        FXint http = str.find("http://", pos);
        FXint https = str.find("https://", pos);
        FXint start = (http < 0) ? https : (https < 0 ? http : FXMIN(http, https));
        if (start < 0)
        {
            break;
        }
        FXint end = start;
        while (end < str.length() && !strchr(" \t\r\n<>\"'()[]", str[end]))
        {
            end++;
        }
        while (end > start && strchr(".,;:!?", str[end - 1]))
        {
            end--;
        }
        linkstart.push_back(start);
        linkend.push_back(end);
        linkurl.push_back(str.mid(start, end - start));
        pos = end;
    }
    FXString lower = str;
    lower.lower();
    for (size_t n = 0; n < namedLinkWords.size(); n++)
    {
        FXString words = namedLinkWords[n];
        words.lower();
        for (FXint start = lower.find(words); start >= 0; start = lower.find(words, start + words.length()))
        {
            linkstart.push_back(start);
            linkend.push_back(start + words.length());
            linkurl.push_back(namedLinkUrls[n]);
        }
    }
    if (linkstart.empty())
    {
        return;
    }
    linkstyle.normalForeColor = linkColor(getBackColor());
    linkstyle.normalBackColor = getBackColor();
    linkstyle.selectForeColor = getSelTextColor();
    linkstyle.selectBackColor = getSelBackColor();
    linkstyle.hiliteForeColor = getHiliteTextColor();
    linkstyle.hiliteBackColor = getHiliteBackColor();
    linkstyle.activeBackColor = getActiveBackColor();
    linkstyle.style = STYLE_UNDERLINE;
    setHiliteStyles(&linkstyle);
    setStyled(true);
    for (size_t i = 0; i < linkstart.size(); i++)
    {
        changeStyle(linkstart[i], linkend[i] - linkstart[i], 1);
    }
}


FXint LinkText::linkAt(FXint x, FXint y) const
{
    FXint pos = getPosAt(x, y);
    for (size_t i = 0; i < linkstart.size(); i++)
    {
        if (pos >= linkstart[i] && pos < linkend[i])
        {
            return (FXint)i;
        }
    }
    return -1;
}


long LinkText::onMotion(FXObject* sender, FXSelector sel, void* ptr)
{
    long ret = FXText::onMotion(sender, sel, ptr);
    FXEvent* event = (FXEvent*)ptr;
    if (!(event->state & LEFTBUTTONMASK))
    {
        FXbool over = linkAt(event->win_x, event->win_y) >= 0;
        setDefaultCursor(getApp()->getDefaultCursor(over ? DEF_HAND_CURSOR : DEF_TEXT_CURSOR));
    }
    return ret;
}


// A plain click (not a text selection drag) on a link opens it in the browser
long LinkText::onLeftBtnRelease(FXObject* sender, FXSelector sel, void* ptr)
{
    long ret = FXText::onLeftBtnRelease(sender, sel, ptr);
    FXEvent* event = (FXEvent*)ptr;
    FXint link = linkAt(event->win_x, event->win_y);
    if (event->moved || link < 0)
    {
        return ret;
    }
    FXString url = linkurl[link];
    const char* hostPath = getenv("SWORDFISH_HOST_PATH");
    FXString opener = FXPath::search(hostPath ? FXString(hostPath) : FXSystem::getExecPath(), "xdg-open");
    if (opener.empty())
    {
        getApp()->beep();
        return 1;
    }
    FXString cmd = xf_hostcommand(xf_quote(opener) + " " + xf_quote(url)) + " >/dev/null 2>&1 &";
    if (system(cmd.text()) != 0)
    {
        getApp()->beep();
    }
    return 1;
}


// Object implementation
FXIMPLEMENT(MessageBox, DialogBox, MessageBoxMap, ARRAYNUMBER(MessageBoxMap))


// Construct message box with given caption, icon, and message text
MessageBox::MessageBox(FXWindow* owner, const FXString& caption, const FXString& text, FXIcon* ic, FXuint opts,
                       FXuint textopts, int x, int y, FXbool scrollable) :
    DialogBox(owner, caption,
              opts | DECOR_TITLE | DECOR_BORDER | DECOR_SHRINKABLE | DECOR_STRETCHABLE | DECOR_MAXIMIZE | DECOR_CLOSE,
              x, y, 0, 0, 0, 0, 0, 0, 4, 4)
{
    initialize(text, ic, opts & BOX_BUTTON_MASK, textopts, scrollable);
}


// Construct free floating message box with given caption, icon, and message text
MessageBox::MessageBox(FXApp* a, const FXString& caption, const FXString& text, FXIcon* ic, FXuint opts,
                       FXuint textopts, int x, int y, FXbool scrollable) :
    DialogBox(a, caption,
              opts | DECOR_TITLE | DECOR_BORDER | DECOR_SHRINKABLE | DECOR_STRETCHABLE | DECOR_MINIMIZE |
              DECOR_MAXIMIZE | DECOR_CLOSE, x, y, 0, 0, 0, 0, 0, 0, 4, 4)
{
    initialize(text, ic, opts & BOX_BUTTON_MASK, textopts, scrollable);
}


// Build contents
void MessageBox::initialize(const FXString& text, FXIcon* ic, FXuint whichbuttons, FXuint textoptions,
                            FXbool scrollable)
{
    FXButton* initial;
    FXVerticalFrame* content = new FXVerticalFrame(this, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXHorizontalFrame* info = new FXHorizontalFrame(content, LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y,
                                                    0, 0, 0, 0, 10, 10, 10, 10);

    if (scrollable)
    {
        if (ic)
        {
            new FXLabel(info, FXString::null, ic, ICON_BEFORE_TEXT | LAYOUT_CENTER_Y);
        }
        scrollmsg = new LinkText(info, NULL, 0, TEXT_READONLY | TEXT_WORDWRAP | LAYOUT_FILL_X | LAYOUT_FILL_Y);
        scrollmsg->setVisibleRows(16);
        scrollmsg->setVisibleColumns(72);
    }
    else
    {
        msg = new FXLabel(info, FXString::null, ic, textoptions);
    }
    setText(text);

    FXHorizontalFrame* buttons = new FXHorizontalFrame(content,
                                                       LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | PACK_UNIFORM_WIDTH, 0,
                                                       0, 0, 0, 10, 10, 10, 10);
    if (whichbuttons == BOX_OK)
    {
        initial = new FXButton(buttons, _("&OK"), NULL, this, ID_CLICKED_OK,
                               BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT |
                               LAYOUT_CENTER_X,
                               0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        initial->setFocus();
    }
    else if (whichbuttons == BOX_OK_SU)
    {
        initial = new FXButton(buttons, _("&OK"), NULL, this, ID_CLICKED_OK,
                               BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT |
                               LAYOUT_CENTER_X,
                               0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);

        // Su button only if allowed
        FXbool root_mode = getApp()->reg().readUnsignedEntry("OPTIONS", "root_mode", TRUE);
        if (root_mode)
        {
            FXString key = getApp()->reg().readStringEntry("KEYBINDINGS", "new_root_window", "Shift-F3");
            // Space before tab is used to set the correct button height
            FXButton* btn = new FXButton(buttons, " " + TAB + _("Launch Swordfish as root") + PARS(key), minixferooticon,
                                         this, ID_CLICKED_SU, BUTTON_DEFAULT | ICON_AFTER_TEXT |
                                         FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                                         0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
            FXHotKey hotkey = xf_parseaccel(key);
            btn->addHotKey(hotkey);
        }
        initial->setFocus();
    }
    else if (whichbuttons == BOX_OK_CANCEL)
    {
        initial = new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                               BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT |
                               LAYOUT_CENTER_X,
                               0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&OK"), NULL, this, ID_CLICKED_OK,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        initial->setFocus();
    }
    else if (whichbuttons == BOX_YES_NO)
    {
        initial = new FXButton(buttons, _("&No"), NULL, this, ID_CLICKED_NO,
                               BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT |
                               LAYOUT_CENTER_X,
                               0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Yes"), NULL, this, ID_CLICKED_YES,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        initial->setFocus();
    }
    else if (whichbuttons == BOX_YES_NO_CANCEL)
    {
        initial = new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                               BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT |
                               LAYOUT_CENTER_X,
                               0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Yes"), NULL, this, ID_CLICKED_YES,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&No"), NULL, this, ID_CLICKED_NO,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        initial->setFocus();
    }
    else if (whichbuttons == BOX_QUIT_CANCEL)
    {
        initial = new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                               BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT |
                               LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Quit"), NULL, this, ID_CLICKED_QUIT,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        initial->setFocus();
    }
    else if (whichbuttons == BOX_QUIT_SAVE_CANCEL)
    {
        initial = new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                               BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT |
                               LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Quit"), NULL, this, ID_CLICKED_QUIT,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Save"), NULL, this, ID_CLICKED_SAVE,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        initial->setFocus();
    }
    else if (whichbuttons == BOX_YES_NO_ALL_CANCEL)
    {
        initial = new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                               BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP |
                               LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Yes"), NULL, this, ID_CLICKED_YES,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&No"), NULL, this, ID_CLICKED_NO,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("Yes for &All"), NULL, this, ID_CLICKED_ALL,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X,
                     0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        initial->setFocus();
    }
}


// Close dialog
long MessageBox::onCmdClicked(FXObject*, FXSelector sel, void*)
{
    getApp()->stopModal(this, BOX_CLICKED_YES + (FXSELID(sel) - ID_CLICKED_YES));
    hide();
    return 1;
}


// Launch a root Xfe (su mode)
long MessageBox::onCmdSu(FXObject* sender, FXSelector sel, void*)
{
    // Close dialog
    getApp()->stopModal(this, BOX_CLICKED_SU);
    hide();

    // Wait cursor
    getApp()->beginWaitCursor();

    // Obtain preferred root authentication mode
    FXuint root_auth = getApp()->reg().readUnsignedEntry("OPTIONS", "root_auth", 0);

    // Selected item in current file panel
    int selitem = ((XFileExplorer*)mainWindow)->getCurrentPanel()->getCurrentItem();

    FXString seldir;

    // No selected item in file panel, get selected directory path
    if (selitem < 0)
    {
        seldir = ((XFileExplorer*)mainWindow)->getDirPanel()->getDirectory();
    }
    // Get selected item path name
    else
    {
        seldir = ((XFileExplorer*)mainWindow)->getCurrentPanel()->getList()->getItemPathname(selitem);
    }

    // Current directory
    FXString currdir = ((XFileExplorer*)mainWindow)->getCurrentPanel()->getDirectory();

    // Use pkexec, sudo or su to launch xfe as root
    int status;
    FXString title, cmd, command;

    // Use pkexec
    if (root_auth == 0)
    {
        // Check if pkexec exists
        if (!xf_existcommand("pkexec"))
        {
            MessageBox::error(this, BOX_OK, _("Error"),
            _("Command pkexec not found!\n\nPlease check that the pkexec package is installed (else use sudo or su root mode)"));
            getApp()->beginWaitCursor();
            return 0;
        }

        // Build command from selected directory
        cmd = getApp()->reg().readStringEntry("OPTIONS", "pkexec_cmd", DEFAULT_PKEXEC_CMD);
        cmd += " " + seldir;

#ifdef STARTUP_NOTIFICATION
        status = runcmd(cmd, "pkexec", currdir, currdir, false, "");
#else
        status = runcmd(cmd, currdir, currdir);
#endif
    }
    // Use sudo or su
    else
    {
        // sudo
        if (root_auth == 1)
        {
            title = _("Enter user password:");
            FXString sudo_cmd = getApp()->reg().readStringEntry("OPTIONS", "sudo_cmd", DEFAULT_SUDO_CMD);
            cmd = " -g 60x4 -e " + sudo_cmd;
        }
        // su
        else if (root_auth == 2)
        {
            title = _("Enter root password:");
            FXString su_cmd = getApp()->reg().readStringEntry("OPTIONS", "su_cmd", DEFAULT_SU_CMD);
            cmd = " -g 60x4 -e " + su_cmd;
        }
        else
        {
            // Should not happen
        }

        cmd += " " + seldir;

        // Get text font
        FXString fontspec = getApp()->reg().readStringEntry("SETTINGS", "textfont", DEFAULT_TEXT_FONT);
        if (fontspec.empty())
        {
            command = "st -t " + xf_quote(title) + cmd;
        }
        else
        {
            FXchar fontsize[32];
            FXFont* font = new FXFont(getApp(), fontspec);
            font->create();
            snprintf(fontsize, sizeof(fontsize), "%d", (int)(font->getSize() / 10)); // Size is in deci-points, thus divide by 10
            command = "st -t " + xf_quote(title) + " -f '" + (font->getFamily()).text() + ":pixelsize=" + fontsize +
                      "'" + cmd;
            delete font;
        }

        // Execute su or sudo command in an internal st terminal
        status = xf_runst(command);
    }

    // If error
    if (status < 0)
    {
        MessageBox::error(getApp(), BOX_OK, _("Error"), _("An error has occurred!"));
        getApp()->endWaitCursor();
        return 0;
    }

    // Wait cursor
    getApp()->endWaitCursor();

    return 1;
}


// Close dialog with a cancel
long MessageBox::onCmdCancel(FXObject* sender, FXSelector, void* ptr)
{
    return MessageBox::onCmdClicked(sender, FXSEL(SEL_COMMAND, ID_CLICKED_CANCEL), ptr);
}


// Show a modal error message
FXuint MessageBox::error(FXWindow* owner, FXuint opts, const char* caption, const char* message, ...)
{
    va_list arguments;

    va_start(arguments, message);
    MessageBox box(owner, caption, FXStringVFormat(message, arguments), bigerroricon,
                   opts | DECOR_TITLE | DECOR_BORDER);
    va_end(arguments);
    return box.execute();
}


// Show a modal error message, in free floating window
FXuint MessageBox::error(FXApp* app, FXuint opts, const char* caption, const char* message, ...)
{
    va_list arguments;

    va_start(arguments, message);
    MessageBox box(app, caption, FXStringVFormat(message, arguments), bigerroricon, opts | DECOR_TITLE | DECOR_BORDER);
    va_end(arguments);
    return box.execute();
}


// Show a modal warning message
FXuint MessageBox::warning(FXWindow* owner, FXuint opts, const char* caption, const char* message, ...)
{
    va_list arguments;

    va_start(arguments, message);
    MessageBox box(owner, caption, FXStringVFormat(message, arguments), bigwarningicon,
                   opts | DECOR_TITLE | DECOR_BORDER);
    va_end(arguments);
    return box.execute();
}


// Show a modal warning message, in free floating window
FXuint MessageBox::warning(FXApp* app, FXuint opts, const char* caption, const char* message, ...)
{
    va_list arguments;

    va_start(arguments, message);
    MessageBox box(app, caption, FXStringVFormat(message, arguments), bigwarningicon,
                   opts | DECOR_TITLE | DECOR_BORDER);
    va_end(arguments);
    return box.execute();
}


// Show a modal question dialog
FXuint MessageBox::question(FXWindow* owner, FXuint opts, const char* caption, const char* message, ...)
{
    va_list arguments;

    va_start(arguments, message);
    MessageBox box(owner, caption, FXStringVFormat(message, arguments), bigquestionicon,
                   opts | DECOR_TITLE | DECOR_BORDER);
    va_end(arguments);
    return box.execute();
}


// Show a modal question dialog, in free floating window
FXuint MessageBox::question(FXApp* app, FXuint opts, const char* caption, const char* message, ...)
{
    va_list arguments;

    va_start(arguments, message);
    MessageBox box(app, caption, FXStringVFormat(message, arguments), bigquestionicon,
                   opts | DECOR_TITLE | DECOR_BORDER);
    va_end(arguments);
    return box.execute();
}


// Show a modal information dialog
FXuint MessageBox::information(FXWindow* owner, FXuint opts, const char* caption, const char* message, ...)
{
    va_list arguments;

    va_start(arguments, message);
    MessageBox box(owner, caption, FXStringVFormat(message, arguments), biginfoicon, opts | DECOR_TITLE | DECOR_BORDER);
    va_end(arguments);
    return box.execute();
}


// Show a modal information dialog, in free floating window
FXuint MessageBox::information(FXApp* app, FXuint opts, const char* caption, const char* message, ...)
{
    va_list arguments;

    va_start(arguments, message);
    MessageBox box(app, caption, FXStringVFormat(message, arguments), biginfoicon, opts | DECOR_TITLE | DECOR_BORDER);
    va_end(arguments);
    return box.execute();
}


// Set message text
void MessageBox::setText(FXString text)
{
    // Set message text with a maximum of MAX_MESSAGE_LENGTH characters per line
    FXString formatted = xf_multilines(text, MAX_MESSAGE_LENGTH);
    if (scrollmsg)
    {
        scrollmsg->setText(formatted);
    }
    else
    {
        msg->setText(formatted);
    }
}
