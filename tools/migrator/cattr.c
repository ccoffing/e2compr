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
#include <linux/config.h>
#include <linux/ext2_fs.h>

#include "migrator.h"

int reset = 0;
int comprblk = -1;
int policy = -1;

#define EXT2_IOC_GETCOMPRRATIO          _IOR('c', 4, long)
#define EXT2_IOC_SETCOMPRPOLICY         _IOW('c', 1, long)
#define EXT2_IOC_GETCOMPRPOLICY         _IOR('c', 1, long)
#define	EXT2_IOC_GETCLUSTERBIT		_IOR('c', 3, long)
#define	EXT2_IOC_ACTIVATECOMPR		_IOW('c', 7, long)


unsigned int clusters(off_t size);
int   main(int argc, char** argv);



void compress_cluster(int fd, int cluster, int compr)
{
	int err;
	long arg = cluster;

	if (fd < 0) {
		printf("filename must be specified first\n");
		return;
	}

	if (compr)
		err = ioctl(fd, EXT2_IOC_SETCLUSTERBIT, &arg);
	else
		err = ioctl(fd, EXT2_IOC_CLRCLUSTERBIT, &arg);

	if (err < 0)
		printf("failed to %scompress cluster %d\n", compr ? "" : "un",
		       cluster);	
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
	int cl = clusters(size);
	int i;
	long c;

	arg[0] = arg[1] = 0;
	for (i=0; i < cl; i++) {
		c = i;
		if (ioctl(fd, EXT2_IOC_GETCLUSTERBIT, &c) < 0) {
			printf("%s: EXT2_IOC_GETCLUSTERBIT failed: %d\n",
			       __PRETTY_FUNCTION__, errno);
		} else {
			if (c)
				arg[1]++;
		}
		arg[0]++;
		printf("cluster %d: compressed=%ld\n", i, c);
	}
}

void usage(void)
{
	printf("cattr -f filename [-r] [-p #] [-c #] ... [-u #] ...\n\n");
	printf("-r    reset error bits\n");
	printf("-p #  set policy, 0-3\n");
	printf("-c #  compress cluster #\n");
	printf("-u #  uncompress cluster #\n\n");

	exit(1);
}


int main(int argc, char** argv)
{
	long arg[2];
	int fd = -1;
	int c;
	struct stat statbuf;
	char *filename = 0;

	while (1) {
		c = getopt_long(argc, argv, "rc:u:p:f:", 0, 0);
		if (c == -1)
			break;
    
		switch (c) {
		case 'r':
			reset = 1;
			break;
		case 'c':
			compress_cluster(fd, atoi(optarg), 1);
			break;
		case 'u':
			compress_cluster(fd, atoi(optarg), 0);
			break;
		case 'f':
			filename = optarg;
			fd = open(filename, O_RDONLY);
			if (fd == -1) {
				printf("%s: failed to open\n", filename);
				exit(2);
			}
			break;
		case 'p':
			policy = atoi(optarg);
			break;
		case 'h':
		default:
			usage();
		}
	}

	if (fd < 0) {
		usage();
	}

	if (reset) {
		if (ioctl(fd, EXT2_IOC_GETFLAGS, arg) < 0) {
			printf("EXT2_IOC_GETFLAGS failed: %d\n", errno);
			exit(4);
		}
		arg[0] &= ~(EXT2_ECOMPR_FL);
		arg[0] &= ~(EXT2_NOCOMPR_FL);
		if (ioctl(fd, EXT2_IOC_SETFLAGS, arg) < 0) {
			printf("EXT2_IOC_SETFLAGS failed: %d\n", errno);
			exit(5);
		}
	}

	if (policy>=0 && policy<=3) {
		arg[0] = policy;
		if (ioctl(fd, EXT2_IOC_SETCOMPRPOLICY, arg) < 0) {
			printf("EXT2_IOC_SETCOMPRPOLICY failed: %d\n", errno);
			exit(4);
		}
		close (fd);
		fd = open(filename, O_RDONLY);
	}

	if (comprblk == 1) {
		if (ioctl(fd, EXT2_IOC_GETFLAGS, arg) < 0) {
			printf("EXT2_IOC_GETFLAGS failed: %d\n", errno);
			exit(4);
		}
		arg[0] |= EXT2_COMPRBLK_FL;
		if (ioctl(fd, EXT2_IOC_SETFLAGS, arg) < 0) {
			printf("EXT2_IOC_SETFLAGS failed: %d\n", errno);
			exit(5);
		}
	} else if (comprblk == 0) {
		if (ioctl(fd, EXT2_IOC_GETFLAGS, arg) < 0) {
			printf("EXT2_IOC_GETFLAGS failed: %d\n", errno);
			exit(4);
		}
		arg[0] &= ~EXT2_COMPRBLK_FL;
		if (ioctl(fd, EXT2_IOC_SETFLAGS, arg) < 0) {
			printf("EXT2_IOC_SETFLAGS failed: %d\n", errno);
			exit(5);
		}
	}

	if (fstat(fd, &statbuf) < 0) {
		printf("%s: failed to stat: %d\n", argv[1], errno);
		exit(3);
	}

	if (ioctl(fd, EXT2_IOC_GETCOMPRPOLICY, arg) < 0) {
		printf("EXT2_IOC_GETCOMPRPOLICY failed: %d\n", errno);
		exit(4);
	}
	printf("policy           %ld\n", arg[0]);

	if (ioctl(fd, EXT2_IOC_GETFLAGS, arg) < 0) {
		printf("EXT2_IOC_GETFLAGS failed: %d\n", errno);
		exit(4);
	}
	printf("EXT2_COMPRBLK_FL %d\n", (arg[0] & EXT2_COMPRBLK_FL) ? 1 : 0);
	printf("EXT2_ECOMPR_FL   %d\n", (arg[0] & EXT2_ECOMPR_FL) ? 1 : 0);
	printf("EXT2_NOCOMPR_FL  %d\n", (arg[0] & EXT2_NOCOMPR_FL) ? 1 : 0);

	count_blocks(fd, arg);
	printf("%.1fk / %.1fk (%.1f%%)\n", arg[1] / 2.0, arg[0] / 2.0, (float)arg[1]/(float)arg[0] * 100.0);
	
	count_clusters(fd, arg, statbuf.st_size);
	printf("compressed cluster(s)   %5ld\n", arg[1]);
	printf("uncompressed cluster(s) %5ld\n", arg[0]-arg[1]);
	printf("total cluster(s)        %5ld\n", arg[0]);

	exit(0);
}
