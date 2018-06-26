/*-------------------------------------------------------------------------
 *
 * resgroup-ops-cgroup.c
 *	  OS dependent resource group operations - cgroup implementation
 *
 * Copyright (c) 2017 Pivotal Software, Inc.
 *
 *
 * IDENTIFICATION
 *	    src/backend/utils/resgroup/resgroup-ops-cgroup.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "cdb/cdbvars.h"
#include "utils/resgroup.h"
#include "utils/resgroup-ops.h"
#include "utils/vmem_tracker.h"

#ifndef __linux__
#error  cgroup is only available on linux
#endif

#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <stdio.h>
#include <mntent.h>

/*
 * Interfaces for OS dependent operations.
 *
 * Resource group relies on OS dependent group implementation to manage
 * resources like cpu usage, such as cgroup on Linux system.
 * We call it OS group in below function description.
 *
 * So far these operations are mainly for CPU rate limitation and accounting.
 */

#define CGROUP_ERROR(...) elog(ERROR, __VA_ARGS__)
#define CGROUP_CONFIG_ERROR(...) \
	CGROUP_ERROR("cgroup is not properly configured: " __VA_ARGS__)

#define PROC_MOUNTS "/proc/self/mounts"
#define MAX_INT_STRING_LEN 20
#define MAX_RETRY 10

/*
 * cgroup memory permission is only mandatory on 6.x and master;
 * on 5.x we need to make it optional to provide backward compatibilities.
 */
#define CGROUP_MEMORY_IS_OPTIONAL (GP_VERSION_NUM < 60000)
/*
 * cpuset permission is only mandatory on 6.x and master;
 * on 5.x we need to make it optional to provide backward compatibilities.
 */
#define CGROUP_CPUSET_IS_OPTIONAL (GP_VERSION_NUM < 60000)

typedef struct PermItem PermItem;
typedef struct PermList PermList;

struct PermItem
{
	const char	*comp;
	const char	*prop;
	int			perm;
};

struct PermList
{
	const PermItem	*items;
	bool			optional;
	bool			*presult;
};

#define foreach_perm_list(i, lists) \
	for ((i) = 0; (lists)[(i)].items; (i)++)

#define foreach_perm_item(i, items) \
	for ((i) = 0; (items)[(i)].comp; (i)++)

static char * buildPath(Oid group, const char *base, const char *comp, const char *prop, char *path, size_t pathsize);
static int lockDir(const char *path, bool block);
static void unassignGroup(Oid group, const char *comp, int fddir);
static bool createDir(Oid group, const char *comp);
static bool removeDir(Oid group, const char *comp, const char *prop, bool unassign);
static int getCpuCores(void);
static size_t readData(const char *path, char *data, size_t datasize);
static void writeData(const char *path, const char *data, size_t datasize);
static int64 readInt64(Oid group, const char *base, const char *comp, const char *prop);
static void writeInt64(Oid group, const char *base, const char *comp, const char *prop, int64 x);
static bool permListCheck(const PermList *permlist, Oid group, bool report);
static bool checkPermission(Oid group, bool report);
static bool checkCpuSetPermission(Oid group, bool report);
static void getMemoryInfo(unsigned long *ram, unsigned long *swap);
static void getCgMemoryInfo(uint64 *cgram, uint64 *cgmemsw);
static int getOvercommitRatio(void);
static bool detectCgroupMountPoint(void);
static void createDefaultCpuSetGroup(void);

/*
 * currentGroupIdInCGroup & oldCaps are used for reducing redundant
 * file operations
 */
static Oid currentGroupIdInCGroup = InvalidOid;
static ResGroupCaps oldCaps;

static char cgdir[MAXPGPATH];

/*
 * These checks should keep in sync with gpMgmt/bin/gpcheckresgroupimpl
 */
static const PermItem perm_items_cpu[] =
{
	{ "cpu", "", R_OK | W_OK | X_OK },
	{ "cpu", "cgroup.procs", R_OK | W_OK },
	{ "cpu", "cpu.cfs_period_us", R_OK | W_OK },
	{ "cpu", "cpu.cfs_quota_us", R_OK | W_OK },
	{ "cpu", "cpu.shares", R_OK | W_OK },
	{ NULL, NULL, 0 }
};
static const PermItem perm_items_cpu_acct[] =
{
	{ "cpuacct", "", R_OK | W_OK | X_OK },
	{ "cpuacct", "cgroup.procs", R_OK | W_OK },
	{ "cpuacct", "cpuacct.usage", R_OK },
	{ "cpuacct", "cpuacct.stat", R_OK },
	{ NULL, NULL, 0 }
};
static const PermItem perm_items_cpuset[] =
{
	{ "cpuset", "", R_OK | W_OK | X_OK },
	{ "cpuset", "cgroup.procs", R_OK | W_OK },
	{ "cpuset", "cpuset.cpus", R_OK | W_OK },
	{ "cpuset", "cpuset.mems", R_OK | W_OK },
	{ NULL, NULL, 0 }
};
static const PermItem perm_items_memory[] =
{
	{ "memory", "", R_OK | W_OK | X_OK },
	{ "memory", "memory.limit_in_bytes", R_OK | W_OK },
	{ "memory", "memory.usage_in_bytes", R_OK },
	{ NULL, NULL, 0 }
};
static const PermItem perm_items_swap[] =
{
	{ "memory", "", R_OK | W_OK | X_OK },
	{ "memory", "memory.memsw.limit_in_bytes", R_OK | W_OK },
	{ "memory", "memory.memsw.usage_in_bytes", R_OK },
	{ NULL, NULL, 0 }
};

