/*
    Copyright (C) 2000 by Andrew Zabolotny

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
    This application will create a socket and then launch TELNETDC with
    the socket number as argument. Then all data coming through the socket
    will be echoed to stdout, and all data coming from stdin will be sent
    into the socket. Effectively this is a "telnet pipe".

    IMPLEMENTATION NOTE: To get rid of the EMX' extra emulation layer
    we link with a synthetic import library which will forward all our
    calls directly into so32dll.dll.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <process.h>
#include <signal.h>
#include <fcntl.h>
#include <io.h>

#define TCPIPV4
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

static unsigned char translate_to [256];
static unsigned char translate_from [256];
static char *init_script = "";

class SocketPipe
{
  // The address of our domain socket
  struct sockaddr_un un;
  // The input socket
  int InSocket;
  // The output socket
  int OutSocket;
public:
  // Initialize the socket pipe object
  SocketPipe ();
  // Destroy the socket pipe object
  ~SocketPipe ();
  // Open the socket pipe
  bool Open ();
  // Close the socket pipe
  void Close ();
  // Get the other end of the pipe
  int GetOut ()
  { return OutSocket; }
  // Get the input end of the pipe
  int GetIn ()
  { return InSocket; }
  // Get data from pipe
  int Read (void *buffer, size_t buffsize);
  // Write data to pipe
  int Write (const void *buffer, size_t buffsize);
};

SocketPipe::SocketPipe ()
{
  InSocket = -1;
  OutSocket = -1;

  memset (&un, 0, sizeof (un));
  un.sun_family = AF_OS2;
  sprintf (un.sun_path, "\\socket\\tnpipe.%d", getpid ());
}

SocketPipe::~SocketPipe ()
{
  Close ();
}

bool SocketPipe::Open ()
{
  InSocket = socket (PF_OS2, SOCK_STREAM, 0);
  if (InSocket < 0) return false;
  OutSocket = socket (PF_OS2, SOCK_STREAM, 0);
  if (OutSocket < 0)
  {
error:
    Close ();
    return false;
  }

  int one = 1;
  setsockopt (InSocket, SOL_SOCKET, SO_USELOOPBACK, &one, sizeof (one));
  setsockopt (OutSocket, SOL_SOCKET, SO_USELOOPBACK, &one, sizeof (one));

  if (bind (InSocket, (struct sockaddr *)&un, sizeof (un)) < 0)
    goto error;

  if (listen (InSocket, 1) < 0)
    goto error;

  if (connect (OutSocket, (struct sockaddr *)&un, sizeof (un)) < 0)
    goto error;

  int addrlen = sizeof (struct sockaddr);
  int newsock = accept (InSocket, (struct sockaddr *)&un, &addrlen);
  soclose (InSocket);
  InSocket = newsock;

  if (InSocket < 0)
    goto error;

  return true;
}

void SocketPipe::Close ()
{
  if (InSocket >= 0)
  {
    soclose (InSocket);
    InSocket = -1;
  }
  if (OutSocket >= 0)
  {
    soclose (OutSocket);
    OutSocket = -1;
  }
}

int SocketPipe::Read (void *buffer, size_t buffsize)
{
  return recv (InSocket, buffer, buffsize, 0);
}

int SocketPipe::Write (const void *buffer, size_t buffsize)
{
  return send (InSocket, buffer, buffsize, 0);
}

static volatile bool done = false;

void sig_child (int)
{
  done = true;
}

/*
 * Our own dummy version of delete' to avoid linking against
 * the big ugly builtin_delete from libgcc
 */
void operator delete (void* p) { }

/*
 * Get a string delimited by either spaces or tabs. If string starts
 * with a double quote, it is expected to end with a double quote.
 * Inside a quoted string all C-style quotation techniques are accepted
 * (\a \b \f \n \r \t \e \x \" \\ and so on)
 */
static char *getstr (char **src, const char *fname, int lineno)
{
  char *s = *src;
  s += strspn (s, " \t");
  if (*s == '"')
    {
      char *d = s++;
      char *org = d;
      while (*s)
        {
          if (*s == '\\')
            switch (s [1])
              {
                case 'a': *d++ = '\a'; s++; break;
                case 'b': *d++ = '\b'; s++; break;
                case 'f': *d++ = '\f'; s++; break;
                case 'n': *d++ = '\n'; s++; break;
                case 'r': *d++ = '\r'; s++; break;
                case 't': *d++ = '\t'; s++; break;
                case 'e': *d++ = '\e'; s++; break;
                case 'x':
                  {
                    char c = 0;
                    s += 2;
                    while ((*s >= '0' && *s <= '9')
                        || (*s >= 'A' && *s <= 'F')
                        || (*s >= 'a' && *s <= 'f'))
                      {
                        char digit = (*s > '9' ? (*s & 0xdf) : *s) - '0';
                        if (digit > 9) digit -= 'A' - '9' - 1;
                        c = (c << 4) | digit;
                        s++;
                      }
                    *d++ = c;
                    continue;
                  }
                default: *d++ = s [1]; s++; break;
              }
          else if (*s == '"')
            break;
          else
            *d++ = *s;
          s++;
        }
      if (*s != '"')
        {
          fprintf (stderr, "File %s line %d: no closing quote in string\n",
            fname, lineno);
          exit (-1);
        }
      *d = 0;
      *src = ++s;
      s = org;
    }
  else
    {
      *src = strpbrk (s, " \t");
      if (!*src)
        *src = strchr (s, 0);
      else
        *(*src)++ = 0;
    }

  return s;
}

