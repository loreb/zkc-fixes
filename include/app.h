#ifndef APP_H
#define APP_H

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
link_notes(sqlite3 *db, const char *uuid_a, const char *uuid_b);

int
links(sqlite3 *db, const char *uuid);

int
tag(sqlite3 *db, const char *uuid, const char *tag_body);

int
tags(sqlite3 *db, const char *uuid);

int
delete_note(sqlite3 *db, const char *uuid);

int
delete_tag(sqlite3 *db, const char *tag_body);

int
delete_link(sqlite3 *db, const char *uuid_a, const char *uuid_b);

int
delete_note_tag(sqlite3 *db, const char *uuid, const char *tag_body);

int
archive(sqlite3 *db, const char *uuid);

int
diff(sqlite3 *db, const char *path);

int
merge(sqlite3 *db, const char *path);

#endif
