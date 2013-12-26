/*
    OS/2 routines for emulating an Unix-like terminal.
    Written by Andrew Zabolotny <bit@eltech.ru>

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

#include "ssh-os2.h"

#define INCL_VIO
#define INCL_KBD
#define INCL_DOS
#include <os2.h>

#include <stdlib.h>
#include <io.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/kbdscan.h>

/* Define INSTANT_REFRESH to 1 to get screen refreshed before returning
 * from tty_write(). Otherwise you'll get a delayed refresh, that is,
 * after some milliseconds after last write() was called.
 */
#define INSTANT_REFRESH		0

/* Wait VC_REFRESH_TIME milliseconds before refreshing the screen */
#define VC_REFRESH_TIME		10

#define VT102_ID		"\033[?6c"	/* DEC VT102 termial ID */
#define VT102_OK		"\033[0n"	/* Terminal ok */

/* Character decoding table */
static unsigned char tty_translate_to [256];
static unsigned char tty_translate_from [4][256];
static unsigned char *tty_translate_out;

static int opt_bell_pitch = 750;
static int opt_bell_duration = 125;

/* Save/restore attributes on <ESC>[s and <ESC>[u? */
static unsigned char opt_save_attr = 1;
/* Underline attribute */
static unsigned char opt_underline_attr = 0x01;
/* Allow switching keypad into numeric/application mode? */
static unsigned char opt_allow_keypad = 1;

typedef struct
{
  unsigned char key;
  char *name;
  unsigned char *translation;
  unsigned char *shifted_translation;
} key_table_t;

static key_table_t opt_keymap [] =
{
  { K_ALT_1,		"alt+1",	"\e1",		"\e!" },
  { K_ALT_2,		"alt+2",	"\e2",		"\e@" },
  { K_ALT_3,		"alt+3",	"\e3",		"\e#" },
  { K_ALT_4,		"alt+4",	"\e4",		"\e$" },
  { K_ALT_5,		"alt+5",	"\e5",		"\e%" },
  { K_ALT_6,		"alt+6",	"\e6",		"\e^" },
  { K_ALT_7,		"alt+7",	"\e7",		"\e&" },
  { K_ALT_8,		"alt+8",	"\e8",		"\e*" },
  { K_ALT_9,		"alt+9",	"\e9",		"\e(" },
  { K_ALT_0,		"alt+0",	"\e0",		"\e)" },
  { K_ALT_A,		"alt+a",	"\ea",		"\eA" },
  { K_ALT_B,		"alt+b",	"\eb",		"\eB" },
  { K_ALT_C,		"alt+c",	"\ec",		"\eC" },
  { K_ALT_D,		"alt+d",	"\ed",		"\eD" },
  { K_ALT_E,		"alt+e",	"\ee",		"\eE" },
  { K_ALT_F,		"alt+f",	"\ef",		"\eF" },
  { K_ALT_G,		"alt+g",	"\eg",		"\eG" },
  { K_ALT_H,		"alt+h",	"\eh",		"\eH" },
  { K_ALT_I,		"alt+i",	"\ei",		"\eI" },
  { K_ALT_J,		"alt+j",	"\ej",		"\eJ" },
  { K_ALT_K,		"alt+k",	"\ek",		"\eK" },
  { K_ALT_L,		"alt+l",	"\el",		"\eL" },
  { K_ALT_M,		"alt+m",	"\em",		"\eM" },
  { K_ALT_N,		"alt+n",	"\en",		"\eN" },
  { K_ALT_O,		"alt+o",	"\eo",		"\eO" },
  { K_ALT_P,		"alt+p",	"\ep",		"\eP" },
  { K_ALT_Q,		"alt+q",	"\eq",		"\eQ" },
  { K_ALT_R,		"alt+r",	"\er",		"\eR" },
  { K_ALT_S,		"alt+s",	"\es",		"\eS" },
  { K_ALT_T,		"alt+t",	"\et",		"\eT" },
  { K_ALT_U,		"alt+u",	"\eu",		"\eU" },
  { K_ALT_V,		"alt+v",	"\ev",		"\eV" },
  { K_ALT_W,		"alt+w",	"\ew",		"\eW" },
  { K_ALT_X,		"alt+x",	"\ex",		"\eX" },
  { K_ALT_Y,		"alt+y",	"\ey",		"\eY" },
  { K_ALT_Z,		"alt+z",	"\ez",		"\eZ" },
  { K_ALT_LEFT_BRACKET,	"alt+[",	"\e[",		"\e{" },
  { K_ALT_RIGHT_BRACKET,"alt+]",	"\e]",		"\e}" },
  { K_ALT_SEMICOLON,	"alt+;",	"\e;",		"\e:" },
  { K_ALT_RIGHT_QUOTE,	"alt+'",	"\e'",		"\e\"" },
  { K_ALT_LEFT_QUOTE,	"alt+`",	"\e`",		"\e~" },
  { K_ALT_BACKSLASH,	"alt+\\",	"\e\\",		"\e|" },
  { K_ALT_COMMA,	"alt+,",	"\e,",		"\e<" },
  { K_ALT_PERIOD,	"alt+.",	"\e.",		"\e>" },
  { K_ALT_SLASH,	"alt+/",	"\e/",		"\e?" },
  { K_ALT_MINUS,	"alt+-",	"\e-",		"\e_" },
  { K_ALT_EQUAL,	"alt+=",	"\e=",		"\e+" },

  { K_F1,		"f1",		"\e[11~" },
  { K_F2,		"f2",		"\e[12~" },
  { K_F3,		"f3",		"\e[13~" },
  { K_F4,		"f4",		"\e[14~" },
  { K_F5,		"f5",		"\e[15~" },
  { K_F6,		"f6",		"\e[17~" },
  { K_F7,		"f7",		"\e[18~" },
  { K_F8,		"f8",		"\e[19~" },
  { K_F9,		"f9",		"\e[20~" },
  { K_F10,		"f10",		"\e[21~" },
  { K_F11,		"f11",		"\e[23~" },
  { K_F12,		"f12",		"\e[24~" },

  { K_SHIFT_F1,		"shift+f1",	"\e[23~" },
  { K_SHIFT_F2,		"shift+f2",	"\e[24~" },
  { K_SHIFT_F3,		"shift+f3",	"\e[25~" },
  { K_SHIFT_F4,		"shift+f4",	"\e[26~" },
  { K_SHIFT_F5,		"shift+f5",	"\e[28~" },
  { K_SHIFT_F6,		"shift+f6",	"\e[29~" },
  { K_SHIFT_F7,		"shift+f7",	"\e[31~" },
  { K_SHIFT_F8,		"shift+f8",	"\e[32~" },
  { K_SHIFT_F9,		"shift+f9",	"\e[33~" },
  { K_SHIFT_F10,	"shift+f10",	"\e[34~" },
  { K_SHIFT_F11,	"shift+f11",	"\e[37~" },
  { K_SHIFT_F12,	"shift+f12",	"\e[38~" },

  { K_CTRL_F1,		"ctrl+f1",	"\eOa" },
  { K_CTRL_F2,		"ctrl+f2",	"\eOb" },
  { K_CTRL_F3,		"ctrl+f3",	"\eOc" },
  { K_CTRL_F4,		"ctrl+f4",	"\eOd" },
  { K_CTRL_F5,		"ctrl+f5",	"\eOe" },
  { K_CTRL_F6,		"ctrl+f6",	"\eOf" },
  { K_CTRL_F7,		"ctrl+f7",	"\eOg" },
  { K_CTRL_F8,		"ctrl+f8",	"\eOh" },
  { K_CTRL_F9,		"ctrl+f9",	"\eOi" },
  { K_CTRL_F10,		"ctrl+f10",	"\eOj" },
  { K_CTRL_F11,		"ctrl+f11",	"\e[39~" },
  { K_CTRL_F12,		"ctrl+f12",	"\e[40~" },

  { K_ALT_F1,		"alt+f1",	"\eOK" },
  { K_ALT_F2,		"alt+f2",	"\eOL" },
  { K_ALT_F3,		"alt+f3",	"\eOO" },
  { K_ALT_F4,		"alt+f4",	"\eON" },
  { K_ALT_F5,		"alt+f5",	"\eOE" },
  { K_ALT_F6,		"alt+f6",	"\eOF" },
  { K_ALT_F7,		"alt+f7",	"\eOG" },
  { K_ALT_F8,		"alt+f8",	"\eOH" },
  { K_ALT_F9,		"alt+f9",	"\eOI" },
  { K_ALT_F10,		"alt+f10",	"\eOJ" },
  { K_ALT_F11,		"alt+f11",	"\e[41~" },
  { K_ALT_F12,		"alt+f12",	"\e[42~" },

  /* These should come before their unshifted versions */
  { K_SHIFT_INS,	"shift+ins",	"\e[2~" },
  { K_SHIFT_DEL,	"shift+del",	"\e[3~" },
  { K_BACKTAB,		"shift+bs",	"\e[Z" },

  { K_UP,		"up",		"\e[A",		"\e[a" },
  { K_DOWN,		"down",		"\e[B",		"\e[b" },
  { K_RIGHT,		"right",	"\e[C",		"\e[c" },
  { K_LEFT,		"left",		"\e[D",		"\e[d" },
  { K_HOME,		"home",		"\e[1~" },
  { K_INS,		"ins",		"\e[2~" },
  { K_DEL,		"del",		"\e[3~" },
  { K_END,		"end",		"\e[4~" },
  { K_PAGEUP,		"pgup",		"\e[5~" },
  { K_PAGEDOWN,		"pgdn",		"\e[6~" },
  { K_CENTER,		"center",	"" },

  { K_CTRL_UP,		"ctrl+up",	"\eOa" },
  { K_CTRL_DOWN,	"ctrl+down",	"\eOb" },
  { K_CTRL_RIGHT,	"ctrl+right",	"\eOc" },
  { K_CTRL_LEFT,	"ctrl+left",	"\eOd" },
  { K_CTRL_HOME,	"ctrl+home",	"\ePD" },
  { K_CTRL_INS,		"ctrl+ins",	"" },
  { K_CTRL_DEL,		"ctrl+del",	"" },
  { K_CTRL_END,		"ctrl+end",	"\ePB" },
  { K_CTRL_PAGEUP,	"ctrl+pgup",	"\ePE" },
  { K_CTRL_PAGEDOWN,	"ctrl+pgdn",	"\ePC" },
  { K_CTRL_TAB,		"ctrl+tab",	"\e\t" },
  { K_CTRL_SPACE,	"ctrl+space",	"" },
  { K_CTRL_PAD_PLUS,	"ctrl+pad+",	"\eOk" },
  { K_CTRL_PAD_MINUS,	"ctrl+pad-",	"\eOm" },
  { K_CTRL_PAD_ASTERISK,"ctrl+pad*",	"\eOj" },
  { K_CTRL_PAD_SLASH,	"ctrl+pad/",	"\eOo" },
  { K_CTRL_CENTER,	"ctrl+center",	"" },
  { K_CTRL_PRTSC,	"ctrl+prtsc",	"" },
  { K_CTRL_AT,		"ctrl+@",	"" },

  { K_ALT_UP,		"alt+up",	"" },
  { K_ALT_DOWN,		"alt+down",	"" },
  { K_ALT_RIGHT,	"alt+right",	"" },
  { K_ALT_LEFT,		"alt+left",	"" },
  { K_ALT_HOME,		"alt+home",	"" },
  { K_ALT_END,		"alt+end",	"" },
  { K_ALT_PAGEUP,	"alt+pgup",	"" },
  { K_ALT_PAGEDOWN,	"alt+pgdn",	"" },
  { K_ALT_INS,		"alt+ins",	"" },
  { K_ALT_DEL,		"alt+del",	"" },
  { K_ALT_TAB,		"alt+tab",	"\e\t" },
  { K_ALT_SPACE,	"alt+space",	"\e " },
  { K_ALT_BACKSPACE,	"alt+bs",	"\e\b" },
  { K_ALT_RETURN,	"alt+enter",	"\e\n" },
  { K_ALT_PAD_PLUS,	"alt+pad+",	"\e+" },
  { K_ALT_PAD_MINUS,	"alt+pad-",	"\e-" },
  { K_ALT_PAD_ASTERISK,	"alt+pad*",	"\e*" },
  { K_ALT_PAD_SLASH,	"alt+pad/",	"\e/" },
  { K_ALT_PAD_ENTER,	"alt+padenter",	"\e\n" },
  { K_ALT_ESC,		"alt+esc",	"\e\e" },

  { 0,			NULL,		NULL }
};

