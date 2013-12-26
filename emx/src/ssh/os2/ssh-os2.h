/*

This is the OS/2 compatibility header file. It contains a lot of definitions
and macros needed to make ssh/sshd/etc compile and work under OS/2 with
EMX/GCC. There is also some support files under os2/ directory.

*/

/*
 * $Id$
 * $Log$
 * $Endlog$
 */

#ifndef SSH_OS2_H
#define SSH_OS2_H

#include <termio.h>
#include <pwd.h>
#include <unistd.h>

/* EMX/GCC not always defines the __OS2__ symbol; fix it */
#ifndef __OS2__
#define __OS2__
#endif

/* Some fixes for config.h */
#undef HAVE_SGTTY_H
#undef HAVE_SOCKETPAIR
#define HAVE_SOCKETPAIR 1

/* OS/2 has rsh too, but not at a fixed location */
#undef RSH_PATH
#define RSH_PATH	"rsh.exe"

/* Use spawn() instead of fork() */
#define HAVE_SPAWN	1

#undef HOST_KEY_FILE
#define HOST_KEY_FILE		"/etc/ssh_host_key"
#undef SERVER_CONFIG_FILE
#define SERVER_CONFIG_FILE	"/etc/sshd_config"
#undef HOST_CONFIG_FILE
#define HOST_CONFIG_FILE	"/etc/ssh_config"

#undef SSH_PROGRAM
#define SSH_PROGRAM		"ssh.exe"
#undef ETCDIR
#define ETCDIR			"/etc"
#undef PIDDIR
#define PIDDIR			"/etc"
#undef SSH_BINDIR
#define SSH_BINDIR		"/emx/bin"
#undef TIS_MAP_FILE
#define TIS_MAP_FILE		"/etc/sshd_tis.map"

#define DEFAULT_SHELL		getenv("COMSPEC")

/* Enable additional TCP/IP definitions */
#define TCPIPV4

/* I don't know what the difference between lstat and stat is,
   but EMX doesn't have lstat and plain stat seems to work ok */
#define lstat stat

/* Use some simple wrappers for opening files */
#include <stdio.h>
#define open	os2_open
extern int os2_open (const char *name, int oflag, ...);
#define fopen	os2_fopen
extern FILE *os2_fopen (const char *fname, const char *mode);

#define SSH_CLIENT_INIT							\
  /* Initialize terminal emulator */					\
  extern void tty_init (int *argc, char ***argv);			\
  tty_init (&ac, &av);							\
  /* Switch stdin, stdout, stderr into binary mode if not console */	\
  if (!isatty (STDIN_FILENO))						\
    setmode (STDOUT_FILENO, O_BINARY);					\
  if (!isatty (STDOUT_FILENO))						\
    setmode (STDOUT_FILENO, O_BINARY);					\
  if (!isatty (STDERR_FILENO))						\
    setmode (STDERR_FILENO, O_BINARY);

/*------------------------------------------------------- Terminal emulation -*/

#define TIOCGWINSZ	12340
#define TIOCSWINSZ	12341

struct winsize
{
  int ws_row;
  int ws_col;
  int ws_xpixel;
  int ws_ypixel;
};

/* Override ioctl() to handle TIOC[GS]WINSZ requests */
#define tty_ioctl	tty_ioctl
extern int tty_ioctl (int handle, int request, void *arg);
/* Query console size */
extern int tty_getsize (struct winsize *ws);
/* Set up console size */
extern int tty_setsize (struct winsize *ws);
/* Check if window size changed since last tty_getsize() */
extern int tty_size_changed ();
/* Read a line (until CR or until buffer fills) from terminal */
#define tty_read tty_read
extern int tty_read (int handle, unsigned char *out, int nbyte);
/* Write a string to terminal */
#define tty_write tty_write
extern int tty_write (int handle, unsigned char *out, int nbyte);

/*-------------------------------------------- System logging facility stabs -*/

/* Replacement for syslog.h */
#define LOG_DAEMON	1
#define LOG_USER	2
#define LOG_AUTH	3
#define LOG_LOCAL0	10
#define LOG_LOCAL1	11
#define LOG_LOCAL2	12
#define LOG_LOCAL3	13
#define LOG_LOCAL4	14
#define LOG_LOCAL5	15
#define LOG_LOCAL6	16
#define LOG_LOCAL7	17

#define	LOG_PID		0x01	/* log the pid with each message */
#define	LOG_CONS	0x02	/* log on the console if errors in sending */
#define	LOG_ODELAY	0x04	/* delay open until first syslog() (default) */
#define	LOG_NDELAY	0x08	/* don't delay open */
#define	LOG_NOWAIT	0x10	/* don't wait for console forks: DEPRECATED */
#define	LOG_PERROR	0x20	/* log to stderr as well */

#define	LOG_EMERG	0	/* system is unusable */
#define	LOG_ALERT	1	/* action must be taken immediately */
#define	LOG_CRIT	2	/* critical conditions */
#define	LOG_ERR		3	/* error conditions */
#define	LOG_WARNING	4	/* warning conditions */
#define	LOG_NOTICE	5	/* normal but significant condition */
#define	LOG_INFO	6	/* informational */
#define	LOG_DEBUG	7	/* debug-level messages */

/* Generate a log message using FMT string and option arguments */
extern void syslog (int pri, const char *fmt, ...);
/* Open connection to system logger.  */
extern void openlog (const char *ident, int option, int facility);
/* Close desriptor used to write to system logger.  */
extern void closelog (void);

/*--------------------------------------------- Password database management -*/

/* Get a database entry by user id */
#define getpwuid	os2_getpwuid
extern struct passwd *os2_getpwuid (uid_t uid);
/* Get a database entry by user name */
#define getpwnam	os2_getpwnam
extern struct passwd *os2_getpwnam (__const__ char *uname);

#define setuid(uid)	0
#define setgid(gid)	0

#endif /* SSH_OS2_H */
