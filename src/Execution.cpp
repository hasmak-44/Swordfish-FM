#include "config.h"
#include "i18n.h"
#include <errno.h>
#include <unistd.h>
#include <vector>
#include "xfedefs.h"
#include "Execution.h"
#include "AppLogger.h"
#include "ExecuteBox.h"
#include "FileDict.h"
#include "MessageBox.h"
#include "startupnotification.h"

static void executionError(FXWindow* owner, const FXString& path, const FXString& error)
{
    owner->getApp()->endWaitCursor();
    appLog(APP_LOG_ERRORS, "execution-failed", "file=\"" + path + "\" error=\"" + error + "\"");
    MessageBox::error(owner, BOX_OK, _("Unable to Execute"), "%s\n\n%s", path.text(), error.text());
}

class UnavailableApplicationDialog : public FXDialogBox
{
public:
    UnavailableApplicationDialog(FXWindow* owner, const FXString& message) :
        FXDialogBox(owner, _("Application Unavailable"), DECOR_TITLE | DECOR_BORDER | DECOR_CLOSE)
    {
        FXVerticalFrame* frame = new FXVerticalFrame(this, LAYOUT_FILL_X | LAYOUT_FILL_Y);
        new FXLabel(frame, message, NULL, JUSTIFY_LEFT);
        FXHorizontalFrame* buttons = new FXHorizontalFrame(frame, LAYOUT_FILL_X | PACK_UNIFORM_WIDTH);
        new FXButton(buttons, _("Use System Default Once"), NULL, this, 1, BUTTON_NORMAL | LAYOUT_FILL_X);
        new FXButton(buttons, _("Open With..."), NULL, this, 2, BUTTON_NORMAL | LAYOUT_FILL_X);
        new FXButton(buttons, _("Cancel"), NULL, this, 3, BUTTON_NORMAL | LAYOUT_FILL_X);
    }
    long handle(FXObject* sender, FXSelector selector, void* ptr)
    {
        if (FXSELTYPE(selector) == SEL_COMMAND && FXSELID(selector) >= 1 && FXSELID(selector) <= 3)
        {
            getApp()->stopModal(this, FXSELID(selector));
            hide();
            return 1;
        }
        return FXDialogBox::handle(sender, selector, ptr);
    }
};

FXbool xf_prepareAssociation(FXWindow* owner, const FXString& command, const FXString& path,
                             FXuint openWithId, FXObject* preferencesTarget, FXuint preferencesId)
{
    if (xf_commandprogram(command) == "swordfish-open")
    {
        owner->getApp()->endWaitCursor();
        FXuint choice = xf_missingassociation(owner, path);
        if (choice == 1)
        {
            owner->handle(owner, FXSEL(SEL_COMMAND, openWithId), NULL);
        }
        else if (choice == 2 && preferencesTarget)
        {
            preferencesTarget->handle(owner, FXSEL(SEL_COMMAND, preferencesId), NULL);
        }
        return choice == 0;
    }
    if (!xf_existcommand(command))
    {
        owner->getApp()->endWaitCursor();
        appLog(APP_LOG_ERRORS, "associated-program-not-found", "file=\"" + path + "\" command=\"" + command + "\"");
        FXString message;
        message.format(_("The saved application for this file is unavailable:\n%s\n\nThe saved choice is kept."),
                       command.text());
        UnavailableApplicationDialog dialog(owner, message);
        FXuint choice = dialog.execute(PLACEMENT_OWNER);
        if (choice == 1)
        {
            appLog(APP_LOG_FILE_OPERATIONS, "associated-program-system-once", path);
            if (xf_prepareAssociation(owner, "swordfish-open", path, openWithId, preferencesTarget, preferencesId))
            {
#ifdef STARTUP_NOTIFICATION
                runcmd("swordfish-open " + xf_quote(path), "swordfish-open", FXPath::directory(path),
                       FXSystem::getCurrentDirectory(), true, "");
#else
                runcmd("swordfish-open " + xf_quote(path), FXPath::directory(path), FXSystem::getCurrentDirectory());
#endif
            }
        }
        else if (choice == 2)
        {
            owner->handle(owner, FXSEL(SEL_COMMAND, openWithId), NULL);
        }
        return false;
    }
    return true;
}

