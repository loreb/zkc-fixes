#include <stdio.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include "app.h"

static void
sha256_string(const char *s, char output_buffer[65])
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
		SHA256((unsigned char *)s, strlen(s), hash);
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output_buffer + (i * 2), "%02x", hash[i]);
    }
    output_buffer[64] = '\0';
}

// Taken from: https://gist.github.com/kvelakur/9069c9896577c3040030
static int
uuid_v4_gen(char *buffer)
{
	union
	{
		struct
		{
			uint32_t time_low;
			uint16_t time_mid;
			uint16_t time_hi_and_version;
			uint8_t  clk_seq_hi_res;
			uint8_t  clk_seq_low;
			uint8_t  node[6];
		};
		uint8_t __rnd[16];
	} uuid;

	int rc = RAND_bytes(uuid.__rnd, sizeof(uuid));

	// Refer Section 4.2 of RFC-4122
	// https://tools.ietf.org/html/rfc4122#section-4.2
	uuid.clk_seq_hi_res = (uint8_t) ((uuid.clk_seq_hi_res & 0x3F) | 0x80);
	uuid.time_hi_and_version = (uint16_t) ((uuid.time_hi_and_version & 0x0FFF) | 0x4000);

	snprintf(buffer, 38, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
			uuid.time_low, uuid.time_mid, uuid.time_hi_and_version,
			uuid.clk_seq_hi_res, uuid.clk_seq_low,
			uuid.node[0], uuid.node[1], uuid.node[2],
			uuid.node[3], uuid.node[4], uuid.node[5]);

	return rc;
}

void
help(void)
{
	printf("zkc usage:\n"
	       "default   - help.\n"
	       "help      - display commands and usage.\n"
	       "init      - create tables.\n"
	       "new       - create new zettel in inbox.\n"
	       "inbox     - list zettels in inbox.\n"
	       "head      - show first zettel in inbox.\n"
	       "tail      - show last zettel in inbox.\n"
	       "view      - [uuid] - view zettel by uuid.\n"
	       "edit      - [uuid] - edit zettel by uuid.\n"
	       "slurp     - [path] - load file into new note.\n"
	       "spit      - [uuid] [path] - write note to file.\n"
	       "search    - [search_type] [search_word] - search notes by search type\n"
	       "            (text|tag) and search word. search_type defaults to text.\n"
	       "link      - [uuid] [uuid] - link note to other note.\n"
	       "links     - [uuid] - display forward and backward links for note.\n"
	       "tag       - [uuid] [tag] - tag note\n"
	       "tags      - [uuid] - list tags for note. list all tags by default.\n"
	       "delete    - [delete_type|uuid] [uuid|tag_name] [uuid|tag_name] - delete note, tag, note_tag, or link.\n"
	       "archive   - [uuid] - move note out of inbox.\n"
				 "diff      - [path] - display differences with database at path.\n"
				 "merge     - [path] - merge differences from database at path.\n"
	      );
}

int
open_db(sqlite3 **db)
{
	char zdir[200];
  char* homedir = getenv("HOME");
  if (homedir == NULL) {
  	homedir = getpwuid(getuid())->pw_dir;
	}

  strcpy(zdir, homedir);
  strcat(zdir, "/.zettelkasten/");

  DIR* dr = opendir(zdir);
  if (dr == NULL) {
  	int result = mkdir(zdir, 0777);
    if (result != 0) {
    	printf("Failed to create zettelkasten directory\n");
      return 1;
    } else {
      printf("Initialized zettelkasten directory in $HOME\n");
    }
  }

  strcat(zdir, "zkc.db");
  int rc = sqlite3_open(zdir, db);

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
		"hash TEXT NOT NULL, "
		"date DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP"
		");";

	int rc = sql_exec(db, create_notes);
	if (rc != SQLITE_OK) {
		return rc;
	}

	const char *create_tags = "CREATE TABLE IF NOT EXISTS tags("
		"id INTEGER PRIMARY KEY, "
		"body TEXT UNIQUE NOT NULL"
		");";

	rc = sql_exec(db, create_tags);
	if (rc != SQLITE_OK) {
		return rc;
	}

	const char *create_note_tags = "CREATE TABLE IF NOT EXISTS note_tags("
		"id INTEGER PRIMARY KEY, "
		"note_id INTEGER NOT NULL, "
		"tag_id INTEGER NOT NULL, "
		"FOREIGN KEY(note_id) REFERENCES notes(id) ON DELETE CASCADE, "
		"FOREIGN KEY(tag_id) REFERENCES tags(id) ON DELETE CASCADE"
		")";

	rc = sql_exec(db, create_note_tags);
	if (rc != SQLITE_OK)
		return rc;

	const char *create_inbox = "CREATE TABLE IF NOT EXISTS inbox("
		"id INTEGER PRIMARY KEY, "
		"note_id INTEGER NOT NULL, "
		"FOREIGN KEY(note_id) REFERENCES notes(id) ON DELETE CASCADE"
		");";

	rc = sql_exec(db, create_inbox);
	if (rc != SQLITE_OK) {
		return rc;
	}

	const char *create_links = "CREATE TABLE IF NOT EXISTS links("
		"id INTEGER PRIMARY KEY, "
		"a_id INTEGER NOT NULL, "
		"b_id INTEGER NOT NULL, "
		"FOREIGN KEY(a_id) REFERENCES notes(id) ON DELETE CASCADE, "
		"FOREIGN KEY(b_id) REFERENCES notes(id) ON DELETE CASCADE"
		");";

	rc = sql_exec(db, create_links);
	if (rc != SQLITE_OK) {
		return rc;
	}

	return SQLITE_OK;
}

