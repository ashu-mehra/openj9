#include "j9port.h"
#include "j9porterror.h"
#include "j9portpg.h"
#include "portpriv.h"
#include "portnls.h"
#include "ut_j9prt.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>

#define CRIU_DUMP_LOG_FILE "dump.log"
#define CRIU_RESTORE_LOG_FILE "restore.log"
#define CRIU_SERVICE_BINARY "/home/ashu/data/criu/criu/criu"

int (*criu_init_opts)(void);
int (*criu_set_log_file)(const char *log_file);
int (*criu_set_log_level)(int log_level);
int (*criu_set_service_binary)(const char *path);
void (*criu_set_images_dir_fd)(int fd); /* must be set for dump/restore */
int (*criu_dump)(void);
int (*criu_restore)(void);

int32_t
j9cr_startup(struct J9PortLibrary *portLibrary) {
	uintptr_t criuLibHandle = -1;
	uintptr_t rc = 0;       
	OMRPORT_ACCESS_FROM_J9PORT(portLibrary);

	rc = omrsl_open_shared_library("criu", &criuLibHandle, J9PORT_SLOPEN_DECORATE | J9PORT_SLOPEN_LAZY);
	if (0 != rc) {
		omrtty_printf("j9cr_startup> failed to open shared library \"criu\"");
		goto _end;
	}

	rc = omrsl_lookup_name(criuLibHandle, "criu_init_opts", (uintptr_t *)&criu_init_opts, "V");
	if (0 != rc) {
		omrtty_printf("j9cr_startup> failed to look up function criu_init_opts\n");
		goto _end;
	}
	rc = omrsl_lookup_name(criuLibHandle, "criu_set_log_file", (uintptr_t *)&criu_set_log_file, "L");
	if (0 != rc) {
		omrtty_printf("j9cr_startup> failed to look up function criu_set_log_file\n");
		goto _end;
	}
	rc = omrsl_lookup_name(criuLibHandle, "criu_set_log_level", (uintptr_t *)&criu_set_log_level, "I");
	if (0 != rc) {
		omrtty_printf("j9cr_startup> failed to look up function criu_set_log_level\n");
		goto _end;
	}
	rc = omrsl_lookup_name(criuLibHandle, "criu_set_images_dir_fd", (uintptr_t *)&criu_set_images_dir_fd, "I");
	if (0 != rc) {
		omrtty_printf("j9cr_startup> failed to look up function criu_set_images_dir_fd\n");
		goto _end;
	}
	rc = omrsl_lookup_name(criuLibHandle, "criu_set_service_binary", (uintptr_t *)&criu_set_service_binary, "L");
	if (0 != rc) {
		omrtty_printf("j9cr_startup> failed to look up function criu_set_service_binary\n");
		goto _end;
	}
	rc = omrsl_lookup_name(criuLibHandle, "criu_dump", (uintptr_t *)&criu_dump, "V");
	if (0 != rc) {
		omrtty_printf("j9cr_startup> failed to look up function criu_dump\n");
		goto _end;
	}
	rc = omrsl_lookup_name(criuLibHandle, "criu_restore", (uintptr_t *)&criu_restore, "V");
	if (0 != rc) {
		omrtty_printf("j9cr_startup> failed to look up function criu_restore\n");
		goto _end;
	}

_end:
	if (0 != rc) {
		if (-1 != criuLibHandle) {
			omrsl_close_shared_library(criuLibHandle);
		}
	} else {
		PPG_criuLibHandle = criuLibHandle;
	}

	omrtty_printf("j9cr_startup> rc: %d\n", rc);
	return rc;
}

int32_t
j9cr_create_checkpoint(struct J9PortLibrary *portLibrary) {
	int32_t rc = -1;
	int fd = -1;
	char *currentDir = getcwd(NULL, 0);
	OMRPORT_ACCESS_FROM_J9PORT(portLibrary);

	fd = open(currentDir, O_DIRECTORY);
	if (-1 == fd) {
		omrtty_printf("criu_dump> opening current directory %s failed\n", currentDir);
	}
	criu_init_opts();
	criu_set_log_file(CRIU_DUMP_LOG_FILE);
	criu_set_log_level(4);
	criu_set_service_binary(CRIU_SERVICE_BINARY);
	criu_set_images_dir_fd(fd);

	rc = criu_dump();
	if (0 == rc) {
		omrtty_printf("criu_dump> Checkpoint created\n");
	} else if (1 == rc) {
		omrtty_printf("criu_dump> Restored from checkpoint\n");
	} else {
		omrtty_printf("criu_dump> Checkpoint failed with return code: %d\n", rc);
	}
	return rc;
}

int32_t
j9cr_restore(struct J9PortLibrary *portLibrary) {
	int32_t rc = -1;
	int fd = -1;
	char *currentDir = getcwd(NULL, 0);
	OMRPORT_ACCESS_FROM_J9PORT(portLibrary);

	fd = open(currentDir, O_DIRECTORY);
	if (-1 == fd) {
		omrtty_printf("criu_dump> opening current directory %s failed\n", currentDir);
	}

	criu_init_opts();
	criu_set_log_file(CRIU_RESTORE_LOG_FILE);
	criu_set_log_level(4);
	criu_set_service_binary(CRIU_SERVICE_BINARY);
	criu_set_images_dir_fd(fd);

	rc = criu_restore();
	if (rc <= 0) {
		omrtty_printf("criu_restore> Restore failed with return code: %d\n", rc);
	} else {
		omrtty_printf("criu_restore> Restore done for pid: %d\n", rc);
	}
	return rc;
}

BOOLEAN
j9cr_checkpoint_exists(struct J9PortLibrary *portLibrary) {
	J9FileStat fileStat;
	int32_t rc = 0;
	OMRPORT_ACCESS_FROM_J9PORT(portLibrary);

	rc = omrfile_stat(CRIU_DUMP_LOG_FILE, 0, &fileStat);
	if (0 == rc) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void
j9cr_shutdown(struct J9PortLibrary *portLibrary) {
	OMRPORT_ACCESS_FROM_J9PORT(portLibrary);

	if (0 != PPG_criuLibHandle) {
		omrsl_close_shared_library(PPG_criuLibHandle);
	}
}
