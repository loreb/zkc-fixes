# zkc

# Build

    cd zkc
    meson build
    ninja -C build

# Install

    sudo ninja -C build install

## Alpine

    doas apk add sqlite-dev openssl-dev

## Debian

    sudo apt install libsqlite3-dev libssl-dev

## FreeBSD

    doas pkg install sqlite3 openssl

# Usage

    zkc

## What?

Zettelkasten Tool in C. A featured CLI note taking tool that tries to strictly adhere to Zettelkasten method.

Recommended reading - [link](https://luhmann.surge.sh/communicating-with-slip-boxes)

The key idea is that the Zettelkasten method is more than a note taking system. It is a communication system.

## How?

zkc stores notes, tags, and links in a sqlite database stored at ~HOME/.local/zkc/zkc.db.

## Workflow

The recommended workflow is to create a note. All new notes are added to the inbox. The most recent note
in the inbox can be referenced as "head". The oldest note in the inbox can be referenced as "tail".

Example:

    zkc view head

The point is to be able to save notes as inspiration strikes. Then you can later go through
the inbox to either tag, link, or delete a note. Once you are happy, you can archive the note
and move it out of the inbox. While order isn't necessary, working on the head or tail note is the
most convenient. Using the keywords prevents you from having to copy and paste uuid's constantly.

### Example Workflow

    zkc init
    zkc new
    zkc inbox
    zkc view head
    zkc tag head foobar
    zkc tags head
    zkc archive head

## Searching

Once notes are moved out of the inbox and into the archive you'll still be able to search for it.

    zkc search foobar

This will return all the notes that have the word "foobar".

There are two types of searches: text matching and tag matching. The default is text as seen above.
Which is equivalent to:

    zkc search text foobar

To search for all notes that are tagged with foobar.

    zkc search tag foobar

## Editor

When opening an editor zkc follows the same process as git. This means it will try to see if
EDITOR or VISUAL are set. If not, then it will fall back to vi. This is similar to how git works.

If ZKC_EDITOR is set it will take precedence over all other options.

## Merging

The downside of using sqlite as the storage layer for zkc is that merging notes
from one computer to the other doesn't work. You would overwrite data with the
new copy of the database. Note systems that use a file per note don't suffer
from this problem in the same way. One can store the notes in a dropbox or nextcloud
and the system would sync the different files together. 

In order to work around this problem, I implemented a very basic merging and 
diff strategy. All notes have an associated hash of the note body. The merging
algorithm will check if a uuid exists in both databases. If it doesn't exist 
in both, it will add it to the database that is being merged into. Merges are 
not bidirectional. If the uuid exists in both databases, it will check the hash. 
If the hash is different, it will then check if the timestamp is greater. If the
timestamp is greater in the other database it will update the note in the 
database that is being merged into. If the timestamp is less, it will keep the 
existing note unchanged.

The workflow when merging looks like this:

    zkc diff other_zkc.db

The diff command will display any mergeable differences with other_zkc.db.
If there are mergeable differences you can then run the merge command:

    zkc merge other_zkc.db

After this if you run the diff command again you should see no differences.

One thing to keep in mind with this strategy is that deletes won't persist after
a merge, if the database that the merge is coming from still has that note, tag, or link.
The recommended workaround is after a delete, one should overwrite all copies of the database
with that version. This is a downside of the simple merging strategy. The tradeoff being made
to prioritize not accidentally deleting data.

## Remote Backups

Remote backups can be acheived easily with a few shell scripts and ssh.

Here is a script to push the local copy of the database:

    #! /bin/sh

    scp ~/.local/zkc/zkc.db foo@example.com:~

Here is a script to pull a remote copy locally:

    #! /bin/sh

    scp foo@example.com:~/zkc.db zkc.db

The idea is that you pull a remote copy locally, but don't overwrite your local copy. To get updates from remote, run the merge command.

# License

GPLv3