int
new(sqlite3 *db)
{
	char uuid[38];
	uuid_v4_gen(uuid);

	char zdir[200];
  char* homedir = getenv("HOME");
  if (homedir == NULL) {
  	homedir = getpwuid(getuid())->pw_dir;
	}

  strcpy(zdir, homedir);
  strcat(zdir, "/.zettelkasten/");
	strcat(zdir, uuid);

	char command[300];
	if (getenv("ZKC_EDITOR") != NULL) {
		sprintf(command, "$ZKC_EDITOR %s", zdir);
	} else if (getenv("EDITOR") != NULL) {
	  sprintf(command, "$EDITOR %s", zdir);
	} else if (getenv("VISUAL") != NULL) {
	  sprintf(command, "$VISUAL %s", zdir);
	} else {
	  sprintf(command, "vi %s", zdir);
	}

	if (system(command) != 0) {
		fprintf(stderr, "Unable to open note with editor!\n");
	  return 1;
	}

	FILE *f = fopen(zdir, "rb");
	if (!f) {
		return 0;
	}

	// TODO: Add error check
	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buffer = (char*)malloc(sizeof(char)*length);

	if (buffer) {
		fread(buffer, sizeof(char), length, f);
	}

	fclose(f);
	remove(zdir);

	int rc = SQLITE_OK;

	if (buffer) {
		char *sql = "INSERT INTO notes(uuid, body, hash) VALUES(?, ?, ?);";
		sqlite3_stmt *stmt;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			goto end;
		}
		
		char hash[65];
		sha256_string(buffer, hash);

		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, buffer, strlen(buffer), SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, hash, strlen(hash), SQLITE_STATIC);

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
	if (buffer != NULL) {
		free(buffer);
	}

	return rc;
}

static int
note_summary_callback(void *not_used, int argc, char **argv, char **col_names)
{
	char *uuid = argv[0];
	char *date = argv[1];
	char *body = argv[2];

	if (strlen(body) > 16) {
		body[16] = '\0';
	}

	// Replace newlines with spaces
	for (size_t i = 0; i < 16; i++)
		if (body[i] == '\n')
			body[i] = ' ';

	// Total width will be 80 chars
	printf("%s - %s - %s...\n", uuid, date, body);
	return 0;
}

int
inbox(sqlite3 *db, int head)
{
	char *sql;
	if (head == 1) { // head
		sql = "SELECT notes.uuid, notes.date, notes.body "
			"FROM notes "
			"INNER JOIN inbox "
			"ON inbox.note_id = notes.id "
			"ORDER BY date DESC "
			"LIMIT 1;";
	} else if (head == -1) { // tail
		sql = "SELECT notes.uuid, notes.date, notes.body "
			"FROM notes "
			"INNER JOIN inbox "
			"ON inbox.note_id = notes.id "
			"ORDER BY date ASC "
			"LIMIT 1;";
	} else { // whole inbox
		sql = "SELECT notes.uuid, notes.date, notes.body "
			"FROM notes "
			"INNER JOIN inbox "
			"ON inbox.note_id = notes.id "
			"ORDER BY date DESC;";
	}

	char *err_msg = 0;
	int rc;
	rc = sqlite3_exec(db, sql, note_summary_callback, 0, &err_msg);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query inbox\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}

	return rc;
}

int
view(sqlite3 *db, const char *uuid)
{
	char *sql;

	if (!strcmp(uuid, "head")) {
		sql = "SELECT body FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1;";
	} else if (!strcmp(uuid, "tail")) {
		sql = "SELECT body FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1;";
	} else {
		sql = "SELECT body FROM notes WHERE uuid = ? LIMIT 1;";
	}

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (strcmp(uuid, "head") && strcmp(uuid, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	}

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
	char *sql;
	if (!strcmp(uuid, "head")) {
		sql = "SELECT body FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1";
	} else if (!strcmp(uuid, "tail")) {
		sql = "SELECT body FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1";
	} else {
		sql = "SELECT body FROM notes WHERE uuid = ? LIMIT 1;";
	}

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (strcmp(uuid, "head") && strcmp(uuid, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	}

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_ROW) {
		fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	char zdir[200];
  char* homedir = getenv("HOME");
  if (homedir == NULL) {
  	homedir = getpwuid(getuid())->pw_dir;
	}

  strcpy(zdir, homedir);
  strcat(zdir, "/.zettelkasten/");
	strcat(zdir, uuid);

	FILE *fw = fopen(zdir, "wb");
	if (fw) {
		fputs((char *)sqlite3_column_text(stmt, 0), fw);
		fclose(fw);
	}

	sqlite3_finalize(stmt);

	char command[300];
	if (getenv("ZKC_EDITOR") != NULL) {
		sprintf(command, "$ZKC_EDITOR %s", zdir);
	} else if (getenv("EDITOR") != NULL) {
	  sprintf(command, "$EDITOR %s", zdir);
	} else if (getenv("VISUAL") != NULL) {
	  sprintf(command, "$VISUAL %s", zdir);
	} else {
	  sprintf(command, "vi %s", zdir);
	}

	if (system(command) != 0) {
		fprintf(stderr, "Unable to open note with editor!\n");
	  return 1;
	}

	FILE *fr = fopen(zdir, "rb");
	if (!fr) {
	  fprintf(stderr, "Unable to open temp file %s after edit!\n", zdir);
		return 1;
	}

	// TODO: Add error check
	fseek(fr, 0, SEEK_END);
	long length = ftell(fr);
	fseek(fr, 0, SEEK_SET);
	char *buffer = (char*)malloc(sizeof(char) * length);

	if (buffer) {
		fread(buffer, sizeof(char), length, fr);
	}

	fclose(fr);
	remove(zdir);

	rc = SQLITE_OK;

	if (buffer) {
		char *sql;
		if (!strcmp(uuid, "head")) {
			sql = "UPDATE notes SET body = ?, hash = ?, date = datetime() WHERE id = "
				"(SELECT notes.id FROM notes "
				"INNER JOIN inbox "
				"ON notes.id = inbox.note_id "
				"ORDER BY notes.date DESC "
				"LIMIT 1"
				")";
		} else if (!strcmp(uuid, "tail")) {
			sql = "UPDATE notes SET body = ?, hash = ?, date = datetime() WHERE id = "
				"(SELECT notes.id FROM notes "
				"INNER JOIN inbox "
				"ON notes.id = inbox.note_id "
				"ORDER BY notes.date ASC "
				"LIMIT 1"
				")";
		} else {
			sql = "UPDATE notes SET body = ?, hash = ?, date = datetime() WHERE uuid = ?;";
		}
		sqlite3_stmt *stmt;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			goto end;
		}

    char hash[65];
		sha256_string(buffer, hash);
		
		sqlite3_bind_text(stmt, 1, buffer, strlen(buffer), SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, hash, strlen(hash), SQLITE_STATIC);

		if (strcmp(uuid, "head") && strcmp(uuid, "tail")) {
			sqlite3_bind_text(stmt, 3, uuid, strlen(uuid), SQLITE_STATIC);
		}

		rc = sqlite3_step(stmt);

		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
			goto end;
		}

		sqlite3_finalize(stmt);
	}

end:
	if (buffer != NULL) {
		free(buffer);
	}

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

	if (buffer) {
		fread(buffer, sizeof(char), length, f);
	}

	fclose(f);

	int rc = SQLITE_OK;

	if (buffer) {
		char *sql = "INSERT INTO notes(uuid, body, hash) VALUES(?, ?, ?);";
		sqlite3_stmt *stmt;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			goto end;
		}

		char uuid[38];
		uuid_v4_gen(uuid);
		
		char hash[65];
		sha256_string(buffer, hash);

		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, buffer, strlen(buffer), SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, hash, strlen(hash), SQLITE_STATIC);

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
	if (buffer != NULL) {
		free(buffer);
	}

	return rc;
}

