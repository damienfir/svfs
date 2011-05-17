#include "params.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <sys/wait.h>
#include <pthread.h>


/* Stores the absolute path in "fpath", based on the file name given in "path" */
static void svfs_fullpath(char fpath[PATH_MAX], const char *path) {
	strcpy(fpath, SVFS_DATA->rootdir);
	strncat(fpath, path, PATH_MAX);
}

#define DEBUG
#define MAX_SIZE 256

#ifdef DEBUG
static FILE *m_debug;
#endif

static void my_log(char *function, const char *path) {
#ifdef DEBUG
	fprintf(m_debug, "[%s] %s\n", function, path);
	fflush(m_debug);
#endif
}

char format[10] = "%s.%d";

typedef struct backup_ backup;
typedef backup* pbackup;
struct backup_
{
    int id;
    time_t time;

    pbackup next;
};


typedef struct backuped_file_ backuped_file;
typedef backuped_file * pbackuped_file;
struct backuped_file_
{
    char* name;
    //simple list system with head maintaining the id of the backuped file
    pbackup backups;
    int N;

	int last_id;
    int open;
    pbackuped_file next;
};

pbackuped_file list = NULL;

int copy(char* src, char* dest)
{
	my_log("copy", "");
	int pid = fork();
	int error;

	if (pid <= 0) {
		char * argv[2] = {src, dest};
		execvp("cp", argv);
	} else {
		waitpid(pid, &error, 0);
		my_log(src, dest);
	}

	return error;
}


void get_filename(pbackuped_file file, pbackup backup, char * dest) {
	sprintf(dest, format, file->name, backup->id);
}

pbackuped_file find_file(pbackuped_file list, char* filename)
{
	my_log("find_file", filename);
	pbackuped_file file = NULL;

	while(list != NULL) 
	{
		if (strcmp(list->name, filename)) 
		{
			file = list;
			break;
		}
		list = list->next;
	}
			
	return file;
}


pbackuped_file create_backuped_file(char* filename)
{
	my_log("create_backuped_file", filename);
    pbackuped_file f = malloc(sizeof(backuped_file));
	
    f->name = calloc(sizeof(char) , strlen(filename) + 1);
    strcpy(f->name , filename);

    f->N = 10;
    f->backups = 0;
    f->next = 0;
	f->last_id = 0;
    f->open = 1;
	return f;
};

pbackuped_file add_backuped_file(pbackuped_file* list, char* name)
{
	my_log("add_backuped_file", name);
    pbackuped_file n = create_backuped_file(name);

    if(list == 0)
        (*list) = n;
    else
    {
        pbackuped_file l = (*list);
        while(l->next != 0)
            l = l->next;
        l->next = n;
    }

	return n;
}

pbackuped_file remove_backuped_file_by_file(pbackuped_file* list, pbackuped_file f)
{
	my_log("remove_backuped_file_by_file", "");
    if(list == 0)
        return 0;

    pbackuped_file l = *list;
    pbackuped_file p = 0;
    do
    {
        if(l == f)
        {
            if(p == 0) // first item
				(*list) = l->next;
			else
				p->next = l->next;
			
			break;
        }
        else
            p = l;
        l = l->next;
    }
    while(l != 0);
	return f;
}

pbackuped_file remove_backuped_file(pbackuped_file* list, char* name)
{
	my_log("remove_backuped_file", name);
    if(list == 0)
        return 0;
	
	return remove_backuped_file_by_file(list, find_file(*list, name));
}

pbackup add_backup(pbackuped_file file)
{
	my_log("Add Backup ", file->name);
	pbackup new = malloc(sizeof(backup));
	new->time = time(NULL);
	new->id = file->last_id + 1;
	new->next = NULL;

	file->last_id = new->id;

	pbackup first = file->backups;
	if(first != 0) // backups already exist
	{
		while(first->next != NULL) 
		{
			first = first->next;
		}
		first->next = new;
	} else { // first backup of the list
		file->backups = new;
	}

	char new_filename[MAX_SIZE];
	get_filename(file, new, new_filename);
	copy(file->name, new_filename);

	return new;
}

// ------------ function to call everywhere -------------
void create_backup(pbackuped_file list, char* filename)
{
	my_log("create_backup", filename);
	pbackuped_file file = find_file(list, filename);

	if (file == NULL)
		file = add_backuped_file(&list, filename);

	add_backup(file); 
}


pbackup add_backup_by_name(pbackuped_file list, char* filename) 
{
	my_log("Add backup by name ",filename);
	return add_backup(find_file(list, filename));
}

void remove_backup_by_file(pbackuped_file file)
{
	my_log("Remove backup by file ", file->name);
	char filename[MAX_SIZE];

	if(file->backups == 0)
		return;

	pbackup backup = file->backups;
	sprintf(filename, format, file->name, backup->id);

	file->backups = backup->next;
	free(backup);

	unlink(filename);
}


void remove_backup_by_name(pbackuped_file list, char* filename) 
{
	my_log("Remove backup by name ",filename);
	remove_backup_by_file(find_file(list, filename));
}

