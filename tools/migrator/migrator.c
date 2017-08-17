/*
 * migrator - a utility to automate file compression status for e2comprII
 *
 * Written by Charles Coffing, <kiowa@mit.edu>
 *
 * This file may be redistributed under the terms of the GNU Public License.
 *
 *
 * Implementation notes:
 *
 * ratio * size = saved bytes
 *
 * "freeze" means to change policy 0->1, 3->2, without activating. i.e.,
 * disable any writeback.  Useful before backups.
 *
 */

#include <stdio.h>
#include <stdlib.h>  
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <linux/ext2_fs.h>

#include "migrator.h"
#include "list.h"

#define BEFORE  0
#define AFTER   1
#define DISK    0
#define VIRTUAL 1

off_t summary[2][2] = {{0, 0}, {0, 0}};

#define UNCOMPR 0
#define COMPR   1
#define IGNORE  2

int verbose      = 0;
int freeze       = 0;
int forcepolicy  = IGNORE;
int forcecompress= IGNORE;
int notitle      = 0;
int default_compress = COMPR;
int default_policy   = 0;

List *list[2] = {0, 0};
char compr_code[4] = {'U', 'C', '?', 0};

#define EXT2_IOC_GETCOMPRRATIO          _IOR('c', 4, long)
#define EXT2_IOC_SETCOMPRPOLICY         _IOW('c', 1, long)
#define EXT2_IOC_GETCOMPRPOLICY         _IOR('c', 1, long)
#define	EXT2_IOC_GETCLUSTERBIT		_IOR('c', 3, long)
#define	EXT2_IOC_ACTIVATECOMPR		_IOW('c', 7, long)


typedef struct _Rule Rule;
struct _Rule {
	unsigned int i;
	char *p;
};


int   check_rule(const char *filename);
unsigned int clusters(off_t size);
void  compress_dir(char *path);
int   compress_file(const char *filename, struct stat *statbuf);
long  compress_file_worth(const char *filename, struct stat *statbuf,
			  int oldpolicy, int fd);
void  conf_read(char *filename);
List *list_match(List *head, const char *pattern);
float lookup_ratio(const char *pattern);
int   main(int argc, char** argv);
int   match(const char *exp, const char *str);
void  print_summary(void);
void  usage(void);
void  version(void);



void version(void)
{
	printf("e2compr migrator version %s (%s)\n", MIGRATOR_VERSION, __DATE__);
}


void usage(void)
{
	fprintf(stderr,
		"Usage: migrator [-vhVT0123cufspnd]\n"
		"-v       verbose\n"
		"-h       help\n"
		"-V       print version\n"
		"-T       print no title info\n"
		"-[0-3]   force policy on all files\n"
		"         default: guess intelligently for each file\n"
		"-[u,c]   force [un]compress on all files\n"
		"         default: guess intelligently for each file\n"
		"-f       freeze (disable writeback in preparation for backup\n"
		"-s       starting directory\n"
		"         default: /\n"
		"-p       persistent, ie, never exit\n"
		"         default: exit after 1 pass\n"
		"-n       run at nice priority\n"
		"-d       run as daemon; implies -p -n\n");
	exit(0);
}


List *list_match(List *head, const char *pattern)
{
	while(head) {
		if (match(head->object, pattern)) {
			return head;
		}
		head=head->next;
	}

	return NULL;
}