int
spit(sqlite3 *db, const char *uuid, const char *path)
{
	char *sql;
	if (!strcmp(uuid, "head")) {
		sql = "SELECT body FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1;";
	} else if (!strcmp(uuid, "tail")) {
		sql = "SELECT body FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1;";
	} else {
		sql = "SELECT body FROM notes WHERE uuid = ? LIMIT 1";
	}

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (strcmp(uuid, "head") && strcmp(uuid, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	}

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

int
search(sqlite3 *db, const char *search_type, const char *search_word)
{
	char *sql;
	char match_phrase[50];
	sprintf(match_phrase, "%%%s%%", search_word);

	if (!strcmp(search_type, "text")) {
		sql = "SELECT uuid, date, body "
			"FROM notes "
			"WHERE body LIKE ?;";
	} else if (!strcmp(search_type, "tag")) {
		sql = "SELECT uuid, date, body "
			"FROM notes "
			"WHERE notes.id IN "
			"(SELECT note_id "
			"FROM note_tags "
			"INNER JOIN tags "
			"ON note_tags.tag_id = tags.id "
			"WHERE tags.body LIKE ?);";
	} else {
		fprintf(stderr, "Invalid search type: %s\n", search_type);
		return 1;
	}

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_bind_text(stmt, 1, match_phrase, strlen(match_phrase), SQLITE_STATIC);

	while(1) {

		rc = sqlite3_step(stmt);

		if (rc == SQLITE_DONE) {
			break;
		}

		if (rc != SQLITE_ROW) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		char *uuid = (char *)sqlite3_column_text(stmt, 0);
		char *date = (char *)sqlite3_column_text(stmt, 1);
		char *body = (char *)sqlite3_column_text(stmt, 2);

		if (strlen(body) > 16) {
			body[16] = '\0';
		}

    // Replace newlines with spaces
		for (size_t i = 0; i < 16; i++) {
			if (body[i] == '\n') {
				body[i] = ' ';
			}
		}

		// Total width will be 80 chars
		printf("%s - %s - %s...\n", uuid, date, body);

	}

	sqlite3_finalize(stmt);
	return SQLITE_OK;
}

int
link_notes(sqlite3 *db, const char *uuid_a, const char *uuid_b)
{
	if (!strcmp(uuid_a, uuid_b)) {
		fprintf(stderr, "Not supposed to link a note to itself!");
		return SQLITE_OK;
	}

	char *sql;
	if (!strcmp(uuid_a, "head") && !strcmp(uuid_b, "tail")) {
		sql = "INSERT INTO links(a_id, b_id) "
			"VALUES("
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1), "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1)"
			");";
	} else if (!strcmp(uuid_a, "tail") && !strcmp(uuid_b, "head")) {
		sql = "INSERT INTO links(a_id, b_id) "
			"VALUES("
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1),"
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1)"
			");";
	} else if (!strcmp(uuid_a, "head")) {
		sql = "INSERT INTO links(a_id, b_id) "
			"VALUES("
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1), "
			"(SELECT id FROM notes WHERE uuid = ?)"
			");";
	} else if (!strcmp(uuid_b, "head")) {
		sql = "INSERT INTO links(a_id, b_id) "
			"VALUES("
			"(SELECT id FROM notes WHERE uuid = ?), "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1)"
			");";
	} else if (!strcmp(uuid_a, "tail")) {
		sql = "INSERT INTO links(a_id, b_id) "
			"VALUES("
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1), "
			"(SELECT id FROM notes WHERE uuid = ?)"
			");";
	} else if (!strcmp(uuid_b, "tail")) {
		sql = "INSERT INTO links(a_id, b_id) "
			"VALUES("
			"(SELECT id FROM notes WHERE uuid = ?), "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1)"
			");";
	} else {
		sql = "INSERT INTO links(a_id, b_id) "
			"VALUES("
			"(SELECT id FROM notes WHERE uuid = ? LIMIT 1), "
			"(SELECT id FROM notes WHERE uuid = ? LIMIT 1)"
			");";
	}

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (!strcmp(uuid_a, "head") && !strcmp(uuid_b, "tail")) {
		/* Leave this blank */
	} else if (!strcmp(uuid_a, "tail") && !strcmp(uuid_b, "head")) {
		/* Leave this blank */
	} else if (!strcmp(uuid_a, "head")) {
		sqlite3_bind_text(stmt, 1, uuid_b, strlen(uuid_b), SQLITE_STATIC);
	} else if (!strcmp(uuid_b, "head")) {
		sqlite3_bind_text(stmt, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);
	} else if (!strcmp(uuid_a, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid_b, strlen(uuid_b), SQLITE_STATIC);
	} else if (!strcmp(uuid_b, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);
	} else {
		sqlite3_bind_text(stmt, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, uuid_b, strlen(uuid_b), SQLITE_STATIC);
	}

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt);

	return rc;
}

