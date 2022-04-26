# zkc

# Build

    cd zkc
    meson build
    ninja -C build

# Install

    sudo ninja -C build install

## Alpine

    doas apk add sqlite-dev util-linux-dev

## Debian

    sudo apt install libsqlite3-dev uuid-dev

## FreeBSD

    doas pkg install sqlite3 e2fsprogs-libuuid

# Usage

    zkc

## What?

Zettelkasten Tool in C. A featured CLI note taking tool that tries to strictly adhere to Zettelkasten method.

Recommended reading - [link](https://luhmann.surge.sh/communicating-with-slip-boxes)

The key idea is that the Zettelkasten method is more than a note taking system. It is a communication system.

## How?

zkc stores notes, tags, and links in a sqlite database stored at ~HOME/.zettelkasten/zkc.db.

## Workflow

The recommended workflow is to create a note that is added to the inbox. The most recent note
in the inbox can be referenced as "head".

Ex:

    zkc view head

The point is to be able to save notes as inspiration strikes. Then you can later go through
the inbox to either tag, link, or delete a note. Once you are happy, you can archive the note
and move it out of the inbox. While order isn't necessary, working on the head note is the
most convenient. This way you don't have to copy and paste uuid's constantly.

## Example Workflow

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

There are two types of searches text matching and tag matching. The default is text as seen above.

    zkc search text foobar

To search for all notes that are tagged with foobar.

    zkc search tag foobar

## Editor

When opening an editor zkc follows the same process as git. This means it will try to see if
EDITOR or VISUAL are set. If not, then it will fall back to vi.

If ZKC_EDITOR is set it will take precedence over all other options.

# License

GPLv3