/*
 * just for cpuset check, same as the cpuset Permlist in permlists
 */
static const PermList cpusetPermList = {
	perm_items_cpuset,
	CGROUP_CPUSET_IS_OPTIONAL,
	&gp_resource_group_enable_cgroup_cpuset,
};

/*
 * Permission groups.
 */
static const PermList permlists[] =
{
	/*
	 * swap permissions are optional.
	 *
	 * cgroup/memory/memory.memsw.* is only available if
	 * - CONFIG_MEMCG_SWAP_ENABLED=on in kernel config, or
	 * - swapaccount=1 in kernel cmdline.
	 *
	 * Without these interfaces the swap usage can not be limited or accounted
	 * via cgroup.
	 */
	{ perm_items_swap, true, &gp_resource_group_enable_cgroup_swap },

	/*
	 * memory permissions can be mandatory or optional depends on the switch.
	 *
	 * resgroup memory auditor is introduced in 6.0 devel and backported
	 * to 5.x branch since 5.6.1.  To provide backward compatibilities memory
	 * permissions are optional on 5.x branch.
	 */
	{ perm_items_memory, CGROUP_MEMORY_IS_OPTIONAL,
		&gp_resource_group_enable_cgroup_memory },

	/* cpu/cpuacct permissions are mandatory */
	{ perm_items_cpu, false, NULL },
	{ perm_items_cpu_acct, false, NULL },

	/*
	 * cpuset permissions can be mandatory or optional depends on the switch.
	 *
	 * resgroup cpuset is introduced in 6.0 devel and backported
	 * to 5.x branch since 5.6.1.  To provide backward compatibilities cpuset
	 * permissions are optional on 5.x branch.
	 */
	{ perm_items_cpuset, CGROUP_CPUSET_IS_OPTIONAL,
		&gp_resource_group_enable_cgroup_cpuset},

	{ NULL, false, NULL }
};

/*
 * Build path string with parameters.
 * - if base is NULL, use default value "gpdb"
 * - if group is RESGROUP_ROOT_ID then the path is for the gpdb toplevel cgroup;
 * - if prop is "" then the path is for the cgroup dir;
 */
static char *
buildPath(Oid group,
		  const char *base,
		  const char *comp,
		  const char *prop,
		  char *path,
		  size_t pathsize)
{
	Assert(cgdir[0] != 0);

	if (!base)
		base = "gpdb";

	if (group == RESGROUP_COMPROOT_ID)
	{
		snprintf(path, pathsize, "%s/%s/%s", cgdir, comp, prop);
	}
	else if (group != RESGROUP_ROOT_ID)
	{
		snprintf(path, pathsize, "%s/%s/%s/%d/%s", cgdir, comp, base, group, prop);
	}
	else
	{
		snprintf(path, pathsize, "%s/%s/%s/%s", cgdir, comp, base, prop);
	}

	return path;
}

/*
 * Unassign all the processes from group.
 *
 * These processes will be moved to the gpdb toplevel cgroup.
 *
 * This function must be called with the gpdb toplevel dir locked,
 * fddir is the fd for this lock, on any failure fddir will be closed
 * (and unlocked implicitly) then an error is raised.
 */
static void
unassignGroup(Oid group, const char *comp, int fddir)
{
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);
	char *buf;
	size_t bufsize;
	const size_t bufdeltasize = 512;
	size_t buflen = -1;
	int fdr = -1;
	int fdw = -1;

	/*
	 * Check an operation result on path.
	 *
	 * Operation can be open(), close(), read(), write(), etc., which must
	 * set the errno on error.
	 *
	 * - condition describes the expected result of the operation;
	 * - action is the cleanup action on failure, such as closing the fd,
	 *   multiple actions can be specified by putting them in brackets,
	 *   such as (op1, op2);
	 * - message describes what's failed;
	 */
#define __CHECK(condition, action, message) do { \
	if (!(condition)) \
	{ \
		/* save errno in case it's changed in actions */ \
		int err = errno; \
		action; \
		CGROUP_ERROR(message ": %s: %s", path, strerror(err)); \
	} \
} while (0)

	buildPath(group, NULL, comp, "cgroup.procs", path, pathsize);

	fdr = open(path, O_RDONLY);
	__CHECK(fdr >= 0, ( close(fddir) ), "can't open file for read");

	buflen = 0;
	bufsize = bufdeltasize;
	buf = palloc(bufsize);

	while (1)
	{
		int n = read(fdr, buf + buflen, bufdeltasize);
		__CHECK(n >= 0, ( close(fdr), close(fddir) ), "can't read from file");

		buflen += n;

		if (n < bufdeltasize)
			break;

		bufsize += bufdeltasize;
		buf = repalloc(buf, bufsize);
	}

	close(fdr);
	if (buflen == 0)
		return;

	buildPath(RESGROUP_ROOT_ID, NULL, comp, "cgroup.procs", path, pathsize);

	fdw = open(path, O_WRONLY);
	__CHECK(fdw >= 0, ( close(fddir) ), "can't open file for write");

	char *ptr = buf;
	char *end = NULL;
	long pid;

	/*
	 * as required by cgroup, only one pid can be migrated in each single
	 * write() call, so we have to parse the pids from the buffer first,
	 * then write them one by one.
	 */
	while (1)
	{
		pid = strtol(ptr, &end, 10);
		__CHECK(pid != LONG_MIN && pid != LONG_MAX,
				( close(fdw), close(fddir) ),
				"can't parse pid");

		if (ptr == end)
			break;

		char str[16];
		sprintf(str, "%ld", pid);
		int n = write(fdw, str, strlen(str));
		if (n < 0)
		{
			elog(LOG, "failed to migrate pid to gpdb root cgroup: pid=%ld: %s",
				 pid, strerror(errno));
		}
		else
		{
			__CHECK(n == strlen(str),
					( close(fdw), close(fddir) ),
					"can't write to file");
		}

		ptr = end;
	}

	close(fdw);