int
links(sqlite3 *db, const char *uuid)
{
	printf("Link ->:\n");

	char *sql;
	if (!strcmp(uuid, "head")) {
		sql = "SELECT uuid, date, body "
			"FROM notes "
			"WHERE id = "
			"(SELECT links.b_id "
			"FROM links "
			"INNER JOIN notes "
			"ON links.a_id = notes.id "
			"WHERE notes.id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1));";
	} else if (!strcmp(uuid, "tail")) {
		sql = "SELECT uuid, date, body "
			"FROM notes "
			"WHERE id = "
			"(SELECT links.b_id "
			"FROM links "
			"INNER JOIN notes "
			"ON links.a_id = notes.id "
			"WHERE notes.id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1));";
	} else {
		sql = "SELECT uuid, date, body "
			"FROM notes "
			"WHERE id = "
			"(SELECT links.b_id "
			"FROM links "
			"INNER JOIN notes "
			"ON links.a_id = notes.id "
			"WHERE notes.uuid = ?);";
	}

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (strcmp(uuid, "head")) {
		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	}

	while(1) {

		rc = sqlite3_step(stmt);

		if (rc == SQLITE_DONE) {
			break;
		}

		if (rc != SQLITE_ROW) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		char *uuid = (char *)sqlite3_column_text(stmt, 0);
		char *date = (char *)sqlite3_column_text(stmt, 1);
		char *body = (char *)sqlite3_column_text(stmt, 2);

		if (strlen(body) > 16) {
			body[16] = '\0';
		}

    // Replace newlines with spaces
		for (size_t i = 0; i < 16; i++) {
			if (body[i] == '\n') {
				body[i] = ' ';
			}
		}

    // Total width will be 80 chars
		printf("%s - %s - %s...\n", uuid, date, body);

	}

	sqlite3_finalize(stmt);

	printf("Link <-:\n");

	char *sql2;

	if (!strcmp(uuid, "head")) {
		sql2 = "SELECT uuid, date, body "
			"FROM notes "
			"WHERE id = "
			"(SELECT links.a_id "
			"FROM links "
			"INNER JOIN notes "
			"ON links.b_id = notes.id "
			"WHERE notes.id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1));";
	} else if (!strcmp(uuid, "tail")) {
		sql2 = "SELECT uuid, date, body "
			"FROM notes "
			"WHERE id = "
			"(SELECT links.a_id "
			"FROM links "
			"INNER JOIN notes "
			"ON links.b_id = notes.id "
			"WHERE notes.id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1));";
	} else {
		sql2 = "SELECT uuid, date, body "
			"FROM notes "
			"WHERE id = "
			"(SELECT links.a_id "
			"FROM links "
			"INNER JOIN notes "
			"ON links.b_id = notes.id "
			"WHERE notes.uuid = ?);";
	}

	sqlite3_stmt *stmt2;
	rc = sqlite3_prepare_v2(db, sql2, -1, &stmt2, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (strcmp(uuid, "head") && strcmp(uuid, "tail")) {
		sqlite3_bind_text(stmt2, 1, uuid, strlen(uuid), SQLITE_STATIC);
	}

	while(1) {

		rc = sqlite3_step(stmt2);

		if (rc == SQLITE_DONE) {
			break;
		}

		if (rc != SQLITE_ROW) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		char *uuid = (char *)sqlite3_column_text(stmt2, 0);
		char *date = (char *)sqlite3_column_text(stmt2, 1);
		char *body = (char *)sqlite3_column_text(stmt2, 2);

		if (strlen(body) > 16) {
			body[16] = '\0';
		}

    // Replace newlines with spaces
		for (size_t i = 0; i < 16; i++) {
			if (body[i] == '\n') {
				body[i] = ' ';
			}
		}

    // Total width will be 80 chars
		printf("%s - %s - %s...\n", uuid, date, body);
	}

	sqlite3_finalize(stmt2);
	return SQLITE_OK;
}

int
tag(sqlite3 *db, const char *uuid, const char *tag_body)
{
	char *sql = "INSERT OR IGNORE INTO tags(body) VALUES(?);";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_bind_text(stmt, 1, tag_body, strlen(tag_body), SQLITE_STATIC);

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "INSERT TAG - execution failed: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt);

	int tag_id = sqlite3_last_insert_rowid(db);

	// Get tag id if tag already exists
	if (!tag_id) {
		char *sql2 = "SELECT id FROM tags WHERE body = ? LIMIT 1;";
		sqlite3_stmt *stmt2;
		rc = sqlite3_prepare_v2(db, sql2, -1, &stmt2, 0);

		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		sqlite3_bind_text(stmt2, 1, tag_body, strlen(tag_body), SQLITE_STATIC);

		rc = sqlite3_step(stmt2);

		if (rc != SQLITE_ROW) {
			fprintf(stderr, "SELECT tag - execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		tag_id = sqlite3_column_int(stmt2, 0);

		sqlite3_finalize(stmt2);
	}

	char *sql3;
	if (!strcmp(uuid, "head")) {
		sql3 = "INSERT INTO note_tags(note_id, tag_id) "
		"VALUES("
		"(SELECT notes.id FROM notes "
		"INNER JOIN inbox ON notes.id = inbox.note_id "
		"ORDER BY notes.date DESC LIMIT 1), "
		"?"
		");";
	} else if (!strcmp(uuid, "tail")) {
		sql3 = "INSERT INTO note_tags(note_id, tag_id) "
		"VALUES("
		"(SELECT notes.id FROM notes "
		"INNER JOIN inbox ON notes.id = inbox.note_id "
		"ORDER BY notes.date ASC LIMIT 1), "
		"?"
		");";
	} else {
		sql3 = "INSERT INTO note_tags(note_id, tag_id) "
		"VALUES("
		"(SELECT id FROM notes WHERE uuid = ? LIMIT 1), "
		"?"
		");";
	}

	sqlite3_stmt *stmt3;
	rc = sqlite3_prepare_v2(db, sql3, -1, &stmt3, 0);

	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (!strcmp(uuid, "head") || !strcmp(uuid, "tail")) {
		sqlite3_bind_int(stmt3, 1, tag_id);
	} else {
		sqlite3_bind_text(stmt3, 1, uuid, strlen(uuid), SQLITE_STATIC);
		sqlite3_bind_int(stmt3, 2, tag_id);
	}

	rc = sqlite3_step(stmt3);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "INSERT NOTE TAG - execution failed: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt3);

	return rc;
}

int
tags(sqlite3 *db, const char *uuid)
{
	char *sql;
	sqlite3_stmt *stmt;
	int rc;

	if (uuid) {
		if (!strcmp(uuid, "head")) {
			sql = "SELECT tags.body "
				"FROM tags "
				"INNER JOIN note_tags "
				"ON tags.id = note_tags.tag_id "
				"INNER JOIN notes "
				"ON notes.id = note_tags.note_id "
				"WHERE notes.id = "
				"(SELECT notes.id FROM notes "
				"INNER JOIN inbox "
				"ON notes.id = inbox.note_id "
				"ORDER BY notes.date DESC "
				"LIMIT 1);";

			rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

			if (rc != SQLITE_OK) {
				fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
				return rc;
			}
		} else if (!strcmp(uuid, "tail")) {
			sql = "SELECT tags.body "
				"FROM tags "
				"INNER JOIN note_tags "
				"ON tags.id = note_tags.tag_id "
				"INNER JOIN notes "
				"ON notes.id = note_tags.note_id "
				"WHERE notes.id = "
				"(SELECT notes.id FROM notes "
				"INNER JOIN inbox "
				"ON notes.id = inbox.note_id "
				"ORDER BY notes.date ASC "
				"LIMIT 1);";

			rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

			if (rc != SQLITE_OK) {
				fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
				return rc;
			}
		} else {
			sql = "SELECT tags.body "
				"FROM tags "
				"INNER JOIN note_tags "
				"ON tags.id = note_tags.tag_id "
				"INNER JOIN notes "
				"ON notes.id = note_tags.note_id "
				"WHERE notes.uuid = ?;";

			rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

			if (rc != SQLITE_OK) {
				fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
				return rc;
			}

			sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
		}
	} else {
		sql = "SELECT tags.body FROM tags;";

		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			return rc;
		}

	}

	while(1) {

		rc = sqlite3_step(stmt);

		if (rc == SQLITE_DONE) {
			break;
		}

		if (rc != SQLITE_ROW) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		printf("%s\n", (char *)sqlite3_column_text(stmt, 0));

	}

	sqlite3_finalize(stmt);

	return rc;
}

int
delete_note(sqlite3 *db, const char *uuid)
{
	char *sql;
        int rc = SQLITE_OK;

	if (!strcmp(uuid, "head")) {
		sql = "DELETE FROM inbox WHERE note_id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1);";
	} else if (!strcmp(uuid, "tail")) {
		sql = "DELETE FROM inbox WHERE note_id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1);";
	} else {
		sql = "DELETE FROM inbox WHERE note_id = (SELECT id FROM notes WHERE uuid = ? LIMIT 1);";
	}

	sqlite3_stmt *stmt;
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (strcmp(uuid, "head") && strcmp(uuid, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	}

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt);        

        char *sql2;
	if (!strcmp(uuid, "head")) {
		sql2 = "DELETE FROM notes WHERE id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1);";
	} else if (!strcmp(uuid, "tail")) {
		sql2 = "DELETE FROM notes WHERE id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1);";
	} else {
		sql2 = "DELETE FROM notes WHERE uuid = ?;";
	}

	sqlite3_stmt *stmt2;
	rc = sqlite3_prepare_v2(db, sql2, -1, &stmt2, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (strcmp(uuid, "head") && strcmp(uuid, "tail")) {
		sqlite3_bind_text(stmt2, 1, uuid, strlen(uuid), SQLITE_STATIC);
	}

	rc = sqlite3_step(stmt2);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt2);

	return rc;
}

int
delete_tag(sqlite3 *db, const char *tag_body)
{
	char *sql = "DELETE FROM tags WHERE body = ?;";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_bind_text(stmt, 1, tag_body, strlen(tag_body), SQLITE_STATIC);

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt);

	return rc;
}

int
delete_note_tag(sqlite3 *db, const char *uuid, const char *tag_body)
{
	char *sql;
	if (!strcmp(uuid, "head")) {
		sql = "DELETE FROM note_tags "
			"WHERE note_id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date DESC "
			"LIMIT 1) "
			"AND tag_id = "
			"(SELECT tags.id FROM tags "
			"WHERE tags.body = ? "
			"LIMIT 1);";
	} else if (!strcmp(uuid, "tail")) {
		sql = "DELETE FROM note_tags "
			"WHERE note_id = "
			"(SELECT notes.id FROM notes "
			"INNER JOIN inbox "
			"ON notes.id = inbox.note_id "
			"ORDER BY notes.date ASC "
			"LIMIT 1) "
			"AND tag_id = "
			"(SELECT tags.id FROM tags "
			"WHERE tags.body = ? "
			"LIMIT 1);";
	} else {
		sql = "DELETE FROM note_tags "
			"WHERE note_id = "
			"(SELECT notes.id FROM notes "
			"WHERE uuid = ? "
			"LIMIT 1) "
			"AND tag_id = "
			"(SELECT tags.id FROM tags "
			"WHERE body = ? "
			"LIMIT 1); ";
	}

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (!strcmp(uuid, "head") || !strcmp(uuid, "tail")) {
		sqlite3_bind_text(stmt, 1, tag_body, strlen(tag_body), SQLITE_STATIC);
	} else {
		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, tag_body, strlen(tag_body), SQLITE_STATIC);
	}

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt);

	return rc;
}

