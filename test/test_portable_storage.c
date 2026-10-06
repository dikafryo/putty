/* Windows integration tests for persistence, escaping, and writer isolation. */
#include <limits.h>
#include <wchar.h>
#include "putty.h"
#include "storage.h"
#include "portable.h"

/* Keep failures readable on unattended Windows runners, without CRT dialogs. */
#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #expression); \
        exit(1); \
    } \
} while (0)

static void report_error(const char *format, va_list args)
{
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    exit(1);
}

void modalfatalbox(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    report_error(format, args);
    va_end(args);
}

void nonfatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    report_error(format, args);
    va_end(args);
}

static void save_session(const char *name)
{
    char *error;
    settings_w *writer = open_settings_w(name, &error);
    CHECK(writer && !error);
    write_setting_s(writer, "HostName", "example.org");
    close_settings_w(writer);
}

static void test_settings(void)
{
    const char *name = "USB / % [] = ; # \xED\x95\x9C\xEA\xB8\x80";
    const char *text = "  %quoted=\"yes\"\r\nline two\t[];#  ";
    char *large = snewn(100001, char);
    memset(large, 'x', 100000);
    large[100000] = '\0';
    char *error;
    settings_w *writer = open_settings_w(name, &error);
    CHECK(writer && !error);
    write_setting_s(writer, "Escaped=Key", text);
    write_setting_s(writer, "Large", large);
    write_setting_i(writer, "Signed", INT_MIN);
    FontSpec *font = fontspec_new("Consolas", true, 13, 1);
    write_setting_fontspec(writer, "Font", font);
    Filename *filename = filename_from_wstr(L"Z:\\USB\\\xD55C\xAE00\\key.ppk");
    write_setting_filename(writer, "KeyFile", filename);
    close_settings_w(writer);
    settings_r *reader = open_settings_r(name);
    CHECK(reader);
    char *loaded = read_setting_s(reader, "Escaped=Key");
    CHECK(loaded && !strcmp(loaded, text));
    sfree(loaded);
    loaded = read_setting_s(reader, "Large");
    CHECK(loaded && !strcmp(loaded, large));
    sfree(loaded);
    CHECK(read_setting_i(reader, "Signed", 0) == INT_MIN);
    CHECK(read_setting_i(reader, "Missing", 123) == 123);
    FontSpec *loaded_font = read_setting_fontspec(reader, "Font");
    CHECK(loaded_font && loaded_font->isbold && loaded_font->height == 13);
    CHECK(!strcmp(loaded_font->name, font->name));
    fontspec_free(loaded_font);
    fontspec_free(font);
    Filename *loaded_filename = read_setting_filename(reader, "KeyFile");
    CHECK(loaded_filename && !wcscmp(loaded_filename->wpath, filename->wpath));
    filename_free(loaded_filename);
    filename_free(filename);
    close_settings_r(reader);
    sfree(large);
    settings_e *enumerator = enum_settings_start();
    strbuf *entry = strbuf_new();
    bool found = false;
    while (enum_settings_next(enumerator, entry)) {
        if (!strcmp(entry->s, name))
            found = true;
        strbuf_clear(entry);
    }
    CHECK(found);
    enum_settings_finish(enumerator);
    strbuf_free(entry);
    del_settings(name);
    CHECK(!open_settings_r(name));
    save_session("Default Settings");
    CHECK((reader = open_settings_r(NULL)) != NULL);
    close_settings_r(reader);
}

