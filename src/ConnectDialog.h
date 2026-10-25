
#ifndef CONNECTDIALOG_H
#define CONNECTDIALOG_H

#include "IconList.h"
#include "DialogBox.h"
#include "ComboBox.h"
#include "DirPanel.h"
#include "FilePanel.h"


class ConnectDialog : public DialogBox
{
    FXDECLARE(ConnectDialog)

protected:

    FilePanel* filepanel = NULL;
    DirPanel* dirpanel = NULL;

    HistComboBox* server = NULL;
    FXSpinner* port = NULL;
    HistComboBox* share = NULL;
    FXButton* acceptbtn = NULL;
    ComboBox* servertype = NULL;
    FXLabel* sharelabel = NULL;
    FXLabel* hint = NULL;

    FXString scheme();
    void updateHint();

private:
    ConnectDialog()
    {
    }

public:
    enum
    {
        ID_ACCEPT=DialogBox::ID_LAST,
        ID_SERVER_TYPE,
        ID_PORT,
        ID_SERVER_NAME,
        ID_SHARE_NAME,
        ID_LAST
    };
    ConnectDialog(FXWindow*, FXString, FilePanel*, DirPanel*);
    virtual void create();

    virtual ~ConnectDialog();
    void setServerName(const FXString& name)
    {
        server->setText(name);
    }
    long onCmdAccept(FXObject*, FXSelector, void*);
    long onCmdServerType(FXObject*, FXSelector, void*);
    long onUpdAccept(FXObject*, FXSelector, void*);
};
#endif