int
delete_link(sqlite3 *db, const char *uuid_a, const char *uuid_b)
{
	if (!strcmp(uuid_a, uuid_b)) {
		fprintf(stderr, "Notes don't link to themselves!");
		return SQLITE_OK;
	}

	char *sql;
	if (!strcmp(uuid_a, "head") && !strcmp(uuid_b, "tail")) {
		sql = "DELETE FROM links "
			"WHERE a_id = (SELECT notes.id FROM notes INNER JOIN inbox ON notes.id = inbox.note_id ORDER BY notes.date DESC LIMIT 1) "
			"AND b_id = (SELECT notes.id FROM notes INNER JOIN inbox ON notes.id = inbox.note_id ORDER BY notes.date ASC LIMIT 1);";
	} else if (!strcmp(uuid_a, "tail") && !strcmp(uuid_b, "head")) {
		sql = "DELETE FROM links "
			"WHERE a_id = (SELECT notes.id FROM notes INNER JOIN inbox ON notes.id = inbox.note_id ORDER BY notes.date ASC LIMIT 1) "
			"AND b_id = (SELECT notes.id FROM notes INNER JOIN inbox ON notes.id = inbox.note_id ORDER BY notes.date DESC LIMIT 1);";
	} else if (!strcmp(uuid_a, "head")) {
		sql = "DELETE FROM links "
			"WHERE a_id = (SELECT notes.id FROM notes INNER JOIN inbox ON notes.id = inbox.note_id ORDER BY notes.date DESC LIMIT 1) "
			"AND b_id = (SELECT id FROM notes WHERE uuid = ? LIMIT 1);";
	} else if (!strcmp(uuid_b, "head")) {
		sql = "DELETE FROM links "
			"WHERE b_id = (SELECT notes.id FROM notes INNER JOIN inbox ON notes.id = inbox.note_id ORDER BY notes.date DESC LIMIT 1) "
			"AND a_id = (SELECT id FROM notes WHERE uuid = ? LIMIT 1);";
	} else if (!strcmp(uuid_a, "tail")) {
		sql = "DELETE FROM links "
			"WHERE a_id = (SELECT notes.id FROM notes INNER JOIN inbox ON notes.id = inbox.note_id ORDER BY notes.date ASC LIMIT 1) "
			"AND b_id = (SELECT id FROM notes WHERE uuid = ? LIMIT 1);";
	} else if (!strcmp(uuid_b, "tail")) {
		sql = "DELETE FROM links "
			"WHERE b_id = (SELECT notes.id FROM notes INNER JOIN inbox ON notes.id = inbox.note_id ORDER BY notes.date ASC LIMIT 1) "
			"AND a_id = (SELECT id FROM notes WHERE uuid = ? LIMIT 1);";
	} else {
		sql = "DELETE FROM links "
			"WHERE a_id = (SELECT id FROM notes WHERE uuid = ? LIMIT 1) "
			"AND b_id = (SELECT id FROM notes WHERE uuid = ? LIMIT 1);";
	}

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (!strcmp(uuid_a, "head") && !strcmp(uuid_b, "tail")) {
		/* Leave blank */
	} else if (!strcmp(uuid_a, "tail") && !strcmp(uuid_b, "head")) {
		/* Leave blank */
	} else if (!strcmp(uuid_a, "head")) {
		sqlite3_bind_text(stmt, 1, uuid_b, strlen(uuid_b), SQLITE_STATIC);
	} else if (!strcmp(uuid_b, "head")) {
		sqlite3_bind_text(stmt, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);	
	} else if (!strcmp(uuid_a, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid_b, strlen(uuid_b), SQLITE_STATIC);
	} else if (!strcmp(uuid_b, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);
	} else {
		sqlite3_bind_text(stmt, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, uuid_b, strlen(uuid_b), SQLITE_STATIC);
	}

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt);

	return rc;
}

