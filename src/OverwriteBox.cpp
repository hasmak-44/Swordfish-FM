#include "config.h"
#include "i18n.h"

#include <fx.h>
#include <fxdefs.h>
#include <FXPNGIcon.h>

#include "icons.h"
#include "xfedefs.h"
#include "xfeutils.h"
#include "OverwriteBox.h"

// Padding for message box buttons
#define HORZ_PAD    30
#define VERT_PAD    2


// Map
FXDEFMAP(OverwriteBox) OverwriteBoxMap[] =
{
    FXMAPFUNCS(SEL_COMMAND, OverwriteBox::ID_CLICKED_CANCEL, OverwriteBox::ID_CLICKED_RENAME,
               OverwriteBox::onCmdClicked),
};



// Object implementation
FXIMPLEMENT(OverwriteBox, DialogBox, OverwriteBoxMap, ARRAYNUMBER(OverwriteBoxMap))


static void conflictDetails(FXComposite* parent, const FXString& title, const FXString& path,
                            const struct stat& info, const FXString& timeformat)
{
    FXGroupBox* group = new FXGroupBox(parent, title, GROUPBOX_TITLE_LEFT | FRAME_GROOVE | LAYOUT_FILL_X);
    FXMatrix* details = new FXMatrix(group, 2, MATRIX_BY_COLUMNS | LAYOUT_FILL_X);
    const FXuint style = JUSTIFY_LEFT | LAYOUT_FILL_X;
    new FXLabel(details, _("Name:"), NULL, style);
    new FXLabel(details, xf_multilines(FXPath::name(path), MAX_MESSAGE_LENGTH), NULL, style);
    new FXLabel(details, _("Location:"), NULL, style);
    FXTextField* location = new FXTextField(details, 60, NULL, 0,
                                          TEXTFIELD_READONLY | FRAME_SUNKEN | LAYOUT_FILL_X);
    location->setText(FXPath::directory(path));
    location->setTipText(path);
    new FXLabel(details, _("Type:"), NULL, style);
    new FXLabel(details, S_ISDIR(info.st_mode) ? _("Folder") :
                S_ISLNK(info.st_mode) ? _("Symbolic link") :
                S_ISREG(info.st_mode) ? _("File") : _("Special file"), NULL, style);
    new FXLabel(details, _("Size:"), NULL, style);
    new FXLabel(details, S_ISDIR(info.st_mode) ? _("Contents not counted") :
                xf_humansize(FXStringVal((FXulong)info.st_size).text()), NULL, style);
    new FXLabel(details, _("Modified:"), NULL, style);
    new FXLabel(details, FXSystem::time(timeformat.text(), info.st_mtime), NULL, style);
}


