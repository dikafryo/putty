/* File storage shared by the Windows portable backend and its tests. */
#ifndef PUTTY_WINDOWS_PORTABLE_H
#define PUTTY_WINDOWS_PORTABLE_H

typedef struct portable_db portable_db;
wchar_t *portable_path(const wchar_t *filename);
portable_db *portable_db_open(bool write, char **error);
bool portable_db_close(portable_db *db, char **error);
const char *portable_db_get(portable_db *db, const char *section,
                            const char *key);
void portable_db_set(portable_db *db, const char *section,
                     const char *key, const char *value);
void portable_db_delete(portable_db *db, const char *section);
char *portable_db_enum(portable_db *db, const char *prefix, int index);
void portable_storage_init(void);
#endif