int
archive(sqlite3 *db, const char *uuid)
{
	char *sql;
	sqlite3_stmt *stmt;
	if (!strcmp(uuid, "head")) {
		sql = "DELETE FROM inbox WHERE note_id = "
			"(SELECT note_id FROM inbox "
			"INNER JOIN notes "
			"ON note_id = notes.id "
			"ORDER BY notes.date DESC LIMIT 1)";
	} else if (!strcmp(uuid, "tail")) {
		sql = "DELETE FROM inbox WHERE note_id = "
			"(SELECT note_id FROM inbox "
			"INNER JOIN notes "
			"ON note_id = notes.id "
			"ORDER BY notes.date ASC LIMIT 1)";
	} else {
		sql = "DELETE FROM inbox WHERE note_id = "
			"(SELECT id FROM notes WHERE uuid = ?);";
	}

	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	if (strcmp(uuid, "head") && strcmp(uuid, "tail")) {
		sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	}

	rc = sqlite3_step(stmt);

	if (rc != SQLITE_DONE) {
		fprintf(stderr, "execution failed: %s", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_finalize(stmt);

	return rc;
}

static int
diff_tags_callback(void *data, int argc, char **argv, char **col_names)
{
	sqlite3 *db = (sqlite3 *)data;

	char *body = argv[0];
	
	char *sql = "SELECT 1 FROM tags WHERE body = ?";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}
	
	sqlite3_bind_text(stmt, 1, body, strlen(body), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	// Tag doesn't exist in db
	if (rc == SQLITE_DONE) {
	  printf("%s\n", body);
	}
	
	return SQLITE_OK;
}

static int
diff_notes_callback(void *data, int argc, char **argv, char **col_names)
{
	sqlite3 *db = (sqlite3 *)data;
	
	char *uuid = argv[0];
	char *hash = argv[1];
	
	char *sql = "SELECT 1 FROM notes WHERE uuid = ? AND hash = ?;";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}
	
	sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, hash, strlen(hash), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	// Note either doesn't exist or is different in db
	if (rc == SQLITE_DONE) {
		printf("%s\n", uuid);
	}
	
	return SQLITE_OK;
}

static int
diff_note_tags_callback(void *data, int argc, char **argv, char **col_names)
{
	sqlite3 *db = (sqlite3 *)data;
	
	char *uuid = argv[0];
	char *body = argv[1];
	
	char *sql = "SELECT 1 FROM note_tags "
		"INNER JOIN notes ON note_tags.note_id = notes.id "
		"INNER JOIN tags ON note_tags.tag_id = tags.id "
		"WHERE notes.uuid = ? AND tags.body = ?;";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}
		
	sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, body, strlen(body), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	// Note tag doesn't exist
	if (rc == SQLITE_DONE) {
		printf("%s %s\n", body, uuid);
	}
	
	return SQLITE_OK;
}

