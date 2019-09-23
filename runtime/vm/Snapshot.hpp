#if !defined(SNAPSHOT_HPP_)
#define SNAPSHOT_HPP_

class J9Snapshot {
protected:
	J9JavaVM *vm;
	char *options;

private:
	J9SnapshotType _type;
	uintptr_t _criuLibHandle;

	static int32_t initialize(JavaVM *vm);
public:
	enum J9SnapshotType {
		USING_CRIU_LIB=1,
		USING_CRIU_RPC=2
	}

	static J9Snapshot *newInstance(J9JavaVM *vm);
	virtual int32_t create();
	virtual int32_t restore();
	int32_t postRestore();
	J9SnapshotType getSnapshotType();
	void setOptions(char *options);
};

#endif /* SNAPSHOT_HPP_ */
