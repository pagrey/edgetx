/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* mountlist.c -- return a list of mounted file systems

   Copyright (C) 1991, 1992, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005, 2006, 2007 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

// This file is not used on some platforms, so don't do anything for them
#if(!(defined(WIN32) || !defined __GNUC__)&&(!defined(__amigaos4__))&&(!defined(__AROS__))&&(!defined(__MORPHOS__))&&(!defined(__amigaos__)))

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)                       // MacOS X is POSIX compliant
    #define MOUNTED_GETMNTINFO
#if defined(__APPLE__)
    #include <sys/types.h>
#endif
#if defined(__OpenBSD__)
    #define HAVE_STRUCT_STATFS_F_FSTYPENAME 1
    #include <sys/param.h> /* types.h needs this */
    #include <sys/types.h>
#endif
#elif defined(__NetBSD__)
    #define MOUNTED_GETMNTINFO2
#elif defined(__BEOS__) || defined(__HAIKU__)
    #define MOUNTED_FS_STAT_DEV
#elif defined(__TRU64__)
    #define MOUNTED_GETFSSTAT 1
    #define HAVE_SYS_MOUNT_H 1
    #include <sys/types.h>
#else
    #define MOUNTED_GETMNTENT1
#endif

#include "mountlist.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include <fcntl.h>
#include <unistd.h>

#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#if defined MOUNTED_GETFSSTAT        /* OSF_1 and Darwin1.3.x */
# if HAVE_SYS_UCRED_H
#  include <grp.h> /* needed on OSF V4.0 for definition of NGROUPS,
                      NGROUPS is used as an array dimension in ucred.h */
#  include <sys/ucred.h> /* needed by powerpc-apple-darwin1.3.7 */
# endif
# if HAVE_SYS_MOUNT_H
#  include <sys/mount.h>
# endif
# if HAVE_SYS_FS_TYPES_H
#  include <sys/fs_types.h> /* needed by powerpc-apple-darwin1.3.7 */
# endif
# if HAVE_STRUCT_FSSTAT_F_FSTYPENAME
#  define FS_TYPE(Ent) ((Ent).f_fstypename)
# else
#  define FS_TYPE(Ent) mnt_names[(Ent).f_type]
# endif
#endif /* MOUNTED_GETFSSTAT */

#ifdef MOUNTED_GETMNTENT1        /* 4.3BSD, SunOS, HP-UX, Dynix, Irix.  */
# include <mntent.h>
# if !defined MOUNTED
#  if defined _PATH_MOUNTED        /* GNU libc  */
#   define MOUNTED _PATH_MOUNTED
#  endif
#  if defined MNT_MNTTAB        /* HP-UX.  */
#   define MOUNTED MNT_MNTTAB
#  endif
#  if defined MNTTABNAME        /* Dynix.  */
#   define MOUNTED MNTTABNAME
#  endif
# endif
#endif

#ifdef MOUNTED_GETMNTINFO        /* 4.4BSD.  */
# include <sys/mount.h>
#endif

#ifdef MOUNTED_GETMNTINFO2        /* NetBSD 3.0.  */
# include <sys/statvfs.h>
#endif

#ifdef MOUNTED_GETMNT                /* Ultrix.  */
# include <sys/mount.h>
# include <sys/fs_types.h>
#endif

#ifdef MOUNTED_FS_STAT_DEV        /* BeOS.  */
# include <fs_info.h>
# include <dirent.h>
#endif

#ifdef MOUNTED_FREAD                /* SVR2.  */
# include <mnttab.h>
#endif

#ifdef MOUNTED_FREAD_FSTYP        /* SVR3.  */
# include <mnttab.h>
# include <sys/fstyp.h>
# include <sys/statfs.h>
#endif

#ifdef MOUNTED_LISTMNTENT
# include <mntent.h>
#endif

#ifdef MOUNTED_GETMNTENT2        /* SVR4.  */
# include <sys/mnttab.h>
#endif

#ifdef MOUNTED_VMOUNT                /* AIX.  */
# include <fshelp.h>
# include <sys/vfs.h>
#endif

#ifdef DOLPHIN
/* So special that it's not worth putting this in autoconf.  */
# undef MOUNTED_FREAD_FSTYP
# define MOUNTED_GETMNTTBL
#endif

#if HAVE_SYS_MNTENT_H
/* This is to get MNTOPT_IGNORE on e.g. SVR4.  */
# include <sys/mntent.h>
#endif

