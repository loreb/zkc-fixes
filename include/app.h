#ifndef APP_H
#define APP_H

int
open_db(sqlite3 **db);

int
sql_exec(sqlite3 *db, const char *sql);

int
create_tables(sqlite3 *db);

#endif