/*
 * Read the tnpipe configuration file.
 */
static void read_config (const char *fname)
{
  FILE *f;
  int i, j, lineno = 1;
  char *t_from = NULL, *t_to = NULL;

  for (i = 0; i < 256; i++)
    translate_to [i] = translate_from [i] = i;

  /* Read terminal configuration file */
  f = fopen (fname, "r");
  if (!f)
    {
      fprintf (stderr, "WARNING: cannot read configuration file %s\n", fname);
      return;
    }

  while (!feof (f))
    {
      char line [500];
      char *cur, *token;
      if (!fgets (line, sizeof (line), f))
        break;
      cur = strchr (line, '\n');
      if (cur) *cur = 0;
      cur = strchr (line, '\r');
      if (cur) *cur = 0;
      cur = line;
      token = getstr (&cur, fname, lineno);
      if (!*token || *token == '#')
        ;
      else if (stricmp (token, "init-script") == 0)
        init_script = strdup (getstr (&cur, fname, lineno));
      else if (stricmp (token, "translate-from") == 0)
        {
          if (t_from) free (t_from);
          t_from = strdup (getstr (&cur, fname, lineno));
        }
      else if (stricmp (token, "translate-to") == 0)
        {
          if (t_to) free (t_to);
          t_to = strdup (getstr (&cur, fname, lineno));
        }
      else
        {
          fprintf (stderr, "File %s line %d: unknown keyword `%s'\n",
            fname, lineno, token);
          exit (-1);
        }
      lineno++;
    }
  fclose (f);

  /* If both from and to tables are defined, build translation tables */
  if (t_from && t_to)
    {
      i = strlen (t_from);
      j = strlen (t_to);
      if (i != j)
        {
          fprintf (stderr, "WARNING: `from' and `to' translation tables are different length! (%d vs %d)\n", i, j);
          if (i > j) i = j;
          t_from [i] = 0; t_to [i] = 0;
        }
      for (i = 1; i < 256; i++)
        if (i != ' ')
          {
            char *t;
            if ((t = strchr (t_from, i)))
              translate_from [i] = t_to [t - t_from];
            if ((t = strchr (t_to, i)))
              translate_to [i] = t_from [t - t_to];
          }
    }
  if (t_from) free (t_from);
  if (t_to) free (t_to);
}

/*
 * Input thread
 */
static void input_thread (void *pipe)
{
  SocketPipe *sp = (SocketPipe *)pipe;
  while (!done)
  {
    char buff [1024];
    int len;
    if (isatty (0))
    {
      len = 1;
      buff [0] = _read_kbd (0, 1, 1);
    }
    else
    {
      len = read (0, buff, sizeof (buff));
      for (int i = 0; i < len; i++)
        if (buff [i] == '\n')
          buff [i] = '\r';
    }
    if (len <= 0)
      break;

    // Translate the outgoing buffer
    for (int c = 0; c < len; c++)
      buff [c] = translate_to [(unsigned char)buff [c]];

    if (sp->Write (buff, len) <= 0)
      break;
  }
}

static void output_thread (SocketPipe *pipe)
{
  enum { mdNORM, mdESC, mdSQBRK } mode = mdNORM;
  while (!done)
  {
    char buff [1024];
    int len = pipe->Read (buff, sizeof (buff));

    // If len < 0 the socket has been closed from the other side
    if (len <= 0)
      break;

    /*
     * Interpret and change the incoming ANSI sequence, if needed.
     * Basically IBM telnet has two quircks which makes it slightly
     * incompatible with other telnets: it does not respect the \e[7h
     * and \e[7l sequences (autowrap on/off), although telnet daemon
     * emmits them (incorrectly), and it does not save/restore cursor
     * attribute with \e[s and \e[u sequences.
     */
    for (int i = 0; i < len; i++)
      switch (mode)
      {
        case mdNORM:
          switch (buff [i])
          {
            case 27:
              mode = mdESC;
              break;
          }
          break;
        case mdESC:
          switch (buff [i])
          {
            case '[':
              mode = mdSQBRK;
              break;
            default:
              mode = mdNORM;
              break;
          }
          break;
        case mdSQBRK:
          switch (buff [i])
          {
            case '?':
              break;
            case '7':
              break;
            case 'h':
              /* BUG: IBM telnet does not interpret <esc>[?7h escape sequence
               * at all, while the telnetd emmits it. This sequence turns the
               * terminal into autowrap mode. We'll change 'h' to 'l' to turn
               * autowrap off - in this mode everything will work as expected
               */
              buff [i] = 'l';
              mode = mdNORM;
              break;
            default:
              mode = mdNORM;
              break;
          }
          break;
      }

    // Translate the outgoing buffer
    for (int c = 0; c < len; c++)
      buff [c] = translate_from [(unsigned char)buff [c]];

    write (1, buff, len);
  }
}

