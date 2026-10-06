/*
 * Portable INI storage. All paths are relative to the executable, never
 * the working directory. Writers lock a sidecar file, reload the latest
 * settings, and atomically replace the INI after flushing a complete copy.
 * We deliberately avoid Windows profile APIs (IniFileMapping can redirect
 * those APIs to the registry).
 */
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <wchar.h>
#include "putty.h"
#include "tree234.h"
#include "portable.h"

struct portable_entry {
    char *section, *key, *value;
};

struct portable_db {
    tree234 *entries;
    HANDLE lock;
    OVERLAPPED lock_offset;
    FILE *output;
    wchar_t *path, *temporary;
    bool dirty;
};

wchar_t *portable_path(const wchar_t *filename)
{
    DWORD capacity = 256;
    wchar_t *path = NULL;
    for (;;) {
        path = sresize(path, capacity, wchar_t);
        DWORD length = GetModuleFileNameW(NULL, path, capacity);
        if (!length)
            modalfatalbox("Cannot locate the portable executable: %s",
                     win_strerror(GetLastError()));
        if (length < capacity - 1)
            break;
        capacity *= 2;
    }
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash)
        modalfatalbox("The portable executable path has no directory");
    size_t prefix = slash + 1 - path;
    path = sresize(path, prefix + wcslen(filename) + 1, wchar_t);
    wcscpy(path + prefix, filename);
    return path;
}

static int entry_compare(void *av, void *bv)
{
    struct portable_entry *a = av, *b = bv;
    int result = strcmp(a->section, b->section);
    return result ? result : strcmp(a->key, b->key);
}

static void entry_free(struct portable_entry *entry)
{
    sfree(entry->section);
    sfree(entry->key);
    sfree(entry->value);
    sfree(entry);
}

const char *portable_db_get(portable_db *db, const char *section,
                            const char *key)
{
    struct portable_entry query = { (char *)section, (char *)key, NULL };
    struct portable_entry *entry = find234(db->entries, &query, NULL);
    return entry ? entry->value : NULL;
}

void portable_db_set(portable_db *db, const char *section,
                     const char *key, const char *value)
{
    struct portable_entry *entry = snew(struct portable_entry);
    entry->section = dupstr(section);
    entry->key = dupstr(key);
    entry->value = dupstr(value);
    struct portable_entry *previous = add234(db->entries, entry);
    if (previous != entry) {
        if (strcmp(previous->value, value)) {
            sfree(previous->value);
            previous->value = dupstr(value);
        }
        entry_free(entry);
    }
    db->dirty = true;
}

void portable_db_delete(portable_db *db, const char *section)
{
    for (int i = count234(db->entries); i-- > 0;) {
        struct portable_entry *entry = index234(db->entries, i);
        if (!strcmp(entry->section, section)) {
            entry_free(delpos234(db->entries, i));
            db->dirty = true;
        }
    }
}

char *portable_db_enum(portable_db *db, const char *prefix, int index)
{
    size_t length = strlen(prefix);
    const char *previous = NULL;
    for (int i = 0; i < count234(db->entries); i++) {
        struct portable_entry *entry = index234(db->entries, i);
        if (strncmp(entry->section, prefix, length) ||
            (previous && !strcmp(previous, entry->section)))
            continue;
        previous = entry->section;
        if (!index--)
            return dupstr(entry->section + length);
    }
    return NULL;
}

static void db_free(portable_db *db)
{
    if (db->output)
        fclose(db->output);
    if (db->temporary)
        DeleteFileW(db->temporary);
    if (db->lock != INVALID_HANDLE_VALUE) {
        UnlockFileEx(db->lock, 0, 1, 0, &db->lock_offset);
        CloseHandle(db->lock);
    }
    while (count234(db->entries))
        entry_free(delpos234(db->entries, 0));
    freetree234(db->entries);
    sfree(db->path);
    sfree(db->temporary);
    sfree(db);
}