#undef __CHECK
}

/*
 * Lock the dir specified by path.
 *
 * - path must be a dir path;
 * - if block is true then lock in block mode, otherwise will give up if
 *   the dir is already locked;
 */
static int
lockDir(const char *path, bool block)
{
	int fddir;

	fddir = open(path, O_RDONLY);
	if (fddir < 0)
	{
		if (errno == ENOENT)
		{
			/* the dir doesn't exist, nothing to do */
			return -1;
		}

		CGROUP_ERROR("can't open dir to lock: %s: %s",
					 path, strerror(errno));
	}

	int flags = LOCK_EX;
	if (!block)
		flags |= LOCK_NB;

	while (flock(fddir, flags))
	{
		/*
		 * EAGAIN is not described in flock(2),
		 * however it does appear in practice.
		 */
		if (errno == EAGAIN)
			continue;

		int err = errno;
		close(fddir);

		/*
		 * In block mode all errors should be reported;
		 * In non block mode only report errors != EWOULDBLOCK.
		 */
		if (block || err != EWOULDBLOCK)
			CGROUP_ERROR("can't lock dir: %s: %s", path, strerror(err));
		return -1;
	}

	/*
	 * Even if we accquired the lock the dir may still been removed by other
	 * processes, e.g.:
	 *
	 * 1: open()
	 * 1: flock() -- process 1 accquired the lock
	 *
	 * 2: open()
	 * 2: flock() -- blocked by process 1
	 *
	 * 1: rmdir()
	 * 1: close() -- process 1 released the lock
	 *
	 * 2:flock() will now return w/o error as process 2 still has a valid
	 * fd (reference) on the target dir, and process 2 does accquired the lock
	 * successfully. However as the dir is already removed so process 2
	 * shouldn't make any further operation (rmdir(), etc.) on the dir.
	 *
	 * So we check for the existence of the dir again and give up if it's
	 * already removed.
	 */
	if (access(path, F_OK))
	{
		/* the dir is already removed by other process, nothing to do */
		close(fddir);
		return -1;
	}

	return fddir;
}

/*
 * Create the cgroup dir for group.
 */
static bool
createDir(Oid group, const char *comp)
{
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);

	buildPath(group, NULL, comp, "", path, pathsize);

	if (mkdir(path, 0755) && errno != EEXIST)
		return false;

	return true;
}

/*
 * Remove the cgroup dir for group.
 *
 * - if unassign is true then unassign all the processes first before removal;
 */
static bool
removeDir(Oid group, const char *comp, const char *prop, bool unassign)
{
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);
	int retry = unassign ? 0 : MAX_RETRY - 1;
	int fddir;

	buildPath(group, NULL, comp, "", path, pathsize);

	/*
	 * To prevent race condition between multiple processes we require a dir
	 * to be removed with the lock accquired first.
	 */
	fddir = lockDir(path, true);
	if (fddir < 0)
	{
		/* the dir is already removed */
		return true;
	}

	/*
	 * Reset the corresponding control file to zero
	 */
	if (prop)
		writeInt64(group, NULL, comp, prop, 0);

	while (++retry <= MAX_RETRY)
	{
		if (unassign)
			unassignGroup(group, comp, fddir);

		if (rmdir(path))
		{
			int err = errno;

			if (err == EBUSY && unassign && retry < MAX_RETRY)
			{
				elog(DEBUG1, "can't remove dir, will retry: %s: %s",
					 path, strerror(err));
				pg_usleep(1000);
				continue;
			}

			/*
			 * we don't check for ENOENT again as we already accquired the lock
			 * on this dir and the dir still exist at that time, so if then
			 * it's removed by other processes then it's a bug.
			 */
			elog(DEBUG1, "can't remove dir, ignore the error: %s: %s",
				 path, strerror(err));
		}
		break;
	}

	if (retry <= MAX_RETRY)
		elog(DEBUG1, "cgroup dir '%s' removed", path);

	/* close() also releases the lock */
	close(fddir);

	return true;
}

/*
 * Get the cpu cores assigned for current system or container.
 *
 * Suppose a physical machine has 8 cpu cores, 2 of them assigned to
 * a container, then the return value is:
 * - 8 if running directly on the machine;
 * - 2 if running in the container;
 */