static FXString runtimePath(const FXString& runtime, const FXString* searchOverride = NULL)
{
    if (runtime.contains('/'))
    {
        return access(runtime.text(), X_OK) == 0 && xf_isfile(runtime) ? runtime : FXString::null;
    }
    const char* env = getenv("PATH");
    FXString search = searchOverride ? *searchOverride : (env ? env : "/usr/local/bin:/usr/bin:/bin");
    for (int i = 0; i <= search.contains(':'); ++i)
    {
        FXString dir = search.section(':', i);
        FXString path = FXPath::absolute(dir.empty() ? "." : dir, runtime);
        if (access(path.text(), X_OK) == 0 && xf_isfile(path))
        {
            return path;
        }
    }
    return FXString::null;
}

// A PE file is managed (.NET) when its CLR runtime header directory is present.
static FXbool peIsManaged(const FXString& path)
{
    FILE* file = fopen(path.text(), "rb");
    if (!file)
    {
        return false;
    }
    unsigned char dos[64];
    unsigned char coff[24];
    unsigned char optional[240];
    FXbool managed = false;
    if (fread(dos, 1, sizeof(dos), file) == sizeof(dos))
    {
        long offset = (long)dos[60] | ((long)dos[61] << 8) | ((long)dos[62] << 16) | ((long)dos[63] << 24);
        if (fseek(file, offset, SEEK_SET) == 0 && fread(coff, 1, sizeof(coff), file) == sizeof(coff) &&
            memcmp(coff, "PE\000\000", 4) == 0)
        {
            size_t optionalSize = (size_t)coff[20] | ((size_t)coff[21] << 8);
            if (optionalSize > sizeof(optional))
            {
                optionalSize = sizeof(optional);
            }
            if (fread(optional, 1, optionalSize, file) == optionalSize && optionalSize >= 2)
            {
                unsigned int magic = optional[0] | (optional[1] << 8);
                size_t countOffset = magic == 0x20b ? 108 : 92;
                size_t clrOffset = countOffset + 4 + 14 * 8;
                if ((magic == 0x10b || magic == 0x20b) && optionalSize >= clrOffset + 8)
                {
                    unsigned long count = (unsigned long)optional[countOffset] | ((unsigned long)optional[countOffset + 1] << 8) |
                                          ((unsigned long)optional[countOffset + 2] << 16) |
                                          ((unsigned long)optional[countOffset + 3] << 24);
                    unsigned long address = 0, size = 0;
                    for (int i = 3; i >= 0; --i)
                    {
                        address = (address << 8) | optional[clrOffset + i];
                        size = (size << 8) | optional[clrOffset + 4 + i];
                    }
                    managed = count > 14 && address != 0 && size != 0;
                }
            }
        }
    }
    fclose(file);
    return managed;
}

static FXbool splitArguments(const FXString& text, std::vector<FXString>& words)
{
    FXString word;
    char quote = 0;
    FXbool escape = false, started = false;
    for (int i = 0; i < text.length(); ++i)
    {
        char c = text[i];
        if (escape)
        {
            word += c;
            escape = false;
        }
        else if (c == '\\' && quote != '\'')
        {
            escape = true;
            started = true;
        }
        else if (quote)
        {
            if (c == quote)
            {
                quote = 0;
            }
            else
            {
                word += c;
            }
        }
        else if (c == '\'' || c == '"')
        {
            quote = c;
            started = true;
        }
        else if (c == ' ' || c == '\t')
        {
            if (started)
            {
                words.push_back(word);
                word.clear();
                started = false;
            }
        }
        else
        {
            word += c;
            started = true;
        }
    }
    if (started)
    {
        words.push_back(word);
    }
    return !quote && !escape && !words.empty();
}