static bool db_load(portable_db *db, char **error)
{
    HANDLE file = CreateFileW(db->path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) {
            db->dirty = true;
            return true;
        }
        *error = dupprintf("Cannot read putty.ini: %s", win_strerror(code));
        return false;
    }
    int descriptor = _open_osfhandle((intptr_t)file, _O_RDONLY | _O_BINARY);
    FILE *input = descriptor < 0 ? NULL : _fdopen(descriptor, "rb");
    if (!input) {
        if (descriptor < 0)
            CloseHandle(file);
        else
            _close(descriptor);
        *error = dupprintf("Cannot open putty.ini stream: %s", strerror(errno));
        return false;
    }
    char *section = NULL, *line;
    bool valid = true;
    while ((line = fgetline(input)) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        size_t length = strlen(line);
        if (!length || line[0] == ';' || line[0] == '#') {
            sfree(line);
            continue;
        }
        if (line[0] == '[' && length > 2 && line[length - 1] == ']') {
            line[length - 1] = '\0';
            sfree(section);
            section = strbuf_to_str(percent_decode_sb(
                ptrlen_from_asciz(line + 1)));
            portable_db_set(db, section, "", "");
        } else {
            char *equals = strchr(line, '=');
            if (!section || !equals || equals == line) {
                valid = false;
            } else {
                *equals = '\0';
                char *key = strbuf_to_str(percent_decode_sb(
                    ptrlen_from_asciz(line)));
                char *value = strbuf_to_str(percent_decode_sb(
                    ptrlen_from_asciz(equals + 1)));
                portable_db_set(db, section, key, value);
                sfree(key);
                sfree(value);
            }
        }
        sfree(line);
        if (!valid)
            break;
    }
    if (ferror(input))
        valid = false;
    fclose(input);
    sfree(section);
    db->dirty = false;
    if (!valid)
        *error = dupstr("Cannot read putty.ini: invalid INI data or read error");
    return valid;
}

portable_db *portable_db_open(bool write, char **error)
{
    *error = NULL;
    portable_db *db = snew(portable_db);
    memset(db, 0, sizeof(*db));
    db->entries = newtree234(entry_compare);
    db->lock = INVALID_HANDLE_VALUE;
    db->path = portable_path(L"putty.ini");
    if (write) {
        wchar_t *lockpath = portable_path(L"putty.ini.lock");
        db->lock = CreateFileW(lockpath, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        sfree(lockpath);
        if (db->lock == INVALID_HANDLE_VALUE ||
            !LockFileEx(db->lock, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0,
                        &db->lock_offset)) {
            *error = dupprintf("Cannot lock portable settings: %s",
                               win_strerror(GetLastError()));
            db_free(db);
            return NULL;
        }
        db->temporary = portable_path(L"putty.ini.tmp");
        db->output = _wfopen(db->temporary, L"wb");
        if (!db->output) {
            *error = dupprintf("Cannot write settings beside the executable: %s",
                               strerror(errno));
            db_free(db);
            return NULL;
        }
    }
    if (!db_load(db, error)) {
        db_free(db);
        return NULL;
    }
    return db;
}

static bool db_save(portable_db *db, char **error)
{
    FILE *output = db->output;
    fputs("; PuTTY Portable settings, format 1. Values use percent encoding.\n", output);
    const char *section = NULL;
    for (int i = 0; i < count234(db->entries); i++) {
        struct portable_entry *entry = index234(db->entries, i);
        if (!section || strcmp(section, entry->section)) {
            fputs("\n[", output);
            percent_encode_fp(output, ptrlen_from_asciz(entry->section), "[]");
            fputs("]\n", output);
            section = entry->section;
        }
        if (!*entry->key)
            continue;
        percent_encode_fp(output, ptrlen_from_asciz(entry->key), "=[];#");
        fputc('=', output);
        percent_encode_fp(output, ptrlen_from_asciz(entry->value), NULL);
        fputc('\n', output);
    }
    bool success = !ferror(output) && !fflush(output) && !_commit(_fileno(output));
    if (fclose(output))
        success = false;
    db->output = NULL;
    if (!success) {
        *error = dupprintf("Cannot flush putty.ini: %s", strerror(errno));
        return false;
    }
    if (!MoveFileExW(db->temporary, db->path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        *error = dupprintf("Cannot replace putty.ini: %s",
                           win_strerror(GetLastError()));
        return false;
    }
    return true;
}

bool portable_db_close(portable_db *db, char **error)
{
    *error = NULL;
    if (!db)
        return true;
    bool success = !db->output || !db->dirty || db_save(db, error);
    db_free(db);
    return success;
}

/* Called with the writer lock held, so cleanup cannot race another save. */
bool portable_db_remove_file(portable_db *db, char **error)
{
    db->dirty = false;
    *error = NULL;
    if (DeleteFileW(db->path) || GetLastError() == ERROR_FILE_NOT_FOUND)
        return true;
    *error = dupprintf("Cannot delete putty.ini: %s", win_strerror(GetLastError()));
    return false;
}