#undef MNT_IGNORE
#if defined MNTOPT_IGNORE && defined HAVE_HASMNTOPT
# define MNT_IGNORE(M) hasmntopt ((M), MNTOPT_IGNORE)
#else
# define MNT_IGNORE(M) 0
#endif

#if USE_UNLOCKED_IO
# include "unlocked-io.h"
#endif

#ifndef SIZE_MAX
# define SIZE_MAX ((size_t) -1)
#endif

/* The results of open() in this file are not used with fchdir,
   therefore save some unnecessary work in fchdir.c.  */
#undef open
#undef close

/* The results of opendir() in this file are not used with dirfd and fchdir,
   therefore save some unnecessary work in fchdir.c.  */
#undef opendir
#undef closedir

// gcc2 under haiku and beos don't like these macros for some reason.
// As they are not used there anyways, we remove them and everyone is happy.
#if !defined(__HAIKU__) && !defined(__BEOS__)
#ifndef ME_DUMMY
# define ME_DUMMY(Fs_name, Fs_type)                \
    (strcmp (Fs_type, "autofs") == 0            \
     || strcmp (Fs_type, "none") == 0           \
     || strcmp (Fs_type, "proc") == 0           \
     || strcmp (Fs_type, "subfs") == 0          \
     || strcmp (Fs_type, "sysfs") == 0          \
     || strcmp (Fs_type, "usbfs") == 0          \
     || strcmp (Fs_type, "devpts") == 0         \
     || strcmp (Fs_type, "tmpfs") == 0          \
     /* for NetBSD 3.0 */                       \
     || strcmp (Fs_type, "kernfs") == 0         \
     /* for Irix 6.5 */                         \
     || strcmp (Fs_type, "ignore") == 0         \
     /* for MacOSX */                           \
     || strcmp (Fs_type, "devfs") == 0          \
     || strcmp (Fs_type, "fdesc") == 0          \
     || strcmp (Fs_type, "nfs") == 0            \
     || strcmp (Fs_type, "volfs") == 0)
#endif

#ifndef ME_REMOTE
/* A file system is `remote' if its Fs_name contains a `:'
   or if (it is of type (smbfs or cifs) and its Fs_name starts with `//').  */
# define ME_REMOTE(Fs_name, Fs_type)                \
    (strchr (Fs_name, ':') != NULL                \
     || ((Fs_name)[0] == '/'                        \
         && (Fs_name)[1] == '/'                        \
         && (strcmp (Fs_type, "smbfs") == 0        \
             || strcmp (Fs_type, "cifs") == 0)))
#endif
#endif // HAIKU / BEOS

#ifdef MOUNTED_GETMNTINFO

# if ! HAVE_STRUCT_STATFS_F_FSTYPENAME && defined(MOUNTED_VMOUNT)
static const char *
fstype_to_string (short int t)
{
  switch (t)
    {
#  ifdef MOUNT_PC
    case MOUNT_PC:
      return "pc";
#  endif
#  ifdef MOUNT_MFS
    case MOUNT_MFS:
      return "mfs";
#  endif
#  ifdef MOUNT_LO
    case MOUNT_LO:
      return "lo";
#  endif
#  ifdef MOUNT_TFS
    case MOUNT_TFS:
      return "tfs";
#  endif
#  ifdef MOUNT_TMP
    case MOUNT_TMP:
      return "tmp";
#  endif
#  ifdef MOUNT_UFS
   case MOUNT_UFS:
     return "ufs" ;
#  endif
#  ifdef MOUNT_NFS
   case MOUNT_NFS:
     return "nfs" ;
#  endif
#  ifdef MOUNT_MSDOS
   case MOUNT_MSDOS:
     return "msdos" ;
#  endif
#  ifdef MOUNT_LFS
   case MOUNT_LFS:
     return "lfs" ;
#  endif
#  ifdef MOUNT_LOFS
   case MOUNT_LOFS:
     return "lofs" ;
#  endif
#  ifdef MOUNT_FDESC
   case MOUNT_FDESC:
     return "fdesc" ;
#  endif
#  ifdef MOUNT_PORTAL
   case MOUNT_PORTAL:
     return "portal" ;
#  endif
#  ifdef MOUNT_NULL
   case MOUNT_NULL:
     return "null" ;
#  endif
#  ifdef MOUNT_UMAP
   case MOUNT_UMAP:
     return "umap" ;
#  endif
#  ifdef MOUNT_KERNFS
   case MOUNT_KERNFS:
     return "kernfs" ;
#  endif
#  ifdef MOUNT_PROCFS
   case MOUNT_PROCFS:
     return "procfs" ;
#  endif
#  ifdef MOUNT_AFS
   case MOUNT_AFS:
     return "afs" ;
#  endif
#  ifdef MOUNT_CD9660
   case MOUNT_CD9660:
     return "cd9660" ;
#  endif
#  ifdef MOUNT_UNION
   case MOUNT_UNION:
     return "union" ;
#  endif
#  ifdef MOUNT_DEVFS
   case MOUNT_DEVFS:
     return "devfs" ;
#  endif
#  ifdef MOUNT_EXT2FS
   case MOUNT_EXT2FS:
     return "ext2fs" ;
#  endif
    default:
      return "?";
    }
}
# endif

