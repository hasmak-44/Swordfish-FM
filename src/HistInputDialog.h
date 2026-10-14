#ifndef HISTINPUTDIALOG_H
#define HISTINPUTDIALOG_H

#include <vector>
#include <map>

#include "ComboBox.h"
#include "DialogBox.h"

struct DesktopApplication
{
    FXString id;
    FXString name;
    FXString command;
    FXString mimetypes;
    FXString categories;
    FXString icon;
};

std::vector<DesktopApplication> xf_installedApplications(FXbool includeUnlisted = false);
std::map<FXString, FXString> xf_defaultApplications(FXString& error);

// Browse types
enum
{
    HIST_INPUT_FILE,
    HIST_INPUT_EXECUTABLE_FILE,
    HIST_INPUT_FOLDER,
    HIST_INPUT_MIXED
};

class XComApp;

class HistComboBox : public ComboBox
{
    FXDECLARE(HistComboBox)
private:
    HistComboBox()
    {
    }

public:
    FXTextField* getTextEntry()
    {
        return field;
    }

    void CursorEnd()
    {
        field->onCmdCursorEnd(0, 0, 0);
        field->setFocus();
    }

    HistComboBox(FXComposite* p, int cols, FXbool addr = false, FXbool crlbtn = false, FXObject* tgt = NULL,
                 FXSelector sel = 0, FXuint opts = COMBOBOX_NORMAL);
    virtual void create();
};

class HistInputDialog : public DialogBox
{
    FXDECLARE(HistInputDialog)
protected:
    FXHorizontalFrame* buttons = NULL;
    FXHorizontalFrame* checkbutton = NULL;
    FXCheckButton* associationOption = NULL;
    FXLabel* messageLabel = NULL;
    FXButton* accept = NULL;
    FXButton* cancel = NULL;
    HistComboBox* input = NULL;
    FXuint browsetype = 0;
    FXString initialdir;
    std::vector<FXString> appnames;
    std::vector<FXString> appcommands;
    FXVerticalFrame* contents = NULL;
    FXMatrix* customCommand = NULL;
    FXTextField* appFilter = NULL;
    FXList* applicationList = NULL;
    FXButton* customToggle = NULL;
    FXLabel* emptyApplications = NULL;
    std::vector<DesktopApplication> applications;
    std::vector<FXString> rowCommands;
    std::vector<FXIcon*> applicationIcons;
    FXString contextPath, contextMime, contextExtension, defaultApplication, savedCommand;
    FXbool editingAssociation = false;
    FXbool customVisible = false;
    void rebuildApplications();
    FXIcon* applicationIcon(const FXString&);
private:
    HistInputDialog()
    {
    }
public:
    enum
    {
        ID_BROWSE_PATH=DialogBox::ID_LAST,
        ID_FILTER_APPLICATIONS,
        ID_APPLICATION_SELECTED,
        ID_CUSTOM_COMMAND,
        ID_LAST
    };
    HistInputDialog(FXWindow*, FXString, FXString, FXString, FXString label = "", FXIcon* ic = NULL,
                    FXbool addr = false, FXbool clrbtn = false, FXuint browse = HIST_INPUT_FILE,
                    FXbool option = false, FXString = FXString::null);
    virtual void create();
    virtual ~HistInputDialog();
    FXuint execute(FXuint placement = PLACEMENT_CURSOR);

    long onCmdKeyPress(FXObject*, FXSelector, void*);
    long onCmdBrowsePath(FXObject*, FXSelector, void*);
    long onUpdAccept(FXObject*, FXSelector, void*);
    long onFilterApplications(FXObject*, FXSelector, void*);
    long onApplicationSelected(FXObject*, FXSelector, void*);
    long onApplicationOpened(FXObject*, FXSelector, void*);
    long onCustomCommand(FXObject*, FXSelector, void*);
    long onAccept(FXObject*, FXSelector, void*);

    FXString getText()
    {
        FXString text = input->getText();
        for (size_t i = 0; i < appnames.size(); i++)
        {
            if (text == appnames[i])
            {
                return appcommands[i];
            }
        }
        return text;
    }

    void setText(const FXString& text)
    {
        input->setText(text);
    }

    void CursorEnd();
    void selectAll();

    void appendItem(char* str)
    {
        input->appendItem(str);
    }

    void clearItems()
    {
        input->clearItems();
        appnames.clear();
        appcommands.clear();
    }

    // History excludes installed applications, which are listed after it
    FXString getHistoryItem(int pos);
    int getHistorySize();

    // Add installed desktop applications after the history entries
    void appendInstalledApplications();
    void setOpenWithContext(const FXString& pathname, FXint count);
    void setAssociationOption(const FXString&, FXbool editing = false);
    void setMessage(const FXString& text)
    {
        messageLabel->setText(text);
    }

    void setDirectory(const FXString&);

    void sortItems()
    {
        input->setSortFunc(FXList::ascendingCase);
        input->sortItems();
    }
};
#endif
