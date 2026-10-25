#include "config.h"
#include "i18n.h"

#include <fx.h>
#include <fxkeys.h>

#include "xfedefs.h"
#include "xfeutils.h"
#include "icons.h"
#include "MessageBox.h"
#include "ConnectDialog.h"


// Server history
extern char ServerHistory[SERVER_HIST_SIZE][MAX_SERVER_SIZE];
extern int ServerNum;

// Share history
extern char ShareHistory[SHARE_HIST_SIZE][MAX_SHARE_SIZE];
extern int ShareNum;


// Server types: label and address scheme
static const char* const serverTypes[][2] =
{
    { N_("Windows share (SMB)"), "smb" },
    { N_("SSH (SFTP)"), "sftp" },
    { N_("FTP"), "ftp" },
    { N_("FTP over TLS (FTPS)"), "ftps" },
    { N_("WebDAV"), "dav" },
    { N_("Secure WebDAV (HTTPS)"), "davs" },
};


FXDEFMAP(ConnectDialog) ConnectDialogMap[] =
{
    FXMAPFUNC(SEL_COMMAND, ConnectDialog::ID_ACCEPT, ConnectDialog::onCmdAccept),
    FXMAPFUNC(SEL_COMMAND, ConnectDialog::ID_SERVER_TYPE, ConnectDialog::onCmdServerType),
    FXMAPFUNC(SEL_UPDATE, ConnectDialog::ID_ACCEPT, ConnectDialog::onUpdAccept),
};


// Object implementation
FXIMPLEMENT(ConnectDialog, DialogBox, ConnectDialogMap, ARRAYNUMBER(ConnectDialogMap))