static FXbool executionCommand(FXWindow* owner, const FXString& path,
                                FXString& command, FXString& program)
{
    if (access(path.text(), R_OK) != 0)
    {
        executionError(owner, path, _("Permission denied or file unavailable: the file cannot be read."));
        return false;
    }
    ExecutionKind kind = xf_executionKind(path);
    FXString ext = FXPath::extension(path).lower();
    std::vector<FXString> args;
    std::vector<FXString> environment;
    FXString interpreterSearch;
    FXbool customSearch = false;
    if (kind == EXECUTION_SCRIPT)
    {
        FILE* file = fopen(path.text(), "rb");
        char line[4096] = {0};
        if (file)
        {
            if (!fgets(line, sizeof(line), file))
            {
                line[0] = '\0';
            }
            fclose(file);
        }
        if (line[0] == '#' && line[1] == '!')
        {
            FXString shebang(line + 2);
            shebang = shebang.before('\n').before('\r').trim();
            if (!splitArguments(shebang, args))
            {
                executionError(owner, path, _("Invalid script interpreter line."));
                return false;
            }
            // Resolve env's interpreter ourselves, including env -S, so pkexec
            // runs the same interpreter rather than an elevated environment shell.
            if (FXPath::name(args[0]) == "env")
            {
                FXString envProgram = args[0];
                args.erase(args.begin());
                if (!args.empty() && (args[0] == "-S" || args[0] == "--split-string"))
                {
                    args.erase(args.begin());
                }
                while (!args.empty())
                {
                    FXString option = args[0];
                    if (option == "-i" || option == "--ignore-environment" ||
                        option.left(8) == "--unset=" || (option[0] != '-' && option.contains('=')))
                    {
                        if (option == "-i" || option == "--ignore-environment" || option == "--unset=PATH")
                        {
                            interpreterSearch = "/bin:/usr/bin";
                            customSearch = true;
                        }
                        else if (option.left(5) == "PATH=")
                        {
                            interpreterSearch = option.mid(5, option.length() - 5);
                            customSearch = true;
                        }
                        environment.push_back(option);
                        args.erase(args.begin());
                    }
                    else if (option == "-u" || option == "--unset")
                    {
                        if (args.size() < 2)
                        {
                            executionError(owner, path, _("Missing variable name in the script interpreter options."));
                            return false;
                        }
                        environment.push_back(option);
                        environment.push_back(args[1]);
                        if (args[1] == "PATH")
                        {
                            interpreterSearch = "/bin:/usr/bin";
                            customSearch = true;
                        }
                        args.erase(args.begin(), args.begin() + 2);
                    }
                    else
                    {
                        break;
                    }
                }
                if (args.empty() || args[0][0] == '-' || args[0].contains('='))
                {
                    executionError(owner, path, _("Unsupported env interpreter options in the script header."));
                    return false;
                }
                if (!environment.empty())
                {
                    FXString envPath = runtimePath(envProgram);
                    if (envPath.empty())
                    {
                        executionError(owner, path, FXString(_("Required interpreter or runtime is missing or not executable: ")) + envProgram);
                        return false;
                    }
                    environment.insert(environment.begin(), envPath);
                }
            }
        }
        else
        {
            FXString interpreter;
            if (ext == "py" || ext == "pyw") interpreter = "python3";
            else if (ext == "pl") interpreter = "perl";
            else if (ext == "rb") interpreter = "ruby";
            else if (ext == "lua") interpreter = "lua";
            else if (ext == "php") interpreter = "php";
            else if (ext == "js") interpreter = "node";
            else if (ext == "tcl") interpreter = "tclsh";
            else if (ext == "sh") interpreter = "sh";
            else interpreter = ext;
            args.push_back(interpreter);
        }
    }
    else if (kind == EXECUTION_PROGRAM)
    {
        FILE* file = fopen(path.text(), "rb");
        unsigned char magic[2] = {0};
        if (file)
        {
            if (fread(magic, 1, 2, file) != 2)
            {
                magic[0] = magic[1] = 0;
            }
            fclose(file);
        }
        if (ext == "jar" && magic[0] == 'P' && magic[1] == 'K')
        {
            args.push_back("java");
            args.push_back("-jar");
        }
        else if (ext == "msi" && magic[0] == 0xD0 && magic[1] == 0xCF)
        {
            if (runtimePath("wine").empty())
            {
                executionError(owner, path, _("Required runtime is missing: Wine is needed to install Windows Installer (MSI) packages."));
                return false;
            }
            args.push_back("wine");
            args.push_back("msiexec");
            args.push_back("/i");
        }
        else if (magic[0] == 'M' && magic[1] == 'Z')
        {
            FXbool managed = peIsManaged(path);
            if (!runtimePath("wine").empty())
            {
                args.push_back("wine");
            }
            else if (managed && !runtimePath("mono").empty())
            {
                args.push_back("mono");
            }
            else
            {
                executionError(owner, path, managed ?
                               _("Required runtime is missing: Wine or Mono is needed to run this .NET Windows program.") :
                               _("Required runtime is missing: Wine is needed to run this Windows program."));
                return false;
            }
        }
        else
        {
            if (access(path.text(), X_OK) != 0)
            {
                executionError(owner, path, _("Permission denied: this program is not executable. Change its permissions explicitly in Properties."));
                return false;
            }
            program = path;
            command = xf_quote(path);
            return true;
        }
    }
    else
    {
        executionError(owner, path, _("This file is not a recognized program or script. Use Open With to select an application."));
        return false;
    }
    program = runtimePath(args[0], customSearch ? &interpreterSearch : NULL);
    if (program.empty())
    {
        executionError(owner, path, FXString(_("Required interpreter or runtime is missing or not executable: ")) + args[0]);
        return false;
    }
    command = xf_quote(program);
    for (size_t i = 1; i < args.size(); ++i)
    {
        command += " " + xf_quote(args[i]);
    }
    command += " " + xf_quote(path);
    if (!environment.empty())
    {
        FXString prefix;
        for (size_t i = 0; i < environment.size(); ++i)
        {
            prefix += xf_quote(environment[i]) + " ";
        }
        command.prepend(prefix);
        program = environment[0];
    }
    return true;
}