static int
getCpuCores(void)
{
	static int cpucores = 0;

	/*
	 * cpuset ops requires _GNU_SOURCE to be defined,
	 * and _GNU_SOURCE is forced on in src/template/linux,
	 * so we assume these ops are always available on linux.
	 */
	cpu_set_t cpuset;
	int i;

	if (cpucores != 0)
		return cpucores;

	if (sched_getaffinity(0, sizeof(cpuset), &cpuset) < 0)
		CGROUP_ERROR("can't get cpu cores: %s", strerror(errno));

	for (i = 0; i < CPU_SETSIZE; i++)
	{
		if (CPU_ISSET(i, &cpuset))
			cpucores++;
	}

	if (cpucores == 0)
		CGROUP_ERROR("can't get cpu cores");

	return cpucores;
}

/*
 * Read at most datasize bytes from a file.
 */
static size_t
readData(const char *path, char *data, size_t datasize)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		elog(ERROR, "can't open file '%s': %s", path, strerror(errno));

	ssize_t ret = read(fd, data, datasize);

	/* save errno before close() */
	int err = errno;
	close(fd);

	if (ret < 0)
		elog(ERROR, "can't read data from file '%s': %s", path, strerror(err));

	return ret;
}

/*
 * Write datasize bytes to a file.
 */
static void
writeData(const char *path, const char *data, size_t datasize)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		elog(ERROR, "can't open file '%s': %s", path, strerror(errno));

	ssize_t ret = write(fd, data, datasize);

	/* save errno before close */
	int err = errno;
	close(fd);

	if (ret < 0)
		elog(ERROR, "can't write data to file '%s': %s", path, strerror(err));
	if (ret != datasize)
		elog(ERROR, "can't write all data to file '%s'", path);
}

/*
 * Read an int64 value from a cgroup interface file.
 */
static int64
readInt64(Oid group, const char *base, const char *comp, const char *prop)
{
	int64 x;
	char data[MAX_INT_STRING_LEN];
	size_t datasize = sizeof(data);
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);

	buildPath(group, base, comp, prop, path, pathsize);

	readData(path, data, datasize);

	if (sscanf(data, "%lld", (long long *) &x) != 1)
		CGROUP_ERROR("invalid number '%s'", data);

	return x;
}

/*
 * Write an int64 value to a cgroup interface file.
 */
static void
writeInt64(Oid group, const char *base, const char *comp, const char *prop, int64 x)
{
	char data[MAX_INT_STRING_LEN];
	size_t datasize = sizeof(data);
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);

	buildPath(group, base, comp, prop, path, pathsize);
	snprintf(data, datasize, "%lld", (long long) x);

	writeData(path, data, strlen(data));
}

/*
 * Read a string value from a cgroup interface file.
 */
static void
readStr(Oid group, const char *base, const char *comp, const char *prop, char *str, int len)
{
	char data[MAX_INT_STRING_LEN];
	size_t datasize = sizeof(data);
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);

	buildPath(group, base, comp, prop, path, pathsize);

	readData(path, data, datasize);

	StrNCpy(str, data, len);
}

/*
 * Write an string value to a cgroup interface file.
 */
static void
writeStr(Oid group, const char *base, const char *comp, const char *prop,
		 const char *strValue)
{
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);

	buildPath(group, base, comp, prop, path, pathsize);
	writeData(path, strValue, strlen(strValue));
}

/*
 * Check a list of permissions on group.
 *
 * - if all the permissions are met then return true;
 * - otherwise:
 *   - raise an error if report is true and permlist is not optional;
 *   - or return false;
 */
static bool
permListCheck(const PermList *permlist, Oid group, bool report)
{
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);
	int i;

	if (group == RESGROUP_ROOT_ID && permlist->presult)
		*permlist->presult = false;

	foreach_perm_item(i, permlist->items)
	{
		const char	*comp = permlist->items[i].comp;
		const char	*prop = permlist->items[i].prop;
		int			perm = permlist->items[i].perm;

		buildPath(group, NULL, comp, prop, path, pathsize);

		if (access(path, perm))
		{
			/* No such file or directory / Permission denied */

			if (report && !permlist->optional)
			{
				CGROUP_CONFIG_ERROR("can't access %s '%s': %s",
									prop[0] ? "file" : "directory",
									path,
									strerror(errno));
			}
			return false;
		}
	}

	if (group == RESGROUP_ROOT_ID && permlist->presult)
		*permlist->presult = true;

	return true;
}

/*
 * Check permissions on group's cgroup dir & interface files.
 *
 * - if report is true then raise an error if any mandatory permission
 *   is not met;
 * - otherwise only return false;
 */
static bool
checkPermission(Oid group, bool report)
{
	int i;

	foreach_perm_list(i, permlists)
	{
		const PermList *permlist = &permlists[i];

		if (!permListCheck(permlist, group, report) && !permlist->optional)
			return false;
	}

	return true;
}

/*
 * Same as checkPermission, just check cpuset dir & interface files
 *
 */
static bool
checkCpuSetPermission(Oid group, bool report)
{
	if (!gp_resource_group_enable_cgroup_cpuset)
		return true;

	if (!permListCheck(&cpusetPermList, group, report) &&
		!cpusetPermList.optional)
		return false;

	return true;
}

