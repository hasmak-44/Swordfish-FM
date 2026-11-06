#ifndef COMMANDWINDOW_H
#define COMMANDWINDOW_H

#include "DialogBox.h"


class CommandWindow : public DialogBox
{
    FXDECLARE(CommandWindow)
protected:

    int pid = 0;                                        // Proccess ID of child (valid if busy).
    int pipes[2] = { 0, 0 };                            // Pipes to communicate with child process.
    FXText* text = NULL;

    FXString command;                                   // Command string
    FXbool killed = false;                              // True if the cancel button was pressed
    FXbool closed = false;                              // True if the closed button was pressed
    FXbool quietSuccess = false;
    FXbool childFinished = false;
    int childStatus = 0;

private:
    CommandWindow()
    {
    }
    CommandWindow(const CommandWindow&);
public:
    enum
    {
        ID_CLOSE=DialogBox::ID_LAST,
        ID_WATCHPROCESS,
        ID_KILLPROCESS,
        ID_LAST
    };
public:
    long onCmdKillProcess(FXObject*, FXSelector, void*);
    long onUpdKillProcess(FXObject*, FXSelector, void*);
    long onWatchProcess(FXObject*, FXSelector, void*);
    long onUpdClose(FXObject*, FXSelector, void*);
    long onCmdKeyPress(FXObject*, FXSelector, void*);
    virtual void create();

    virtual ~CommandWindow();
    int execCmd(FXString);
    static FXbool harvest(int child, int status);
    CommandWindow(FXWindow* owner, const FXString& name, FXString strcmd, int nblines, int nbcols,
                  FXbool quiet = false);
    CommandWindow(FXApp* a, const FXString& name, FXString strcmd, int nblines, int nbcols);
    long onCmdClose(FXObject*, FXSelector, void*);
    void setText(const char*);
    void appendText(const char* str);
    void scrollToLastLine(void);
    int getLength(void);
};

#endif