void rename_backup_file(pbackuped_file list, char * old_filename , char * new_filename)
{
	my_log("rename_backup_file", old_filename);
	pbackuped_file file = find_file(list, old_filename);
	
	free(file->name);

	file->name = calloc(sizeof(char), strlen(new_filename)+1);
	strcpy(file->name,new_filename);	
	
	//now rename all backups

	pbackup head = file->backups;
	
	while(head != NULL) 
	{
		char  old[MAX_SIZE];
		sprintf(old , format , old_filename , head->id);
	
		char new[MAX_SIZE];
		sprintf(new , format , new_filename , head->id);
		
		rename(old, new);
		head = head->next;
	}
}




/** Get file attributes. */
int svfs_getattr(const char *path, struct stat *statbuf) {
	char fpath[PATH_MAX];

	my_log("svfs_getattr", path);
	svfs_fullpath(fpath, path);

	if (lstat(fpath, statbuf))
		return -errno;
	return 0;
}

/** Creates a file node */
int svfs_mknod(const char *path, mode_t mode, dev_t dev) {
	int retstat;
	char fpath[PATH_MAX];

	my_log("svfs_mknod", path);
	svfs_fullpath(fpath, path);

	retstat = mknod(fpath, mode, dev);
	if (retstat)
		return -errno;
	return 0;
}

/** Create a directory */
int svfs_mkdir(const char *path, mode_t mode) {
	char fpath[PATH_MAX];

	my_log("svfs_mkdir", path);
	svfs_fullpath(fpath, path);

	if (mkdir(fpath, mode))
		return -errno;
	return 0;
}

/** Remove a file */
int svfs_unlink(const char *path) {
	char fpath[PATH_MAX];

	my_log("svfs_unlink", path);
	svfs_fullpath(fpath, path);

	if(unlink(fpath))
		return -errno;
	return 0;
}

/** Remove a directory */
int svfs_rmdir(const char *path) {
	char fpath[PATH_MAX];

	my_log("svfs_rmdir", path);
	svfs_fullpath(fpath, path);

	if (rmdir(fpath))
		return -errno;
	return 0;
}

/** Rename a file */
int svfs_rename(const char *path, const char *newpath) {
	char fpath[PATH_MAX];
	char fnewpath[PATH_MAX];

	my_log("svfs_rename", path);
	svfs_fullpath(fpath, path);
	svfs_fullpath(fnewpath, newpath);

	pbackuped_file f = find_file(list,fpath);
	
	if(f != 0)
	{
		free(f->name);
		f->name = calloc(sizeof(char), PATH_MAX);
		strcpy(f->name, fpath);

		pbackup b = f->backups;

		while(b != 0)
		{
			char new_name[PATH_MAX];
			char old_name[PATH_MAX];
			strcpy(old_name, fpath);
			sprintf(new_name,format,fpath, b->id);
			if(rename(old_name, new_name))
				my_log("Error", "cannot rename a backup file");
			b = b->next;
		}
	}

	if (rename(fpath, fnewpath))
		return -errno;
	return 0;
}

/** Change the permission bits of a file */
int svfs_chmod(const char *path, mode_t mode) {
	char fpath[PATH_MAX];

	my_log("svfs_chmod", path);
	svfs_fullpath(fpath, path);

	if (chmod(fpath, mode))
		return -errno;
	return 0;
}

/** Change the owner and group of a file */
int svfs_chown(const char *path, uid_t uid, gid_t gid) {
	char fpath[PATH_MAX];

	my_log("svfs_chown", path);
	svfs_fullpath(fpath, path);

	if (chown(fpath, uid, gid))
		return -errno;
	return 0;
}

/** Change the size of a file */
int svfs_truncate(const char *path, off_t newsize) {
	char fpath[PATH_MAX];

	my_log("svfs_truncate", path);
	svfs_fullpath(fpath, path);

	if (truncate(fpath, newsize))
		return -errno;
	return 0;
}

/** Change the access and/or modification times of a file */
int svfs_utime(const char *path, struct utimbuf *ubuf) {
	char fpath[PATH_MAX]; 
	my_log("svfs_utime", path);
	svfs_fullpath(fpath, path);

	if (utime(fpath, ubuf))
		return -errno;
	return 0;
}

/** File open operation */
int svfs_open(const char *path, struct fuse_file_info *fi) {
	int fd;
	char fpath[PATH_MAX];

	my_log("svfs_open", path);
	svfs_fullpath(fpath, path);
	my_log("svfs_open_full",fpath);
	char t[2] = {(char)((fi->flags & O_WRONLY) + '0'),'\0'};
	my_log("file open mode write",t);
	fd = open(fpath, fi->flags);
	fi->fh = fd;

	if (fd < 0)
		return -1;

	//open success !


	return 0;
}

/** Read data from an open file */
int svfs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi) {
	my_log("svfs_read", path);
	return (int) pread(fi->fh, buf, size, offset);
}

