#include "Snapshot.hpp"

static Snapshot *
J9Snapshot::newInstance(JavaVM *vm) {
	J9Snapshot *snapshot = NULL;
	
	snapshot = SnapshotCriuLib::newInstance(vm);
	if (NULL != snapshot) {
		type = USING_CRIU_LIB;
		goto exit;
	}

	snapshot = SnapshotCriuRpc::newInstance(vm);
	if (NULL != snapshot) {
		type = USING_CRIU_RPC;
		goto exit;
	}
exit:
	return snapshot;
}

int32_t
J9Snapshot::initialize(JavaVM *vm) {
	this->vm = vm;
	return 0;
}

int32_t
J9Snapshot::create() {
	UDATA continueSnapshot = TRUE;
	
	TRIGGER_J9HOOK_VM_PRE_SNAPSHOT(vm->hookInterface, vm, continueSnapshot);
	
	if (continueSnapshot) {
		return 0;
	} else {
		return 1;
	}
}

int32_t
J9Snapshot::restore() {
	return 0;
}

int32_t
J9Snapshot::postRestore() {
	UDATA continueRunning = FALSE;
	TRIGGER_J9HOOK_VM_POST_RESTORE(vm->hookInterface, vm, continueRunning);

	if (continueRunning) {
		return 0;
	} else {
		return 1;
	}
}

void
J9Snapshot::setOptions(const char *options) {
	this->options = options;
}
