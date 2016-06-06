#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#define PATHLEN_MAX 1024

#include "debugf.c"

//#include "cache.c"

static char initial_working_dir[PATHLEN_MAX+1] ={ '\0' };
static char cached_mountpoint[PATHLEN_MAX+1] ={ '\0' };
static int save_dir;

const char *full(const char *path) /* ajout du point de montage au chemin (CHEMIN COMPLET) */;
const char *full(const char *path) /* ajout du point de montage au chemin (CHEMIN COMPLET) */
{

  char *ep, *buff;

  buff = strdup(path+1); if (buff == NULL) exit(1);

  ep = buff + strlen(buff) - 1; if (*ep == '/') *ep = '\0';

  if (*buff == '\0') strcpy(buff, ".");

  return buff;
}


/*
 * Cette fonction lit les métadonnées d'un chemin
 * passé en paramètre. Doit systématiquement être appelé  
 * avant toute opération.
 */
static int fonction_getattr(const char *path, struct stat *stbuf)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: getattr(%s)\n", path);
    res = lstat(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

/**
 * Cette fonction retourne:
 * -ENOENT si le chemin n'existe pas
 * -EACCESS si le chemin est inaccessible
 * O si tout est OK
 */
static int fonction_access(const char *path, int mask)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: access(%s)\n", path);
    res = access(path, mask);
    if (res == -1)
        return -errno;

    return 0;
}

/**
  * Si le paramètre "path" est un chemin symbolique
  * On doit remplir "buf" avec la cible du  
  * chemin symbolique.
  **/
static int fonction_readlink(const char *path, char *buf, size_t size)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: readlink(%s)\n", path);
    res = readlink(path, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}

/**
  * Retourne un ou plusieurs dossiers.
  * Trouve le premier dossier en fonction du "offset" que l'on a.
  * Crée éventuellement un struc qui décrit le ficher (un peu comme getattr).
  * On appelle la fonction filler avec les arguments suivants: buf, NULL, l'adresse du struct (ou NULL) et l'offset du prochain dossier
  * Si le filler retourne une valeur != de 0 ou s'il n'y a plus de fichiers, retourner 0
  * Trouver le prochain fichier.
  * Recommencer.
  **/
static int fonction_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

path = full(path);
debugf("PROJET DEBUG FUSE: readdir(%s)\n", path);
    dp = opendir(path);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
    return 0;
}

/**
 * BONUS: 
 * Cette fonction est rarement utilisée, elle sert à créer un fichier "device"
 **/
static int fonction_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: mknod(%s)\n", path);
    if (S_ISREG(mode)) {
        res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(path, mode);
    else
        res = mknod(path, mode, rdev);
    if (res == -1)
        return -errno;

    return 0;
}

/**
 * Crée un dossier avec le nom passé en paramètre.
 * Les permissions sont dans le paramètre 'mode'
 **/
static int fonction_mkdir(const char *path, mode_t mode)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: mkdir(%s)\n", path);
    res = mkdir(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

/**
 * Supprime le fichier (ou lien symbolique etc.) en fonction du paramètre donné.
 **/
static int fonction_unlink(const char *path)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: unlink(%s)\n", path);
    res = unlink(path);
    if (res == -1)
        return -errno;

    return 0;
}


/**
 * Supprime un dossier passé en paramètre
 **/
static int fonction_rmdir(const char *path)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: rmdir(%s)\n", path);
    res = rmdir(path);
    if (res == -1)
        return -errno;

    return 0;
}

/**
 * BONUS:
 * Crée un lien symbolique.
 **/
static int fonction_symlink(const char *from, const char *to)
{
    int res;

from = full(from);
to = full(to);
debugf("PROJET DEBUG FUSE: symlink(%s, %s)\n", from, to);
    res = symlink(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

/**
 * Renommer un fichier (ancien, nouveau)
 **/
static int fonction_rename(const char *from, const char *to)
{
    int res;

from = full(from);
to = full(to);
debugf("PROJET DEBUG FUSE: rename(%s, %s)\n", from, to);
    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

/**
 * Crée un lien physique. (nouvel inode à chaque fois)
 **/
static int fonction_link(const char *from, const char *to)
{
    int res;

from = full(from);
to = full(to);
debugf("PROJET DEBUG FUSE: link(%s, %s)\n", from, to);
    res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

/**
 * BONUS: Mettre en place les permissions
 **/
static int fonction_chmod(const char *path, mode_t mode)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: chmod(%s)\n", path);
    res = chmod(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}


/**
 * BONUS: Change le "propriétaire" et le groupe de l'objet
 **/
static int fonction_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: lchown(%s)\n", path);
    res = lchown(path, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}


/**
 * Cette fonction ouvre un fichier.
 */
static int fonction_open(const char *path, struct fuse_file_info *fi)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: open(%s)\n", path);
    res = open(path, fi->flags);
    if (res == -1)
        return -errno;

    close(res);
    return 0;
}

/**
 * BONUS: Lis la taille en bytes d'un fichier.
 **/
static int fonction_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;
path = full(path);
debugf("PROJET DEBUG FUSE: read(%s)\n", path);
    fd = open(path, O_RDONLY);
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

/**
 * BONUS: retourne des statistiques à propos du filesystem
 **/
static int fonction_statfs(const char *path, struct statvfs *stbuf)
{
    int res;

path = full(path);
debugf("PROJET DEBUG FUSE: statvfs(%s)\n", path);
    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static void *fonction_init(void)
{
  debugf("PROJET DEBUG FUSE: init()\n");
  fchdir(save_dir);
  close(save_dir);
  return NULL;
}

static struct fuse_operations fonction_oper = {
    .init       = fonction_init,
    .getattr	= fonction_getattr,
    .access	= fonction_access,
    .readlink	= fonction_readlink,
    .readdir	= fonction_readdir,
    .mknod	= fonction_mknod,
    .mkdir	= fonction_mkdir,
    .symlink	= fonction_symlink,
    .unlink	= fonction_unlink,
    .rmdir	= fonction_rmdir,
    .rename	= fonction_rename,
    .link	= fonction_link,
    .chmod	= fonction_chmod,
    .chown	= fonction_chown,
    .open	= fonction_open,
    .read	= fonction_read,
    .statfs	= fonction_statfs
};

int main(int argc, char *argv[])
{
int rc;

    umask(0);
    getcwd(initial_working_dir, PATHLEN_MAX);
debugf("PROJET DEBUG FUSE: cwd=%s\n", initial_working_dir);

debugf("PROJET DEBUG FUSE: main(%s, %s, %s, %s)\n", argv[0], argv[1], argv[2], argv[3]);
    strncpy(cached_mountpoint, argv[1], strlen(argv[1]));
debugf("PROJET DEBUG FUSE: mountpoint=%s\n", cached_mountpoint);
    save_dir = open(cached_mountpoint, O_RDONLY);
    rc = fuse_main(argc, argv, &fonction_oper, NULL);

debugf("PROJET DEBUG FUSE: umount(%s, %s, %s, %s)\n", argv[0], argv[1], argv[2], argv[3]);
    return rc;
}