FXbool xf_editFiles(FXWindow* owner, const vector_FXString& paths, const FXString& directory,
                    const FXString& startDirectory, FXuint openWithId,
                    FXObject* preferencesTarget, FXuint preferencesId)
{
    FXApp* app = owner->getApp();
    std::vector<FXString> commands;
    for (size_t i = 0; i < paths.size(); i++)
    {
        const FXString& path = paths[i];
        FXString extension = FileDict::associationExtension(path);
        FXString editor = extension.empty() ? FXString::null :
            FXString(app->reg().readStringEntry("EDIT_ASSOCIATIONS", extension.text(), ""));
        FXString mime;
        if (editor.empty() && !xf_filemimetype(owner, path, mime))
        {
            return false;
        }
        if (editor.empty() && mime == "application/pdf")
        {
            editor = app->reg().readStringEntry("PROGS", "pdfeditor", "");
        }
        else if (editor.empty() && mime.left(6) == "image/")
        {
            editor = app->reg().readStringEntry("PROGS", "imgeditor", DEFAULT_IMGEDITOR);
        }
        else if (editor.empty() && (mime.left(5) == "text/" || xf_istextfile(path) == 1))
        {
            editor = app->reg().readStringEntry("PROGS", "txteditor", DEFAULT_TXTEDITOR);
        }
        if (editor.empty())
        {
            appLog(APP_LOG_FILE_OPERATIONS, "edit-open-with", path);
            return false;
        }
        if (!xf_existcommand(editor))
        {
            executionError(owner, path, FXString(_("The preferred editor is unavailable: ")) + editor);
            return false;
        }
        if (xf_commandprogram(editor) == "swordfish-open" &&
            !xf_prepareAssociation(owner, editor, path, openWithId, preferencesTarget, preferencesId))
        {
            return true;
        }
        commands.push_back(editor);
    }
    for (size_t i = 0; i < paths.size(); i++)
    {
        appLog(APP_LOG_FILE_OPERATIONS, "file-edit", paths[i]);
        FXString launch = commands[i];
        FXString program = FXPath::name(xf_commandprogram(launch));
        if (program != "swordfish-open" && program != "swfw" && program != "xfw")
        {
            launch = xf_hostcommand(launch);
        }
#ifdef STARTUP_NOTIFICATION
        runcmd(launch + " " + xf_quote(paths[i]), commands[i], directory, startDirectory, false, "");
#else
        runcmd(launch + " " + xf_quote(paths[i]), directory, startDirectory);
#endif
    }
    return true;
}

