/* mon_hooks.c — sync-step monitor IPC for C interpreters
 *
 * write() on a named FIFO is atomic for lines < PIPE_BUF (4096 bytes).
 * No locking needed — each participant has its own FIFO pair.
 *
 * Protocol per event:
 *   1. Write "KIND funcname [result]\n" to evt_fd
 *   2. Block on read() from ack_fd — waits for controller's G or S
 *   3. G → return normally; S → _exit(2) (stop signal from controller)
 *
 * mon_open() is called once at interpreter startup.
 * If MON_EVT / MON_ACK env vars are not set, all hooks are no-ops.
 */

#include "mon_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int evt_fd = -1;
static int ack_fd = -1;
static int enabled = 0;

void mon_open(const char *evt_fifo, const char *ack_fifo)
{
    if (!evt_fifo || !ack_fifo) return;
    /* O_WRONLY on a FIFO blocks until reader opens — controller opens reader first */
    evt_fd = open(evt_fifo, O_WRONLY);
    if (evt_fd < 0) { perror("mon_open: evt"); return; }
    /* O_RDONLY on ack FIFO — controller opens writer before we open reader */
    ack_fd = open(ack_fifo, O_RDONLY);
    if (ack_fd < 0) { perror("mon_open: ack"); close(evt_fd); evt_fd=-1; return; }
    enabled = 1;
}

static void send_event(const char *line)
{
    if (!enabled) return;
    size_t n = strlen(line);
    /* write is atomic for n < PIPE_BUF */
    if (write(evt_fd, line, n) < 0) { enabled = 0; return; }
    /* block on ack */
    char c = 0;
    if (read(ack_fd, &c, 1) <= 0) { enabled = 0; return; }
    if (c == 'S') _exit(2);   /* controller says stop */
    /* c == 'G' → continue */
}

void mon_enter(const char *fn)
{
    if (!enabled) return;
    char buf[512];
    snprintf(buf, sizeof buf, "ENTER %s\n", fn);
    send_event(buf);
}

void mon_exit(const char *fn, const char *result)
{
    if (!enabled) return;
    char buf[512];
    snprintf(buf, sizeof buf, "EXIT %s %s\n", fn, result ? result : "?");
    send_event(buf);
}

void mon_close(void)
{
    if (enabled) {
        /* send DONE sentinel so controller sees clean EOF without O_RDWR tricks */
        char buf[] = "DONE\n";
        write(evt_fd, buf, sizeof(buf)-1);
        /* no ack expected for DONE — controller just marks us done */
    }
    enabled = 0;
    if (evt_fd >= 0) { close(evt_fd); evt_fd = -1; }
    if (ack_fd >= 0) { close(ack_fd); ack_fd = -1; }
}