static void test_host_keys_and_cas(void)
{
    CHECK(check_stored_host_key("host", 22, "ed25519", "key") == 1);
    store_host_key(NULL, "host", 22, "ed25519", "key");
    CHECK(check_stored_host_key("host", 22, "ed25519", "key") == 0);
    CHECK(check_stored_host_key("host", 22, "ed25519", "changed") == 2);
    CHECK(check_stored_host_key("host", 23, "ed25519", "key") == 1);
    CHECK(have_ssh_host_key("host", 22, "ed25519"));
    host_ca *ca = host_ca_new();
    ca->name = dupstr("USB CA / [] %");
    ca->ca_public_key = strbuf_new();
    put_data(ca->ca_public_key, "a\0b\xFF", 4);
    ca->validity_expression = dupstr("*.example.org");
    ca->opts.permit_rsa_sha1 = true;
    ca->opts.permit_rsa_sha256 = false;
    CHECK(!host_ca_save(ca));
    host_ca *loaded = host_ca_load(ca->name);
    CHECK(loaded && loaded->ca_public_key->len == 4);
    CHECK(!memcmp(loaded->ca_public_key->s, ca->ca_public_key->s, 4));
    CHECK(!strcmp(loaded->validity_expression, ca->validity_expression));
    CHECK(loaded->opts.permit_rsa_sha1 && !loaded->opts.permit_rsa_sha256);
    host_ca_free(loaded);
    host_ca_enum *enumerator = enum_host_ca_start();
    strbuf *name = strbuf_new();
    CHECK(enum_host_ca_next(enumerator, name) && !strcmp(name->s, ca->name));
    enum_host_ca_finish(enumerator);
    strbuf_free(name);
    CHECK(!host_ca_delete(ca->name));
    CHECK(!host_ca_load(ca->name));
    host_ca_free(ca);
}

static void test_parallel_writers(void)
{
    PROCESS_INFORMATION processes[8];
    wchar_t executable[32768];
    CHECK(GetModuleFileNameW(NULL, executable, lenof(executable)));
    for (int i = 0; i < lenof(processes); i++) {
        wchar_t command[32768];
        _snwprintf(command, lenof(command), L"\"%ls\" --write child-%d", executable, i);
        STARTUPINFOW startup = {0};
        startup.cb = sizeof(startup);
        CHECK(CreateProcessW(executable, command, NULL, NULL, false, 0,
                              NULL, NULL, &startup, &processes[i]));
    }
    for (int i = 0; i < lenof(processes); i++) {
        CHECK(WaitForSingleObject(processes[i].hProcess, 30000) == WAIT_OBJECT_0);
        DWORD status;
        CHECK(GetExitCodeProcess(processes[i].hProcess, &status) && !status);
        CloseHandle(processes[i].hThread);
        CloseHandle(processes[i].hProcess);
        char *name = dupprintf("child-%d", i);
        settings_r *reader = open_settings_r(name);
        CHECK(reader);
        close_settings_r(reader);
        sfree(name);
    }
}

static strbuf *seed;
static void consume_seed(void *data, int length)
{
    put_data(seed, data, length);
}

static void test_errors(void)
{
    wchar_t *path = portable_path(L"putty.ini.tmp");
    FILE *file = _wfopen(path, L"wb");
    CHECK(file);
    fclose(file);
    CHECK(SetFileAttributesW(path, FILE_ATTRIBUTE_READONLY));
    char *error;
    CHECK(!portable_db_open(true, &error) && error);
    sfree(error);
    CHECK(SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL));
    CHECK(DeleteFileW(path));
    sfree(path);
    path = portable_path(L"putty.ini");
    file = _wfopen(path, L"wb");
    CHECK(file);
    fputs("this is corrupt data\n", file);
    fclose(file);
    CHECK(!portable_db_open(true, &error) && error);
    sfree(error);
    file = _wfopen(path, L"rb");
    CHECK(file && fgetc(file) == 't');
    fclose(file);
    CHECK(DeleteFileW(path));
    sfree(path);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "--write")) {
        save_session(argv[2]);
        return 0;
    }
    /* Must run in an isolated copy of the executable directory. */
    puts("Testing initial portable storage creation");
    cleanup_all();
    portable_storage_init();
    wchar_t *path = portable_path(L"putty.ini");
    CHECK(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES);
    puts("Testing settings, Unicode filenames, fonts, and long values");
    test_settings();
    puts("Testing SSH host keys and host CAs");
    test_host_keys_and_cas();
    puts("Testing concurrent processes");
    test_parallel_writers();
    seed = strbuf_new();
    write_random_seed("seed\0data", 9);
    read_random_seed(consume_seed);
    CHECK(seed->len == 9 && !memcmp(seed->s, "seed\0data", 9));
    strbuf_free(seed);
    cleanup_all();
    CHECK(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES);
    sfree(path);
    puts("Testing read-only storage and corrupt settings");
    test_errors();
    puts("Portable storage tests passed");
    return 0;
}
