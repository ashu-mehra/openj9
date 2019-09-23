#if !defined(SNAPSHOTCRIULIB_HPP_)
#define SNAPSHOTCRIULIB_HPP_ 

class J9SnapshotCriuLib : public J9Snapshot {
private:
	uintptr_t _criuLibHandle;
	const char *dumpLogFile;
	const char *restoreLogFile;
	int32_t logLevel;
	const char *criuBinaryPath;
	const char *imagesDir;	

	/* Function pointers to libcriu apis */
	int (*criu_init_opts)(void);
	int (*criu_set_log_file)(const char *log_file);
	int (*criu_set_log_level)(int log_level);
	int (*criu_set_service_binary)(const char *path);
	void (*criu_set_images_dir_fd)(int fd); /* must be set for dump/restore */
	int (*criu_dump)(void);
	int (*criu_restore)(void);

	int32_t J9SnapshotCriuLib::initialize(J9JavaVM *vm);
	int32_t loadCriuLib();

public:
	static J9SnapshotCriuLib *newInstance(J9JavaVM *vm);
	
	int32_t create();
	int32_t restore();
};

#endif /* SNAPSHOTCRIULIB_HPP_ */