#define LINESIZE 1023
void conf_read(char *filename)
{
	char line[LINESIZE+1];
	FILE *fp;
	int i=-1;
	int policy = 0;
	int compress = COMPR;
	
	if (!(fp = fopen("/etc/migrator.conf", "r"))) {
		if (!(fp = fopen("/usr/local/etc/migrator.conf", "r"))) {
			fprintf(stderr, "migrator: cannot open %s\n", filename);
			exit(1);
		}
	}

	while (!feof(fp)) {
		char *p = line;
		if (fgets(line, LINESIZE, fp)==NULL)
			break;

		for (p=line; *p; p++)
			if (*p=='\n')
				*p = 0;
		for (p=line; *p && (*p==' ' || *p=='\t'); p++);
		if (!*p || *p == '#')
			continue;

		if (!strncmp("compress=", p, 9)) {
			char tmp = *(p+9);
			if (tmp == '0')
				tmp = UNCOMPR;
			else if (tmp == '1')
				tmp = COMPR;
			else
				tmp = IGNORE;

			if (i == 0)
				compress = tmp;
			else
				default_compress = tmp;
		} else if (!strncmp("policy=", p, 7)) {
			int tmp = atoi(p+7);

			if (i == 0)
				policy = tmp;
			else if (i == 1)
				default_policy = tmp;
		} else if (!strcmp("force:", p)) {
			i = 0;
		} else if (!strcmp("default:", p)) {
			i = 1;
		} else if (!strcmp("ratios:", p)) {
			i = 2;
		} else {
			if (i == 0) {
				Rule *rule = (Rule *)malloc(sizeof (Rule));
				if (rule==NULL)
					continue;

				rule->i = policy & ((compress & 3) << 2);
				rule->p = strdup(p);
				list[0] = List_prepend(list[0], rule);
			} else if (i == 1) {
				/* ignore files in default: */
			} else if (i == 2) {
				float r;
				char pattern[LINESIZE];
				Rule *rule;
				
				if (sscanf(p, "%f %s", &r, pattern) != 2)
					continue;

				rule = (Rule *)malloc(sizeof (Rule));
				if (rule==NULL)
					continue;

				if (r<0.0)
					r = 0.0;
				if (r>1.0)
					r = 1.0;
				rule->i = (int)(r*100);
				rule->p = strdup(pattern);
				list[1] = List_prepend(list[1], rule);
			}
		}
	}
	fclose(fp);
}
#undef LINESIZE


/*
 *  A simple boolean globbing matcher thingy.  Handles multiple * and ?.
 *
 */

int match(const char *exp, const char *str)
{
	while (*exp || *str) {
		if (! *exp && *str)
			return 0;
		if (! *str)
			return (*exp == '*');

		if (*exp == '*') {
			if (! *(exp+1))
				return 1;
			for (;;) {
				if (match(exp+1, str))
					return 1;
				str++;
				if (! *str) {
					return 0;
				}
			}
		}
		if (*exp == '?' || *exp == *str) {
			exp++;
			str++;
		} else
			return 0;
	}
	return 1;
}


float lookup_ratio(const char *pattern)
{
	List *head = list[1];
	while(head) {
		if (match(((Rule *)head->object)->p, pattern)) {
			return ((Rule *)head->object)->i / 100.0;
		}
		head=head->next;
	}

	return 0.25;
}


int check_rule(const char *filename)
{
	List *l;

	if ((l=list_match(list[0], filename)) != NULL) {
		Rule *r = (Rule*) l->object;
		return r->i;
	} else
		return -1;
}


unsigned int clusters(off_t size)
{
	return size/CLUSTER_BYTES + (size%CLUSTER_BYTES == 0 ? 0 : 1);
}


void count_blocks(int fd, long *arg)
{
	arg[0] = arg[1] = 0;

	if (ioctl(fd, EXT2_IOC_GETCOMPRRATIO, arg) < 0) {
		printf("%s: EXT2_IOC_GETCOMPRRATIO failed: %d\n",
		       __PRETTY_FUNCTION__, errno);
		/* FIXME: arg[0] = size; arg[1] = ??; */
	}
}

void count_clusters(int fd, long *arg, off_t size)
{
	unsigned int clstrs = clusters(size);
	int i;

	arg[0] = arg[1] = 0;
	for (i=0; i < clstrs; i++) {
		long c = i;
		if (ioctl(fd, EXT2_IOC_GETCLUSTERBIT, &c) < 0) {
			printf("%s: EXT2_IOC_GETCLUSTERBIT failed: %d\n",
			       __PRETTY_FUNCTION__, errno);
		} else {
			if (c)
				arg[1]++;
		}
		arg[0]++;
		printf("i = %d  compressed = %ld\n", i, c);
	}
}


/* compress_file_worth
 *
 * effects: Uses a heuristic to estimate the gain obtained by
 *          compressing a given file.
 * input:   filename to study
 *          statbuf for that filename
 *          current policy
 * output:  output = (compress << 2) + policy
 *
 * policy:
 * 0 -- read w/ uncompressed writeback, write uncompressed (max uncompression)
 * 1 -- read whatever, write uncompressed
 * 2 -- read whatever, write compressed
 * 3 -- read w/ compressed writeback, write compressed
 *
 */

