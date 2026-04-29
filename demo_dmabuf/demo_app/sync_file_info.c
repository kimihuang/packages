/*
 * sync_file_info.c - Query and print sync_file fence information
 *
 * Usage: sync_file_info <fence_fd>
 *
 * Opens a fence file descriptor by duplicating the passed fd number
 * (via /proc/self/fd/<n>) and queries its status via SYNC_IOC_FILE_INFO.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/types.h>

/* ======================================================================
 * Manual definitions for sync_file ioctls/structures that may not be
 * present in older kernel headers.
 * ====================================================================== */

#ifndef SYNC_IOC_FILE_INFO
struct sync_fence_info {
	char    driver_name[32];
	char    timeline_name[32];
	__u64   status;
	__u32   timestamp_ns;
	__u32   driver_data_size;
	char    driver_data[];
};

struct sync_file_info {
	char    name[32];
	__s32   status;
	__u32   flags;
	__u32   num_fences;
	__u64   pad;
	__u64   sync_fence_info; /* pointer to struct sync_fence_info array */
};

#define SYNC_IOC_FILE_INFO _IOWR('f', 5, struct sync_file_info)
#endif

/* ======================================================================
 * Helpers
 * ====================================================================== */

static const char *fence_status_str(__s32 status)
{
	if (status > 0)
		return "SIGNALED";
	if (status == 0)
		return "ACTIVE";
	return "ERROR";
}

static void print_timestamp_ns(__u32 ts_ns)
{
	/* timestamp_ns is a nanosecond counter; just display as-is */
	printf("  timestamp_ns  : %u\n", ts_ns);
}

/*
 * query_sync_file_info - First query with num_fences=0 to get count,
 * then re-query with allocated buffer for fence info array.
 */
static int query_sync_file_info(int fd)
{
	struct sync_file_info *info;
	struct sync_fence_info *fences;
	__u32 num_fences;
	int ret;

	info = calloc(1, sizeof(*info));
	if (!info) {
		perror("calloc");
		return -ENOMEM;
	}

	/* First call: get the number of fences */
	info->sync_fence_info = 0; /* NULL */
	info->num_fences = 0;

	ret = ioctl(fd, SYNC_IOC_FILE_INFO, info);
	if (ret < 0) {
		perror("SYNC_IOC_FILE_INFO (count query)");
		free(info);
		return -errno;
	}

	num_fences = info->num_fences;

	printf("Sync File Info:\n");
	printf("  name          : %s\n", info->name);
	printf("  status        : %s\n", fence_status_str(info->status));
	printf("  flags         : 0x%x\n", info->flags);
	printf("  num_fences    : %u\n", num_fences);

	if (num_fences == 0) {
		printf("  (no constituent fences)\n");
		free(info);
		return 0;
	}

	/* Allocate buffer for fence info array */
	fences = calloc(num_fences, sizeof(*fences));
	if (!fences) {
		perror("calloc fences");
		free(info);
		return -ENOMEM;
	}

	info->sync_fence_info = (__u64)(unsigned long)fences;
	ret = ioctl(fd, SYNC_IOC_FILE_INFO, info);
	if (ret < 0) {
		perror("SYNC_IOC_FILE_INFO (detail query)");
		free(fences);
		free(info);
		return -errno;
	}

	/* Print each fence */
	for (__u32 i = 0; i < num_fences && i < info->num_fences; i++) {
		struct sync_fence_info *fi = &fences[i];
		printf("\n  Fence[%u]:\n", i);
		printf("    driver_name  : %s\n", fi->driver_name);
		printf("    timeline_name: %s\n", fi->timeline_name);
		printf("    status       : %s\n", fence_status_str((__s32)fi->status));
		print_timestamp_ns(fi->timestamp_ns);
		if (fi->driver_data_size > 0)
			printf("    driver_data  : %u bytes\n", fi->driver_data_size);
	}

	free(fences);
	free(info);
	return 0;
}

/* ======================================================================
 * main
 * ====================================================================== */

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s <fence_fd>\n", prog);
	fprintf(stderr, "\n");
	fprintf(stderr, "Query and display sync_file fence information.\n");
	fprintf(stderr, "The fence_fd is a numeric file descriptor number\n");
	fprintf(stderr, "(e.g. 12) that must be open in the calling process.\n");
}

int main(int argc, char *argv[])
{
	int fd, ret;
	const char *path;

	if (argc != 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	int n = atoi(argv[1]);
	if (n < 0) {
		fprintf(stderr, "Invalid fence fd: %s\n", argv[1]);
		return EXIT_FAILURE;
	}

	/* Open the fd via /proc/self/fd to get a fresh fd */
	path = argv[1]; /* for error messages */

	char proc_path[64];
	snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", n);

	fd = open(proc_path, O_CLOEXEC);
	if (fd < 0) {
		perror(proc_path);
		return EXIT_FAILURE;
	}

	ret = query_sync_file_info(fd);
	close(fd);

	return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
