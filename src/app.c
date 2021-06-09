#include <stdio.h>
#include <sqlite3.h>
#include <uuid/uuid.h>
#include <stdlib.h>
#include "app.h"

void
help(void)
{
	printf("Welcome to zkc. Usage:\n"
	       "zkc [command]\n"
	       "Commands:\n"
	       "help (default) - display commands\n"
	       "init - create tables\n"
	       "new - create new zettel in inbox\n"
	       "inbox - list zettels in inbox\n");
}

int
open_db(sqlite3 **db)
{
	int rc = sqlite3_open(DB_PATH, db);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot open zkc database: %s\n", sqlite3_errmsg(*db));
	}

	return rc;
}

int
sql_exec(sqlite3 *db, const char *sql)
{
	char *err_msg = 0;
	int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}

	return rc;
}

int
create_tables(sqlite3 *db)
{
	const char *create_notes = "CREATE TABLE IF NOT EXISTS notes("
		"id INTEGER PRIMARY KEY, "
		"uuid VARCHAR(36), "
		"body TEXT, "
		"date DATETIME DEFAULT CURRENT_TIMESTAMP"
		");";

	int rc = sql_exec(db, create_notes);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_tags = "CREATE TABLE IF NOT EXISTS tags("
		"id INTEGER PRIMARY KEY, "
		"body TEXT"
		");";

	rc = sql_exec(db, create_tags);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_note_tags = "CREATE TABLE IF NOT EXISTS note_tags("
		"note_id INTEGER, "
		"tag_id INTEGER, "
		"FOREIGN KEY(note_id) REFERENCES notes(id), "
		"FOREIGN KEY(tag_id) REFERENCES tags(id)"		
		")";

	rc = sql_exec(db, create_note_tags);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_inbox = "CREATE TABLE IF NOT EXISTS inbox("
		"note_id INTEGER, "
		"FOREIGN KEY(note_id) REFERENCES notes(id)"
		");";

	rc = sql_exec(db, create_inbox);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_links = "CREATE TABLE IF NOT EXISTS links("
		"a_id INTEGER, "
		"b_id INTEGER, "
		"FOREIGN KEY(a_id) REFERENCES notes(id), "
		"FOREIGN KEY(b_id) REFERENCES notes(id)"
		");";

	rc = sql_exec(db, create_links);
	if (rc != SQLITE_OK)
		return rc;

	return SQLITE_OK;
}

int
new(sqlite3 *db)
{
	char uuid[37];
	uuid_t binuuid;
	uuid_generate_random(binuuid);
	uuid_unparse_lower(binuuid, uuid);

	if (getenv("EDITOR") == NULL) {
		printf("EDITOR variable is not set.\n");
		return 1;
	}

	char command[100];
	sprintf(command, "$EDITOR %s", uuid);

	system(command);
	return 0;
}

int
inbox(sqlite3 *db)
{
	char *sql = "SELECT notes.uuid, notes.body "
		"FROM notes "
		"INNER JOIN inbox "
		"ON inbox.note_id = notes.id";

	char *err_msg = 0;
	int rc = sqlite3_exec(db, sql, inbox_callback, 0, &err_msg);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query inbox\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}

	return rc;
}

int
inbox_callback(void *not_used, int argc, char **argv, char **col_names)
{
	for (int i = 0; i < argc; i++) {
		printf("%s = %s\n", col_names[i], argv[i] ? argv[i] : "NULL");
	}

	printf("\n");
	return 0;
}
