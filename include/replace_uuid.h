/*
 * uuid: wrapper declarations.
 *
 *	heartbeat originally used "uuid" functionality by calling directly,
 *	and only, onto the "e2fsprogs" implementation.
 *
 *	The run-time usages in the code have since been abstracted, funnelled
 *	through a thin, common interface layer: a Good Thing.
 *
 *	Similarly, the compile-time usages of "include <uuid/uuid.h>" are
 *	replaced, being funnelled through a reference to this header file.
 *
 *	This header file interfaces onto the actual underlying implementation.
 *	In the case of the "e2fsprogs" implementation, it is simply a stepping
 *	stone onto "<uuid/uuid.h>".  As other implementations are accommodated,
 *	so their header requirements can be accommodated here.
 *
 * Copyright (C) 2004 David Lee <t.d.lee@durham.ac.uk>
 */

#ifndef REPLACE_UUID_H
#define REPLACE_UUID_H

typedef unsigned char uuid_t[16];
void uuid_clear(uuid_t uu);
int uuid_compare(const uuid_t uu1, const uuid_t uu2);
void uuid_copy(uuid_t dst, const uuid_t src);
void uuid_generate(uuid_t out);
void uuid_generate_random(uuid_t out);
int uuid_is_null(const uuid_t uu);
int uuid_parse(const char *in, uuid_t uu);
void uuid_unparse(const uuid_t uu, char *out);

#endif /* REPLACE_UUID_H */