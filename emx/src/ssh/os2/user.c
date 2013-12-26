/*
    Add, remove, change entries in 'passwd'.
    Copyright (C) 2000 Andrew Zabolotny <bit@eltech.ru>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; see the file COPYING. If not, write to
    the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
    Boston, MA 02111-1307, USA.
*/

/*
    Well, this program was initially based on cvspw.c by
    Andreas Huber <ahuber@ping.at>, but I started to modify it
    and finished with a file that barely has a single common
    line with the original :-)
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <crypt.h>
#include <getopt.h>
#include <pwd.h>
#include <time.h>

#define EXIT_USAGE	1
#define EXIT_SIGNAL	2
#define EXIT_ERROR	3

static char *progname;
static const char *progversion = "0.2.1";
static int be_quiet = 0;

static void display_version ()
{
    printf ("Password database manager  Version %s\n", progversion);
    printf ("Copyright (C) 2000 Andrew Zabolotny <bit@eltech.ru>\n");
    printf ("\n");
}

static void display_help ()
{
    if (!be_quiet)
    {
        display_version ();
        printf ("Usage: %s [username] [option/s]\n"
            " -a, --add           Add user/password\n"
            " -c, --change        Change user/password\n"
            " -d, --directory=#   Set database location (default = ${ETC})\n"
            " -h, --help          Display usage information\n"
            " -q, --quiet         Be quiet (dont be too wordy)\n"
            " -l, --list          Display database entries for specified users\n"
            " -r, --remove        Remove user/password\n"
            " -t, --test          Test password for given user\n"
            " -v, --version       Display program version\n",
            progname);
    }
    exit (EXIT_USAGE);
}

static void error (char const* format, ...)
{
    va_list argp;

    fprintf (stderr, "%s: ", progname);
    va_start (argp, format);
    vfprintf (stderr, format, argp);
    va_end (argp);
}

static void signal_handler (int sig)
{
    fcloseall ();
    fprintf (stderr, "^C\n");
    error ("terminated by %s\n", sig == SIGINT ? "SIGINT" : "SIGBREAK");
    exit (EXIT_SIGNAL);
}

static char *get_password (int verify, const char *username)
{
    char pass1 [_PASSWORD_LEN + 1];
    char *pass2;
    for (;;)
    {
        sprintf (pass1, "Please enter password for %s: ", username);
        strcpy (pass1, getpass (pass1));
        if (!verify
         || strcmp (pass2 = getpass ("Re-enter for verification: "), pass1) == 0)
        {
            /* Generate the two-character encryption salt */
            const char *salt_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            size_t salt_len = strlen (salt_chars);
            char salt [3];
            salt [0] = salt_chars [rand () % salt_len];
            salt [1] = salt_chars [rand () % salt_len];
            salt [2] = 0;
            return crypt (pass1, salt);
        }
        error ("Passwords mismatch, please try again.\n");
    }
}

struct passwd *database;
int database_entries;
int database_modified = 0;

void load_database (const char *fname)
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

static int pw_compare (const void *x1, const void *x2)
{
    const struct passwd *p1 = (const struct passwd *)x1;
    const struct passwd *p2 = (const struct passwd *)x2;
    return (p1->pw_uid < p2->pw_uid) ? -1 :
           (p1->pw_uid > p2->pw_uid) ? +1 : 0;
}

int save_database (const char *fname)
{
    int i;
    FILE *f;

    /* Sort database by uid before saving */
    qsort (database, database_entries, sizeof (struct passwd), pw_compare);

    f = fopen (fname, "wt");
    if (!f) return -1;

    for (i = 0; i < database_entries; i++)
    {
        char dir [strlen (database [i].pw_dir) + 1];
        char shell [strlen (database [i].pw_shell) + 1];

        if (!*database [i].pw_name)
            continue;

        strcpy (dir, database [i].pw_dir);
        if (dir [1] == ':')
        {
            dir [1] = dir [0];
            dir [0] = '$';
        } /* endif */

        strcpy (shell, database [i].pw_shell);
        if (shell [1] == ':')
        {
            shell [1] = shell [0];
            shell [0] = '$';
        } /* endif */

        fprintf (f, "%s:%s:%d:%d:%s:%s:%s\n", database [i].pw_name,
            database [i].pw_passwd, database [i].pw_uid, database [i].pw_gid,
            database [i].pw_comment, dir, shell);
    } /* endfor */
    return 0;
}

struct passwd *__getpwuid (uid_t uid)
{
    int i;
    for (i = 0; i < database_entries; i++)
        if (uid == database [i].pw_uid)
            return &database [i];
    return NULL;
}

struct passwd *__getpwnam (__const__ char *username)
{
    int i;
    for (i = 0; i < database_entries; i++)
        if (strcmp (username, database [i].pw_name) == 0)
            return &database [i];
    return NULL;
}