OverwriteBox::OverwriteBox(FXWindow* win, const FXString& source, const FXString& destination,
                           const struct stat& srcinfo, const struct stat& dstinfo,
                           const FXString& renamed, FXbool merge) :
    DialogBox(win, merge ? _("Folder already exists") : _("Name already exists"),
              DECOR_TITLE | DECOR_BORDER | DECOR_STRETCHABLE | DECOR_CLOSE)
{
    FXVerticalFrame* content = new FXVerticalFrame(this, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXString message = merge ?
        _("Merge these folders?\nExisting destination files will be kept. Conflicting files will be handled separately.") :
        S_ISDIR(dstinfo.st_mode) ?
        _("Replace the destination folder?\nReplacing permanently deletes that folder and everything in it.") :
        _("An item with this name already exists.\nReplace it with the source item?");
    new FXLabel(content, xf_multilines(message, MAX_MESSAGE_LENGTH), bigquestionicon,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_FILL_X);
    FXString timeformat = getApp()->reg().readStringEntry("SETTINGS", "time_format", DEFAULT_TIME_FORMAT);
    conflictDetails(content, _("Source (incoming)"), source, srcinfo, timeformat);
    conflictDetails(content, _("Destination (existing)"), destination, dstinfo, timeformat);
    new FXLabel(content, xf_multilines(_("Rename keeps both. New name: ") + FXPath::name(renamed),
                                     MAX_MESSAGE_LENGTH), NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);
    applyToAll = new FXCheckButton(content, _("Apply to all similar conflicts in this operation"));
    FXHorizontalFrame* buttons = new FXHorizontalFrame(content, LAYOUT_FILL_X | PACK_UNIFORM_WIDTH);
    new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                 BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_CENTER_X,
                 0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
    new FXButton(buttons, _("&Skip"), NULL, this, ID_CLICKED_SKIP,
                 FRAME_GROOVE | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
    new FXButton(buttons, _("Re&name"), NULL, this, ID_CLICKED_RENAME,
                 FRAME_GROOVE | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
    new FXButton(buttons, merge ? _("&Merge") : _("&Replace"), NULL, this, ID_CLICKED_OVERWRITE,
                 FRAME_GROOVE | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
}


// Create message box with text
OverwriteBox::OverwriteBox(FXWindow* win, const FXString& name, const FXString& text,
                           FXuint type, FXuint opts, int x, int y) :
    DialogBox(win, name, opts | DECOR_TITLE | DECOR_BORDER | DECOR_STRETCHABLE | DECOR_CLOSE, x, y, 0, 0)
{
    FXVerticalFrame* content = new FXVerticalFrame(this, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXHorizontalFrame* info = new FXHorizontalFrame(content, LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y,
                                                    0, 0, 0, 0, 10, 10, 10, 10);

    new FXLabel(info, FXString::null, bigquestionicon,
                ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    // Set message text with a maximum of MAX_MESSAGE_LENGTH characters per line
    FXString str = xf_multilines(text, MAX_MESSAGE_LENGTH);
    new FXLabel(info, str, NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    FXHorizontalFrame* buttons = new FXHorizontalFrame(content,
                                                       LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | PACK_UNIFORM_WIDTH, 0,
                                                       0, 0, 0, 10, 10, 10, 10);

    // Dialog with five options for multiple files
    if (type == OVWBOX_MULTIPLE_FILES)
    {
        new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                     BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0,
                     0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Skip"), NULL, this, ID_CLICKED_SKIP,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("Skip A&ll"), NULL, this, ID_CLICKED_SKIP_ALL,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Yes"), NULL, this, ID_CLICKED_OVERWRITE,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("Yes for &All"), NULL, this, ID_CLICKED_OVERWRITE_ALL,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
    }
    // Dialog with two options for single file
    else
    {
        new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                     BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0,
                     0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Yes"), NULL, this, ID_CLICKED_OVERWRITE,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
    }
}


// Create message box with text, source and target size, source and target modified time
OverwriteBox::OverwriteBox(FXWindow* win, const FXString& name, const FXString& text, FXString& srcsize,
                           FXString& srcmtime, FXString& tgtsize, FXString& tgtmtime, FXuint type, FXuint opts, int x,
                           int y) :
    DialogBox(win, name, opts | DECOR_TITLE | DECOR_BORDER | DECOR_STRETCHABLE | DECOR_CLOSE, x, y, 0, 0)
{
    FXVerticalFrame* content = new FXVerticalFrame(this, LAYOUT_FILL_X | LAYOUT_FILL_Y);
    FXVerticalFrame* vframe = new FXVerticalFrame(content, LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y, 0,
                                                  0, 0, 0, 0, 0, 0, 0);

    FXHorizontalFrame* info = new FXHorizontalFrame(vframe, LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y, 0,
                                                    0, 0, 0, 0, 0, 0, 0);

    new FXLabel(info, FXString::null, bigquestionicon,
                ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    // Set message text with a maximum of MAX_MESSAGE_LENGTH characters per line
    FXString str = xf_multilines(text, MAX_MESSAGE_LENGTH);
    new FXLabel(info, str, NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    FXMatrix* matrix = new FXMatrix(vframe, 5, MATRIX_BY_COLUMNS | LAYOUT_SIDE_TOP | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    new FXVerticalSeparator(matrix, SEPARATOR_NONE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_CENTER_Y, 0, 0,
                            0, 0, 0, 40);
    new FXLabel(matrix, _("Source size:"), NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new FXLabel(matrix, srcsize, NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new FXLabel(matrix, _("- Modified date:"), NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new FXLabel(matrix, srcmtime, NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    new FXVerticalSeparator(matrix, SEPARATOR_NONE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_CENTER_Y, 0, 0,
                            0, 0, 0, 40);
    new FXLabel(matrix, _("Target size:"), NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new FXLabel(matrix, tgtsize, NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new FXLabel(matrix, _("- Modified date:"), NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);
    new FXLabel(matrix, tgtmtime, NULL,
                JUSTIFY_LEFT | ICON_BEFORE_TEXT | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    FXHorizontalFrame* buttons = new FXHorizontalFrame(content,
                                                       LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_FILL_X | PACK_UNIFORM_WIDTH, 0,
                                                       0, 0, 0, 10, 10, 10, 10);

    // Dialog with five options for multiple files
    if (type == OVWBOX_MULTIPLE_FILES)
    {
        new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                     BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0,
                     0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Skip"), NULL, this, ID_CLICKED_SKIP,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("Skip A&ll"), NULL, this, ID_CLICKED_SKIP_ALL,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Yes"), NULL, this, ID_CLICKED_OVERWRITE,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("Yes for &All"), NULL, this, ID_CLICKED_OVERWRITE_ALL,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
    }
    // Dialog with two options for single file
    else
    {
        new FXButton(buttons, _("&Cancel"), NULL, this, ID_CLICKED_CANCEL,
                     BUTTON_INITIAL | BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0,
                     0, 0, HORZ_PAD, HORZ_PAD, VERT_PAD, VERT_PAD);
        new FXButton(buttons, _("&Yes"), NULL, this, ID_CLICKED_OVERWRITE,
                     BUTTON_DEFAULT | FRAME_GROOVE | LAYOUT_TOP | LAYOUT_LEFT | LAYOUT_CENTER_X, 0, 0, 0, 0, HORZ_PAD,
                     HORZ_PAD, VERT_PAD, VERT_PAD);
    }
}


// Close dialog
long OverwriteBox::onCmdClicked(FXObject*, FXSelector sel, void*)
{
    FXuint answer = OVWBOX_CLICKED_CANCEL + (FXSELID(sel) - ID_CLICKED_CANCEL);
    if (applyToAll && applyToAll->getCheck())
    {
        if (answer == OVWBOX_CLICKED_OVERWRITE) answer = OVWBOX_CLICKED_OVERWRITE_ALL;
        else if (answer == OVWBOX_CLICKED_SKIP) answer = OVWBOX_CLICKED_SKIP_ALL;
        else if (answer == OVWBOX_CLICKED_RENAME) answer = OVWBOX_CLICKED_RENAME_ALL;
    }
    getApp()->stopModal(this, answer);
    hide();
    return 1;
}