// Not used
#if 0
static const char *
fsp_to_string (const struct statfs *fsp)
{
# if HAVE_STRUCT_STATFS_F_FSTYPENAME
  return (char *) (fsp->f_fstypename);
# else
  return fstype_to_string (fsp->f_type);
# endif
}
#endif

#endif /* MOUNTED_GETMNTINFO */

#ifdef MOUNTED_VMOUNT                /* AIX.  */
static char *
fstype_to_string (int t)
{
  struct vfs_ent *e;

  e = getvfsbytype (t);
  if (!e || !e->vfsent_name)
    return "none";
  else
    return e->vfsent_name;
}
#endif /* MOUNTED_VMOUNT */

#if defined MOUNTED_GETMNTENT1 || defined MOUNTED_GETMNTENT2

/* Return the device number from MOUNT_OPTIONS, if possible.
   Otherwise return (dev_t) -1.  */

static dev_t
dev_from_mount_options (char const *mount_options)
{
  /* GNU/Linux allows file system implementations to define their own
     meaning for "dev=" mount options, so don't trust the meaning
     here.  */
#ifndef __linux__
  static char const dev_pattern[] = ",dev=";
  char const *devopt = strstr (mount_options, dev_pattern);

  if (devopt)
    {
      char const *optval = devopt + sizeof dev_pattern - 1;
      char *optvalend;
      unsigned long int dev;
      errno = 0;
      dev = strtoul (optval, &optvalend, 16);
      if (optval != optvalend
          && (*optvalend == '\0' || *optvalend == ',')
          && ! (dev == ULONG_MAX && errno == ERANGE)
          && dev == (dev_t) dev)
        return dev;
    }
#else
  (void)mount_options; // unused
# endif

  return -1;
}

#endif

/* Return a list of the currently mounted file systems, or NULL on error.
   Add each entry to the tail of the list so that they stay in order.
   If NEED_FS_TYPE is true, ensure that the file system type fields in
   the returned list are valid.  Otherwise, they might not be.  */

