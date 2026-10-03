/* MWLD insists on a `__start` entry symbol even for an overlay that is never
 * entered. The retail `__start` (0x8000522C) keeps running the game; this one
 * exists only to satisfy the linker and is never called. */
void __start(void);
void __start(void) {}
