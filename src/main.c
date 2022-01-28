#include <stdio.h>
#include <sqlite3.h>
#include <string.h>
#include "app.h"

int
main(int argc, char **argv)
{
	sqlite3 *db;
	int rc, err = 1;

	rc = open_db(&db);
	if (rc != SQLITE_OK)
		goto end;

	if (argc == 2) {
		if (!strcmp(argv[1], "help")) {
			help();
		} else if (!strcmp(argv[1], "init")) {
			rc = create_tables(db);
			if (rc != SQLITE_OK)
				goto end;
			printf("zkc initialized\n");
		} else if (!strcmp(argv[1], "inbox")) {
			printf("Inbox:\n");
			rc = inbox(db, 0);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "head")) {
			rc = inbox(db, 1);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "tail")) {
			rc = inbox(db, -1);
			if (rc != SQLITE_OK)
				goto end;			
		} else if (!strcmp(argv[1], "new")) {
			rc = new(db);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "tags")) {
			rc = tags(db, NULL);
			if (rc != SQLITE_OK)
				goto end;
		} else {
			printf("Invalid command or missing arguments: %s\n", argv[1]);
		}
	} else if (argc == 3) {
		if (!strcmp(argv[1], "view")) {
			rc = view(db, argv[2]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "edit")) {
			rc = edit(db, argv[2]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "slurp")) {
			rc = slurp(db, argv[2]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "search")) {
			rc = search(db, "text", argv[2]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "links")) {
			rc = links(db, argv[2]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "tags")) {
			rc = tags(db, argv[2]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "delete")) {
			rc = delete_note(db, argv[2]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "archive")) {
			rc = archive(db, argv[2]);
			if (rc != SQLITE_OK)
				goto end;
		} else {
			printf("Invalid command: %s\n", argv[1]);
		}
	} else if (argc == 4) {
		if (!strcmp(argv[1], "spit")) {
			rc = spit(db, argv[2], argv[3]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "search")) {
			rc = search(db, argv[2], argv[3]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "link")) {
			rc = link_notes(db, argv[2], argv[3]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "tag")) {
			rc = tag(db, argv[2], argv[3]);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "delete")) {
			if (!strcmp(argv[2], "note")) {
				rc = delete_note(db, argv[3]);
				if (rc != SQLITE_OK)
					goto end;				
			} else if (!strcmp(argv[2], "tag")) {
				rc = delete_tag(db, argv[3]);
				if (rc != SQLITE_OK)
					goto end;
			} else {
				printf("Invalid delete type: %s\n", argv[2]);
			}
		} else {
			printf("Invalid command: %s\n", argv[1]);
		}
	} else if (argc == 5) {
		if (!strcmp(argv[1], "delete")) {
			if (!strcmp(argv[2], "link")) {
				rc = delete_link(db, argv[3], argv[4]);
				if (rc != SQLITE_OK)
					goto end;
			} else if (!strcmp(argv[2], "note_tag")) {
				rc = delete_note_tag(db, argv[3], argv[4]);
				if (rc != SQLITE_OK)
					goto end;
			} else {
				printf("Invalid delete type: %s\n", argv[2]);
			}
		} else {
			printf("Invalid command: %s\n", argv[1]);			
		}
	} else {
		help();
	}

	// no error
	err = 0;

end:

	sqlite3_close(db);
	return err;
}