long compress_file_worth(const char *filename, struct stat *statbuf,
			 int oldpolicy, int fd)
{
	int compress, policy, i;
	unsigned int freespace = 32*1024*1024;  /* FIXME */
	float ratio = lookup_ratio(filename);
	float aage = (time(NULL) - statbuf->st_atime) / (24.0*60.0*60.0);
	off_t size = statbuf->st_size;

	if ((i = check_rule(filename)) != -1) {
		compress = (i >> 2) & 3;
		policy = i & 3;
		if (verbose)
			printf("forced %c%d ", compr_code[compress], policy);
	}
	else if (size < BLOCK_SIZE ||
		 ratio<0.06 ||
		 (size-ratio*size)/BLOCK_SIZE >= size/BLOCK_SIZE) {
		compress = UNCOMPR;
		policy = 1;
		if (verbose)
			printf("no-gain %c%d ", compr_code[compress], policy);
	}
	else if (size*5 > freespace ||
		 size > LARGE_BYTES) {
		compress = COMPR;
		policy = 3;
		if (verbose)
			printf("large %c%d ", compr_code[compress], policy);
	}
	else if (aage<AGE_CUTOFF && size<LARGE_BYTES) {
		compress = UNCOMPR;
		policy = 0;
		if (verbose)
			printf("small-young %c%d ", compr_code[compress], policy);
	}
	else if (aage<AGE_CUTOFF) {
		long arg[2];
		float compr_percent, target_percent;

		count_clusters(fd, arg, size);
		compr_percent = arg[0] ? arg[1]/arg[0] : 1.0;
		target_percent = 1.0 - (float)size/(float)HUGE_BYTES;
		target_percent = target_percent<0 ? 0 : target_percent;

		if (compr_percent > target_percent) {
                        /* large barely accessed: don't touch compression */
			compress = IGNORE;
			policy = 0;
		} else {
			compress = UNCOMPR;
			policy = 0;
		}

		if (verbose)
			printf("large-young %c%d ", compr_code[compress], policy);
	} else {
		compress = default_compress;
		policy = default_policy;
		if (verbose)
			printf("default %c%d ", compr_code[compress], policy);
	}
		
	/*
	 * weighted_age = 1 - (exp(-TIME_CONST * aage));
	 * weighted_bytes = 1 - (exp(-BYTE_CONST * (ratio * size)));
	 * worth = weighted_bytes * weighted_age;
	 *
	 */

	if (forcepolicy != 2) {
		policy = forcepolicy;
		printf("forced=%d ", forcepolicy);
	}

	if (forcecompress != IGNORE) {
		compress = forcecompress;
		printf("forced=%c ", compr_code[forcecompress]);
	}

	return policy | (compress << 2);
}


int compress_file(const char *filename, struct stat *statbuf)
{
	int fd=-1;
	long arg=0, policy=0, oldpolicy=0;
	int compress;

	do {
		printf("%s: ", filename);

		fd = open(filename, O_RDONLY);
		
		if (fd == -1) {
			printf("failed to open\n");
			break;
		}
					
		if (verbose) {
			long arg[2];
			count_blocks(fd, arg);

			summary[BEFORE][DISK]    += arg[1];
			summary[BEFORE][VIRTUAL] += arg[0];
		}

		if (ioctl(fd, EXT2_IOC_GETCOMPRPOLICY, &oldpolicy) < 0) {
			printf("EXT2_IOC_GETCOMPRPOLICY failed: %d\n", errno);
			break;
		}
		if (verbose)
			printf("%ld -> ", oldpolicy);

		if (freeze) {
			if (oldpolicy==3)
				policy = 2;
			if (oldpolicy==0)
				policy = 1;
			compress = IGNORE;
		} else {
			arg = compress_file_worth(filename, statbuf, oldpolicy, fd);
			compress = (arg >> 2) & 3;
			policy = arg & 3;
 		}

		if (compress != IGNORE) {
			arg = compress==COMPR ? 2 : 1;
			if (ioctl(fd, EXT2_IOC_SETCOMPRPOLICY, &arg) < 0) {
				printf("\nEXT2_IOC_SETCOMPRPOLICY failed; can't activate: %d\n", errno);
				break;
			}
			if (ioctl(fd, EXT2_IOC_ACTIVATECOMPR, &arg) < 0) {
				printf("\nfailed to enforce policy: %d\n", errno);
				break;
			} else {
				if (verbose)
					printf("%c", compr_code[compress]);
			}			
			close (fd);
			fd = open(filename, O_RDONLY);
		}

		if (ioctl(fd, EXT2_IOC_SETCOMPRPOLICY, &policy) < 0) {
			printf("\nEXT2_IOC_SETCOMPRPOLICY failed; can't set policy: %d\n", errno);
			break;
		}

		if (verbose)
			printf("**%ld** ", policy);
		printf("OK\n");
	} while (0);

	if (fd != -1 && verbose) {
		long arg[2];
		count_blocks(fd, arg);
		
		summary[AFTER][DISK]    += arg[1];
		summary[AFTER][VIRTUAL] += arg[0];
	}
	
	if (fd != -1)
		close (fd);

	return 0;
}