/*------------------------------------------------------------ Config parser -*/

/*
 * Get a string delimited by either spaces or tabs. If string starts
 * with a double quote, it is expected to end with a double quote.
 * Inside a quoted string all C-style quotation techniques are accepted
 * (\a \b \f \n \r \t \e \x \" \\ and so on)
 */
static unsigned char *getstr (unsigned char **src, const char *fname, int lineno)
{
  unsigned char *s = *src;
  s += strspn (s, " \t");
  if (*s == '"')
    {
      unsigned char *d = s++;
      unsigned char *org = d;
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
                    unsigned char c = 0;
                    s += 2;
                    while ((*s >= '0' && *s <= '9')
                        || (*s >= 'A' && *s <= 'F')
                        || (*s >= 'a' && *s <= 'f'))
                      {
                        unsigned char digit = (*s > '9' ? (*s & 0xdf) : *s) - '0';
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
 * Read the TTY configuration file.
 */
static void tty_read_config (const char *fname)
{
  FILE *f;
  int i, j, lineno = 1;
  unsigned char *t_from = NULL, *t_to = NULL;
  char tcname [100];

  for (i = 0; i < 256; i++)
    tty_translate_to [i] =
    tty_translate_from [0][i] =
    tty_translate_from [1][i] =
    tty_translate_from [2][i] =
    tty_translate_from [3][i] = i;

  /* Read terminal configuration file */
  f = fopen (fname, "r");
  if (!f)
    {
      sprintf (tcname, "/etc/ssh_term.%s", fname);
      fname = tcname;
      f = fopen (fname, "r");
      if (!f)
        {
          fprintf (stderr, "WARNING: cannot read terminal configuration file %s\n", fname);
          return;
        }
    }

  while (!feof (f))
    {
      unsigned char line [200];
      unsigned char *cur, *token;
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
      else if (stricmp (token, "key") == 0)
        for (;;)
          {
            unsigned char *keyname = getstr (&cur, fname, lineno);
            unsigned char *keyvalue = getstr (&cur, fname, lineno);
            unsigned char shift_keyname [40];
            key_table_t *key = opt_keymap;
            if (!keyname || !*keyname) break;

            while (key->key)
              {
                if (stricmp (keyname, key->name) == 0)
                  {
                    key->translation = strdup (keyvalue);
                    key = NULL;
                    break;
                  }
                else if (strncmp (key->name, "shift+", 6))
                  {
                    /* Try shift+key->name */
                    strcat (strcpy (shift_keyname, "shift+"), key->name);
                    if (stricmp (keyname, shift_keyname) == 0)
                      {
                        key->shifted_translation = strdup (keyvalue);
                        key = NULL;
                        break;
                      }
                  }
                key++;
              }
            if (key)
              {
                fprintf (stderr, "File %s line %d: unknown key name `%s'\n",
                  fname, lineno, keyname);
                exit (-1);
              }
          }
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
      else if (stricmp (token, "term") == 0)
        {
          char termstr [100];
          snprintf (termstr, sizeof (termstr), "TERM=%s", getstr (&cur, fname, lineno));
          putenv (strdup (termstr));
        }
      else if (stricmp (token, "bell") == 0)
        {
          unsigned char *pitch = getstr (&cur, fname, lineno);
          unsigned char *duration = getstr (&cur, fname, lineno);
          if (!pitch || !*pitch || !duration || !*duration)
            {
              fprintf (stderr, "File %s line %d: bad bell parameters\n",
                fname, lineno);
              exit (-1);
            }
          opt_bell_pitch = atoi (pitch);
          opt_bell_duration = atoi (duration);
        }
      else if (stricmp (token, "backspace-sends") == 0)
        {
          unsigned char *bsmode = getstr (&cur, fname, lineno);
          if (stricmp (bsmode, "backspace") == 0
           || stricmp (bsmode, "bs") == 0)
            {
              tty_translate_to ['\b'] = '\b';
              tty_translate_from [0][127] =
              tty_translate_from [1][127] =
              tty_translate_from [2][127] =
              tty_translate_from [3][127] = 127;
            }
          else if (stricmp (bsmode, "delete") == 0
                || stricmp (bsmode, "del") == 0)
            {
              tty_translate_to ['\b'] = 127;
              tty_translate_from [0][127] =
              tty_translate_from [1][127] =
              tty_translate_from [2][127] =
              tty_translate_from [3][127] = '\b';
            }
          else
            {
              fprintf (stderr, "File %s line %d: bad backspace mode `%s'\n",
                fname, lineno, bsmode);
              exit (-1);
            }
        }
      else if (stricmp (token, "save-cursor-attr") == 0)
        {
          unsigned char *scmode = getstr (&cur, fname, lineno);
          if (stricmp (scmode, "yes") == 0
           || stricmp (scmode, "on") == 0)
            opt_save_attr = 1;
          else if (stricmp (scmode, "no") == 0
           || stricmp (scmode, "off") == 0)
            opt_save_attr = 0;
          else
            {
              fprintf (stderr, "File %s line %d: bad save-cursor-attr mode `%s'\n",
                fname, lineno, scmode);
              exit (-1);
            }
        }
      else if (stricmp (token, "underline-color") == 0)
        {
          unsigned char *ulattr = getstr (&cur, fname, lineno);
          if (!ulattr || !*ulattr)
            {
              fprintf (stderr, "File %s line %d: bad underline attribute `%s'\n",
                fname, lineno, ulattr);
              exit (-1);
            }
          opt_underline_attr = (atoi (ulattr)) & 0x07;
        }
      else if (stricmp (token, "allow-keypad-switch") == 0)
        {
          unsigned char *kpmode = getstr (&cur, fname, lineno);
          if (stricmp (kpmode, "yes") == 0
           || stricmp (kpmode, "on") == 0)
            opt_allow_keypad = 1;
          else if (stricmp (kpmode, "no") == 0
           || stricmp (kpmode, "off") == 0)
            opt_allow_keypad = 0;
          else
            {
              fprintf (stderr, "File %s line %d: bad allow-keypad-switch mode `%s'\n",
                fname, lineno, kpmode);
              exit (-1);
            }
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
            unsigned char *t;
            if ((t = strchr (t_from, i)))
              tty_translate_from [0][i] = t_to [t - t_from];
            if ((t = strchr (t_to, i)))
              tty_translate_to [i] = t_from [t - t_to];
          }
    }
  if (t_from) free (t_from);
  if (t_to) free (t_to);

  /* Fill VT100 graphics table */
  tty_translate_from [1] ['+'] = 0x1A;
  tty_translate_from [1] [','] = 0x1B;
  tty_translate_from [1] ['.'] = 0x19;
  tty_translate_from [1] ['-'] = 0x18;
  tty_translate_from [1] ['0'] = 'þ';
  tty_translate_from [1] ['`'] = 0x04;
  tty_translate_from [1] ['a'] = '±';
  tty_translate_from [1] ['f'] = 'ø';
  tty_translate_from [1] ['g'] = 'ñ';
  tty_translate_from [1] ['h'] = '²';
  tty_translate_from [1] ['j'] = 'Ù';
  tty_translate_from [1] ['k'] = '¿';
  tty_translate_from [1] ['l'] = 'Ú';
  tty_translate_from [1] ['m'] = 'À';
  tty_translate_from [1] ['n'] = 'Å';
  tty_translate_from [1] ['o'] = 'Ä';
  tty_translate_from [1] ['p'] = 'Ä';
  tty_translate_from [1] ['q'] = 'Ä';
  tty_translate_from [1] ['r'] = 'Ä';
  tty_translate_from [1] ['s'] = 'Ä';
  tty_translate_from [1] ['t'] = 'Ã';
  tty_translate_from [1] ['u'] = '´';
  tty_translate_from [1] ['v'] = 'Á';
  tty_translate_from [1] ['w'] = 'Â';
  tty_translate_from [1] ['x'] = '³';
  tty_translate_from [1] ['y'] = 'ó';
  tty_translate_from [1] ['z'] = 'ò';
  tty_translate_from [1] ['{'] = 'ã';
  tty_translate_from [1] ['|'] = '';
  tty_translate_from [1] ['}'] = 'œ';
  tty_translate_from [1] ['~'] = 'ú';
}

/*-------------------------------------------------------- Terminal emulator -*/

/* If kbd_buff points to something, it is received instead of keyboard input */
static unsigned char *kbd_buff = "";
/* Emit either CR/LF or just LF */
static unsigned char kbd_crlf = 0;
/* Use DEC-style arrow keys or not */
static unsigned char kbd_keypad_mode = 0;

/* Keypad is in application mode (vs numeric) */
#define KEYPAD_APPCUR	0x01
/* Keypad is in VT52 mode */
#define KEYPAD_VT52	0x02
/* Keyboard in application mode (vs normal mode) */
#define KEYPAD_APP	0x04

static int get_shift_state ()
{
  KBDINFO ki;
  ki.cb = sizeof (ki);
  KbdGetStatus (&ki, 0);
  return ki.fsState;
}

int tty_check_keypad (unsigned char code)
{
  int rc = 1;

  if (code == K_F1)
    kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\eP" : "\eOP";
  else if (code == K_F2)
    kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\eQ" : "\eOQ";
  else if (code == K_F3)
    kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\eR" : "\eOR";
  else if (code == K_F4)
    kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\eS" : "\eOS";
  else if (kbd_keypad_mode & KEYPAD_APPCUR)
  {
    if (code == K_UP)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\eA" : "\eOA";
    else if (code == K_DOWN)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\eB" : "\eOB";
    else if (code == K_RIGHT)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\eC" : "\eOC";
    else if (code == K_LEFT)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\eD" : "\eOD";
    else if (kbd_keypad_mode & KEYPAD_APP)
      goto kepad_app;
    else
      rc = 0;
  }
  else if (kbd_keypad_mode & KEYPAD_APP)
  {
kepad_app:
    if (code == K_UP)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?x" : "\eOx";
    else if (code == K_DOWN)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?r" : "\eOr";
    else if (code == K_RIGHT)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?v" : "\eOv";
    else if (code == K_LEFT)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?t" : "\eOt";
    else if (code == K_CENTER)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?u" : "\eOu";
    else if (code == K_HOME)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?w" : "\eOw";
    else if (code == K_END)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?q" : "\eOq";
    else if (code == K_PAGEUP)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?y" : "\eOy";
    else if (code == K_PAGEDOWN)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?s" : "\eOs";
    else if (code == K_INS)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?p" : "\eOp";
    else if (code == K_DEL)
      kbd_buff = (kbd_keypad_mode & KEYPAD_VT52) ? "\e?n" : "\eOn";
    else
      rc = 0;
  }
  else
    rc = 0;
  return rc;
}

int tty_read (int handle, unsigned char *out, int nbyte)
{
  static int kbd_initialized = 0;
  unsigned char *org = out;
  int noblock;

  if (!isatty (handle))
    return read (handle, out, nbyte);

  noblock = fcntl (handle, F_GETFL) & O_NONBLOCK;

  /* Set keyboard parameters upon first call to tty_read () */
  if (!kbd_initialized)
    {
      struct termios tio;
      kbd_initialized = 1;
      if (tcgetattr (handle, &tio) == 0)
        {
          tio.c_lflag &= ~(IDEFAULT|ICANON|ECHO|ECHOE|ECHOK|ECHONL);
          tcsetattr (handle, TCSADRAIN, &tio);
        }
    }

  while (nbyte)
    {
      int i, shift_flag;
      unsigned char code;
      unsigned char buffered = *kbd_buff;

      if (buffered)
        code = *kbd_buff++;
      else
      {
        /* If we already have read something, and there is nothing more, drop out */
        if (out != org)
          {
            int chars_left;
            ioctl (handle, FIONREAD, &chars_left);
            if (!chars_left)
              break;
          }
        /* Fetch a character from stdin */
        i = read (handle, &code, 1);
        if (i < 0) goto done;
        /* Translate the character code according to our "from console" table */
        if (code) code = tty_translate_to [(unsigned)code];
      }

      switch (code)
        {
          case 0:
            i = read (handle, &code, 1);
            shift_flag = get_shift_state () & 3 ? 1 : 0;
            if (i < 0) goto done;

            /* Check numeric keypad flag */
            if (kbd_keypad_mode && !shift_flag && opt_allow_keypad)
              if (tty_check_keypad (code))
                break;

            for (i = 0; opt_keymap [i].translation; i++)
              if (opt_keymap [i].key == code)
                {
                  kbd_buff = shift_flag && opt_keymap [i].shifted_translation ?
                    opt_keymap [i].shifted_translation :
                    opt_keymap [i].translation;
                  if (!kbd_buff) kbd_buff = "";
                  break;
                }
            break;
          case '\n':
            *out++ = '\n';
            nbyte--;
            if (kbd_crlf && !buffered)
              {
                out [-1] = '\r';
                if (nbyte)
                  {
                    *out++ = '\n';
                    nbyte--;
                  }
                else
                  kbd_buff = "\n";
              }
            break;
          default:
            *out++ = code;
            nbyte--;
            break;
        }
    }
done:
  if ((out == org) && noblock)
    {
      errno = EAGAIN;
      return -1;
    }
  return (int)(out - org);
}

/*------------------------------------------------ Virtual console emulation -*/

/*
    Since its rather slow to output each character using VIO, we allocate
    a virtual framebuffer instead and do all operations inside that buffer.
    The changed area of buffer is dumped to the screen after some (small)
    delay since last tty output occured.
*/

#if INSTANT_REFRESH
#  define VC_REFRESH vc_flush ()
#else
#  define VC_REFRESH vc_check_flush ()
#endif

/* Initialize the virtual console */
static void vc_init ();
/* Finish the virtual console */
static void vc_exit ();
/* Lock access to virtual console (useful when using the second thread) */
static void vc_lock (int enable);
/* Resize the virtual console */
static void vc_resize (int w, int h);
/* Check if physical console has resized and call vc_resize if so */
static int vc_check_resize ();
/* Add a rectangle to dirty region */
static void vc_dirty_add (int xl, int yt, int xr, int yb);
/* Flush the dirty region */
static void vc_flush ();
/* Check if something has changed, and start a timer to refresh the screen */
static void vc_check_flush ();
/* Scroll a region up */
static void vc_scroll_up (int xl, int yt, int xr, int yb, int count);
/* Scroll a region down */
static void vc_scroll_down (int xl, int yt, int xr, int yb, int count);
/* Scroll a region left */
static void vc_scroll_left (int xl, int yt, int xr, int yb, int count);
/* Scroll a region right */
static void vc_scroll_right (int xl, int yt, int xr, int yb, int count);
/* Clear the virtual console */
static void vc_clear ();
/* Put a character into current cursor position */
static void vc_putc (unsigned short cell);
/* Mark given line with given attribute (one of LINE_XXX attributes) */
static void vc_line_attr (int y, int attr);

/* Physical console size (minus one) */
static int vc_console_w, vc_console_h;
/* Active viewport */
static int vc_console_t, vc_console_b, vc_console_l, vc_console_r;
/* Cursor position */
static int vc_cursor_x, vc_cursor_y;
/* Cursor is visible ? */
static int vc_cursor_visible;
/* The virtual console framebuffer */
static unsigned char *vc_fb = NULL;
/* An empty cell (character plus attribute) */
static unsigned short vc_empty_cell = 0x0720;
/* The per-line attributes array */
static unsigned char *vc_la = NULL;

/* Double-width line */
#define LINE_DW		0x01
/* Double-height line, upper half */
#define LINE_DH_U	0x02
/* Double-height line, lower half */
#define LINE_DH_L	0x04

/* Virtual console simultaneous access protection semaphore */
static HMTX vc_lock_sem;
/* Console refresh event semaphore */
static HEV vc_refresh_sem;
/* Set this to 1 (and post the semaphore) to cause refresh thread to exit */
static int vc_shutdown;
/* Refresh thread ID */
static TID vc_tid = 0;
/* The timer that posts the event semaphore */
static HTIMER vc_timer;

/* The dirty region */
static int __dirty_xl, __dirty_yt, __dirty_xr, __dirty_yb;
/* Current (physical) cursor position and visibility */
static int __old_x = -9999, __old_y, __old_vis = -1;

static void vc_dirty_add (int xl, int yt, int xr, int yb)
{
  if (__dirty_yt > __dirty_yb)
    {
      __dirty_xl = xl;
      __dirty_xr = xr;
      __dirty_yt = yt;
      __dirty_yb = yb;
    }
  else
    {
      if (__dirty_xl > xl) __dirty_xl = xl;
      if (__dirty_xr < xr) __dirty_xr = xr;
      if (__dirty_yt > yt) __dirty_yt = yt;
      if (__dirty_yb < yb) __dirty_yb = yb;
    }
}

static void vc_flush ()
{
  int new_x, new_y, cursor_vis;

  if (__dirty_xl <= __dirty_xr)
    {
      int scanlinew = (vc_console_w + 1) * 2;
      unsigned char *s = vc_fb + __dirty_yt * scanlinew + __dirty_xl * 2;
      int linew = (__dirty_xr - __dirty_xl + 1) * 2;
      while (__dirty_yt <= __dirty_yb)
        {
          if (vc_la [__dirty_yt] & LINE_DW)
            {
              int w2 = vc_console_w / 2, xmin, xmax;
              xmin = __dirty_xl;
              xmax = __dirty_xr;
              if (xmax > w2) xmax = w2;
              if (xmin <= xmax)
                {
                  char *buff = malloc ((xmax - xmin + 1) * 4);
                  for (w2 = xmin; w2 <= xmax; w2++)
                    {
                      unsigned char *str = s + 2 * (w2 - xmin);
                      unsigned short cell = *(unsigned short *)str;
                      *(unsigned short *)&buff [(w2 - xmin) * 4] = cell;
                      *(unsigned short *)&buff [(w2 - xmin) * 4 + 2] =
                        (cell & 0xff00) | 0x20;
                    }

                  VioWrtCellStr (buff, (xmax - xmin + 1) * 4, __dirty_yt, xmin * 2, 0);
                  free (buff);
                }
            }
          else
            VioWrtCellStr (s, linew, __dirty_yt, __dirty_xl, 0);
          s += scanlinew;
          __dirty_yt++;
        }
      __dirty_xl = 9999; __dirty_xr = -9999;
      __dirty_yt = 9999; __dirty_yb = -9999;
    }

  new_x = vc_cursor_x;
  new_y = vc_cursor_y;
  if (vc_la [new_y] & LINE_DW)
    new_x *= 2;
  cursor_vis = (new_x >= 0) && (new_x <= vc_console_w);
  if (__old_x != new_x || __old_y != new_y)
    VioSetCurPos (__old_y = new_y, __old_x = new_x, 0);

  if (__old_vis != (vc_cursor_visible && cursor_vis))
    {
      VIOCURSORINFO ci;
      VioGetCurType (&ci, 0);
      ci.attr = (__old_vis = (vc_cursor_visible && cursor_vis)) ? 0 : -1;
      VioSetCurType (&ci, 0);
    }

  vc_check_resize ();
}

static void vc_fill_empty (unsigned char *dest, int count)
{
  unsigned short *d = (unsigned short *)dest;
  while (count)
    *d++ = vc_empty_cell, count -= 2;
}

static void vc_resize (int w, int h)
{
  int i, old_w, old_h;

  /* We will do weird things with the framebuffer, so lock it */
  vc_lock (1);

  old_w = vc_console_w + 1;
  old_h = vc_console_h + 1;

  vc_console_l = vc_console_t = 0;
  vc_console_r = vc_console_w = w - 1;
  vc_console_b = vc_console_h = h - 1;
  if (vc_fb)
  {
    unsigned char *old_vc_fb = vc_fb;
    vc_fb = malloc (w * h * 2);
    memset (vc_fb, 0, w * h * 2);

    if (w > old_w) w = old_w;
    if (h > old_h) h = old_h;

    for (i = 0; i < h; i++)
      memcpy (vc_fb + (vc_console_w + 1) * 2 * i,
              old_vc_fb + old_w * 2 * i, w * 2);

    free (old_vc_fb);
  }
  else
  {
    vc_fb = malloc (w * h * 2);

    /* Read the contents of user screen if there is no backbuffer */
    for (i = 0; i < h; i++)
      {
        USHORT length = w * 2;
        VioReadCellStr (vc_fb + i * length, &length, i, 0, 0);
      }
  }

  if (vc_la) free (vc_la);
  vc_la = malloc (vc_console_h + 1);
  memset (vc_la, 0, vc_console_h + 1);

  __dirty_yt = 1; __dirty_yb = 0;
  vc_lock (0);
}

/* Acquire/release exclusive access to the virtual console */
static void vc_lock (int enable)
{
  if (!vc_tid)
    return;
  if (enable)
    DosRequestMutexSem (vc_lock_sem, SEM_INDEFINITE_WAIT);
  else
    DosReleaseMutexSem (vc_lock_sem);
}

/* We don't use any C runtime function here, so it is
   not neccessary to compile the program with -Zmt switch */
static void vc_refresh_thread (ULONG dummy)
{
  (void)dummy;
  DosSetPriority (PRTYS_THREAD, PRTYC_TIMECRITICAL, PRTYD_MINIMUM, 0);
  while (!vc_shutdown)
    {
      ULONG count;
      DosWaitEventSem (vc_refresh_sem, SEM_INDEFINITE_WAIT);
      if (vc_timer) DosStopTimer (vc_timer);
      vc_timer = 0;
      DosResetEventSem (vc_refresh_sem, &count);
      vc_lock (1);
      vc_timer = 0;
      vc_flush ();
      vc_lock (0);
    }
}

static void vc_check_flush ()
{
  if (!vc_tid)
    return;

  if ( ((__dirty_xl <= __dirty_xr) && (__dirty_yt <= __dirty_yb))
    || (__old_x != vc_cursor_x) || (__old_y != vc_cursor_y)
    || (__old_vis != vc_cursor_visible))
    {
      /* It looks like a bug (Warp4, fp9): If we use DosAsyncTimer()
       * instead of DosStartTimer(), we aren't able to stop that timer
       * with DosStopTimer(). Thus, we're forced to use DosStartTimer()
       * and stop it as soon as the event semaphore is posted.
       */
      if (vc_timer) DosStopTimer (vc_timer);
      DosStartTimer (VC_REFRESH_TIME, (HSEM)vc_refresh_sem, &vc_timer);
    }
}

static void vc_exit ()
{
  if (!vc_tid)
    return;
  vc_shutdown = 1;
  DosPostEventSem (vc_refresh_sem);
  DosWaitThread (&vc_tid, DCWW_WAIT);
  vc_tid = 0;

  DosCloseMutexSem (vc_lock_sem);
  DosCloseEventSem (vc_refresh_sem);
}

static void vc_init ()
{
  VIOMODEINFO vi;
  vi.cb = sizeof (vi);
  VioGetMode (&vi, 0);
  __dirty_yt = 0; __dirty_yb = -1;
  vc_resize (vi.col, vi.row);
  VioGetCurPos ((PUSHORT)&vc_cursor_y, (PUSHORT)&vc_cursor_x, 0);
  vc_cursor_visible = 1;

  /* Start terminal refresh thread */
  vc_shutdown = 0; vc_timer = 0;
  DosCreateMutexSem (NULL, &vc_lock_sem, 0, FALSE);
  DosCreateEventSem (NULL, &vc_refresh_sem, DC_SEM_SHARED, FALSE);
  DosCreateThread (&vc_tid, vc_refresh_thread, 0, CREATE_READY | STACK_COMMITTED, 32768);

  /* Flush terminal before exit as well */
  atexit (vc_exit);
}

static void vc_set_mode (int w, int h, int reverse)
{
  /* Save the old attribute */
  unsigned short old_vc_empty_cell = vc_empty_cell;

  /* Just set the VIO mode, vc_check_resize will catch the rest */
  VIOMODEINFO vi;
  vi.cb = sizeof (vi);
  VioGetMode(&vi, 0);

  if ((w < 0 || w == vi.col)
   && (h < 0 || h == vi.row))
    return;

  if (w > 0) vi.col = w;
  if (h > 0) vi.row = h;
  vi.cb = sizeof (vi.cb) + sizeof (vi.fbType) + sizeof (vi.color) +
          sizeof (vi.col) + sizeof (vi.row);
  VioSetMode (&vi, 0);

  vc_check_resize ();

  /* Clear screen */
  vc_empty_cell = reverse ? 0x7020 : 0x0720;
  vc_clear ();
  vc_empty_cell = old_vc_empty_cell;
}

static int vc_check_resize ()
{
  VIOMODEINFO vi;
  vi.cb = sizeof (vi);
  VioGetMode (&vi, 0);

  if ((vc_console_h != vi.row - 1)
   || (vc_console_w != vi.col - 1))
    {
      vc_resize (vi.col, vi.row);
      return 1;
    }
  return 0;
}

#define CHECK_RANGE_X(x) \
  if (x < 0) x = 0; if (x > vc_console_w) x = vc_console_w;
#define CHECK_RANGE_Y(y) \
  if (y < 0) y = 0; if (y > vc_console_h) y = vc_console_h;

static void vc_scroll_up (int xl, int yt, int xr, int yb, int count)
{
  unsigned char *d;
  int lines, movelines, linew, scanlinew;

  if ((xl > xr) || (yt > yb))
    return;

  CHECK_RANGE_X (xl);
  CHECK_RANGE_X (xr);
  CHECK_RANGE_Y (yt);
  CHECK_RANGE_Y (yb);

  vc_dirty_add (xl, yt, xr, yb);

  xl <<= 1;
  xr <<= 1;

  linew = xr - xl + 2;
  scanlinew = (vc_console_w + 1) * 2;

  lines = yb - yt + 1;
  movelines = (count == -1) ? 0 : (lines - count);

  d = vc_fb + yt * scanlinew + xl;

  if (movelines)
    {
      unsigned char *s = vc_fb + (yt + count) * scanlinew + xl;
      memmove (vc_la + yt, vc_la + yt + count, lines);
      while (movelines && lines)
        {
          memcpy (d, s, linew);
          s += scanlinew;
          d += scanlinew;
          movelines--;
          lines--;
        }
    }

  while (lines)
    {
      vc_la [yb--] = 0;
      vc_fill_empty (d, linew);
      d += scanlinew;
      lines--;
    }
}

static void vc_scroll_down (int xl, int yt, int xr, int yb, int count)
{
  unsigned char *d;
  int lines, movelines, linew, scanlinew;

  if ((xl > xr) || (yt > yb))
    return;

  CHECK_RANGE_X (xl);
  CHECK_RANGE_X (xr);
  CHECK_RANGE_Y (yt);
  CHECK_RANGE_Y (yb);

  vc_dirty_add (xl, yt, xr, yb);

  xl <<= 1;
  xr <<= 1;

  linew = xr - xl + 2;
  scanlinew = (vc_console_w + 1) * 2;

  lines = yb - yt + 1;
  movelines = (count == -1) ? 0 : (lines - count);

  d = vc_fb + yb * scanlinew + xl;

  if (movelines)
    {
      unsigned char *s = vc_fb + (yb - count) * scanlinew + xl;
      memmove (vc_la + yt + count, vc_la + yt, lines);
      while (movelines && lines)
        {
          memcpy (d, s, linew);
          s -= scanlinew;
          d -= scanlinew;
          movelines--;
          lines--;
        }
    }

  while (lines)
    {
      vc_la [yt++] = 0;
      vc_fill_empty (d, linew);
      d -= scanlinew;
      lines--;
    }
}

static void vc_scroll_left (int xl, int yt, int xr, int yb, int count)
{
  unsigned char *d;
  int lines, fillw, linew, scanlinew;

  if ((xl > xr) || (yt > yb))
    return;

  CHECK_RANGE_X (xl);
  CHECK_RANGE_X (xr);
  CHECK_RANGE_Y (yt);
  CHECK_RANGE_Y (yb);

  vc_dirty_add (xl, yt, xr, yb);

  xl <<= 1;
  xr <<= 1;

  if (count == -1)
    {
      fillw = xr - xl + 2;
      linew = 0;
    }
  else
    {
      fillw = count * 2;
      linew = (xr - xl + 2 - fillw);
      if (linew < 0)
        {
          fillw = (xr - xl + 2);
          linew = 0;
        }
    }

  lines = yb - yt + 1;
  scanlinew = (vc_console_w + 1) * 2;
  d = vc_fb + yt * scanlinew + xl;

  while (lines)
    {
      if (linew) memmove (d, d + fillw, linew);
      vc_fill_empty (d + linew, fillw);
      d += scanlinew;
      lines--;
    }
}

static void vc_scroll_right (int xl, int yt, int xr, int yb, int count)
{
  unsigned char *d;
  int lines, fillw, linew, scanlinew;

  if ((xl > xr) || (yt > yb))
    return;

  CHECK_RANGE_X (xl);
  CHECK_RANGE_X (xr);
  CHECK_RANGE_Y (yt);
  CHECK_RANGE_Y (yb);

  vc_dirty_add (xl, yt, xr, yb);

  xl <<= 1;
  xr <<= 1;

  if (count == -1)
    {
      fillw = xr - xl + 2;
      linew = 0;
    }
  else
    {
      fillw = count * 2;
      linew = (xr - xl + 2 - fillw);
      if (linew < 0)
        {
          fillw = (xr - xl + 2);
          linew = 0;
        }
    }

  lines = yb - yt + 1;
  scanlinew = (vc_console_w + 1) * 2;
  d = vc_fb + yt * scanlinew + xl;

  while (lines)
    {
      if (linew) memmove (d + fillw, d, linew);
      vc_fill_empty (d, fillw);
      d += scanlinew;
      lines--;
    }
}

static void vc_clear ()
{
  vc_scroll_up (vc_console_l, vc_console_t, vc_console_r, vc_console_b, -1);
  vc_cursor_x = vc_cursor_y = 0;
  vc_dirty_add (0, 0, vc_console_w, vc_console_h);
}

static void vc_putc (unsigned short cell)
{
  *(unsigned short *)
    (vc_fb + (vc_cursor_y * (vc_console_w + 1) + vc_cursor_x) * 2) = cell;
  vc_dirty_add (vc_cursor_x, vc_cursor_y, vc_cursor_x, vc_cursor_y);
}

static void vc_invert_attributes ()
{
  int cells;
  unsigned short *cur = (unsigned short *)vc_fb;
  for (cells = (vc_console_w + 1) * (vc_console_h + 1); cells; cur++, cells--)
    *cur = (*cur & 0x88ff) | ((*cur & 0x7000) >> 4) | ((*cur & 0x0700) << 4);
  vc_dirty_add (0, 0, vc_console_w, vc_console_h);
}

static void vc_line_attr (int y, int attr)
{
  if (vc_la [y] != attr)
    {
      vc_la [y] = attr;
      vc_dirty_add (0, y, vc_console_w, y);
    }
}

/*------------------------------------------------------- Terminal emulation -*/

/* Initialize the TTY */
void tty_init (int *argc, char ***argv);
/* Reset the TTY */
static void tty_reset (int clear);
/* Save cursor position */
static void tty_save ();
/* Restore cursor position */
static void tty_restore ();
/* Set TTY attribute */
static void tty_setattr (unsigned char attr);
/* Set cursor X */
static void tty_set_x (int x);
/* Set cursor Y */
static void tty_set_y (int y);
/* Put some data to console stdin */
static void tty_put_stdin (const unsigned char *s);
/* Output a single character at cursor position and move the cursor */
static void tty_out (unsigned char c);
/* Main entry: handle escape codes etc. */
static int tty_putchar (int handle, unsigned char c);

static enum
{
  cmNormal,
  cmEscape,		/* esc   */
  cmOSquare,		/* esc [ */
  cmCSquare,		/* esc ] */
  cmPercent,		/* esc % */
  cmHash,		/* esc # */
  cmCharset0,		/* esc ( */
  cmCharset1,		/* esc ) */
  cmGetParams,		/* esc [ ... */
  cmFuncKey,		/* esc [ [ */
} console_state = cmNormal;

/* 1 for tab stop, 0 for no tab stop */
static int console_tab_stop [5] =
{ 0x01010100, 0x01010101, 0x01010101, 0x01010101, 0x01010101 };

/* ANSI -> PC color mapping */
static unsigned char color_table [8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

/* Console character attribute */
static int console_a = 0x0700;
/* Real console attribute (after all ATTR_XXX modes applied) */
static int console_real_a;
/* Default attribute */
static unsigned char console_default_a = 0x07;

/* For underline we'll use one of the unused bits in high half */
#define ATTR_UNDERLINE		0x80000000
/* Reverse video flag */
#define ATTR_REVERSE		0x40000000
/* DEC reverse video */
#define ATTR_DEC_REVERSE	0x20000000

/* Bell parameters */
static int bell_pitch;
static int bell_duration;

/* Set to 1 after esc [ ? */
static int quest_mark_seen;
/* Not implemented for now */
static int console_autowrap;
/* No mouse support for now */
static int console_mouse;
/* Show ctrl characters as ^X or not */
static int console_ctrl;
/* Insert mode (not implemented) */
static int console_insert;
/* Set highest character bit before output? */
static int console_meta;
/* Cursor absolute/relative mode */
static int console_origin_mode;

/* Character set 0 */
static int console_G0;
/* Character set 1 */
static int console_G1;
/* Console character set index (0 or 1) */
static int console_charset;

#define TTY_MAP_LATIN1		0
#define TTY_MAP_DECGFX		1
#define TTY_MAP_IBMPC		2
#define TTY_MAP_USER		3

/* Primary and secondary console framebuffers */
static unsigned char *console_fb [2] = { NULL, NULL };
static int console_fb_size [2];
static int console_fb_active = 0;

/* A temporary storage for console escape sequences */
#define MAX_CONSOLE_PARAMS	32
static int console_params [MAX_CONSOLE_PARAMS];
static int console_params_count;

static void tty_setattr (unsigned char attr)
{
  console_a = (console_a & 0xffffff00) | attr;
  console_real_a = console_a & 0xff;
  if (console_a & ATTR_UNDERLINE)
    console_real_a = (console_real_a & 0xf8) | opt_underline_attr;
  if (!!(console_a & ATTR_REVERSE) ^ !!(console_a & ATTR_DEC_REVERSE))
    console_real_a = (console_real_a & 0x88)
                   | (((console_real_a >> 4) | (console_real_a << 4)) & 0x77);
  console_real_a <<= 8;
  vc_empty_cell = ' ' | console_real_a;
}

static void tty_reset (int clear)
{
  tty_setattr (console_a = 0x07);
  vc_console_l = vc_console_t = 0;
  vc_console_b = vc_console_h;
  vc_console_r = vc_console_w;
  bell_pitch = opt_bell_pitch;
  bell_duration = opt_bell_duration;
  quest_mark_seen = 0;
  console_autowrap = 1;
  console_mouse = 0;
  console_ctrl = 0;
  console_insert = 0;
  console_meta = 0;
  console_G0 = TTY_MAP_LATIN1;
  console_G1 = TTY_MAP_DECGFX;
  console_charset = 0;
  console_origin_mode = 0;

  if (console_fb [0]) free (console_fb [0]);
  if (console_fb [1]) free (console_fb [1]);
  memset (&console_fb, 0, sizeof (console_fb));
  console_fb_active = 0;

  console_state = cmNormal;

  tty_translate_out = tty_translate_from [TTY_MAP_LATIN1];

  /* Clear the terminal */
  if (clear)
    {
      vc_clear ();
      tty_save ();
    }
}

void tty_init (int *argc, char ***argv)
{
  int i, new_argc;
  char *config_name = "/etc/ssh_term";
  char **new_argv;

  /* Parse command line arguments for OS/2-specific arguments */
  new_argc = 0;
  new_argv = (char **)malloc (*argc * sizeof (char *));
  for (i = 0; i < *argc; i++)
    {
      if (strcmp ((*argv) [i], "-tc") == 0)
        i++, config_name = (*argv) [i];
      else
        new_argv [new_argc++] = (*argv) [i];
    }
  new_argv [new_argc] = NULL;
  *argv = new_argv;
  *argc = new_argc;

  tty_read_config (config_name);
  vc_init ();
  tty_reset (0);
}

/*
    If you are setting both X and Y, ALWAYS first set Y then X
    This is because on double-width lines the cursor could
    exceed the width/2 value and x will be corrected then.
*/
static void tty_set_x (int x)
{
  int maxw = vc_console_w;
  if (vc_la [vc_cursor_y] & LINE_DW)
    maxw /= 2;

  vc_cursor_x = x;
  if (vc_cursor_x < 0)
    vc_cursor_x = 0;
  if (vc_cursor_x > maxw)
    vc_cursor_x = maxw;
}

static void tty_set_y (int y)
{
  vc_cursor_y = y;
  if (console_origin_mode)
    {
      vc_cursor_y += vc_console_t;
      if (vc_cursor_y < vc_console_t)
        vc_cursor_y = vc_console_t;
      if (vc_cursor_y > vc_console_b)
        vc_cursor_y = vc_console_b;
    }
  else
    {
      if (vc_cursor_y < 0)
        vc_cursor_y = 0;
      if (vc_cursor_y > vc_console_h)
        vc_cursor_y = vc_console_h;
    }

  /* Check if cursor X does not exceed max line width */
  tty_set_x (vc_cursor_x);
}

static inline void cr ()
{
  vc_cursor_x = 0;
}

static inline void lf ()
{
  if (vc_cursor_y == vc_console_b)
    vc_scroll_up (vc_console_l, vc_console_t, vc_console_r, vc_console_b, 1);
  else if (vc_cursor_y < vc_console_h)
    vc_cursor_y++;
}

static inline void ri ()
{
  if (vc_cursor_y == vc_console_t)
    vc_scroll_down (vc_console_l, vc_console_t, vc_console_r, vc_console_b, 1);
  else if (vc_cursor_y > 0)
    vc_cursor_y--;
}

static inline void bs ()
{
  if (vc_cursor_x)
    vc_cursor_x--;
}

static void set_tty_mode (int enable)
{
  int i;

  for (i = 0; i <= console_params_count; i++)
    if (quest_mark_seen)
      switch (console_params [i])
        {
          /* DEC private modes set/reset */
          case 1:
            /* Numeric/application keypad mode */
            kbd_keypad_mode = (kbd_keypad_mode & ~KEYPAD_APPCUR) |
              (enable ? KEYPAD_APPCUR : 0);
            break;
          case 2:
            /* VT52 keypad mode */
            kbd_keypad_mode |= KEYPAD_VT52;
            break;
          case 3:
            /* 80/132 mode switch */
            vc_set_mode (enable ? 132 : 80, -1 /* 24? */,
              console_a & ATTR_DEC_REVERSE);
            break;
          case 5:
            /* Inverted screen on/off */
            if (!!(console_a & ATTR_DEC_REVERSE) != enable)
            {
              if (enable)
                console_a |= ATTR_DEC_REVERSE;
              else
                console_a &= ~ATTR_DEC_REVERSE;
              vc_invert_attributes ();
              tty_setattr (console_a);
            }
            break;
          case 6:
            /* Origin relative/absolute */
            console_origin_mode = enable;
            tty_set_y (0); tty_set_x (0);
            break;
          case 7:
            /* Autowrap on/off */
            console_autowrap = enable;
            break;
          case 8:
            /* Autorepeat on/off */
            break;
          case 9:
            console_mouse = enable;
            break;
          case 25:
            /* Cursor on/off */
            vc_cursor_visible = enable;
            break;
          case 47:
            {
              /* Switch terminal buffer */
              int x = vc_cursor_x, y = vc_cursor_y;
              int fb_size = (vc_console_w + 1) * (vc_console_h + 1) * 2;
              if (enable) enable = 1;
              if (enable == console_fb_active)
                break;

              /* Save current console framebuffer */
              console_fb [console_fb_active] = malloc (fb_size);
              console_fb_size [console_fb_active] = fb_size;
              memcpy (console_fb [console_fb_active], vc_fb, fb_size);
              vc_clear ();
              vc_cursor_x = x;
              vc_cursor_y = y;

              if (console_fb [enable] && fb_size != console_fb_size [enable])
                {
                  free (console_fb [enable]);
                  console_fb [enable] = NULL;
                }
              if (console_fb [enable])
                {
                  memcpy (vc_fb, console_fb [enable], fb_size);
                  free (console_fb [enable]);
                  console_fb [enable] = NULL;
                }
              console_fb_active = enable;
            }
            break;
          case 1000:
            console_mouse = enable;
            break;
        }
    else
      switch (console_params [i])
        {
          /* ANSI modes set/reset */
          case 3:
            /* Monitor (display ctrls) */
            console_ctrl = enable;
            break;
          case 4:
            /* Insert Mode on/off */
            console_insert = enable;
            break;
          case 20:
            /* Lf, Enter == CrLf/Lf */
            kbd_crlf = enable;
            break;
        }
}

static void set_attribute ()
{
  int i;
  unsigned char a = console_a;
  for (i = 0; i <= console_params_count; i++)
    switch (console_params [i])
      {
        case 0:
          /* all attributes off */
          a = 0x07;
          console_a &= ~(ATTR_UNDERLINE | ATTR_REVERSE);
          break;
        case 1:
          /* bold */
          a |= 0x08;
          break;
        case 2:
          /* bold off */
          a &= ~0x08;
          break;
        case 4:
          /* toggle underline */
          console_a ^= ATTR_UNDERLINE;
          break;
        case 5:
          /* blink */
          a |= 0x80;
          break;
        case 7:
          /* reverse on */
          console_a |= ATTR_REVERSE;
          break;
        case 10:
          tty_translate_out = tty_translate_from [console_charset ?
            console_G1 : console_G0];
          console_ctrl = 0;
          console_meta = 0;
          break;
        case 11:
          tty_translate_out = tty_translate_from [TTY_MAP_IBMPC];
          console_ctrl = 1;
          console_meta = 0;
          break;
        case 12:
          tty_translate_out = tty_translate_from [TTY_MAP_IBMPC];
          console_ctrl = 1;
          console_meta = 0x80;
          break;
        case 21:
        case 22:
          /* intensity */
          a |= 0x08;
          break;
        case 24:
          /* underline off */
          console_a &= ~ATTR_UNDERLINE;
          break;
        case 25:
          /* blink off */
          a &= 0x7f;
          break;
        case 27:
          /* reverse off */
          console_a &= ~ATTR_REVERSE;
          break;
        case 38:
          a = (a & 0xf0) | (console_default_a & 0x0f);
          console_a |= ATTR_UNDERLINE;
          break;
        case 39:
          a = (a & 0xf0) | (console_default_a & 0x0f);
          console_a &= ~ATTR_UNDERLINE;
          break;
        case 49:
          a = (a & 0x0f) | (console_default_a & 0xf0);
          break;
        default:
          if (console_params [i] >= 30 && console_params [i] <= 37)
            a = (a & 0xf8) | color_table [console_params [i] - 30];
          else if (console_params [i] >= 40 && console_params [i] <= 47)
            a = (a & 0x8f) | (color_table [console_params [i] - 40] << 4);
          break;
      }
  tty_setattr (a);
}

static void tty_put_stdin (const unsigned char *s)
{
  /* no check for tty_buff overflow :-) */
  static unsigned char tty_buff [100];
  size_t sl = strlen (kbd_buff);
  memmove (tty_buff, kbd_buff, sl);
  strcpy (tty_buff + sl, s);
  kbd_buff = tty_buff;
}

static void tty_out (unsigned char c)
{
  int cell = tty_translate_out [console_meta | (unsigned)c] | console_real_a;
  int maxw = vc_console_w;
  if (vc_la [vc_cursor_y] & LINE_DW)
    maxw /= 2;

  if (vc_cursor_x > maxw)
  {
    if (console_autowrap)
    {
      cr (); lf ();
    }
    else
      vc_cursor_x = maxw;
  }

  if (console_insert)
    vc_scroll_right (vc_cursor_x, vc_cursor_y, vc_console_r, vc_cursor_y, 1);
  vc_putc (cell);
  if (vc_cursor_x <= maxw)
    vc_cursor_x++;
}

static int saved_cursor_x = 0;
static int saved_cursor_y = 0;
static int saved_console_a = 0x07;
static int saved_console_charset = 0;
static int saved_G0 = 0;
static int saved_G1 = 1;
static unsigned char *saved_tty_translate_out;

static void tty_save ()
{
  saved_cursor_x = vc_cursor_x;
  saved_cursor_y = vc_cursor_y;
  saved_console_a = console_a;
  saved_console_charset = console_charset;
  saved_G0 = console_G0;
  saved_G1 = console_G1;
  saved_tty_translate_out = tty_translate_out;
}

static void tty_restore ()
{
  vc_cursor_x = saved_cursor_x;
  vc_cursor_y = saved_cursor_y;
  if (opt_save_attr)
    tty_setattr (console_a = saved_console_a);
  console_charset = saved_console_charset;
  console_G0 = saved_G0;
  console_G1 = saved_G1;
  tty_translate_out = saved_tty_translate_out;
}

static int tty_putchar (int handle, unsigned char c)
{
  int keep_state;
  int old_console_state;

  vc_lock (1);

  keep_state = 1;
  old_console_state = console_state;

  /* Check for control characters (effective in any console state) */
  switch (c)
    {
      case 0:
        goto done;
      case '\a':
        DosBeep (bell_pitch, bell_duration);
        goto done;
      case '\b':
      case 127:
        if (vc_cursor_x)
          vc_cursor_x--;
        goto done;
      case '\t':
        {
          int maxw = vc_console_w;
          if (vc_la [vc_cursor_y] & LINE_DW)
            maxw /= 2;
          while (vc_cursor_x < maxw)
            {
              vc_cursor_x++;
              if (console_tab_stop [vc_cursor_x >> 5] & (1 << (vc_cursor_x & 31)))
                break;
            }
        }
        goto done;
      case '\r':
        cr ();
        goto done;
      case '\n':
        lf ();
        if (!kbd_crlf) cr ();
        goto done;
      case 14:
        console_charset = 1;
        tty_translate_out = tty_translate_from [console_G1];
        console_ctrl = 1;
        goto done;
      case 15:
        console_charset = 0;
        tty_translate_out = tty_translate_from [console_G0];
        console_ctrl = 0;
        goto done;
      case 24:
      case 26:
        console_state = cmNormal;
        goto done;
      case '\e':
        console_state = cmEscape;
        goto done;
#if 0
      /* This is ugly as it possibly conflicts with some national characters */
      case 128+27:
        console_state = cmOSquare;
        break;
#endif
    }

  keep_state = 0;
  switch (console_state)
    {
      case cmNormal:
        if (c >= ' ')
          tty_out (c);
        else if (console_ctrl || c == 3)
          {
            tty_out ('^');
            tty_out (c + 64);
          }
        break;
      case cmEscape:
        switch (c)
          {
            case '[':
              console_state = cmOSquare;
              break;
            case ']':
              console_state = cmCSquare;
              break;
            case '%':
              console_state = cmPercent;
              break;
            case 'E':
              cr (); lf ();
              break;
            case 'M':
              ri ();
              break;
            case 'D':
              lf ();
              break;
            case 'H':
              console_tab_stop [vc_cursor_x >> 5] |= (1 << (vc_cursor_x & 31));
              break;
            case 'Z':
              tty_put_stdin (VT102_ID);
              break;
            case '7':
              tty_save ();
              break;
            case '8':
              tty_restore ();
              break;
            case 'c':
              tty_reset (1);
              break;
            case '(':	/* Charset 0 */
              console_state = cmCharset0;
              break;
            case ')':	/* Charset 1 */
              console_state = cmCharset1;
              break;
            case '#':	/* unimplemented */
              console_state = cmHash;
              break;
            case '<':	/* ANSI keypad mode */
              kbd_keypad_mode &= ~KEYPAD_VT52;
              break;
            case '>':	/* Numeric Mode */
              kbd_keypad_mode &= ~KEYPAD_APP;
              break;
            case '=':	/* Application Mode */
              kbd_keypad_mode |= KEYPAD_APP;
              break;
            case 'O':	/* Some xterm keys */
            case 'P':	/* Some other xterm keys */
              console_state = cmFuncKey;
              break;
          }
        break;
      case cmCSquare:
        /* Not implemented */
        break;
      case cmOSquare:
        memset (&console_params, 0, sizeof (console_params));
        console_params_count = 0;
        console_state = cmGetParams;
        if (c == '[')
          { /* Function key */
            console_state = cmFuncKey;
            break;
          }
        if ((quest_mark_seen = (c == '?')))
          break;
        // fallback
      case cmGetParams:
        keep_state = 1;
        if (c == ';' && console_params_count < MAX_CONSOLE_PARAMS)
           console_params_count++;
        else if (c >= '0' && c <= '9')
          console_params [console_params_count] =
            (console_params [console_params_count] * 10 + c - '0');
        else
          {
            console_state = cmNormal;
            switch (c)
              {
                case 'h':
                  set_tty_mode (1);
                  goto done;
                case 'l':
                  set_tty_mode (0);
                  goto done;
                case 'n':
                  if (!quest_mark_seen)
                    {
                      if (console_params [0] == 5)
                        tty_put_stdin (VT102_OK);
                      else if (console_params [0] == 6)
                      {
                        unsigned char tmp [32];
                        snprintf (tmp, sizeof (tmp), "\033[%d;%dR",
                          vc_cursor_y + 1, vc_cursor_x + 1);
                        tty_put_stdin (tmp);
                      }
                    }
                  goto done;
                }
              if (quest_mark_seen)
                {
                  quest_mark_seen = 0;
                  break;
                }
              switch (c)
                {
                  case 'G':
                  case '`':
                    tty_set_x (console_params [0] - 1);
                    break;
                  case 'A':
                    if (!console_params [0])
                      console_params [0]++;
                    tty_set_y (vc_cursor_y - console_params [0]);
                    break;
                  case 'B':
                  case 'e':
                    if (!console_params [0])
                      console_params [0]++;
                    tty_set_y (vc_cursor_y + console_params [0]);
                    break;
                  case 'C':
                  case 'a':
                    if (!console_params [0])
                      console_params [0]++;
                    tty_set_x (vc_cursor_x + console_params [0]);
                    break;
                  case 'D':
                    if (!console_params [0])
                      console_params [0]++;
                    tty_set_x (vc_cursor_x - console_params [0]);
                    break;
                  case 'E':
                    if (!console_params [0])
                      console_params [0]++;
                    tty_set_y (vc_cursor_y + console_params [0]);
                    tty_set_x (0);
                    break;
                  case 'F':
                    if (!console_params [0])
                      console_params [0]++;
                    tty_set_y (vc_cursor_y - console_params [0]);
                    tty_set_x (0);
                    break;
                  case 'd':
                    if (console_params [0])
                      console_params [0]--;
                    tty_set_y (console_params [0]);
                    break;
                  case 'H':
                  case 'f':
                    if (console_params [0])
                      console_params [0]--;
                    if (console_params [1])
                      console_params [1]--;
                    tty_set_y (console_params [0]);
                    tty_set_x (console_params [1]);
                    break;
                  case 'J':
                    {
                      int top = 0, bot = vc_console_h;
                      switch (console_params [0])
                        {
                          case 0:
                            /* erase from cursor to end of display */
                            vc_scroll_up (vc_cursor_x, vc_cursor_y, vc_console_r, vc_cursor_y, -1);
                            top = vc_cursor_y + 1;
                            break;
                          case 1:
                            /* erase from start to cursor */
                            vc_scroll_up (vc_console_l, vc_cursor_y, vc_cursor_x, vc_cursor_y, -1);
                            bot = vc_cursor_y - 1;
                            break;
                          case 2:
                            /* erase whole display */
                            tty_set_y (0);
                            tty_set_x (0);
                            break;
                          default:
                            bot = -1;
                        }
                      if (top <= bot)
                        vc_scroll_up (vc_console_l, top, vc_console_r, bot, -1);
                    }
                    break;
                  case 'K':
                    {
                      int left = 0, right = vc_console_w;
                      switch (console_params [0])
                        {
                          case 0:
                            /* erase from cursor to end of line */
                            left = vc_cursor_x;
                            break;
                          case 1:
                            /* erase from start of line to cursor */
                            right = vc_cursor_x;
                            break;
                          case 2:
                            /* erase whole line */
                            break;
                          default:
                            right = -1;
                            break;
                        }
                      if (left <= right)
                        vc_scroll_left (left, vc_cursor_y, right, vc_cursor_y, -1);
                      break;
                    }
                  case 'L':
                    if (!console_params [0])
                      console_params [0] = 1;
                    if (console_params [0] > vc_console_h)
                      console_params [0] = vc_console_h;
                    vc_scroll_down (vc_console_l, vc_cursor_y, vc_console_r,
                      vc_console_b, console_params [0]);
                    break;
                  case 'M':
                    if (!console_params [0])
                      console_params [0] = 1;
                    else if (console_params [0] > vc_console_h)
                      console_params [0] = vc_console_h;
                    vc_scroll_up (vc_console_l, vc_cursor_y, vc_console_r,
                      vc_console_b, console_params [0]);
                    break;
                  case 'P':
                    if (!console_params [0])
                      console_params [0] = 1;
                    if (console_params [0] > vc_console_w)
                      console_params [0] = vc_console_w;
                    vc_scroll_left (vc_cursor_x, vc_cursor_y, vc_console_r,
                      vc_cursor_y, console_params [0]);
                    break;
                  case '@':
                    if (!console_params [0])
                      console_params [0] = 1;
                    if (console_params [0] > vc_console_w)
                      console_params [0] = vc_console_w;
                    vc_scroll_right (vc_cursor_x, vc_cursor_y, vc_console_r,
                      vc_cursor_y, console_params [0]);
                    break;
                  case 'c':
                    if (!console_params [0])
                      tty_put_stdin (VT102_ID);
                    break;
                  case 'g':
                    if (!console_params [0])
                      console_tab_stop [vc_cursor_x >> 5] &= ~(1 << (vc_cursor_x & 31));
                    else if (console_params [0] == 3)
                      memset (&console_tab_stop, 0, sizeof (console_tab_stop));
                    break;
                  case 'm':
                    set_attribute ();
                    break;
                  case 'r':
                    if (console_params [0])
                      console_params [0]--;
                    if (!console_params [1])
                      console_params [1] = vc_console_h;
                    else
                      console_params [1]--;
                    /* Minimum allowed region is 2 lines */
                    if (console_params [0] < console_params [1] &&
                        console_params [1] <= vc_console_h)
                    {
                      vc_console_t = console_params [0];
                      vc_console_b = console_params [1];
                      tty_set_y (0);
                      tty_set_x (0);
                    }
                    break;
                  case 's':
                    tty_save ();
                    break;
                  case 'u':
                    tty_restore ();
                    break;
                }
          }
        break;
      case cmCharset0:
        console_state = cmNormal;
        if (c == 'B')
          console_G0 = TTY_MAP_LATIN1;
        else if (c == '0')
          console_G0 = TTY_MAP_DECGFX;
        else if (c == 'U')
          console_G0 = TTY_MAP_IBMPC;
        else if (c == 'K')
          console_G0 = TTY_MAP_USER;
        if (console_charset == 0)
          tty_translate_out = tty_translate_from [console_G0];
        break;
      case cmCharset1:
        console_state = cmNormal;
        if (c == 'B')
          console_G1 = TTY_MAP_LATIN1;
        else if (c == '0')
          console_G1 = TTY_MAP_DECGFX;
        else if (c == 'U')
          console_G1 = TTY_MAP_IBMPC;
        else if (c == 'K')
          console_G1 = TTY_MAP_USER;
        if (console_charset == 1)
          tty_translate_out = tty_translate_from [console_G1];
        break;
      case cmPercent:
      case cmFuncKey:
        console_state = cmNormal;
        break;
      case cmHash:
        console_state = cmNormal;
        switch (c)
          {
            case '3': /* Double-height line, upper half */
              vc_line_attr (vc_cursor_y, LINE_DH_U | LINE_DW);
              break;
            case '4': /* Double-height line, lower half */
              vc_line_attr (vc_cursor_y, LINE_DH_L | LINE_DW);
              break;
            case '6': /* Double-width line */
              vc_line_attr (vc_cursor_y, LINE_DW);
              break;
            case '5': /* Single-width line */
              vc_line_attr (vc_cursor_y, 0);
              break;
            case '8':
              /* DEC screen alignment test. kludge :-) */
              vc_empty_cell = (vc_empty_cell & 0xff00) | 'E';
              vc_scroll_left (vc_console_l, vc_console_t, vc_console_r, vc_console_b, -1);
              vc_empty_cell = (vc_empty_cell & 0xff00) | ' ';
              break;
          }
        break;
    }
done:

  if (!keep_state && (old_console_state == console_state))
    console_state = cmNormal;

  vc_lock (0);

  return 1;
}

int tty_write (int handle, unsigned char *out, int nbyte)
{
  int bytes_out = 0;

  if (!isatty (handle))
    return write (handle, out, nbyte);

//  write (2, out, nbyte); /* DEBUG output for testing only */
  while (nbyte--)
  {
    int rc = tty_putchar (handle, *out++);
    if (rc <= 0)
      break;
    bytes_out += rc;
  }

  if (bytes_out)
    VC_REFRESH;

  return bytes_out;
}
