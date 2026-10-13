# Sync via browser (Kermit server)

F1 -> "Sync via browser" turns Draftling into a Kermit file server on
its serial console, so a computer connected to the device's USB port
can list, download, upload, rename and delete the files on the SD
card. No WiFi is needed.

Kermit is a long-established file-transfer protocol for serial lines
(<https://www.kermitproject.org>). Any Kermit client that can talk to a
Kermit server works: C-Kermit (`kermit`), Kermit 95, or a web page
that implements the protocol over the browser's Web Serial API.

## Starting and stopping

1. Press F1, select "Sync via browser" (letter V) and choose
   **Start**.
2. Draftling saves every open document, closes the editor and shows
   the file browser with "Sync via browser active -- F1 to stop" in
   its status bar. Transfers show their progress there as well.
3. When you are done, press F1, select "Sync via browser" and choose
   **Stop**. The file list is refreshed with whatever changed.

Start is refused while "SD card via USB" or a Git sync is running,
when no SD card is mounted, and while a new (never saved) document
has unsaved changes -- save or discard it first. While the server is
running, the editor, Git sync, Draftbox, "Format SD card" and
"SD card via USB" are not available.

While the server runs, the console carries Kermit packets instead of
the firmware's log messages: all console output is switched off, and
it comes back when the server is stopped.

## Connecting

The server listens on the console UART and, on ESP32-S3 boards, also
on the chip's built-in USB-Serial/JTAG port, and answers on whichever
one the client talks to:

- Boards whose USB-C socket goes straight to the ESP32-S3 (most
  supported boards) appear on the computer as a USB serial port
  (`/dev/ttyACM0` on Linux, `/dev/cu.usbmodem*` on macOS, a COM port
  on Windows). The speed setting does not matter.
- Boards with a USB-to-UART bridge chip (for example the Seeed
  reTerminal E1001 and reTerminal Sticky) appear as the bridge's
  serial port; use 115200 bits per second, 8 data bits, no parity, no
  flow control.

Example with C-Kermit on Linux:

```
kermit -l /dev/ttyACM0 -b 115200
C-Kermit> set carrier-watch off
C-Kermit> set flow none
C-Kermit> remote directory
C-Kermit> get notes.md
C-Kermit> send chapter-3.md
C-Kermit> finish
```

## Supported commands

| Client command | What the server does |
|----------------|----------------------|
| `send` | Stores the file(s) on the SD card (a file with the same name is replaced only once the transfer has completed) |
| `get` | Sends the named file(s); `*` and `?` wildcards are allowed in the file name |
| `remote directory` | Lists a directory (name, size, date) |
| `remote cd`, `remote pwd` | Changes / shows the current directory; `/` is the top of the SD card |
| `remote delete` | Deletes the named file(s); wildcards are allowed |
| `remote rename` | Renames a file |
| `remote mkdir`, `remote rmdir` | Creates / removes a directory |
| `remote space` | Shows the SD card's size and free space |
| `remote type` | Shows a file's contents |
| `remote help` | Lists these commands |
| `finish`, `bye` | Acknowledged; the server keeps running until you stop it on the device |

The client cannot reach anything outside the SD card.

## Notes

- Files are sent and stored byte for byte. When a client sends a file
  in text mode (C-Kermit does this for files it recognises as text),
  its CR LF line endings are turned back into the LF line endings
  Draftling uses.
- C-Kermit sends file names in upper case unless told
  `set file names literal`; a name without any lower-case letters is
  stored in lower case.
- The protocol supports long packets (up to about 9 KB), block checks
  1, 2 and 3 (CRC-16), repeat-count compression, 8th-bit prefixing
  and attribute packets (file type, size and date). Sliding windows,
  streaming and transfer recovery are not supported.
- A transfer in progress counts as activity for the standby timer.
- Stopping the server during a transfer cancels it; a partially
  received file is deleted.