/* get total ram and total swap (in Byte) from sysinfo */
static void
getMemoryInfo(unsigned long *ram, unsigned long *swap)
{
	struct sysinfo info;
	if (sysinfo(&info) < 0)
		elog(ERROR, "can't get memory infomation: %s", strerror(errno));
	*ram = info.totalram;
	*swap = info.totalswap;
}

/* get cgroup ram and swap (in Byte) */
static void
getCgMemoryInfo(uint64 *cgram, uint64 *cgmemsw)
{
	*cgram = readInt64(RESGROUP_ROOT_ID, "", "memory", "memory.limit_in_bytes");

	if (gp_resource_group_enable_cgroup_swap)
	{
		*cgmemsw = readInt64(RESGROUP_ROOT_ID, "",
							 "memory", "memory.memsw.limit_in_bytes");
	}
	else
	{
		elog(DEBUG1, "swap memory is unlimited");
		*cgmemsw = (uint64) -1LL;
	}
}

/* get vm.overcommit_ratio */
static int
getOvercommitRatio(void)
{
	int ratio;
	char data[MAX_INT_STRING_LEN];
	size_t datasize = sizeof(data);
	const char *path = "/proc/sys/vm/overcommit_ratio";

	readData(path, data, datasize);

	if (sscanf(data, "%d", &ratio) != 1)
		elog(ERROR, "invalid number '%s' in '%s'", data, path);

	return ratio;
}

/* detect cgroup mount point */
static bool
detectCgroupMountPoint(void)
{
	struct mntent *me;
	FILE *fp;

	if (cgdir[0])
		return true;

	fp = setmntent(PROC_MOUNTS, "r");
	if (fp == NULL)
		CGROUP_CONFIG_ERROR("can not open '%s' for read", PROC_MOUNTS);


	while ((me = getmntent(fp)))
	{
		char * p;

		if (strcmp(me->mnt_type, "cgroup"))
			continue;

		strncpy(cgdir, me->mnt_dir, sizeof(cgdir) - 1);

		p = strrchr(cgdir, '/');
		if (p == NULL)
			CGROUP_CONFIG_ERROR("cgroup mount point parse error: %s", cgdir);
		else
			*p = 0;
		break;
	}

	endmntent(fp);

	return !!cgdir[0];
}

/* Return the name for the OS group implementation */
const char *
ResGroupOps_Name(void)
{
	return "cgroup";
}

/*
 * Probe the configuration for the OS group implementation.
 *
 * Return true if everything is OK, or false is some requirements are not
 * satisfied.  Will not fail in either case.
 */
bool
ResGroupOps_Probe(void)
{
	/*
	 * We only have to do these checks and initialization once on each host,
	 * so only let postmaster do the job.
	 */
	if (IsUnderPostmaster)
		return true;

	/*
	 * Ignore the error even if cgroup mount point can not be successfully
	 * probed, the error will be reported in Bless() later.
	 */
	if (!detectCgroupMountPoint())
		return false;

	/*
	 * Probe for optional features like the 'cgroup' memory auditor,
	 * do not raise any errors.
	 */
	if (!checkPermission(RESGROUP_ROOT_ID, false))
		return false;

	return true;
}

/* Check whether the OS group implementation is available and useable */
void
ResGroupOps_Bless(void)
{
	/*
	 * We only have to do these checks and initialization once on each host,
	 * so only let postmaster do the job.
	 */
	if (IsUnderPostmaster)
		return;

	/*
	 * We should have already detected for cgroup mount point in Probe(),
	 * it was not an error if the detection failed at that step.  But once
	 * we call Bless() we know we want to make use of cgroup then we must
	 * know the mount point, otherwise it's a critical error.
	 */
	if (!cgdir[0])
		CGROUP_CONFIG_ERROR("can not find cgroup mount point");

	/*
	 * Check again, this time we will fail on unmet requirements.
	 */
	checkPermission(RESGROUP_ROOT_ID, true);
}

