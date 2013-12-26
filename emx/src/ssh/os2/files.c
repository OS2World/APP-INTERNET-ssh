/*

os2/files.c

Author: Andrew Zabolotny <bit@eltech.ru>

Routines for emulating some Unix file system features on OS/2.

*/

/*
 * $Id$
 * $Log$
 * $Endlog$
 */

#include "ssh-os2.h"

#define INCL_VIO
#include <os2.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <share.h>
#include <sys/types.h>
#include <dirent.h>
#include <emx/io.h>

/*---------------------------------------------- Directory names replacement -*/

/*
 * These routines are replacement for EMX`s own open(), fopen()
 * taking care of converting such unix-specific things like '/tmp/',
 * '/dev/null' etc into their os/2 equivalents
 */

static struct
{
  char *path;
  int mode;
  char *repl;
  int len;
} conv_path [] =
{
  { "/dev/null", 0, "/dev/nul" },
  { "/tmp/", 1, "TMP" },
  { "/tmp/", 1, "TEMP" },
  { "/etc/", 1, "ETC" },
  { NULL }
};

static char *convert_filename (const char *name, char *dest, int maxlen)
{
  char *s;
  int i;

  for (i = 0; conv_path[i].path; i++)
  {
    if (!conv_path[i].len)
      conv_path[i].len = strlen (conv_path[i].path);
    if (strncmp (name, conv_path[i].path, conv_path[i].len) == 0)
      switch (conv_path[i].mode)
      {
        case 0:
          strncpy (dest, conv_path[i].repl, maxlen);
          return (dest);
        case 1:
          if ((s = getenv (conv_path[i].repl)) && *s)
          {
            strncpy (dest, s, maxlen);
            s = strchr (dest, 0);
            maxlen -= (s - dest);
            if ((s [-1] != '/') && (s [-1] != '\\'))
              *s++ = '/', maxlen--;
            strncpy (s, name + conv_path[i].len, maxlen);
            return (dest);
          }
          break;
      }
  }
  strncpy (dest, name, maxlen);
  return (dest);
}

int os2_open (const char *name, int oflag,...)
{
  va_list va;
  int h;
  char *new_name = convert_filename (name, alloca (MAXPATHLEN + 1), MAXPATHLEN + 1);

  if (!new_name)
    return (-1);

  va_start (va, oflag);
  h = _vsopen (new_name, oflag, SH_DENYNO, va);
  va_end (va);
  return h;
}

FILE *os2_fopen (const char *fname, const char *mode)
{
  char *new_fname = convert_filename (fname, alloca (MAXPATHLEN + 1), MAXPATHLEN + 1);

  if (!new_fname)
    return (NULL);

#undef fopen
  return fopen (new_fname, mode);
}

/* These functions are defined here instead of term.c because sshd
   needs to be able to set the size of the console but doesn't need
   a full terminal emulator.
*/

static int last_col = 0, last_row = 0;

int tty_size_changed ()
{
  VIOMODEINFO vi;
  vi.cb = sizeof (vi);
  VioGetMode (&vi, 0);
  return (last_col != vi.col || last_row != vi.row);
}

int tty_setsize (struct winsize *ws)
{
  USHORT cx, cy;
  VIOMODEINFO vi;
  vi.cb = sizeof (vi);
  VioGetMode(&vi, 0);

  VioGetCurPos (&cx, &cy, 0);

  vi.col = ws->ws_col;
  vi.row = ws->ws_row;
  vi.cb = sizeof (vi.cb) + sizeof (vi.fbType) + sizeof (vi.color) +
          sizeof (vi.col) + sizeof (vi.row);
  VioSetMode (&vi, 0);

  VioSetCurPos (cx, cy, 0);
  return 0;
}

int tty_getsize (struct winsize *ws)
{
  VIOMODEINFO vi;
  vi.cb = sizeof (vi);
  VioGetMode (&vi, 0);
  last_col = ws->ws_col = vi.col;
  last_row = ws->ws_row = vi.row;
  ws->ws_xpixel = vi.hres;
  ws->ws_ypixel = vi.vres;
  return 0;
}

int tty_ioctl (int handle, int request, void *arg)
{
  if (isatty (handle))
    {
      if (request == TIOCGWINSZ)
        {
          tty_getsize ((struct winsize *)arg);
          return 0;
        }
      else if (request == TIOCSWINSZ)
        {
          tty_setsize ((struct winsize *)arg);
          return 0;
        }
    }
  return ioctl (handle, request, arg);
}
