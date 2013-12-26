/*

passwd.c

Author: Andrew Zabolotny <bit@eltech.ru>

OS/2 routines for password database management

*/

/*
 * $Id$
 * $Log$
 * $Endlog$
 */

#include "ssh-os2.h"
#include <string.h>

#define PASSWORD_DATABASE_FILE	"/etc/passwd"

static struct passwd *database;
static int database_entries = 0;
static void load_database (const char *fname);

#undef getpwuid
struct passwd *os2_getpwuid (uid_t uid)
{
  int i;

  if (!database_entries)
    load_database (PASSWORD_DATABASE_FILE);

  for (i = 0; i < database_entries; i++)
    if (uid == database [i].pw_uid)
      return &database [i];
  return NULL;
}

#undef getpwnam
struct passwd *os2_getpwnam (__const__ char *uname)
{
  int i;

  if (!database_entries)
    load_database (PASSWORD_DATABASE_FILE);

  for (i = 0; i < database_entries; i++)
    if (strcmp (uname, database [i].pw_name) == 0)
      return &database [i];
  return NULL;
}

static void load_database (const char *fname)
{
  FILE *f;
  database_entries = 1;
  database = (struct passwd *)malloc (sizeof (struct passwd));
  database [0] = *getpwuid (0);
  database [0].pw_name = strdup (database [0].pw_name);
  database [0].pw_passwd = strdup (database [0].pw_passwd);
  database [0].pw_comment = strdup (database [0].pw_comment);
  database [0].pw_dir = strdup (database [0].pw_dir);
  database [0].pw_shell = strdup (database [0].pw_shell);

  f = fopen (fname, "rt");
  if (!f) return;
  while (!feof (f))
  {
    char *tmp1, *tmp2;
    struct passwd entry;
    char line [300];
    int eol;
    if (!fgets (line, sizeof (line), f))
      break;

    /* Allocate a new record */
    memset (&entry, 0, sizeof (entry));
    eol = 0;

    /* Fetch username */
    tmp1 = line + strspn (line, " \t");
    if (*tmp1 == '#') continue;
    tmp2 = strpbrk (tmp1, ":\n");
    if (!tmp2) tmp2 = strchr (tmp1, 0), eol = 1; else *tmp2 = 0;
    entry.pw_name = (char *)malloc (tmp2 - tmp1 + 1);
    strcpy (entry.pw_name, tmp1);
    if (eol) goto done;

    /* Fetch password */
    tmp1 = tmp2 + 1;
    tmp2 = strpbrk (tmp1, ":\n");
    if (!tmp2) tmp2 = strchr (tmp1, 0), eol = 1; else *tmp2 = 0;
    entry.pw_passwd = (char *)malloc (tmp2 - tmp1 + 1);
    strcpy (entry.pw_passwd, tmp1);
    if (eol) goto done;

    /* Fetch user id */
    entry.pw_uid = strtol (tmp2 + 1, &tmp1, 10);
    tmp2 = strpbrk (tmp1, ":\n");
    if (!tmp2) goto done;

    /* Fetch group id */
    entry.pw_gid = strtol (tmp2 + 1, &tmp1, 10);
    tmp2 = strpbrk (tmp1, ":\n");
    if (!tmp2) goto done;

    /* Fetch description */
    tmp1 = tmp2 + 1;
    tmp2 = strpbrk (tmp1, ":\n");
    if (!tmp2) tmp2 = strchr (tmp1, 0), eol = 1; else *tmp2 = 0;
    entry.pw_comment = (char *)malloc (tmp2 - tmp1 + 1);
    strcpy (entry.pw_comment, tmp1);
    if (eol) goto done;

    /* Fetch home directory */
    tmp1 = tmp2 + 1;
    tmp2 = strpbrk (tmp1, ":\n");
    if (!tmp2) tmp2 = strchr (tmp1, 0), eol = 1; else *tmp2 = 0;
    entry.pw_dir = (char *)malloc (tmp2 - tmp1 + 1);
    strcpy (entry.pw_dir, tmp1);
    if (eol) goto done;

    /* Fetch shell */
    tmp1 = tmp2 + 1;
    tmp2 = strpbrk (tmp1, ":\n");
    if (!tmp2) tmp2 = strchr (tmp1, 0); else *tmp2 = 0;
    entry.pw_shell = (char *)malloc (tmp2 - tmp1 + 1);
    strcpy (entry.pw_shell, tmp1);

done:
    if (!entry.pw_shell) entry.pw_shell = strdup ("");
    if (!entry.pw_dir) entry.pw_dir = strdup ("");
    if (!entry.pw_comment) entry.pw_comment = strdup ("");
    if (!entry.pw_passwd) entry.pw_passwd = strdup ("");
    if (!entry.pw_name) entry.pw_name = strdup ("");

    /* Translate "$x" into "x:" in directory/file names */
    if (entry.pw_shell [0] == '$' && entry.pw_shell [1])
      {
        entry.pw_shell [0] = entry.pw_shell [1];
        entry.pw_shell [1] = ':';
      } /* endif */
    if (entry.pw_dir [0] == '$' && entry.pw_dir [1])
      {
        entry.pw_dir [0] = entry.pw_dir [1];
        entry.pw_dir [1] = ':';
      } /* endif */

    /* Look for a database entry with same id or same uid, and replace if found */
    for (eol = 0; eol < database_entries; eol++)
      if (strcmp (database [eol].pw_name, entry.pw_name) == 0
       || database [eol].pw_uid == entry.pw_uid)
        {
          database [eol] = entry;
          eol = -1;
          break;
        } /* endif */

    if (eol >= 0)
      {
        database_entries++;
        database = (struct passwd *)realloc (database,
          database_entries * sizeof (struct passwd));
        database [database_entries - 1] = entry;
      } /* endif */
  }
  fclose (f);
}
