#ifndef APP_H
#define APP_H

#define DB_PATH "zkc.db"

int
open_db(sqlite3 **db);

int
sql_exec(sqlite3 *db, const char *sql);

int
create_tables(sqlite3 *db);

void
help(void);

int
new(sqlite3 *db);

int
inbox(sqlite3 *db, int head);

int
note_summary_callback(void *not_used, int argc, char **argv, char **col_names);

int
view(sqlite3 *db, const char *uuid);

int
edit(sqlite3 *db, const char *uuid);

int
slurp(sqlite3 *db, const char *path);

int
spit(sqlite3 *db, const char *uuid, const char *path);

int
search(sqlite3 *db, const char *search_type, const char *search_word);

int
link(sqlite3 *db, const char *uuid_a, const char *uuid_b);

int
links(sqlite3 *db, const char *uuid);

#endif