// Construct
ConnectDialog::ConnectDialog(FXWindow* win, FXString title, FilePanel* fpanel, DirPanel* dpanel) :
    DialogBox(win, title, DECOR_TITLE | DECOR_CLOSE | DECOR_BORDER | DECOR_STRETCHABLE)
{
    // File and dir panels
    filepanel = fpanel;
    dirpanel = dpanel;

    FXVerticalFrame* contents = new FXVerticalFrame(this, LAYOUT_SIDE_TOP | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    FXGroupBox* group = new FXGroupBox(contents, _("Server Details"),
                                        GROUPBOX_TITLE_LEFT | FRAME_GROOVE | LAYOUT_SIDE_TOP | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    FXVerticalFrame* vframe = new FXVerticalFrame(group, LAYOUT_SIDE_TOP | LAYOUT_FILL_X | LAYOUT_FILL_Y);

    FXMatrix* matrix = new FXMatrix(vframe, 2, MATRIX_BY_COLUMNS | LAYOUT_SIDE_TOP | LAYOUT_FILL_COLUMN | LAYOUT_FILL_X);

    new FXLabel(matrix, _("Type:"), NULL, JUSTIFY_LEFT);
    servertype = new ComboBox(matrix, 30, false, false, this, ID_SERVER_TYPE,
                              COMBOBOX_STATIC | LAYOUT_FILL_COLUMN | LAYOUT_FILL_X);
    servertype->setNumVisible(ARRAYNUMBER(serverTypes));
    for (FXuint i = 0; i < ARRAYNUMBER(serverTypes); i++)
    {
        servertype->appendItem(_(serverTypes[i][0]));
    }
    servertype->setCurrentItem(0);

    new FXLabel(matrix, _("Server:") + FXString("    "), NULL, JUSTIFY_LEFT);
    server = new HistComboBox(matrix, 30, false, true, this, ID_SERVER_NAME,
                                COMBOBOX_INSERT_LAST | LAYOUT_CENTER_Y | LAYOUT_CENTER_X | LAYOUT_FILL_COLUMN |
                                LAYOUT_FILL_ROW | LAYOUT_FILL_X);
    server->setNumVisible(5);
    for (int i = 0; i < ServerNum; i++)
    {
        server->appendItem(ServerHistory[i]);
    }

    new FXLabel(matrix, _("Port:"), NULL, JUSTIFY_LEFT);
    FXHorizontalFrame* portframe = new FXHorizontalFrame(matrix, LAYOUT_FILL_COLUMN | LAYOUT_FILL_X, 0, 0, 0, 0, 0, 0, 0, 0);
    port = new FXSpinner(portframe, 5, this, ID_PORT, JUSTIFY_RIGHT | FRAME_SUNKEN | FRAME_THICK, 0, 0, 0, 0, 2, 2, 1, 1);
    port->setRange(0, 65535);
    port->setValue(0);
    new FXLabel(portframe, _("(0 = standard port)"), NULL, JUSTIFY_LEFT | LAYOUT_CENTER_Y);

    sharelabel = new FXLabel(matrix, _("Share:"), NULL, JUSTIFY_LEFT);
    share = new HistComboBox(matrix, 30, false, true, this, ID_SHARE_NAME,
                                COMBOBOX_INSERT_LAST | LAYOUT_CENTER_Y | LAYOUT_CENTER_X | LAYOUT_FILL_COLUMN |
                                LAYOUT_FILL_ROW | LAYOUT_FILL_X);
    share->setNumVisible(5);
    for (int i = 0; i < ShareNum; i++)
    {
        share->appendItem(ShareHistory[i]);
    }

    hint = new FXLabel(vframe, FXString::null, NULL, JUSTIFY_LEFT | LAYOUT_FILL_X);

    // Buttons
    FXHorizontalFrame* buttons = new FXHorizontalFrame(contents, LAYOUT_SIDE_BOTTOM | LAYOUT_FILL_X, 0, 0, 0, 0, 10, 10,
                                                       5, 5);

    // Connect button
    acceptbtn = new FXButton(buttons, _("C&onnect"), NULL, this, ConnectDialog::ID_ACCEPT,
                             FRAME_GROOVE | LAYOUT_RIGHT | LAYOUT_CENTER_Y, 0, 0, 0, 0, 20, 20);
    acceptbtn->addHotKey(KEY_Return);

    // Cancel button
    new FXButton(buttons, _("&Cancel"), NULL, this, DialogBox::ID_CANCEL,
                 FRAME_GROOVE | LAYOUT_RIGHT | LAYOUT_CENTER_Y, 0, 0, 0, 0, 20, 20);

    updateHint();
}


// Create window
void ConnectDialog::create()
{
    // Create dialog box
    DialogBox::create();
    server->CursorEnd();
}


// Delete objects
ConnectDialog::~ConnectDialog()
{
    delete acceptbtn;
    delete server;
    delete servertype;
    delete port;
    delete sharelabel;
    delete share;
    delete hint;
}


// Address scheme of the selected server type
FXString ConnectDialog::scheme()
{
    int index = servertype->getCurrentItem();
    if (index < 0 || index >= (int)ARRAYNUMBER(serverTypes))
    {
        index = 0;
    }
    return serverTypes[index][1];
}


// Share label and help text for the selected server type
void ConnectDialog::updateHint()
{
    if (scheme() == "smb")
    {
        sharelabel->setText(_("Share (optional):"));
        hint->setText(_("Leave the share empty to see all shared folders of the server.\n\
You will be asked for a user name and password if the server needs them."));
    }
    else
    {
        sharelabel->setText(_("Folder (optional):"));
        hint->setText(_("Leave the folder empty to open the top folder of the server.\n\
You will be asked for a user name and password if the server needs them."));
    }
}


// Percent-encode a share or folder name for a server address
static FXString connectEncode(const FXString& str)
{
    const char* hex = "0123456789ABCDEF";
    FXString out;
    for (int i = 0; i < str.length(); i++)
    {
        FXuchar c = (FXuchar)str[i];
        if (Ascii::isAlphaNumeric(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/')
        {
            out += (FXchar)c;
        }
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}


// Connect the same way as an address typed in the address bar: the server asks for the login
long ConnectDialog::onCmdAccept(FXObject* sender, FXSelector sel, void* ptr)
{
    DialogBox::onCmdAccept(sender, sel, ptr);

    FXString host = server->getText().trim();
    // Accept a pasted address such as smb://server or \\server
    while (host.left(1) == "\\" || host.left(1) == "/")
    {
        host.erase(0, 1);
    }
    if (host.find("://") >= 0)
    {
        host = host.after('/', 2);
    }
    host = host.before('/').before('\\');
    if (port->getValue() != 0 && host.find(':') < 0)
    {
        host += ":" + FXStringVal(port->getValue());
    }

    FXString target = share->getText().trim();
    target.substitute('\\', '/');
    while (target.left(1) == "/")
    {
        target.erase(0, 1);
    }

    FXString address = scheme() + "://" + host + "/" + connectEncode(target);

    FilePanel* panel = filepanel->getCurrent();
    FXString localpath;
    if (panel->resolveNetworkPath(address, localpath) == 1)
    {
        panel->navigatePane(localpath);
    }
    return 1;
}


// Update dialog items according to server type
long ConnectDialog::onCmdServerType(FXObject*, FXSelector, void*)
{
    updateHint();
    return 1;
}


// Update accept button and the histories
long ConnectDialog::onUpdAccept(FXObject*, FXSelector, void*)
{
    if (server->getText().trim().empty())
    {
        acceptbtn->disable();
    }
    else
    {
        acceptbtn->enable();
    }

    // Sort server history
    server->setSortFunc(FXList::ascendingCase);
    server->sortItems();

    // Update server history
    ServerNum = 0;
    for (int i = 0; i < server->getNumItems() && ServerNum < SERVER_HIST_SIZE; i++)
    {
        FXString str = server->getItemText(i);
        xf_strlcpy(ServerHistory[i], str.text(), str.length() + 1);
        ServerNum++;
    }

    // Sort share history
    share->setSortFunc(FXList::ascendingCase);
    share->sortItems();

    // Update share history
    ShareNum = 0;
    for (int i = 0; i < share->getNumItems() && ShareNum < SHARE_HIST_SIZE; i++)
    {
        FXString str = share->getItemText(i);
        xf_strlcpy(ShareHistory[i], str.text(), str.length() + 1);
        ShareNum++;
    }

    return 1;
}