static int add_user (const char *username)
{
    struct passwd *entry = __getpwnam (username);
    char tmp [260 + 1];
    char *env;

    if (entry)
    {
        error ("User %s already exists, use -c to change password\n", username);
        return -1;
    } /* endif */

    database = (struct passwd *)realloc (database,
        (database_entries + 1) * sizeof (struct passwd));
    entry = &database [database_entries];
    memset (entry, 0, sizeof (*entry));

    entry->pw_name = strdup (username);
    entry->pw_passwd = strdup (get_password (1, username));
    for (entry->pw_uid = 100; entry->pw_uid < 10000; entry->pw_uid++)
        if (!__getpwuid (entry->pw_uid))
            break;
    if (entry->pw_uid >= 10000)
    {
        error ("User database is full\n");
        return -1;
    } /* endif */
    entry->pw_gid = entry->pw_uid;
    entry->pw_comment = strdup ("");
    env = getenv ("HOME");
    if (!env) env = "/home/";
    strcpy (tmp, env);
    strcpy (_getname (tmp), username);
    entry->pw_dir = strdup (tmp);
    entry->pw_shell = getenv ("SHELL");
    if (!entry->pw_shell) entry->pw_shell = getenv ("EMX_SHELL");
    if (!entry->pw_shell) entry->pw_shell = getenv ("COMSPEC");
    if (!entry->pw_shell) entry->pw_shell = "";
    entry->pw_shell = strdup (entry->pw_shell);

    database_entries++;
    database_modified = 1;
    return 0;
}

static int remove_user (const char *username)
{
    struct passwd *entry = __getpwnam (username);
    if (!entry)
    {
        error ("No such user `%s'\n", username);
        return -1;
    } /* endif */
    free (entry->pw_name);
    entry->pw_name = strdup ("");
    database_modified = 1;
    return 0;
}

static int change_user (const char *username)
{
    struct passwd *entry = __getpwnam (username);
    if (!entry)
    {
        error ("No such user `%s'\n", username);
        return -1;
    } /* endif */
    free (entry->pw_passwd);
    entry->pw_passwd = strdup (get_password (1, username));
    database_modified = 1;
    return 0;
}

static int test_password (const char *username)
{
    struct passwd *entry = __getpwnam (username);
    if (!entry)
    {
        error ("No such user `%s'\n", username);
        return -1;
    } /* endif */
    if (strcmp (entry->pw_passwd, get_password (0, username)))
    {
        error ("Bad password for `%s'\n", username);
        return -1;
    } /* endif */
    return 0;
}

static int list_database (const char *username)
{
    int i;
    for (i = 0; i < database_entries; i++)
    {
        if (!username || strcmp (username, database [i].pw_name) == 0)
            printf ("%s:%s:%d:%d:%s:%s:%s\n", database [i].pw_name,
                database [i].pw_passwd, database [i].pw_uid, database [i].pw_gid,
                database [i].pw_comment, database [i].pw_dir, database [i].pw_shell);
    } /* endfor */
    return 0;
}

int main (int argc, char **argv)
{
    static struct option long_options [] =
    {
        {"add", no_argument, 0, 'a'},
        {"change", no_argument, 0, 'c'},
        {"directory", required_argument, 0, 'd'},
        {"test", no_argument, 0, 't'},
        {"list", no_argument, 0, 'l'},
        {"quiet", no_argument, 0, 'q'},
        {"remove", no_argument, 0, 'r'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    int c;
    char op = 0;
    char passwfile [FILENAME_MAX + 1];

    progname = _getname (argv [0]);
    (void)signal (SIGINT, signal_handler);
    (void)signal (SIGBREAK, signal_handler);
    srand (time (NULL));

    if (getenv ("ETC"))
        sprintf (passwfile, "%s/passwd", getenv ("ETC"));
    else
        strcpy (passwfile, "passwd");

    while ((c = getopt_long (argc, argv, "acd:hlqrtv", long_options, 0)) != EOF)
        switch (c)
        {
            case '?':
                // unknown option
                return -1;
            case 'a':
            case 'r':
            case 'c':
            case 't':
            case 'l':
                op = c;
                break;
            case 'd':
                sprintf (passwfile, "%s/passwd", optarg);
                break;
            case 'q':
                be_quiet = 1;
                break;
            case 'h':
                display_help ();
                break;
            case 'v':
                display_version ();
                printf (
                    "This program is free software; you can redistribute it and/or\n"
                    "modify it under the terms of the GNU General Public License\n"
                    "as published by the Free Software Foundation; either version 2\n"
                    "of the License, or (at your option) any later version.\n"
                    "\n"
                    "This program is distributed in the hope that it will be useful,\n"
                    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
                    "GNU General Public License for more details.\n"
                    "\n"
                    "You should have received a copy of the GNU General Public License\n"
                    "along with this program; see the file COPYING. If not, write to\n"
                    "the Free Software Foundation, Inc., 59 Temple Place - Suite 330,\n"
                    "Boston, MA 02111-1307, USA.\n");
                exit (EXIT_USAGE);
            default:
                // oops!
                abort ();
        }

    if (!op)
        display_help ();

    load_database (passwfile);

    if (optind >= argc)
    {
        if (op == 'l')
        {
            if (list_database (NULL))
                exit (EXIT_ERROR);
        }
        else
        {
            error ("No user name(s) listed on command line\n");
            exit (EXIT_ERROR);
        }
    }

    // Interpret the non-option arguments as file names
    for (; optind < argc; ++optind)
        switch (op)
        {
            case 'a':
                if (add_user (argv [optind]))
                    exit (EXIT_ERROR);
                break;
            case 'r':
                if (remove_user (argv [optind]))
                    exit (EXIT_ERROR);
                break;
            case 'c':
                if (change_user (argv [optind]))
                    exit (EXIT_ERROR);
                break;
            case 't':
                if (test_password (argv [optind]))
                    exit (EXIT_ERROR);
                break;
            case 'l':
                if (list_database (argv [optind]))
                    exit (EXIT_ERROR);
                break;
        }

    if (database_modified)
        if (save_database (passwfile))
            exit (EXIT_ERROR);

    if (!be_quiet)
        printf ("%s: success\n", progname);

    return 0;
}