static int child_pid = 0;

extern "C" void DosKillProcess (int, int);

static void proc_term (int sig)
{
  /*kill (child_pid, SIGKILL);*/
  DosKillProcess (0/*DKP_PROCESSTREE*/, child_pid);
  signal (SIGTERM, SIG_DFL);
  raise (SIGTERM);
}

static int command (int argc, char **argv)
{
  int i;
  char *new_argv [argc + 1];
  new_argv [0] = getenv ("COMSPEC");
  for (i = 1; i < argc; i++)
    new_argv [i] = argv [i];
  new_argv [argc] = NULL;
  signal (SIGTERM, proc_term);
  child_pid = spawnv (P_NOWAIT, new_argv [0], argv);
  wait (&i);
  return 0;
}

int main (int argc, char **argv)
{
  // If we were run by telnetdc as the login command, return success
  if (getenv ("TNPIPE.LOGIN"))
    return 0;

  // If we were launched as "tnpipe -c ..." the user wants to execute a command
  if (argc > 1 && !strcmp (argv [1], "/c"))
    return command (argc, argv);

  // First of all read config file
  char name [260];
  if (getenv ("ETC"))
    strcpy (name, getenv ("ETC"));
  else
    strcpy (name, "\\etc");
  strcat (name, "\\tnpipe_config");
  read_config (name);

  SocketPipe sp;
  if (!sp.Open ())
  {
    fprintf (stderr, "Cannot open socket pipe!\n");
    return -1;
  }

  char sock [10];
  char *new_argv [5];
  new_argv [0] = "telnetdc";
  _itoa (sp.GetOut (), sock, 10);
  new_argv [1] = sock;
  new_argv [2] = init_script;
  _execname (name, sizeof (name));
  new_argv [3] = name;
  new_argv [4] = NULL;

  // Notify our second copy that it should emulate login
  putenv ("TNPIPE.LOGIN=1");

  // Allright. Now save stdin/stdout and temporary
  // redirect them to the real console
  int old_stdin = dup (0);
  int old_stdout = dup (1);
  int old_stderr = dup (2);
  int console = open ("/dev/con", O_RDONLY | O_BINARY);
  dup2 (console, 0);
  close (console);
  console = open ("/dev/con", O_WRONLY | O_BINARY);
  dup2 (console, 1);
  dup2 (console, 2);
  close (console);

  int pid = spawnvp (P_SESSION | P_BACKGROUND, new_argv [0], new_argv);
  dup2 (old_stdin, 0);
  dup2 (old_stdout, 1);
  dup2 (old_stderr, 2);

  /* Do not perform any CR/LF translation on stdin and stdout */
  setmode (0, O_BINARY);
  setmode (1, O_BINARY);
  setmode (2, O_BINARY);

  if (pid < 0)
  {
    int i;
    char buff [500];
    buff [0] = 0;
    for (i = 0; new_argv [i]; i++)
    {
      if (i) strcat (buff, " ");
      if (!*new_argv [i])
        strcat (buff, "\"\"");
      else
        strcat (buff, new_argv [i]);
    }
    fprintf (stderr, "Error running `%s' (%s)\n", buff, strerror (errno));
    return -1;
  }

  signal (SIGCHLD, sig_child);

  // TELNETDC will want to know our terminal type first of all
  char buff [20];
  sp.Read (buff, 3 * 4);
  // Tell him the terminal type we want.
  // We will always want the ANSI terminal. Other terminal types supported
  // by TELNETDC (e.g. dumb and vt100) are indeed too dumb for our needs
  static const char termtype [] = "\xff\xfa\x18\0ansi\xff\xf0";
  sp.Write (termtype, 10);
  // Read same string which is echoed back to us
  sp.Read (buff, 10);

  // Start the input thread
  _beginthread (input_thread, NULL, 0x4000, &sp);
  // Copy the data coming from socket to stdout
  output_thread (&sp);

  return 0;
}