/* Initialize the OS group */
void
ResGroupOps_Init(void)
{
	/*
	 * cfs_quota_us := cfs_period_us * ncores * gp_resource_group_cpu_limit
	 * shares := 1024 * gp_resource_group_cpu_priority
	 *
	 * We used to set a large shares (like 1024 * 256, the maximum possible
	 * value), it has very bad effect on overall system performance,
	 * especially on 1-core or 2-core low-end systems.
	 * Processes in a cold cgroup get launched and scheduled with large
	 * latency (a simple `cat a.txt` may executes for more than 100s).
	 * Here a cold cgroup is a cgroup that doesn't have active running
	 * processes, this includes not only the toplevel system cgroup,
	 * but also the inactive gpdb resgroups.
	 */

	int64 cfs_period_us;
	int ncores = getCpuCores();
	const char *comp = "cpu";

	cfs_period_us = readInt64(RESGROUP_ROOT_ID, NULL, comp, "cpu.cfs_period_us");
	writeInt64(RESGROUP_ROOT_ID, NULL, comp, "cpu.cfs_quota_us",
			   cfs_period_us * ncores * gp_resource_group_cpu_limit);
	writeInt64(RESGROUP_ROOT_ID, NULL, comp, "cpu.shares",
			   1024LL * gp_resource_group_cpu_priority);

	if (gp_resource_group_enable_cgroup_cpuset)
	{
		/*
		 * Get cpuset.mems and cpuset.cpus values from cgroup cpuset root path,
		 * and set them to cpuset/gpdb/cpuset.mems and cpuset/gpdb/cpuset.cpus
		 * to make sure that gpdb directory configuration is same as its
		 * parent directory
		 */
		char buffer[MaxCpuSetLength];
		readStr(RESGROUP_COMPROOT_ID, NULL, "cpuset", "cpuset.mems",
				buffer, sizeof(buffer));
		writeStr(RESGROUP_ROOT_ID, NULL, "cpuset", "cpuset.mems", buffer);
		readStr(RESGROUP_COMPROOT_ID, NULL, "cpuset", "cpuset.cpus",
				buffer, sizeof(buffer));
		writeStr(RESGROUP_ROOT_ID, NULL, "cpuset", "cpuset.cpus", buffer);

		createDefaultCpuSetGroup();
	}

	/*
	 * Put postmaster and all the children processes into the gpdb cgroup,
	 * otherwise auxiliary processes might get too low priority when
	 * gp_resource_group_cpu_priority is set to a large value
	 */
	ResGroupOps_AssignGroup(RESGROUP_ROOT_ID, NULL, PostmasterPid);
}

/* Adjust GUCs for this OS group implementation */
void
ResGroupOps_AdjustGUCs(void)
{
	/*
	 * cgroup cpu limitation works best when all processes have equal
	 * priorities, so we force all the segments and postmaster to
	 * work with nice=0.
	 *
	 * this function should be called before GUCs are dispatched to segments.
	 */
	gp_segworker_relative_priority = 0;
}

/*
 * Create the OS group for group.
 */
void
ResGroupOps_CreateGroup(Oid group)
{
	int retry = 0;

	if (!createDir(group, "cpu")
		|| !createDir(group, "cpuacct")
		|| (gp_resource_group_enable_cgroup_cpuset &&
			!createDir(group, "cpuset"))
		|| (gp_resource_group_enable_cgroup_memory &&
			!createDir(group, "memory")))
	{
		CGROUP_ERROR("can't create cgroup for resgroup '%d': %s",
					 group, strerror(errno));
	}

	/*
	 * although the group dir is created the interface files may not be
	 * created yet, so we check them repeatedly until everything is ready.
	 */
	while (++retry <= MAX_RETRY && !checkPermission(group, false))
		pg_usleep(1000);

	if (retry > MAX_RETRY)
	{
		/*
		 * still not ready after MAX_RETRY retries, might be a real error,
		 * raise the error.
		 */
		checkPermission(group, true);
	}

	if (gp_resource_group_enable_cgroup_cpuset)
	{
		/*
		 * Initialize cpuset.mems and cpuset.cpus values as its parent directory
		 */
		char buffer[MaxCpuSetLength];

		readStr(RESGROUP_ROOT_ID,
				NULL,
				"cpuset",
				"cpuset.mems",
				buffer,
				sizeof(buffer));
		writeStr(group, NULL, "cpuset", "cpuset.mems", buffer);

		readStr(RESGROUP_ROOT_ID,
				NULL,
				"cpuset",
				"cpuset.cpus",
				buffer,
				sizeof(buffer));
		writeStr(group, NULL, "cpuset", "cpuset.cpus", buffer);
	}
}

/*
 * Create the OS group for default cpuset group.
 * default cpuset group is a special group, only take effect in cpuset
 */
static void
createDefaultCpuSetGroup(void)
{
	int retry = 0;

	if (!createDir(DEFAULT_CPUSET_GROUP_ID, "cpuset"))
	{
		CGROUP_ERROR("can't create cpuset cgroup for resgroup '%d': %s",
					 DEFAULT_CPUSET_GROUP_ID, strerror(errno));
	}

	/*
	 * although the group dir is created the interface files may not be
	 * created yet, so we check them repeatedly until everything is ready.
	 */
	while (++retry <= MAX_RETRY &&
		   !checkCpuSetPermission(DEFAULT_CPUSET_GROUP_ID, false))
		pg_usleep(1000);

	if (retry > MAX_RETRY)
	{
		/*
		 * still not ready after MAX_RETRY retries, might be a real error,
		 * raise the error.
		 */
		checkCpuSetPermission(DEFAULT_CPUSET_GROUP_ID, true);
	}

	/*
	 * Initialize cpuset.mems and cpuset.cpus in default group as its
	 * parent directory
	 */
	char buffer[MaxCpuSetLength];

	readStr(RESGROUP_ROOT_ID,
			NULL,
			"cpuset",
			"cpuset.mems",
			buffer,
			sizeof(buffer));
	writeStr(DEFAULT_CPUSET_GROUP_ID, NULL, "cpuset", "cpuset.mems", buffer);

	readStr(RESGROUP_ROOT_ID,
			NULL,
			"cpuset",
			"cpuset.cpus",
			buffer,
			sizeof(buffer));
	writeStr(DEFAULT_CPUSET_GROUP_ID, NULL, "cpuset", "cpuset.cpus", buffer);
}

/*
 * Destroy the OS group for group.
 *
 * One OS group can not be dropped if there are processes running under it,
 * if migrate is true these processes will be moved out automatically.
 */