void compress_dir(char *path)
{
	DIR *dir;
	struct dirent *dirent;
	struct stat statbuf;
	char *newpath = 0;

	dir = opendir(path);

	do {
		dirent = readdir(dir);
		if (dirent) {
      
			newpath = malloc(strlen(path)+strlen(dirent->d_name)+2);
			if (!newpath) {
				fprintf(stderr, "%s/%s: out of memory\n",
					path, dirent->d_name);
				break;
			} else {
				strcpy(newpath, path);
				strcat(newpath, "/");
				strcat(newpath, dirent->d_name);
			}

			if (!stat(newpath, &statbuf)) {
				if (S_ISLNK(statbuf.st_mode)) {
					if (verbose)
						fprintf(stderr, "%s: ignoring link\n",
							newpath);
				} else if (S_ISDIR(statbuf.st_mode)) {
					if (strcmp(dirent->d_name, ".") &&
					    strcmp(dirent->d_name, ".."))
						compress_dir(newpath);
					else
						/* ignore */;
				} else if (S_ISREG(statbuf.st_mode)) {
					compress_file(newpath, &statbuf);
				} else {
					if (verbose)
						fprintf(stderr, "%s: ignoring\n",
							newpath);
				}
			} else {
				perror(newpath);
			}
		}
	} while (dirent);
 
	if (newpath)
		free (newpath);
	if (dir)
		closedir(dir);
}


void print_summary(void)
{
	if (verbose) {
		printf("\n%.1fk / %.1fk (%.1f%%)  ->  %.1fk / %.1fk (%.1f%%)\n",
		       summary[BEFORE][DISK] / 2.0,
		       summary[BEFORE][VIRTUAL] / 2.0,
		       (float)summary[BEFORE][DISK] / (float)summary[BEFORE][VIRTUAL] * 100.0,
		       summary[AFTER][DISK] / 2.0,
		       summary[AFTER][VIRTUAL] / 2.0,
		       (float)summary[AFTER][DISK] / (float)summary[AFTER][VIRTUAL] * 100.0);
#if 0
		printf("before                              after\n");
		printf("----------------------------------  ----------------------------------\n");
		printf("  compressed clusters %12d    compressed clusters %12d\n",
		       summary_s_cc, summary_e_cc);
		printf("uncompressed clusters %12d  uncompressed clusters %12d\n",
		       summary_s_uc, summary_e_uc);
		printf("       total clusters %12d         total clusters %12d\n",
		       summary_s_c, summary_e_c);
#endif
	}
}


int main(int argc, char** argv)
{
	int c;
	int persistent = 0;
	char *path = "/";
	
	while (1) {
		c = getopt_long(argc, argv, "vhVT0123cufs:pnd", 0, 0);
    
		if (c == -1)
			break;
    
		switch (c) {
		case 'V':
			version();
			exit(0);
		case 'T':
			notitle++;
			break;
		case 'c':
			forcecompress = COMPR;
			break;
		case 'u':
			forcecompress = UNCOMPR;
			break;
		case '0':
			forcepolicy = 0;
			break;
		case '1':
			forcepolicy = 1;
			break;
		case '2':
			forcepolicy = 2;
			break;
		case '3':
			forcepolicy = 3;
			break;
		case 'f':
			freeze = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 's':
			path = optarg;
			break;
		case 'p':
			persistent = 1;
			break;
		case 'n':
			nice(20);
			break;
		case 'd':
			persistent = 1;
			nice(20);
			break;
		case 'h':
		default:
			usage();
		}
	}

	conf_read("migrator.conf");
	
	if (freeze) {
		if (forcepolicy != 2 ||
		    forcecompress != IGNORE) {
			printf("Can't force policy or compression during freeze.\n");
			exit(-1);
		}
	}
	
#if 0
	p = "./writings/letters/sarah.tar.gz";
	e = "*.gz";
	printf("%s has ratio %f\n", p, lookup_ratio(p));
	printf("%s matches %s ? %d\n", p, e, match(e, p));
	exit(0);
#endif

	if (!notitle)
		version();

	for (;;) {
		compress_dir(path);

		print_summary();

		if (persistent)
			sleep(5);
		else
			break;
	}
	
	return 0;
}