static FXbool editScript(FXWindow* owner, const FXString& path, const FXString& directory,
                         const FXString& startDirectory)
{
    FXApp* app = owner->getApp();
    FileDict dict(app);
    FileAssoc* assoc = dict.findFileBinding(path.text());
    FXString extension = FileDict::associationExtension(path);
    FXString editor = extension.empty() ? FXString::null :
        FXString(app->reg().readStringEntry("EDIT_ASSOCIATIONS", extension.text(), ""));
    if (editor.empty())
    {
        editor = assoc ? assoc->command.section(',', 2) : FXString::null;
    }
    if (editor.empty() || editor == "<txteditor>" || xf_commandprogram(editor) == "swordfish-open")
    {
        editor = app->reg().readStringEntry("PROGS", "txteditor", DEFAULT_TXTEDITOR);
    }
    else if (editor == "<imgeditor>")
    {
        editor = app->reg().readStringEntry("PROGS", "imgeditor", DEFAULT_IMGEDITOR);
    }
    else if (editor == "<archiver>")
    {
        editor = app->reg().readStringEntry("PROGS", "archiver", DEFAULT_ARCHIVER);
    }
    if (!xf_existcommand(editor))
    {
        executionError(owner, path, FXString(_("The preferred editor is unavailable: ")) + editor);
        return false;
    }
    appLog(APP_LOG_FILE_OPERATIONS, "script-edit", path);
#ifdef STARTUP_NOTIFICATION
    runcmd(editor + " " + xf_quote(path), editor, directory, startDirectory, false, "");
#else
    runcmd(editor + " " + xf_quote(path), directory, startDirectory);
#endif
    return true;
}

FXbool xf_executeFile(FXWindow* owner, const FXString& path, const FXString& directory,
                      const FXString& startDirectory, ExecutionOrigin origin, FXbool terminal)
{
    FXApp* app = owner->getApp();
    if (xf_executionKind(path) == EXECUTION_SCRIPT)
    {
        FXuint action = app->reg().readUnsignedEntry("OPTIONS", "script_action", 1);
        if (action == 2 && origin == EXECUTION_OPEN)
        {
            return editScript(owner, path, directory, startDirectory);
        }
        if (action != 0)
        {
            app->endWaitCursor();
            FXString message;
            message.format(_("File %s is a script. What do you want to do?"), path.text());
            ExecuteBox dialog(owner, _("Confirm Execute"), message);
            FXuint answer = dialog.execute(PLACEMENT_CURSOR);
            if (answer == EXECBOX_CLICKED_CANCEL)
            {
                return true;
            }
            if (answer == EXECBOX_CLICKED_EDIT)
            {
                return editScript(owner, path, directory, startDirectory);
            }
            terminal = terminal || answer == EXECBOX_CLICKED_CONSOLE;
        }
    }
    FXString command, program;
    if (!executionCommand(owner, path, command, program))
    {
        return true;
    }
    if (origin == EXECUTION_ADMINISTRATOR)
    {
        FXString pkexec = runtimePath("pkexec");
        if (pkexec.empty())
        {
            executionError(owner, path, _("Administrator execution is unavailable: pkexec was not found."));
            return true;
        }
        FXString displayEnvironment;
        const char* display = getenv("DISPLAY");
        const char* authority = getenv("XAUTHORITY");
        if ((display && *display) || (authority && *authority))
        {
            FXString env = runtimePath("env");
            if (env.empty())
            {
                executionError(owner, path, _("Administrator GUI execution is unavailable: env was not found."));
                return true;
            }
            displayEnvironment = xf_quote(env) + " ";
            if (display && *display)
            {
                displayEnvironment += xf_quote(FXString("DISPLAY=") + display) + " ";
            }
            if (authority && *authority)
            {
                displayEnvironment += xf_quote(FXString("XAUTHORITY=") + authority) + " ";
            }
        }
        command = xf_quote(pkexec) + " --disable-internal-agent " + displayEnvironment + command;
        program = pkexec;
    }
    if (terminal)
    {
        FXString terminalProgram = xf_terminalprogram(app);
        if (terminalProgram.empty() || !xf_existcommand(terminalProgram))
        {
            executionError(owner, path, _("Terminal unavailable. Please select a terminal in Preferences."));
            return true;
        }
        FXString script = command + "; echo; printf '%s' " +
                          xf_quote(FXString(_("Press Enter to close...")) + " ") + "; read _";
        command = xf_terminalcommand(terminalProgram, directory, script);
        program = terminalProgram;
    }
    appLog(APP_LOG_FILE_OPERATIONS, origin == EXECUTION_ADMINISTRATOR ?
           "administrator-execute" : "file-execute", "file=\"" + path + "\" command=\"" + command + "\" cwd=\"" + directory + "\"");
#ifdef STARTUP_NOTIFICATION
    int result = runcmd(command, program, directory, startDirectory, false, "");
#else
    int result = runcmd(command, directory, startDirectory);
#endif
    if (result < 0)
    {
        executionError(owner, path, _("The application could not be launched. Check its permissions and runtime."));
    }
    return true;
}