void
ResGroupOps_DestroyGroup(Oid group, bool migrate)
{
	if (!removeDir(group, "cpu", "cpu.shares", migrate)
		|| !removeDir(group, "cpuacct", NULL, migrate)
		|| (gp_resource_group_enable_cgroup_cpuset &&
			!removeDir(group, "cpuset", NULL, migrate))
		|| (gp_resource_group_enable_cgroup_memory &&
			!removeDir(group, "memory", "memory.limit_in_bytes", migrate)))
	{
		CGROUP_ERROR("can't remove cgroup for resgroup '%d': %s",
			 group, strerror(errno));
	}
}

/*
 * Assign a process to the OS group. A process can only be assigned to one
 * OS group, if it's already running under other OS group then it'll be moved
 * out that OS group.
 *
 * pid is the process id.
 */
void
ResGroupOps_AssignGroup(Oid group, ResGroupCaps *caps, int pid)
{
	bool oldViaCpuset = oldCaps.cpuRateLimit == CPU_RATE_LIMIT_DISABLED;
	bool curViaCpuset = caps ? caps->cpuRateLimit == CPU_RATE_LIMIT_DISABLED : false;

	/* needn't write to file if the pid has already been written in.
	 * Unless it has not been writtien or the group has changed or
	 * cpu control mechanism has changed */
	if (IsUnderPostmaster &&
		group == currentGroupIdInCGroup &&
		caps != NULL &&
		oldViaCpuset == curViaCpuset
		)
		return;

	writeInt64(group, NULL, "cpu", "cgroup.procs", pid);
	writeInt64(group, NULL, "cpuacct", "cgroup.procs", pid);

	if (gp_resource_group_enable_cgroup_cpuset)
	{
		if (caps == NULL || !curViaCpuset)
		{
			/* add pid to default group */
			writeInt64(DEFAULT_CPUSET_GROUP_ID, NULL, "cpuset", "cgroup.procs", pid);
		}
		else
		{
			writeInt64(group, NULL, "cpuset", "cgroup.procs", pid);
		}
	}

	/*
	 * Do not assign the process to cgroup/memory for now.
	 */

	currentGroupIdInCGroup = group;
	if (caps != NULL)
	{
		oldCaps.cpuRateLimit = caps->cpuRateLimit;
		StrNCpy(oldCaps.cpuset, caps->cpuset, sizeof(oldCaps.cpuset));
	}
}

/*
 * Lock the OS group. While the group is locked it won't be removed by other
 * processes.
 *
 * This function would block if block is true, otherwise it return with -1
 * immediately.
 *
 * On success it return a fd to the OS group, pass it to
 * ResGroupOps_UnLockGroup() to unblock it.
 */
int
ResGroupOps_LockGroup(Oid group, const char *comp, bool block)
{
	char path[MAXPGPATH];
	size_t pathsize = sizeof(path);

	buildPath(group, NULL, comp, "", path, pathsize);

	return lockDir(path, block);
}

/*
 * Unblock a OS group.
 *
 * fd is the value returned by ResGroupOps_LockGroup().
 */
void
ResGroupOps_UnLockGroup(Oid group, int fd)
{
	if (fd >= 0)
		close(fd);
}

/*
 * Set the cpu rate limit for the OS group.
 *
 * cpu_rate_limit should be within [0, 100].
 */
void
ResGroupOps_SetCpuRateLimit(Oid group, int cpu_rate_limit)
{
	const char *comp = "cpu";

	/* SUB/shares := TOP/shares * cpu_rate_limit */

	int64 shares = readInt64(RESGROUP_ROOT_ID, NULL, comp, "cpu.shares");
	writeInt64(group, NULL, comp, "cpu.shares", shares * cpu_rate_limit / 100);
}

/*
 * Set the memory limit for the OS group by rate.
 *
 * memory_limit should be within [0, 100].
 */
void
ResGroupOps_SetMemoryLimit(Oid group, int memory_limit)
{
	int fd;
	int32 memory_limit_in_chunks;

	memory_limit_in_chunks = ResGroupGetVmemLimitChunks() * memory_limit / 100;
	memory_limit_in_chunks *= ResGroupGetSegmentNum();

	fd = ResGroupOps_LockGroup(group, "memory", true);
	ResGroupOps_SetMemoryLimitByValue(group, memory_limit_in_chunks);
	ResGroupOps_UnLockGroup(group, fd);
}

/*
 * Set the memory limit for the OS group by value.
 *
 * memory_limit is the limit value in chunks
 *
 * If cgroup supports memory swap, we will write the same limit to
 * memory.memsw.limit and memory.limit.
 */
