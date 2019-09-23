#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>

#include "SnapshotCriuLib.hpp"

static J9SnapshotCriuLib *
J9SnapshotCriuLib::newInstance(J9JavaVM *vm) {
	int32_t rc = 0;
	J9SnapshotCriuLib *snapshot = NULL;
	PORT_ACCESS_FROM_JAVAVM(vm);
	OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

	snapshot = (J9SnapshotCriuLib *)omrmem_allocate_memory(sizeof(J9SnapshotCriuLib));

	if (NULL != snapshot) {
		new(snapshot) J9SnapshotCriuLib(vm);
		rc = snapshot->initialize(vm);
		if (0 != rc) {
			snapshot->cleanup();
			snapshot = NULL;
		}
	}
	return snapshot;
}

int32_t
J9SnapshotCriuLib::initialize(J9JavaVM *vm) {
	int32_t rc = 0;

	J9Snapshot::initialize(vm);
	rc = loadCriuLib();

	dumpLogFile = "/tmp/javasnapshot/dump.log"
	restoreLogFile = "/tmp/javasnapshot/restore.log"
	imagesDir= "/tmp/javasnapshot"
	logLevel = 4;
	
	return rc;
}

int32_t
J9SnapshotCriuLib::loadCriuLib() {
	uintptr_t rc = 0;
	PORT_ACCESS_FROM_JAVAVM(vm);
	OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

	rc = omrsl_open_shared_library("criu", &_criuLibHandle, J9PORT_SLOPEN_DECORATE | J9PORT_SLOPEN_LAZY);
	if (0 != rc) {
		omrtty_printf("loadCriuLib> failed to open shared library \"criu\"");
		goto _end;
	}

	rc = omrsl_lookup_name(_criuLibHandle, "criu_init_opts", (uintptr_t *)&criu_init_opts, "V");
	if (0 != rc) {
		omrtty_printf("loadCriuLib> failed to look up function criu_init_opts\n");
		goto _end;
	}
	rc = omrsl_lookup_name(_criuLibHandle, "criu_set_log_file", (uintptr_t *)&criu_set_log_file, "L");
	if (0 != rc) {
		omrtty_printf("loadCriuLib> failed to look up function criu_set_log_file\n");
		goto _end;
	}
	rc = omrsl_lookup_name(_criuLibHandle, "criu_set_log_level", (uintptr_t *)&criu_set_log_level, "I");
	if (0 != rc) {
		omrtty_printf("loadCriuLib> failed to look up function criu_set_log_level\n");
		goto _end;
	}
	rc = omrsl_lookup_name(_criuLibHandle, "criu_set_images_dir_fd", (uintptr_t *)&criu_set_images_dir_fd, "I");
	if (0 != rc) {
		omrtty_printf("loadCriuLib> failed to look up function criu_set_images_dir_fd\n");
		goto _end;
	}
	rc = omrsl_lookup_name(_criuLibHandle, "criu_set_service_binary", (uintptr_t *)&criu_set_service_binary, "L");
	if (0 != rc) {
		omrtty_printf("loadCriuLib> failed to look up function criu_set_service_binary\n");
		goto _end;
	}
	rc = omrsl_lookup_name(_criuLibHandle, "criu_dump", (uintptr_t *)&criu_dump, "V");
	if (0 != rc) {
		omrtty_printf("loadCriuLib> failed to look up function criu_dump\n");
		goto _end;
	}
	rc = omrsl_lookup_name(_criuLibHandle, "criu_restore", (uintptr_t *)&criu_restore, "V");
	if (0 != rc) {
		omrtty_printf("loadCriuLib> failed to look up function criu_restore\n");
		goto _end;
	}

_end:
	if (0 != rc) {
		if (-1 != _criuLibHandle) {
			omrsl_close_shared_library(_criuLibHandle);
		}
	}

	return rc;
}

int32_t
J9SnapshotCriuLib::create() {
	int32_t rc = -1;
	int32_t fd = -1;
	PORT_ACCESS_FROM_JAVAVM(vm);
	OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

	rc = J9Snapshot::create();
	if (0 != rc) {
		goto _end;
	}

	fd = open(imagesDir, O_DIRECTORY);
	if (-1 == fd) {
		omrtty_printf("J9SnapshotCriuLib::create> opening current directory %s failed\n", currentDir);
	}
	criu_init_opts();
	criu_set_log_file(dumpLogFile);
	criu_set_log_level(logLevel);
	criu_set_service_binary(criuBinaryPath);
	criu_set_images_dir_fd(fd);

	rc = criu_dump();
	if (0 == rc) {
		omrtty_printf("J9SnapshotCriuLib::create> Checkpoint created\n");
	} else if (1 == rc) {
		omrtty_printf("J9SnapshotCriuLib::criu_dump> Restored from checkpoint\n");
		rc = postRestore();
	} else {
		omrtty_printf("J9SnapshotCriuLib::criu_dump> Checkpoint failed with return code: %d\n", rc);
	}

_end:
	return rc;
}

int32_t
J9SnapshotCriuLib::restore() {
	int32_t rc = -1;
	int fd = -1;
	PORT_ACCESS_FROM_JAVAVM(vm);
	OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

	rc = J9Snapshot::restore();
	if (0 != rc) {
		goto _end;
	}

	fd = open(imagesDir, O_DIRECTORY);
	if (-1 == fd) {
		omrtty_printf("J9SnapshotCriuLib::restore> opening current directory %s failed\n", currentDir);
	}

	criu_init_opts();
	criu_set_log_file(restoreLogFile);
	criu_set_log_level(logLevel);
	criu_set_service_binary(criuBinaryPath);
	criu_set_images_dir_fd(fd);

	rc = criu_restore();
	if (rc <= 0) {
		omrtty_printf("J9SnapshotCriuLib::restore> Restore failed with return code: %d\n", rc);
	} else {
		omrtty_printf("J9SnapshotCriuLib::restore> Restore done for pid: %d\n", rc);
	}
_end:
	return rc;
}

