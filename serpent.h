/*
Edited by Medicijnman 20150310
update log:
20150310 code reformatted and reduced from 800+ lines to 99
20150722 encblk and decblk implemented
See below for original author and license.
*/
// Copyright in this code is held by Dr B. R. Gladman but free direct or
// derivative use is permitted subject to acknowledgement of its origin.
// Dr B. R. Gladman                               .   25th January 2000.
#ifndef SERPENT_H
#define SERPENT_H

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#include <stddef.h>
#include <stdint.h>
#include <endian.h>

typedef struct {
	uint32_t l_key[140];
} serpent_ctx;

/*
important notes:
key_len in init should not exceed 256
encrypt and decrypt functions do not support padding
blocks are divided in 16 bytes
*/

void serpent_init(serpent_ctx *ctx, const void *src, int key_len);
void serpent_encrypt(serpent_ctx *ctx, const void *src, void *dest);
void serpent_encblk(serpent_ctx *ctx, const void *src, void *dest, size_t length);
void serpent_decrypt(serpent_ctx *ctx, const void *src, void *dest);
void serpent_decblk(serpent_ctx *ctx, const void *src, void *dest, size_t length);

#endif
