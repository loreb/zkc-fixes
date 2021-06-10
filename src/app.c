#include <stdio.h>
#include <sqlite3.h>
#include <uuid/uuid.h>
#include <stdlib.h>
#include <string.h>
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
	       "inbox - list zettels in inbox\n"
	       "head - show first zettel in inbox\n"
	       "view - view zettel by uuid\n"
	       "edit - edit zettel by uuid\n"
	       "slurp - load file into note\n"
	       "spit - write note to file"
	      );
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
		"uuid VARCHAR(36) NOT NULL, "
		"body TEXT NOT NULL, "
		"date DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP"
		");";

	int rc = sql_exec(db, create_notes);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_tags = "CREATE TABLE IF NOT EXISTS tags("
		"id INTEGER PRIMARY KEY, "
		"body TEXT NOT NULL"
		");";

	rc = sql_exec(db, create_tags);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_note_tags = "CREATE TABLE IF NOT EXISTS note_tags("
		"id INTEGER PRIMARY KEY, "
		"note_id INTEGER NOT NULL, "
		"tag_id INTEGER NOT NULL, "
		"FOREIGN KEY(note_id) REFERENCES notes(id), "
		"FOREIGN KEY(tag_id) REFERENCES tags(id)"		
		")";

	rc = sql_exec(db, create_note_tags);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_inbox = "CREATE TABLE IF NOT EXISTS inbox("
		"id INTEGER PRIMARY KEY, "
		"note_id INTEGER NOT NULL, "
		"FOREIGN KEY(note_id) REFERENCES notes(id)"
		");";

	rc = sql_exec(db, create_inbox);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_links = "CREATE TABLE IF NOT EXISTS links("
		"id INTEGER PRIMARY KEY, "
		"a_id INTEGER NOT NULL, "
		"b_id INTEGER NOT NULL, "
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

	FILE *f = fopen(uuid, "rb");
	if (!f)
		return 0;

	// TODO: Add error check
	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buffer = (char*)malloc(sizeof(char)*length);
	
	if (buffer)
		fread(buffer, sizeof(char), length, f);

	fclose(f);
	remove(uuid);

	int rc = SQLITE_OK;

	if (buffer) {
		char *sql = "INSERT INTO notes(uuid, body) VALUES(?, ?);";
		sqlite3_stmt *stmt;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			goto end;
		}

		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, buffer, strlen(buffer), SQLITE_TRANSIENT);

		rc = sqlite3_step(stmt);

		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
			goto end;
		}

		sqlite3_finalize(stmt);
	} else {
		goto end;
	}

	int note_id = sqlite3_last_insert_rowid(db);

	char *sql = "INSERT INTO inbox(note_id) VALUES(?);";
	sqlite3_stmt *stmt;
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		goto end;
	}

	sqlite3_bind_int(stmt, 1, note_id);

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
		goto end;
	}

	sqlite3_finalize(stmt);
	
end:
	if (buffer != NULL)
		free(buffer);

	return rc;
}

int
inbox(sqlite3 *db, int head)
{
	char *sql;
	if (head) {
		sql = "SELECT notes.uuid, notes.date, notes.body "
			"FROM notes "
			"INNER JOIN inbox "
			"ON inbox.note_id = notes.id "
			"ORDER BY date DESC "
			"LIMIT 1;";
	} else {
		sql = "SELECT notes.uuid, notes.date, notes.body "
			"FROM notes "
			"INNER JOIN inbox "
			"ON inbox.note_id = notes.id "
			"ORDER BY date DESC;";
	}

	char *err_msg = 0;
	int rc;
	rc = sqlite3_exec(db, sql, inbox_callback, 0, &err_msg);

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
	char *uuid = argv[0];
	char *date = argv[1];
	char *body = argv[2];

	if (strlen(body) > 16)
		body[16] = '\0';

	// Replace newlines with spaces
	for (size_t i = 0; i < 16; i++)
		if (body[i] == '\n')
			body[i] = ' ';
	
	printf("%s - %s - %s...\n", uuid, date, body);
	return 0;
}

