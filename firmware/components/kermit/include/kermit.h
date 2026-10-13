#pragma once
/*
 * Kermit file-transfer server on the serial console.
 *
 * While running, the server owns the console port(s): log output and
 * ROM printf are silenced, and the UART console (and the
 * USB-Serial/JTAG console, when the build has one) carry Kermit
 * packets instead. A client talks to it with the standard Kermit
 * server commands: SEND (upload), GET (download), REMOTE DIRECTORY,
 * CD, PWD, DELETE, RENAME, MKDIR, RMDIR, SPACE, TYPE, HELP, FINISH
 * and BYE. File names are relative to the root directory passed to
 * kermit_server_start() (the SD card mount point); the client cannot
 * leave it. Files are transferred as-is (binary).
 *
 * Protocol reference: https://www.kermitproject.org/kpackets.html
 */

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Progress / result report from the server task. msg is a short
 * human-readable line ("Received notes.md", "Sending a.md: 12 KB").
 * files_changed is true when the SD card contents changed (a file was
 * received, deleted, renamed or a directory created / removed).
 * Called from the server task: the callee must do its own locking and
 * must not block for long, and must not call kermit_server_stop(). */
typedef void (*kermit_status_cb_t)(const char *msg, bool files_changed);

/* Silence the console and start serving files under root_dir. Returns
 * ESP_ERR_INVALID_STATE if the server is already running (or still
 * shutting down), ESP_ERR_NO_MEM, or the error from installing a
 * console driver (the console is restored in that case). */
esp_err_t kermit_server_start(const char *root_dir, kermit_status_cb_t cb);

/* Stop the server: a transfer in progress is cancelled (an error
 * packet tells the client, a partially received file is deleted), the
 * console drivers are removed and normal console output resumes.
 * Waits up to a few seconds for the server task to finish. */
void kermit_server_stop(void);

/* True from kermit_server_start() until the server task has finished
 * after kermit_server_stop(). */
bool kermit_server_is_running(void);

#ifdef __cplusplus
}
#endif