void
ResGroupOps_SetMemoryLimitByValue(Oid group, int32 memory_limit)
{
	const char *comp = "memory";
	int64 memory_limit_in_bytes;

	if (!gp_resource_group_enable_cgroup_memory)
		return;

	memory_limit_in_bytes = VmemTracker_ConvertVmemChunksToBytes(memory_limit);

	/* Is swap interfaces enabled? */
	if (!gp_resource_group_enable_cgroup_swap)
	{
		/* No, then we only need to setup the memory limit */
		writeInt64(group, NULL, comp, "memory.limit_in_bytes",
				memory_limit_in_bytes);
	}
	else
	{
		/* Yes, then we have to setup both the memory and mem+swap limits */

		int64 memory_limit_in_bytes_old;

		/*
		 * Memory limit should always <= mem+swap limit, then the limits
		 * must be set in a proper order depending on the relation between
		 * new and old limits.
		 */
		memory_limit_in_bytes_old = readInt64(group, NULL,
				comp, "memory.limit_in_bytes");

		if (memory_limit_in_bytes > memory_limit_in_bytes_old)
		{
			/* When new value > old memory limit, write mem+swap limit first */
			writeInt64(group, NULL, comp, "memory.memsw.limit_in_bytes",
					memory_limit_in_bytes);
			writeInt64(group, NULL, comp, "memory.limit_in_bytes",
					memory_limit_in_bytes);
		}
		else if (memory_limit_in_bytes < memory_limit_in_bytes_old)
		{
			/* When new value < old memory limit,  write memory limit first */
			writeInt64(group, NULL, comp, "memory.limit_in_bytes",
					memory_limit_in_bytes);
			writeInt64(group, NULL, comp, "memory.memsw.limit_in_bytes",
					memory_limit_in_bytes);
		}
	}
}

/*
 * Get the cpu usage of the OS group, that is the total cpu time obtained
 * by this OS group, in nano seconds.
 */
int64
ResGroupOps_GetCpuUsage(Oid group)
{
	const char *comp = "cpuacct";

	return readInt64(group, NULL, comp, "cpuacct.usage");
}

/*
 * Get the memory usage of the OS group
 *
 * memory usage is returned in chunks
 */
int32
ResGroupOps_GetMemoryUsage(Oid group)
{
	const char *comp = "memory";
	int64 memory_usage_in_bytes;
	char *prop;

	/* Report 0 if cgroup memory is not enabled */
	if (!gp_resource_group_enable_cgroup_memory)
		return 0;

	prop = gp_resource_group_enable_cgroup_swap
		? "memory.memsw.usage_in_bytes"
		: "memory.usage_in_bytes";

	memory_usage_in_bytes = readInt64(group, NULL, comp, prop);

	return VmemTracker_ConvertVmemBytesToChunks(memory_usage_in_bytes);
}

/*
 * Get the memory limit of the OS group
 *
 * memory limit is returned in chunks
 */
int32
ResGroupOps_GetMemoryLimit(Oid group)
{
	const char *comp = "memory";
	int64 memory_limit_in_bytes;

	/* Report unlimited (max int32) if cgroup memory is not enabled */
	if (!gp_resource_group_enable_cgroup_memory)
		return (int32) ((1U << 31) - 1);

	memory_limit_in_bytes = readInt64(group, NULL,
			comp, "memory.limit_in_bytes");

	return VmemTracker_ConvertVmemBytesToChunks(memory_limit_in_bytes);
}

/*
 * Get the count of cpu cores on the system.
 */
int
ResGroupOps_GetCpuCores(void)
{
	return getCpuCores();
}

/*
 * Get the total memory on the system in MB.
 * Read from sysinfo and cgroup to get correct ram and swap.
 * (total RAM * overcommit_ratio + total Swap)
 */
int
ResGroupOps_GetTotalMemory(void)
{
	unsigned long ram, swap, total;
	int overcommitRatio;
	uint64 cgram, cgmemsw;
	uint64 memsw;
	uint64 outTotal;

	overcommitRatio = getOvercommitRatio();
	getMemoryInfo(&ram, &swap);
	/* Get sysinfo total ram and swap size. */
	memsw = ram + swap;
	outTotal = swap + ram * overcommitRatio / 100;
	getCgMemoryInfo(&cgram, &cgmemsw);
	ram = Min(ram, cgram);
	/*
	 * In the case that total ram and swap read from sysinfo is larger than
	 * from cgroup, ram and swap must both be limited, otherwise swap must
	 * not be limited(we can safely use the value from sysinfo as swap size).
	 */
	if (cgmemsw < memsw)
		swap = cgmemsw - ram;
	/* 
	 * If it is in container, the total memory is limited by both the total
	 * memoery outside and the memsw of the container.
	 */
	total = Min(outTotal, swap + ram); 
	return total >> BITS_IN_MB;
}

/*
 * Set the cpuset for the OS group.
 * @param group: the destination group
 * @param cpuset: the value to be set
 * The syntax of CPUSET is a combination of the tuples, each tuple represents
 * one core number or the core numbers interval, separated by comma.
 * E.g. 0,1,2-3.
 */
void
ResGroupOps_SetCpuSet(Oid group, const char *cpuset)
{
	if (!gp_resource_group_enable_cgroup_cpuset)
		return ;
	const char *comp = "cpuset";
	writeStr(group, NULL, comp, "cpuset.cpus", cpuset);
}

/*
 * Get the cpuset of the OS group.
 * @param group: the destination group
 * @param cpuset: the str to be set
 * @param len: the upper limit of the str
 */
void
ResGroupOps_GetCpuSet(Oid group, char *cpuset, int len)
{
	if (!gp_resource_group_enable_cgroup_cpuset)
		return ;
	const char *comp = "cpuset";
	readStr(group, NULL, comp, "cpuset.cpus", cpuset, len);
}
