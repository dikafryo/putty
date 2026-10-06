/* Windows portable implementation of storage.h; no registry access. */
#include <errno.h>
#include <limits.h>
#include "putty.h"
#include "storage.h"
#include "portable.h"

struct settings_w { portable_db *db; char *section; };
struct settings_r { portable_db *db; char *section; };
struct settings_e { portable_db *db; int index; };
struct host_ca_enum { portable_db *db; int index; };

static char *named_section(const char *prefix, const char *name)
{
    strbuf *encoded = percent_encode_sb(ptrlen_from_asciz(name), "/[]");
    char *section = dupcat(prefix, encoded->s);
    strbuf_free(encoded);
    return section;
}

static void close_db(portable_db *db)
{
    char *error;
    if (!portable_db_close(db, &error)) {
        nonfatal("%s", error);
        sfree(error);
    }
}

static portable_db *read_db(void)
{
    char *error;
    portable_db *db = portable_db_open(false, &error);
    if (error) {
        nonfatal("%s", error);
        sfree(error);
    }
    return db;
}

void portable_storage_init(void)
{
    char *error;
    portable_db *db = portable_db_open(true, &error);
    if (!db)
        modalfatalbox("%s", error);
    if (!portable_db_close(db, &error))
        modalfatalbox("%s", error);
}

settings_w *open_settings_w(const char *name, char **error)
{
    portable_db *db = portable_db_open(true, error);
    if (!db)
        return NULL;
    settings_w *handle = snew(settings_w);
    handle->db = db;
    handle->section = named_section(
        "Sessions/", name && *name ? name : "Default Settings");
    portable_db_set(db, handle->section, "", "");
    return handle;
}

void write_setting_s(settings_w *handle, const char *key, const char *value)
{
    if (handle)
        portable_db_set(handle->db, handle->section, key, value);
}

void write_setting_i(settings_w *handle, const char *key, int value)
{
    char *text = dupprintf("%d", value);
    write_setting_s(handle, key, text);
    sfree(text);
}

void close_settings_w(settings_w *handle)
{
    if (!handle)
        return;
    close_db(handle->db);
    sfree(handle->section);
    sfree(handle);
}

settings_r *open_settings_r(const char *name)
{
    portable_db *db = read_db();
    if (!db)
        return NULL;
    char *section = named_section(
        "Sessions/", name && *name ? name : "Default Settings");
    if (!portable_db_get(db, section, "")) {
        close_db(db);
        sfree(section);
        return NULL;
    }
    settings_r *handle = snew(settings_r);
    handle->db = db;
    handle->section = section;
    return handle;
}

char *read_setting_s(settings_r *handle, const char *key)
{
    const char *value = handle ?
        portable_db_get(handle->db, handle->section, key) : NULL;
    return value ? dupstr(value) : NULL;
}

static int db_read_int(portable_db *db, const char *section,
                       const char *key, int fallback)
{
    const char *text = portable_db_get(db, section, key);
    if (!text || !*text)
        return fallback;
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    return errno || *end || value < INT_MIN || value > INT_MAX ?
        fallback : (int)value;
}

int read_setting_i(settings_r *handle, const char *key, int fallback)
{
    return handle ?
        db_read_int(handle->db, handle->section, key, fallback) : fallback;
}

FontSpec *read_setting_fontspec(settings_r *handle, const char *name)
{
    char *settingname;
    char *fontname;
    FontSpec *ret;
    int isbold, height, charset;

    fontname = read_setting_s(handle, name);
    if (!fontname)
        return NULL;

    settingname = dupcat(name, "IsBold");
    isbold = read_setting_i(handle, settingname, -1);
    sfree(settingname);
    if (isbold == -1) {
        sfree(fontname);
        return NULL;
    }

    settingname = dupcat(name, "CharSet");
    charset = read_setting_i(handle, settingname, -1);
    sfree(settingname);
    if (charset == -1) {
        sfree(fontname);
        return NULL;
    }

    settingname = dupcat(name, "Height");
    height = read_setting_i(handle, settingname, INT_MIN);
    sfree(settingname);
    if (height == INT_MIN) {
        sfree(fontname);
        return NULL;
    }

    ret = fontspec_new(fontname, isbold, height, charset);
    sfree(fontname);
    return ret;
}

