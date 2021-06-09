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
			printf("zkc tables created\n");
		} else if (!strcmp(argv[1], "inbox")) {
			printf("Inbox:\n");
			rc = inbox(db);
			if (rc != SQLITE_OK)
				goto end;
		} else if (!strcmp(argv[1], "new")) {
			rc = new(db);
			if (rc != SQLITE_OK)
				goto end;
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