struct mount_entry *
read_file_system_list (bool need_fs_type)
{
  struct mount_entry *mount_list;
  struct mount_entry *me;
  struct mount_entry **mtail = &mount_list;
  (void)need_fs_type; // may be unused

#ifdef MOUNTED_LISTMNTENT
  {
    struct tabmntent *mntlist, *p;
    struct mntent *mnt;
    struct mount_entry *me;

    /* the third and fourth arguments could be used to filter mounts,
       but Crays doesn't seem to have any mounts that we want to
       remove. Specifically, automount create normal NFS mounts.
       */

    if (listmntent (&mntlist, KMTAB, NULL, NULL) < 0)
      return NULL;
    for (p = mntlist; p; p = p->next) {
      mnt = p->ment;
      me = xmalloc (sizeof *me);
      me->me_devname = xstrdup (mnt->mnt_fsname);
      me->me_mountdir = xstrdup (mnt->mnt_dir);
      me->me_type = xstrdup (mnt->mnt_type);
      me->me_type_malloced = 1;
      me->me_dummy = ME_DUMMY (me->me_devname, me->me_type);
      me->me_remote = ME_REMOTE (me->me_devname, me->me_type);
      me->me_dev = -1;
      *mtail = me;
      mtail = &me->me_next;
    }
    freemntlist (mntlist);
  }
#endif

#ifdef MOUNTED_GETMNTENT1 /* GNU/Linux, 4.3BSD, SunOS, HP-UX, Dynix, Irix.  */
  {
    struct mntent *mnt;
    const char *table = MOUNTED;
    FILE *fp;

    fp = setmntent (table, "r");
    if (fp == NULL)
      return NULL;

    while ((mnt = getmntent (fp)))
      {
        me = (mount_entry *) malloc (sizeof *me);
        me->me_devname = strdup (mnt->mnt_fsname);
        me->me_mountdir = strdup (mnt->mnt_dir);
        me->me_type = strdup (mnt->mnt_type);
        me->me_type_malloced = 1;
        me->me_dummy = ME_DUMMY (me->me_devname, me->me_type);
        me->me_remote = ME_REMOTE (me->me_devname, me->me_type);
        me->me_dev = dev_from_mount_options (mnt->mnt_opts);

        /* Add to the linked list. */
        *mtail = me;
        mtail = &me->me_next;
      }

    if (endmntent (fp) == 0)
      goto free_then_fail;
  }
#endif /* MOUNTED_GETMNTENT1. */

#ifdef MOUNTED_GETMNTINFO        /* 4.4BSD.  */
  {
    struct statfs *fsp;
    int entries;

    entries = getmntinfo (&fsp, MNT_NOWAIT);
    if (entries < 0)
      return NULL;
    for (; entries-- > 0; fsp++)
      {
        me = (mount_entry *) malloc (sizeof *me);
        me->me_devname = strdup (fsp->f_mntfromname);
        me->me_mountdir = strdup (fsp->f_mntonname);
#if defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__)
        me->me_type = fsp->f_fstypename;
#else
        me->me_type = fsp->fs_typename;
#endif
        me->me_type_malloced = 0;
        me->me_dummy = ME_DUMMY (me->me_devname, me->me_type);
        me->me_remote = ME_REMOTE (me->me_devname, me->me_type);
        me->me_dev = (dev_t) -1;        /* Magic; means not known yet. */

        /* Add to the linked list. */
        *mtail = me;
        mtail = &me->me_next;
      }
  }
#endif /* MOUNTED_GETMNTINFO */

#ifdef MOUNTED_GETMNTINFO2        /* NetBSD 3.0.  */
  {
    struct statvfs *fsp;
    int entries;

    entries = getmntinfo (&fsp, MNT_NOWAIT);
    if (entries < 0)
      return NULL;
    for (; entries-- > 0; fsp++)
      {
        me = malloc (sizeof *me);
        me->me_devname = strdup (fsp->f_mntfromname);
        me->me_mountdir = strdup (fsp->f_mntonname);
        me->me_type = strdup (fsp->f_fstypename);
        me->me_type_malloced = 1;
        me->me_dummy = ME_DUMMY (me->me_devname, me->me_type);
        me->me_remote = ME_REMOTE (me->me_devname, me->me_type);
        me->me_dev = (dev_t) -1;        /* Magic; means not known yet. */

        /* Add to the linked list. */
        *mtail = me;
        mtail = &me->me_next;
      }
  }
#endif /* MOUNTED_GETMNTINFO2 */

#ifdef MOUNTED_GETMNT                /* Ultrix.  */
  {
    int offset = 0;
    int val;
    struct fs_data fsd;

    while (errno = 0,
           0 < (val = getmnt (&offset, &fsd, sizeof (fsd), NOSTAT_MANY,
                              (char *) 0)))
      {
        me = xmalloc (sizeof *me);
        me->me_devname = xstrdup (fsd.fd_req.devname);
        me->me_mountdir = xstrdup (fsd.fd_req.path);
        me->me_type = gt_names[fsd.fd_req.fstype];
        me->me_type_malloced = 0;
        me->me_dummy = ME_DUMMY (me->me_devname, me->me_type);
        me->me_remote = ME_REMOTE (me->me_devname, me->me_type);
        me->me_dev = fsd.fd_req.dev;

        /* Add to the linked list. */
        *mtail = me;
        mtail = &me->me_next;
      }
    if (val < 0)
      goto free_then_fail;
  }
#endif /* MOUNTED_GETMNT. */

#if defined MOUNTED_FS_STAT_DEV /* BeOS */
  {
    /* The next_dev() and fs_stat_dev() system calls give the list of
       all file systems, including the information returned by statvfs()
       (fs type, total blocks, free blocks etc.), but without the mount
       point. But on BeOS all file systems except / are mounted in the
       rootfs, directly under /.
       The directory name of the mount point is often, but not always,
       identical to the volume name of the device.
       We therefore get the list of subdirectories of /, and the list
       of all file systems, and match the two lists.  */

    DIR *dirp;
    struct rootdir_entry
      {
        char *name;
        dev_t dev;
        ino_t ino;
        struct rootdir_entry *next;
      };
    struct rootdir_entry *rootdir_list;
    struct rootdir_entry **rootdir_tail;
    int32 pos;
    dev_t dev;
    fs_info fi;

    /* All volumes are mounted in the rootfs, directly under /. */
    rootdir_list = NULL;
    rootdir_tail = &rootdir_list;
    dirp = opendir ("/");
    if (dirp)
      {
        struct dirent *d;

        while ((d = readdir (dirp)) != NULL)
          {
            char *name;
            struct stat statbuf;

            if (strcmp (d->d_name, "..") == 0)
              continue;

            if (strcmp (d->d_name, ".") == 0)
              name = strdup ("/");
            else
              {
                name = malloc (1 + strlen (d->d_name) + 1);
                name[0] = '/';
                strcpy (name + 1, d->d_name);
              }

            if (lstat (name, &statbuf) >= 0 && S_ISDIR (statbuf.st_mode))
              {
                struct rootdir_entry *re = malloc (sizeof *re);
                re->name = name;
                re->dev = statbuf.st_dev;
                re->ino = statbuf.st_ino;

                /* Add to the linked list.  */
                *rootdir_tail = re;
                rootdir_tail = &re->next;
              }
            else
              free (name);
          }
        closedir (dirp);
      }
    *rootdir_tail = NULL;

    for (pos = 0; (dev = next_dev (&pos)) >= 0; )
      if (fs_stat_dev (dev, &fi) >= 0)
        {
          /* Note: fi.dev == dev. */
          struct rootdir_entry *re;

          for (re = rootdir_list; re; re = re->next)
            if (re->dev == fi.dev && re->ino == fi.root)
              break;

          me = malloc (sizeof *me);
          me->me_devname = strdup (fi.device_name[0] != '\0' ? fi.device_name : fi.fsh_name);
          me->me_mountdir = strdup (re != NULL ? re->name : fi.fsh_name);
          me->me_type = strdup (fi.fsh_name);
          me->me_type_malloced = 1;
          me->me_dev = fi.dev;
          me->me_dummy = 0;
          me->me_remote = (fi.flags & B_FS_IS_SHARED) != 0;

          /* Add to the linked list. */
          *mtail = me;
          mtail = &me->me_next;
        }
    *mtail = NULL;

    while (rootdir_list != NULL)
      {
        struct rootdir_entry *re = rootdir_list;
        rootdir_list = re->next;
        free (re->name);
        free (re);
      }
  }
#endif /* MOUNTED_FS_STAT_DEV */

#if defined MOUNTED_GETFSSTAT        /* __alpha running OSF_1 */
  {
    int numsys, counter;
    size_t bufsize;
    struct statfs *stats;

    numsys = getfsstat ((struct statfs *)0, 0L, MNT_NOWAIT);
    if (numsys < 0)
      return (NULL);
      /*
    if (SIZE_MAX / sizeof *stats <= numsys)
      xalloc_die ();*/

    bufsize = (1 + numsys) * sizeof *stats;
    stats = malloc (bufsize);
    numsys = getfsstat (stats, bufsize, MNT_NOWAIT);

    if (numsys < 0)
      {
        free (stats);
        return (NULL);
      }

    for (counter = 0; counter < numsys; counter++)
      {
        me = malloc (sizeof *me);
        me->me_devname = strdup (stats[counter].f_mntfromname);
        me->me_mountdir = strdup (stats[counter].f_mntonname);
        //me->me_type = strdup (FS_TYPE (stats[counter]));
        me->me_type_malloced = 1;
        me->me_dummy = ME_DUMMY (me->me_devname, me->me_type);
        me->me_remote = ME_REMOTE (me->me_devname, me->me_type);
        me->me_dev = (dev_t) -1;        /* Magic; means not known yet. */

        /* Add to the linked list. */
        *mtail = me;
        mtail = &me->me_next;
      }

    free (stats);
  }
#endif /* MOUNTED_GETFSSTAT */

#if defined MOUNTED_FREAD || defined MOUNTED_FREAD_FSTYP /* SVR[23].  */
  {
    struct mnttab mnt;
    char *table = "/etc/mnttab";
    FILE *fp;

    fp = fopen (table, "r");
    if (fp == NULL)
      return NULL;

    while (fread (&mnt, sizeof mnt, 1, fp) > 0)
      {
        me = xmalloc (sizeof *me);
# ifdef GETFSTYP                        /* SVR3.  */
        me->me_devname = xstrdup (mnt.mt_dev);
# else
        me->me_devname = xmalloc (strlen (mnt.mt_dev) + 6);
        strcpy (me->me_devname, "/dev/");
        strcpy (me->me_devname + 5, mnt.mt_dev);
# endif
        me->me_mountdir = xstrdup (mnt.mt_filsys);
        me->me_dev = (dev_t) -1;        /* Magic; means not known yet. */
        me->me_type = "";
        me->me_type_malloced = 0;
# ifdef GETFSTYP                        /* SVR3.  */
        if (need_fs_type)
          {
            struct statfs fsd;
            char typebuf[FSTYPSZ];

            if (statfs (me->me_mountdir, &fsd, sizeof fsd, 0) != -1
                && sysfs (GETFSTYP, fsd.f_fstyp, typebuf) != -1)
              {
                me->me_type = xstrdup (typebuf);
                me->me_type_malloced = 1;
              }
          }
# endif
        me->me_dummy = ME_DUMMY (me->me_devname, me->me_type);
        me->me_remote = ME_REMOTE (me->me_devname, me->me_type);

        /* Add to the linked list. */
        *mtail = me;
        mtail = &me->me_next;
      }

    if (ferror (fp))
      {
        /* The last fread() call must have failed.  */
        int saved_errno = errno;
        fclose (fp);
        errno = saved_errno;
        goto free_then_fail;
      }

    if (fclose (fp) == EOF)
      goto free_then_fail;
  }
#endif /* MOUNTED_FREAD || MOUNTED_FREAD_FSTYP.  */

#ifdef MOUNTED_GETMNTTBL        /* DolphinOS goes its own way.  */
  {
    struct mntent **mnttbl = getmnttbl (), **ent;
    for (ent=mnttbl;*ent;ent++)
      {
        me = xmalloc (sizeof *me);
        me->me_devname = xstrdup ( (*ent)->mt_resource);
        me->me_mountdir = xstrdup ( (*ent)->mt_directory);
        me->me_type = xstrdup ((*ent)->mt_fstype);
        me->me_type_malloced = 1;
        me->me_dummy = ME_DUMMY (me->me_devname, me->me_type);
        me->me_remote = ME_REMOTE (me->me_devname, me->me_type);
        me->me_dev = (dev_t) -1;        /* Magic; means not known yet. */

        /* Add to the linked list. */
        *mtail = me;
        mtail = &me->me_next;
      }
    endmnttbl ();
  }
#endif

#ifdef MOUNTED_GETMNTENT2        /* SVR4.  */
  {
    struct mnttab mnt;
    char *table = MNTTAB;
    FILE *fp;
    int ret;
    int lockfd = -1;

# if defined F_RDLCK && defined F_SETLKW
    /* MNTTAB_LOCK is a macro name of our own invention; it's not present in
       e.g. Solaris 2.6.  If the SVR4 folks ever define a macro
       for this file name, we should use their macro name instead.
       (Why not just lock MNTTAB directly?  We don't know.)  */
#  ifndef MNTTAB_LOCK
#   define MNTTAB_LOCK "/etc/.mnttab.lock"
#  endif
    lockfd = open (MNTTAB_LOCK, O_RDONLY);
    if (0 <= lockfd)
      {
        struct flock flock;
        flock.l_type = F_RDLCK;
        flock.l_whence = SEEK_SET;
        flock.l_start = 0;
        flock.l_len = 0;
        while (fcntl (lockfd, F_SETLKW, &flock) == -1)
          if (errno != EINTR)
            {
              int saved_errno = errno;
              close (lockfd);
              errno = saved_errno;
              return NULL;
            }
      }
    else if (errno != ENOENT)
      return NULL;
# endif

    errno = 0;
    fp = fopen (table, "r");
    if (fp == NULL)
      ret = errno;
    else
      {
        while ((ret = getmntent (fp, &mnt)) == 0)
          {
            me = xmalloc (sizeof *me);
            me->me_devname = xstrdup (mnt.mnt_special);
            me->me_mountdir = xstrdup (mnt.mnt_mountp);
            me->me_type = xstrdup (mnt.mnt_fstype);
            me->me_type_malloced = 1;
            me->me_dummy = MNT_IGNORE (&mnt) != 0;
            me->me_remote = ME_REMOTE (me->me_devname, me->me_type);
            me->me_dev = dev_from_mount_options (mnt.mnt_mntopts);

            /* Add to the linked list. */
            *mtail = me;
            mtail = &me->me_next;
          }

        ret = fclose (fp) == EOF ? errno : 0 < ret ? 0 : -1;
      }

    if (0 <= lockfd && close (lockfd) != 0)
      ret = errno;

    if (0 <= ret)
      {
        errno = ret;
        goto free_then_fail;
      }
  }
#endif /* MOUNTED_GETMNTENT2.  */

#ifdef MOUNTED_VMOUNT                /* AIX.  */
  {
    int bufsize;
    char *entries, *thisent;
    struct vmount *vmp;
    int n_entries;
    int i;

    /* Ask how many bytes to allocate for the mounted file system info.  */
    if (mntctl (MCTL_QUERY, sizeof bufsize, (struct vmount *) &bufsize) != 0)
      return NULL;
    entries = xmalloc (bufsize);

    /* Get the list of mounted file systems.  */
    n_entries = mntctl (MCTL_QUERY, bufsize, (struct vmount *) entries);
    if (n_entries < 0)
      {
        int saved_errno = errno;
        free (entries);
        errno = saved_errno;
        return NULL;
      }

    for (i = 0, thisent = entries;
         i < n_entries;
         i++, thisent += vmp->vmt_length)
      {
        char *options, *ignore;

        vmp = (struct vmount *) thisent;
        me = xmalloc (sizeof *me);
        if (vmp->vmt_flags & MNT_REMOTE)
          {
            char *host, *dir;

            me->me_remote = 1;
            /* Prepend the remote dirname.  */
            host = thisent + vmp->vmt_data[VMT_HOSTNAME].vmt_off;
            dir = thisent + vmp->vmt_data[VMT_OBJECT].vmt_off;
            me->me_devname = xmalloc (strlen (host) + strlen (dir) + 2);
            strcpy (me->me_devname, host);
            strcat (me->me_devname, ":");
            strcat (me->me_devname, dir);
          }
        else
          {
            me->me_remote = 0;
            me->me_devname = xstrdup (thisent +
                                      vmp->vmt_data[VMT_OBJECT].vmt_off);
          }
        me->me_mountdir = xstrdup (thisent + vmp->vmt_data[VMT_STUB].vmt_off);
        me->me_type = xstrdup (fstype_to_string (vmp->vmt_gfstype));
        me->me_type_malloced = 1;
        options = thisent + vmp->vmt_data[VMT_ARGS].vmt_off;
        ignore = strstr (options, "ignore");
        me->me_dummy = (ignore
                        && (ignore == options || ignore[-1] == ',')
                        && (ignore[sizeof "ignore" - 1] == ','
                            || ignore[sizeof "ignore" - 1] == '\0'));
        me->me_dev = (dev_t) -1; /* vmt_fsid might be the info we want.  */

        /* Add to the linked list. */
        *mtail = me;
        mtail = &me->me_next;
      }
    free (entries);
  }
#endif /* MOUNTED_VMOUNT. */

  *mtail = NULL;
  return mount_list;
  
#if defined(MOUNTED_GETMNTENT1) || defined(MOUNTED_GETMNTENT2) || defined(MOUNTED_GETMNT) || defined(MOUNTED_FREAD) || defined(MOUNTED_FREAD_FSTYP)
  free_then_fail:
  {
    int saved_errno = errno;
    *mtail = NULL;

    while (mount_list)
      {
        me = mount_list->me_next;
        free (mount_list->me_devname);
        free (mount_list->me_mountdir);
        if (mount_list->me_type_malloced)
          free (mount_list->me_type);
        free (mount_list);
        mount_list = me;
      }

    errno = saved_errno;
    return NULL;
  }
#endif
}

void free_file_system_list(struct mount_entry * mount_list)
{
  struct mount_entry * me;
  while (mount_list)
  {
    me = mount_list->me_next;
    free (mount_list->me_devname);
    free (mount_list->me_mountdir);
    if (mount_list->me_type_malloced)
      free (mount_list->me_type);
    free (mount_list);
    mount_list = me;
  }
}

#endif
