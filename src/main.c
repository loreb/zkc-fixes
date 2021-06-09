#include <stdio.h>
#include <sqlite3.h>
#include "app.h"

int
main(int argc, char **argv)
{
	sqlite3 *db;
	int rc, err = 1;

	rc = open_db(&db);
	if (rc != SQLITE_OK)
		goto end;

	rc = create_tables(db);
	if (rc != SQLITE_OK)
		goto end;

	// no error
	err = 0;

end:

	sqlite3_close(db);
	return err;
}