void write_setting_fontspec(settings_w *handle,
                            const char *name, FontSpec *font)
{
    char *settingname;

    write_setting_s(handle, name, font->name);
    settingname = dupcat(name, "IsBold");
    write_setting_i(handle, settingname, font->isbold);
    sfree(settingname);
    settingname = dupcat(name, "CharSet");
    write_setting_i(handle, settingname, font->charset);
    sfree(settingname);
    settingname = dupcat(name, "Height");
    write_setting_i(handle, settingname, font->height);
    sfree(settingname);
}

Filename *read_setting_filename(settings_r *handle, const char *name)
{
    char *text = read_setting_s(handle, name);
    if (!text)
        return NULL;
    Filename *filename = filename_from_utf8(text);
    sfree(text);
    return filename;
}

void write_setting_filename(settings_w *handle, const char *name, Filename *filename)
{
    write_setting_s(handle, name, filename->utf8path);
}

void close_settings_r(settings_r *handle)
{
    if (!handle)
        return;
    close_db(handle->db);
    sfree(handle->section);
    sfree(handle);
}

static char *delete_named_section(const char *prefix, const char *name)
{
    char *error;
    portable_db *db = portable_db_open(true, &error);
    if (!db)
        return error;
    char *section = named_section(prefix, name);
    portable_db_delete(db, section);
    sfree(section);
    portable_db_close(db, &error);
    return error;
}

void del_settings(const char *name)
{
    char *error = delete_named_section("Sessions/", name);
    if (error) {
        nonfatal("%s", error);
        sfree(error);
    }
}

settings_e *enum_settings_start(void)
{
    portable_db *db = read_db();
    if (!db)
        return NULL;
    settings_e *handle = snew(settings_e);
    handle->db = db;
    handle->index = 0;
    return handle;
}

static bool enum_section(portable_db *db, const char *prefix, int *index, strbuf *out)
{
    char *name = portable_db_enum(db, prefix, (*index)++);
    if (!name)
        return false;
    percent_decode_bs(BinarySink_UPCAST(out), ptrlen_from_asciz(name));
    sfree(name);
    return true;
}

bool enum_settings_next(settings_e *handle, strbuf *out)
{
    return enum_section(handle->db, "Sessions/", &handle->index, out);
}

void enum_settings_finish(settings_e *handle)
{
    close_db(handle->db);
    sfree(handle);
}

static char *hostkey_name(const char *hostname, int port, const char *type)
{
    return dupprintf("%s@%d:%s", type, port, hostname);
}

int check_stored_host_key(const char *hostname, int port, const char *type, const char *key)
{
    portable_db *db = read_db();
    if (!db)
        return 1;
    char *name = hostkey_name(hostname, port, type);
    const char *stored = portable_db_get(db, "HostKeys", name);
    int result = !stored ? 1 : strcmp(stored, key) ? 2 : 0;
    sfree(name);
    close_db(db);
    return result;
}

bool have_ssh_host_key(const char *hostname, int port, const char *type)
{
    return check_stored_host_key(hostname, port, type, "") != 1;
}

void store_host_key(Seat *seat, const char *hostname, int port,
                    const char *type, const char *key)
{
    char *error;
    portable_db *db = portable_db_open(true, &error);
    if (db) {
        char *name = hostkey_name(hostname, port, type);
        portable_db_set(db, "HostKeys", name, key);
        sfree(name);
        portable_db_close(db, &error);
    }
    if (error) {
        if (seat)
            seat_nonfatal(seat, "%s", error);
        else
            nonfatal("%s", error);
        sfree(error);
    }
}

host_ca_enum *enum_host_ca_start(void)
{
    portable_db *db = read_db();
    if (!db)
        return NULL;
    host_ca_enum *handle = snew(host_ca_enum);
    handle->db = db;
    handle->index = 0;
    return handle;
}