static int
diff_note_links_callback(void *data, int argc, char **argv, char **col_names)
{
	sqlite3 *db = (sqlite3 *)data;
	
	char *uuid_a = argv[0];
	char *uuid_b = argv[1];
	
	char *sql = "SELECT 1 FROM links "
	  "INNER JOIN notes notes_a ON links.a_id = notes_a.id "
		"INNER JOIN notes notes_b ON links.b_id = notes_b.id "
		"WHERE notes_a.uuid = ? AND notes_b.uuid = ?;";
		
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}
	
	sqlite3_bind_text(stmt, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, uuid_b, strlen(uuid_b), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	// Note link doesn't exist
	if (rc == SQLITE_DONE) {
		printf("%s %s\n", uuid_a, uuid_b);
	}
	
	return SQLITE_OK;
}

int
diff(sqlite3 *db, const char *path)
{
  sqlite3 *db2;
	int rc = 1;
	char *err_msg = NULL;

	rc = sqlite3_open_v2(path, &db2, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot open zkc database: %s\n", path);
		goto end;
	}

	printf("notes diff:\n");
	
	char *sql = "SELECT uuid, hash FROM notes;";
	rc = sqlite3_exec(db2, sql, diff_notes_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query notes\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}

	printf("tags diff:\n");
	
	sql = "SELECT body FROM tags;";
	
	rc = sqlite3_exec(db2, sql, diff_tags_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query tags\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	
	printf("note tags diff:\n");
	
	sql = "SELECT notes.uuid, tags.body FROM note_tags "
		"INNER JOIN notes ON note_tags.note_id = notes.id "
		"INNER JOIN tags ON note_tags.tag_id = tags.id;";
	
	rc = sqlite3_exec(db2, sql, diff_note_tags_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query note tags\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	
	printf("note links diff\n");
	
	sql = "SELECT notes_a.uuid, notes_b.uuid FROM links "
		"INNER JOIN notes notes_a ON links.a_id = notes_a.id "
		"INNER JOIN notes notes_b ON links.b_id = notes_b.id;";
		
	rc = sqlite3_exec(db2, sql, diff_note_links_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to query note links\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}

end:
  sqlite3_close(db2);
	return rc;
}

static int
merge_notes_callback(void *data, int argc, char **argv, char **col_names)
{
	sqlite3 *db = (sqlite3 *)data;
	
	char *uuid = argv[0];
	char *hash = argv[1];
	char *body = argv[2];
	char *date = argv[3];
	char *date_temp = argv[4];
	
	int date_int = atoi(date_temp);
	
	char *sql = "SELECT hash, unixepoch(date) FROM notes WHERE uuid = ? LIMIT 1;";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}

	sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	if (rc == SQLITE_DONE) {
		sql = "INSERT INTO notes (uuid, hash, body, date) VALUES (?, ?, ?, ?);";
		sqlite3_stmt *stmt2;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt2, 0);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			return rc;
		}
		
		sqlite3_bind_text(stmt2, 1, uuid, strlen(uuid), SQLITE_STATIC);
		sqlite3_bind_text(stmt2, 2, hash, strlen(hash), SQLITE_STATIC);
		sqlite3_bind_text(stmt2, 3, body, strlen(body), SQLITE_STATIC);
		sqlite3_bind_text(stmt2, 4, date, strlen(date), SQLITE_STATIC);
		
		rc = sqlite3_step(stmt2);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}
		
		sqlite3_finalize(stmt2);
		return SQLITE_OK;
	
	} else if (rc == SQLITE_ROW) {
		char *hash2 = (char *)sqlite3_column_text(stmt, 0);
		char *date_temp2 = (char *)sqlite3_column_text(stmt, 1);
		
		int date_int2 = atoi(date_temp2);
		
		// Hashes are equivalent
		if (!strcmp(hash, hash2)) {
			return SQLITE_OK;
		} else if (date_int2 >= date_int) {
			return SQLITE_OK;
		} else {

			sql = "UPDATE notes SET body = ?, hash = ?, date = ? WHERE uuid = ?;";
		
			sqlite3_stmt *stmt2;
			rc = sqlite3_prepare_v2(db, sql, -1, &stmt2, 0);
			if (rc != SQLITE_OK) {
				fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
				return rc;
			}
		
			sqlite3_bind_text(stmt2, 1, body, strlen(body), SQLITE_STATIC);
			sqlite3_bind_text(stmt2, 2, hash, strlen(hash), SQLITE_STATIC);
			sqlite3_bind_text(stmt2, 3, date, strlen(date), SQLITE_STATIC);	
			sqlite3_bind_text(stmt2, 4, uuid, strlen(uuid), SQLITE_STATIC);

			rc = sqlite3_step(stmt2);
			if (rc != SQLITE_DONE) {
				fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
				return rc;
			}
		
			sqlite3_finalize(stmt2);
			return SQLITE_OK;
		}
	}
	
	return rc;
}

