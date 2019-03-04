
int32_t
j9cr_startup(struct J9PortLibrary *portLibrary) {
	return 0;
}

BOOLEAN
j9cr_create_checkpoint(struct j9portlibrary *portlibrary) {
	return FALSE;
}

int32_t
j9cr_checkpoint_exists(struct j9portlibrary *portlibrary) {
	return 0;
}

int32_t
j9cr_restore(struct J9PortLibrary *portLibrary) {
	return 0;
}
void
j9cr_shutdown(struct J9PortLibrary *portLibrary) {
}