bool enum_host_ca_next(host_ca_enum *handle, strbuf *out)
{
    return enum_section(handle->db, "HostCAs/", &handle->index, out);
}

void enum_host_ca_finish(host_ca_enum *handle)
{
    close_db(handle->db);
    sfree(handle);
}

host_ca *host_ca_load(const char *name)
{
    portable_db *db = read_db();
    if (!db)
        return NULL;
    char *section = named_section("HostCAs/", name);
    const char *public_key = portable_db_get(db, section, "PublicKey");
    const char *validity = portable_db_get(db, section, "Validity");
    host_ca *ca = NULL;
    if (public_key && validity) {
        ca = host_ca_new();
        ca->name = dupstr(name);
        ca->ca_public_key = base64_decode_sb(ptrlen_from_asciz(public_key));
        ca->validity_expression = dupstr(validity);
        ca->opts.permit_rsa_sha1 = db_read_int(
            db, section, "PermitRSASHA1", ca->opts.permit_rsa_sha1);
        ca->opts.permit_rsa_sha256 = db_read_int(
            db, section, "PermitRSASHA256", ca->opts.permit_rsa_sha256);
        ca->opts.permit_rsa_sha512 = db_read_int(
            db, section, "PermitRSASHA512", ca->opts.permit_rsa_sha512);
    }
    sfree(section);
    close_db(db);
    return ca;
}

char *host_ca_save(host_ca *ca)
{
    if (!*ca->name)
        return dupstr("CA record must have a name");
    char *error;
    portable_db *db = portable_db_open(true, &error);
    if (!db)
        return error;
    char *section = named_section("HostCAs/", ca->name);
    strbuf *public_key = base64_encode_sb(ptrlen_from_strbuf(ca->ca_public_key), 0);
    portable_db_set(db, section, "PublicKey", public_key->s);
    portable_db_set(db, section, "Validity", ca->validity_expression);
    portable_db_set(db, section, "PermitRSASHA1", ca->opts.permit_rsa_sha1 ? "1" : "0");
    portable_db_set(db, section, "PermitRSASHA256", ca->opts.permit_rsa_sha256 ? "1" : "0");
    portable_db_set(db, section, "PermitRSASHA512", ca->opts.permit_rsa_sha512 ? "1" : "0");
    strbuf_free(public_key);
    sfree(section);
    portable_db_close(db, &error);
    return error;
}

char *host_ca_delete(const char *name)
{
    return delete_named_section("HostCAs/", name);
}

void read_random_seed(noise_consumer_t consumer)
{
    wchar_t *path = portable_path(L"PUTTY.RND");
    FILE *file = _wfopen(path, L"rb");
    sfree(path);
    if (!file)
        return;
    char buffer[1024];
    size_t length;
    while ((length = fread(buffer, 1, sizeof(buffer), file)) != 0)
        consumer(buffer, length);
    fclose(file);
}

void write_random_seed(void *data, int length)
{
    wchar_t *path = portable_path(L"PUTTY.RND");
    FILE *file = _wfopen(path, L"wb");
    sfree(path);
    if (!file) {
        nonfatal("Cannot write PUTTY.RND beside the executable");
        return;
    }
    bool success = fwrite(data, 1, length, file) == (size_t)length;
    if (fclose(file))
        success = false;
    if (!success)
        nonfatal("Cannot save the portable random seed");
}

void cleanup_all(void)
{
    char *error;
    portable_db *db = portable_db_open(true, &error);
    if (!db) {
        nonfatal("%s", error);
        sfree(error);
        return;
    }
    if (!portable_db_remove_file(db, &error)) {
        nonfatal("%s", error);
        sfree(error);
    }
    wchar_t *path = portable_path(L"PUTTY.RND");
    if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND)
        nonfatal("Cannot delete PUTTY.RND: %s", win_strerror(GetLastError()));
    sfree(path);
    /* No pending changes: closing only releases the writer lock. */
    close_db(db);
}
