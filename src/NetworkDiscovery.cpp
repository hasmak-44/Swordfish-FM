#include <gio/gio.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace
{
struct MountResult
{
    GMainLoop* loop;
    GError* error;
};

struct DirectoryLoad
{
    GMainLoop* loop;
    GFileEnumerator* enumerator;
    GError* error;
};

void nextEntries(GObject* source, GAsyncResult* result, gpointer data)
{
    DirectoryLoad* load = static_cast<DirectoryLoad*>(data);
    GList* entries = g_file_enumerator_next_files_finish(G_FILE_ENUMERATOR(source), result, &load->error);
    if (load->error || !entries)
    {
        g_main_loop_quit(load->loop);
        return;
    }
    for (GList* node = entries; node; node = node->next)
    {
        GFileInfo* info = G_FILE_INFO(node->data);
        const char* name = g_file_info_get_name(info);
        const char* target = g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET) ?
                             g_file_info_get_symlink_target(info) : NULL;
        if (!name) continue;
        guint32 mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
        GFileType type = g_file_info_get_file_type(info);
        // Remote backends need not expose Unix permission bits.
        if (!mode)
            mode = (type == G_FILE_TYPE_DIRECTORY ? S_IFDIR | 0755 : S_IFREG | 0644);
        if (g_file_info_get_is_symlink(info))
            mode = (mode & ~S_IFMT) | (type == G_FILE_TYPE_DIRECTORY ? S_IFDIR :
                                     type == G_FILE_TYPE_SYMBOLIC_LINK ? S_IFLNK : S_IFREG);
        gchar* encodedName = g_base64_encode(reinterpret_cast<const guchar*>(name), strlen(name));
        gchar* encodedTarget = g_base64_encode(reinterpret_cast<const guchar*>(target ? target : ""),
                                                target ? strlen(target) : 0);
        GVariant* value = g_variant_ref_sink(g_variant_new("(suuuxxxsbii)",
            encodedName, mode,
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_UID) ?
                g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_UID) : G_MAXUINT32,
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_GID) ?
                g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_GID) : G_MAXUINT32,
            static_cast<gint64>(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED)),
            static_cast<gint64>(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_CHANGED)),
            static_cast<gint64>(g_file_info_get_size(info)), encodedTarget,
            g_file_info_get_is_symlink(info),
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ) ?
                static_cast<int>(g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ)) : -1,
            g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE) ?
                static_cast<int>(g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE)) : -1));
        gchar* record = g_variant_print(value, true);
        std::puts(record);
        g_free(record);
        g_variant_unref(value);
        g_free(encodedName);
        g_free(encodedTarget);
    }
    g_list_free_full(entries, g_object_unref);
    std::fflush(stdout);
    g_file_enumerator_next_files_async(load->enumerator, 100, G_PRIORITY_DEFAULT, NULL, nextEntries, load);
}

void enumerated(GObject* source, GAsyncResult* result, gpointer data)
{
    DirectoryLoad* load = static_cast<DirectoryLoad*>(data);
    load->enumerator = g_file_enumerate_children_finish(G_FILE(source), result, &load->error);
    if (!load->enumerator) g_main_loop_quit(load->loop);
    else g_file_enumerator_next_files_async(load->enumerator, 100, G_PRIORITY_DEFAULT, NULL, nextEntries, load);
}

int listDirectory(const char* path)
{
    GFile* directory = g_file_new_for_path(path);
    gchar* uri = g_file_get_uri(directory);
    // GVFS's native VFS maps its FUSE path back to the remote URI.
    // Refuse a local-FUSE fallback: it would recreate per-entry round trips.
    if (strstr(path, "/gvfs/") && g_str_has_prefix(uri, "file:"))
    {
        std::puts("ERROR Host GVFS could not resolve the mounted share to its native URI");
        g_free(uri);
        g_object_unref(directory);
        return 1;
    }
    g_free(uri);
    DirectoryLoad load = {g_main_loop_new(NULL, false), NULL, NULL};
    g_file_enumerate_children_async(directory,
        "standard::name,standard::type,standard::size,standard::is-symlink,standard::symlink-target,"
        "unix::mode,unix::uid,unix::gid,time::modified,time::changed,access::can-read,access::can-execute",
        G_FILE_QUERY_INFO_NONE, G_PRIORITY_DEFAULT, NULL, enumerated, &load);
    g_main_loop_run(load.loop);
    if (load.enumerator)
    {
        GError* closeError = NULL;
        if (!g_file_enumerator_close(load.enumerator, NULL, &closeError))
        {
            if (!load.error) load.error = closeError;
            else g_clear_error(&closeError);
        }
        g_object_unref(load.enumerator);
    }
    g_main_loop_unref(load.loop);
    g_object_unref(directory);
    if (load.error)
    {
        gchar* message = g_strescape(load.error->message, NULL);
        std::printf("ERROR %s\n", message);
        g_free(message);
        g_error_free(load.error);
        return 1;
    }
    std::puts("DONE");
    return 0;
}

void anonymousPassword(GMountOperation* operation, const char*, const char*, const char*,
                       GAskPasswordFlags, gpointer)
{
    g_mount_operation_set_anonymous(operation, true);
    g_mount_operation_reply(operation, G_MOUNT_OPERATION_HANDLED);
}

void mounted(GObject* source, GAsyncResult* result, gpointer data)
{
    MountResult* mount = static_cast<MountResult*>(data);
    g_file_mount_enclosing_volume_finish(G_FILE(source), result, &mount->error);
    g_main_loop_quit(mount->loop);
}
}

int main(int argc, char** argv)
{
    if (argc == 3 && strcmp(argv[1], "--list") == 0) return listDirectory(argv[2]);
    if (argc != 1)
    {
        std::fprintf(stderr, "Usage: swordfish-network [--list PATH]\n");
        return 2;
    }
    GFile* root = g_file_new_for_uri("smb://");
    GError* error = NULL;
    GFileEnumerator* entries = g_file_enumerate_children(root, G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (!entries && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED))
    {
        g_clear_error(&error);
        MountResult result = {g_main_loop_new(NULL, false), NULL};
        GMountOperation* operation = g_mount_operation_new();
        g_signal_connect(operation, "ask-password", G_CALLBACK(anonymousPassword), NULL);
        // Mount only the anonymous browsing root, never a user's share.
        g_file_mount_enclosing_volume(root, G_MOUNT_MOUNT_NONE, operation, NULL, mounted, &result);
        g_main_loop_run(result.loop);
        g_main_loop_unref(result.loop);
        g_object_unref(operation);
        if (result.error && !g_error_matches(result.error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED))
        {
            error = result.error;
        }
        else
        {
            g_clear_error(&result.error);
            entries = g_file_enumerate_children(root, G_FILE_ATTRIBUTE_STANDARD_NAME,
                G_FILE_QUERY_INFO_NONE, NULL, &error);
        }
    }
    if (entries)
    {
        GFileInfo* info;
        while ((info = g_file_enumerator_next_file(entries, NULL, &error)))
        {
            const char* name = g_file_info_get_name(info);
            if (name) std::printf("%s\n", name);
            g_object_unref(info);
        }
        GError* closeError = NULL;
        if (!g_file_enumerator_close(entries, NULL, &closeError))
        {
            if (!error) error = closeError;
            else g_clear_error(&closeError);
        }
        g_object_unref(entries);
    }
    g_object_unref(root);
    if (error)
    {
        std::fprintf(stderr, "gio: smb://: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    return 0;
}