static int
merge_tags_callback(void *data, int argc, char **argv, char **col_names)
{

  sqlite3 *db = (sqlite3 *)data;

  char *body = argv[0];
	
	char *sql = "SELECT 1 FROM tags WHERE body = ?;";
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}
	
	sqlite3_bind_text(stmt, 1, body, strlen(body), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	if (rc == SQLITE_DONE) {
		sql = "INSERT INTO tags (body) VALUES (?);";
		
		sqlite3_stmt *stmt2;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt2, 0);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			return rc;
		}
		
		sqlite3_bind_text(stmt2, 1, body, strlen(body), SQLITE_STATIC);
		
		rc = sqlite3_step(stmt2);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}
		
		sqlite3_finalize(stmt2);
	}
	
	return SQLITE_OK;
}

static int
merge_note_tags_callback(void *data, int argc, char **argv, char **col_names)
{
	sqlite3 *db = (sqlite3 *)data;
	
	char *uuid = argv[0];
	char *body = argv[1];
	
	char *sql = "SELECT 1 FROM note_tags "
		"INNER JOIN notes ON note_tags.note_id = notes.id "
		"INNER JOIN tags ON note_tags.tag_id = tags.id "
		"WHERE notes.uuid = ? AND tags.body = ?;";

	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}
	
	sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, body, strlen(body), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	if (rc == SQLITE_DONE) {
		sql = "INSERT INTO note_tags (note_id, tag_id) VALUES ("
		  "(SELECT id FROM notes WHERE uuid = ? LIMIT 1), "
			"(SELECT id FROM tags WHERE body = ? LIMIT 1));";
		
		sqlite3_stmt *stmt2;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt2, 0);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			return rc;
		}
		
		sqlite3_bind_text(stmt2, 1, uuid, strlen(uuid), SQLITE_STATIC);
		sqlite3_bind_text(stmt2, 2, body, strlen(body), SQLITE_STATIC);
		
		rc = sqlite3_step(stmt2);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		sqlite3_finalize(stmt2);
	}
	
	return SQLITE_OK;
}

static int
merge_links_callback(void *data, int argc, char **argv, char **col_names)
{
	sqlite3 *db = (sqlite3 *)data;
	
	char *uuid_a = argv[0];
	char *uuid_b = argv[1];
	
	char *sql = "SELECT 1 FROM links "
	  "INNER JOIN notes notes_a ON links.a_id = notes_a.id "
		"INNER JOIN notes notes_b ON links.b_id = notes_b.id "
		"WHERE notes_a.uuid = ? AND notes_b.uuid = ?;";
	
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}
	
	sqlite3_bind_text(stmt, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, uuid_b, strlen(uuid_b), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	if (rc == SQLITE_DONE) {
		sql = "INSERT INTO links (a_id, b_id) VALUES ("
		  "(SELECT id FROM notes WHERE uuid = ? LIMIT 1),"
			"(SELECT id FROM notes WHERE uuid = ? LIMIT 1));";
			
		sqlite3_stmt *stmt2;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt2, 0);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		sqlite3_bind_text(stmt2, 1, uuid_a, strlen(uuid_a), SQLITE_STATIC);
		sqlite3_bind_text(stmt2, 2, uuid_b, strlen(uuid_b), SQLITE_STATIC);
		
		rc = sqlite3_step(stmt2);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}
		
		sqlite3_finalize(stmt2);
	}
	
	return SQLITE_OK;
}

static int
merge_inbox_callback(void *data, int argc, char **argv, char **col_names)
{
	sqlite3 *db = (sqlite3 *)data;
	
	char *uuid = argv[0];
	
	char *sql = "SELECT 1 FROM inbox "
	  "INNER JOIN notes ON inbox.note_id = notes.id "
		"WHERE notes.uuid = ?;";
	
	sqlite3_stmt *stmt;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
		return rc;
	}
	
	sqlite3_bind_text(stmt, 1, uuid, strlen(uuid), SQLITE_STATIC);
	
	rc = sqlite3_step(stmt);
	
	if (rc == SQLITE_DONE) {
	  sql = "INSERT INTO inbox (note_id) VALUES ("
		  "(SELECT id FROM notes WHERE uuid = ? LIMIT 1));";
		
    sqlite3_stmt *stmt2;
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt2, 0);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
			return rc;
		}

		sqlite3_bind_text(stmt2, 1, uuid, strlen(uuid), SQLITE_STATIC);
		
		rc = sqlite3_step(stmt2);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "execution failed: %s\n", sqlite3_errmsg(db));
			return rc;
		}
	}
	
	return SQLITE_OK;
}

int
merge(sqlite3 *db, const char *path)
{
	sqlite3 *db2;
	int rc = 1;
	char *err_msg = NULL;

	rc = sqlite3_open_v2(path, &db2, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cannot open zkc database: %s\n", path);
		goto end;
	}
	
	// merge notes
	char *sql = "SELECT uuid, hash, body, date, unixepoch(date) FROM notes;";
	rc = sqlite3_exec(db2, sql, merge_notes_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to merge notes\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	
	// merge tags
	sql = "SELECT body FROM tags;";
	rc = sqlite3_exec(db2, sql, merge_tags_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to merge tags\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	
	// merge note tags
	sql = "SELECT notes.uuid, tags.body FROM note_tags "
	  "INNER JOIN notes ON note_tags.note_id = notes.id "
		"INNER JOIN tags ON note_tags.tag_id = tags.id;";
	rc = sqlite3_exec(db2, sql, merge_note_tags_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to merge note tags\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	
	// merge links
	sql = "SELECT notes_a.uuid, notes_b.uuid FROM links "
	  "INNER JOIN notes notes_a ON links.a_id = notes_a.id "
		"INNER JOIN notes notes_b ON links.b_id = notes_b.id;";
	rc = sqlite3_exec(db2, sql, merge_links_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to merge note tags\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	
	// merge inbox
	sql = "SELECT notes.uuid FROM inbox "
	  "INNER JOIN notes ON inbox.note_id = notes.id;";
	rc = sqlite3_exec(db2, sql, merge_inbox_callback, db, &err_msg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Failed to merge inbox\n");
		fprintf(stderr, "SQL Error: %s\n", err_msg);
		sqlite3_free(err_msg);
	}
	
end:
	sqlite3_close(db2);
	return rc;
}