int
view(sqlite3 *db, const char *uuid)
{
	char *sql = "SELECT body FROM notes WHERE uuid = ? LIMIT 1;";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_ROW) {
		fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
		return rc;
	}
	
	printf("%s\n", (char *)sqlite3_column_text(stmt, 0));

	sqlite3_finalize(stmt);
	return SQLITE_OK;
}

int
edit(sqlite3 *db, const char *uuid)
{
	char *sql = "SELECT body FROM notes WHERE uuid = ? LIMIT 1";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_ROW) {
		fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	FILE *fw = fopen(uuid, "wb");
	if (fw) {
		fputs((char *)sqlite3_column_text(stmt, 0), fw);
		fclose(fw);
	}

	sqlite3_finalize(stmt);	

	char command[100];
	sprintf(command, "$EDITOR %s", uuid);

	system(command);

	FILE *fr = fopen(uuid, "rb");
	if (!fr)
		return 0;

	// TODO: Add error check
	fseek(fr, 0, SEEK_END);
	long length = ftell(fr);
	fseek(fr, 0, SEEK_SET);
	char *buffer = (char*)malloc(sizeof(char) * length);
	
	if (buffer)
		fread(buffer, sizeof(char), length, fr);

	fclose(fr);
	remove(uuid);

	rc = SQLITE_OK;

	if (buffer) {
		char *sql = "UPDATE notes SET body = ? WHERE uuid = ?;";
		sqlite3_stmt *stmt;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			goto end;
		}

		sqlite3_bind_text(stmt, 1, buffer, strlen(buffer), SQLITE_TRANSIENT);		
		sqlite3_bind_text(stmt, 2, uuid, strlen(uuid), SQLITE_STATIC);

		rc = sqlite3_step(stmt);

		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
			goto end;
		}

		sqlite3_finalize(stmt);
	}

end:
	if (buffer != NULL)
		free(buffer);	
	
	return rc;
}

int
slurp(sqlite3 *db, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "No file found at: %s\n", path);
		return 1;
	}

	// TODO: Add error check
	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buffer = (char*)malloc(sizeof(char)*length);
	
	if (buffer)
		fread(buffer, sizeof(char), length, f);

	fclose(f);

	int rc = SQLITE_OK;

	if (buffer) {
		char *sql = "INSERT INTO notes(uuid, body) VALUES(?, ?);";
		sqlite3_stmt *stmt;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			goto end;
		}

		char uuid[37];
		uuid_t binuuid;
		uuid_generate_random(binuuid);
		uuid_unparse_lower(binuuid, uuid);
		

		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, buffer, strlen(buffer), SQLITE_TRANSIENT);

		rc = sqlite3_step(stmt);

		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
			goto end;
		}

		sqlite3_finalize(stmt);
	} else {
		fprintf(stderr, "Not loading empty file\n");
		goto end;
	}

	int note_id = sqlite3_last_insert_rowid(db);

	char *sql = "INSERT INTO inbox(note_id) VALUES(?);";
	sqlite3_stmt *stmt;
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		goto end;
	}

	sqlite3_bind_int(stmt, 1, note_id);

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
		goto end;
	}

	sqlite3_finalize(stmt);
	
end:
	if (buffer != NULL)
		free(buffer);

	return rc;	
}

int
spit(sqlite3 *db, const char *uuid, const char *path)
{
	char *sql = "SELECT body FROM notes WHERE uuid = ? LIMIT 1";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_ROW) {
		fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	FILE *fw = fopen(path, "wb");
	if (fw) {
		fputs((char *)sqlite3_column_text(stmt, 0), fw);
		fclose(fw);
	}

	sqlite3_finalize(stmt);

	return SQLITE_OK;
}
