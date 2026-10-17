#include <gio/gio.h>
#include <cassert>
#include <cstdio>

enum Scenario { READY, NOT_MOUNTED, MOUNT_FAILED, ALREADY_MOUNTED, UNAVAILABLE, RETRY_FAILED };
static Scenario scenario;
static int enumerations, mounts;
static const char* fixtures;

static GFileEnumerator* enumerate(GFile*, const char* attributes, GFileQueryInfoFlags flags,
                                  GCancellable* cancellable, GError** error)
{
    ++enumerations;
    if (scenario == UNAVAILABLE || (scenario == RETRY_FAILED && enumerations == 2))
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Operation not supported");
        return NULL;
    }
    if (scenario != READY && enumerations == 1)
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED, "The specified location is not mounted");
        return NULL;
    }
    GFile* local = g_file_new_for_path(fixtures);
    GFileEnumerator* result = g_file_enumerate_children(local, attributes, flags, cancellable, error);
    g_object_unref(local);
    return result;
}

static void mountRoot(GFile* root, GMountMountFlags, GMountOperation* operation,
                      GCancellable* cancellable, GAsyncReadyCallback callback, gpointer data)
{
    ++mounts;
    gchar* uri = g_file_get_uri(root);
    assert(g_strcmp0(uri, "smb:///") == 0 || g_strcmp0(uri, "smb://") == 0);
    g_free(uri);
    g_signal_emit_by_name(operation, "ask-password", "Password", "", "",
                         G_ASK_PASSWORD_ANONYMOUS_SUPPORTED);
    assert(g_mount_operation_get_anonymous(operation));
    GTask* task = g_task_new(root, cancellable, callback, data);
    if (scenario == MOUNT_FAILED)
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Anonymous browsing denied");
    else if (scenario == ALREADY_MOUNTED)
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED, "Already mounted");
    else
        g_task_return_boolean(task, true);
    g_object_unref(task);
}

static gboolean finishMount(GFile*, GAsyncResult* result, GError** error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}

#define g_file_enumerate_children enumerate
#define g_file_mount_enclosing_volume mountRoot
#define g_file_mount_enclosing_volume_finish finishMount
#define main networkDiscoveryMain
#include "../src/NetworkDiscovery.cpp"
#undef main

int main(int argc, char** argv)
{
    assert(argc == 2);
    fixtures = argv[1];
    struct Test { Scenario scenario; int result, enumerations, mounts; };
    const Test tests[] = {
        {READY, 0, 1, 0},
        {NOT_MOUNTED, 0, 2, 1},
        {MOUNT_FAILED, 1, 1, 1},
        {ALREADY_MOUNTED, 0, 2, 1},
        {UNAVAILABLE, 1, 1, 0},
        {RETRY_FAILED, 1, 2, 1}
    };
    for (const Test& test : tests)
    {
        scenario = test.scenario;
        enumerations = mounts = 0;
        assert(networkDiscoveryMain(1, NULL) == test.result);
        assert(enumerations == test.enumerations && mounts == test.mounts);
    }
    puts("PASS: mounted root, exact not-mounted recovery, anonymous-only browse mount, mount denial, mount race, unavailable backend, retry failure");
}
