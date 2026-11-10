#ifndef CUSTOMACTIONS_H
#define CUSTOMACTIONS_H

#include <fx.h>
#include <vector>

struct CustomAction
{
    enum Kind { ACTION, FOLDER, SEPARATOR };
    FXString id, parent, name, description, icon, command;
    FXString program, parameters, workingDirectory;
    FXString patterns = "*";
    int kind = ACTION;
    bool toolbar = false, files = true, folders = true;
    bool single = true, multiple = true, background = false;
    bool terminal = false, confirm = false, showOutput = false;
    bool programMode = false;
    bool operator==(const CustomAction&) const;
};

struct CustomActionContext
{
    FXString directory, opposite;
    std::vector<FXString> paths;
    std::vector<bool> directories;
    bool local = true;
};

class CustomActions
{
public:
    std::vector<CustomAction> items;
    static const unsigned MAX_ITEMS = 256;
    const CustomAction* find(const FXString&) const;
    FXString newId() const;
    FXString duplicate(const FXString& id, const FXString& copySuffix, FXString& error);
    bool validate(FXString&) const;
    bool load(FXSettings&, FXString&);
    void save(FXSettings&) const;
    bool move(const FXString& id, const FXString& parent, FXString& error);
    void remove(const FXString&);
    void reorder(const FXString&, int direction);
    std::vector<unsigned> children(const FXString& parent) const;
    std::vector<unsigned> toolbarItems() const;
    bool matches(const CustomAction&, const CustomActionContext&) const;
    bool visible(unsigned, const CustomActionContext&) const;
    static bool expand(const FXString&, const CustomActionContext&, FXString&, FXString&, bool currentFolder = false);
    static bool prepare(const CustomAction&, const CustomActionContext&, FXString& command,
                        FXString& directory, FXString& error, bool verify = true);
    static bool check(const CustomAction&, const CustomActionContext&, FXString& error);
    // Program-mode actions are shown as one equivalent shell command
    static FXString commandText(const CustomAction&);
};

#endif