/** Write data to an open file */
int svfs_write(const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi) {
	my_log("svfs_write", path);
	char fpath[PATH_MAX];
	svfs_fullpath(fpath, path);
	create_backup(list, fpath);
	return pwrite(fi->fh, buf, size, offset);
}

int svfs_flush(const char *path, struct fuse_file_info *fi) {
	return 0;
}

/** Release an open file */
int svfs_release(const char *path, struct fuse_file_info *fi) {
	my_log("svfs_release", path);
	return close(fi->fh);
}

/** Open directory */
int svfs_opendir(const char *path, struct fuse_file_info *fi) {
	DIR *dp;
	char fpath[PATH_MAX];

	my_log("svfs_opendir", path);
	svfs_fullpath(fpath, path);

	dp = opendir(fpath);
	fi->fh = (intptr_t) dp;

	if (dp == NULL)
		return -errno;
	return 0;
}

/** Read directory */
int svfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		off_t offset, struct fuse_file_info *fi) {
	DIR *dp;
	struct dirent *de;

	my_log("svfs_readdir", path);
	dp = (DIR *) (uintptr_t) fi->fh;
    de = readdir(dp);

	if (de == NULL)
		return -errno;

	do {
		if (filler(buf, de->d_name, NULL, 0) != 0) {
			return -errno;
		}
	} while ((de = readdir(dp)) != NULL);

	return 0;
}

/** Release directory */
int svfs_releasedir(const char *path, struct fuse_file_info *fi) {
	my_log("svfs_releasedir", path);
	if (closedir((DIR *) (uintptr_t) fi->fh))
		return -errno;
	return 0;
}

/** Initialize filesystem */
void *svfs_init(struct fuse_conn_info *conn) {
	return SVFS_DATA;
}

/** Clean up filesystem */
void svfs_destroy(void *userdata) {
}

/** Create and open a file */
int svfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	char fpath[PATH_MAX];
	int fd;

	my_log("svfs_create", path);
	svfs_fullpath(fpath, path);

	fd = creat(fpath, mode);
	fi->fh = fd;
	if (fd < 0)
		return -1;
	return 0;
}

struct fuse_operations svfs_oper = {
	.getattr = svfs_getattr,
	.mknod = svfs_mknod,
	.mkdir = svfs_mkdir,
	.unlink = svfs_unlink,
	.rmdir = svfs_rmdir,
	.rename = svfs_rename,
	.chmod = svfs_chmod,
	.chown = svfs_chown,
	.truncate = svfs_truncate,
	.utime = svfs_utime,
	.open = svfs_open,
	.read = svfs_read,
	.write = svfs_write,
	.flush = svfs_flush,
	.opendir = svfs_opendir,
	.readdir = svfs_readdir,
	.releasedir = svfs_releasedir,
	.init = svfs_init,
	.destroy = svfs_destroy,
};

void svfs_usage() {
	fprintf(stderr, "Usage:  svfs rootDirectory mountPointDirectory\n");
	exit(1);
}


// 10 mins = time of live of backups
#define BASE_LIVING_TIME 600

void GarbageCollector()
{
	my_log("Garbage collector", "running ...");
    while(1)
    {
		my_log("Garbage collector", "collectoring");
        int act = time(0);
        pbackuped_file l = list;
        //list non empty -> backup exist
        while(l != 0)
        {
            int n = l->N;
            pbackup b = l->backups;
            while(b != 0)
            {
                pbackup temp = b->next;
                if(act - b->time > n) //
                    remove_backup_by_file(l);
                else
                    break; // only more recent backup are comming
                b = temp;
            }
            int c;
            for(c = 0, b = l->backups; b != 0; c++, b = b->next);

            // adaptive N
            // actually adds x minutes where x is the number of backup files create per minutes
            l->N = BASE_LIVING_TIME + (c/(BASE_LIVING_TIME/60))*60;

            // if their is no more backups and the file is closed -> remove it
            pbackuped_file temp = l->next;
            if(c == 0 && l->open == 0)
                remove_backuped_file_by_file(&list, l);

            l = temp;
        }
		my_log("Garbage collector", "sleeping");
        int c = sleep(60);
		char time[2];
		time[0] = c/10 + '0';
		time[1] = c % 10 + '0';
		my_log("Garbage collector slept", time);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
	int i;
	struct svfs_state *svfs_data;

	svfs_data = malloc(sizeof(struct svfs_state));
	if (svfs_data == NULL)
		exit(0);

#ifdef DEBUG
	m_debug = fopen("svfs.log", "a");
#endif

	for (i = 1; (i < argc) && (argv[i][0] == '-'); i++)
		if (argv[i][1] == 'o')
			i++;

	if ((argc - i) != 2)
		svfs_usage();

	svfs_data->rootdir = realpath(argv[i], NULL);

	argv[i] = argv[i + 1];
	argc--;

   int pid = fork();

   if(pid <= 0)
		GarbageCollector();

	// We should not return from here
	return fuse_main(argc, argv, &svfs_oper, svfs_data);
}
