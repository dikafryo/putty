/*
 * no-jump-list.c: stub jump list functions for Windows executables
 * that don't update the jump list.
 */

#include "putty.h"

void add_session_to_jumplist(const char * const sessionname) {}
void remove_session_from_jumplist(const char * const sessionname) {}
void clear_jumplist(void) {}

/* Portable windows do not register an application ID with the shell. */
bool set_explicit_app_user_model_id(void) { return false; }
