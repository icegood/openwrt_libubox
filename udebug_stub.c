/*
 * udebug - debug ring buffer library
 *
 * Copyright (C) 2023 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "udebug.h"
/* ---- time ---- */

uint64_t udebug_timestamp(void)
{
	return 0;
}

/* ---- entry handling ---- */

void udebug_entry_init_ts(struct udebug_buf *buf, uint64_t timestamp)
{
	(void)buf;
	(void)timestamp;
}

void *udebug_entry_append(struct udebug_buf *buf, const void *data, uint32_t len)
{
	(void)buf;
	(void)data;
	(void)len;
	return NULL;
}

int udebug_entry_printf(struct udebug_buf *buf, const char *fmt, ...)
{
	(void)buf;
	(void)fmt;
	return 0;
}

int udebug_entry_vprintf(struct udebug_buf *buf, const char *fmt, va_list ap)
{
	(void)buf;
	(void)fmt;
	(void)ap;
	return 0;
}

uint16_t udebug_entry_trim(struct udebug_buf *buf, uint16_t len)
{
	(void)buf;
	return len;
}

void udebug_entry_set_length(struct udebug_buf *buf, uint16_t len)
{
	(void)buf;
	(void)len;
}

void udebug_entry_add(struct udebug_buf *buf)
{
	(void)buf;
}

/* ---- local buffer ---- */

int udebug_buf_init(struct udebug_buf *buf, size_t entries, size_t size)
{
	(void)entries;
	(void)size;

	if (!buf)
		return -1;

	memset(buf, 0, sizeof(*buf));
	return 0;
}

int udebug_buf_add(struct udebug *ctx, struct udebug_buf *buf,
		   const struct udebug_buf_meta *meta)
{
	(void)ctx;
	(void)buf;
	(void)meta;
	return 0;
}

uint64_t udebug_buf_flags(struct udebug_buf *buf)
{
	(void)buf;
	return 0;
}

void udebug_buf_free(struct udebug_buf *buf)
{
	(void)buf;
}

/* ---- remote buffer ---- */

struct udebug_remote_buf *
udebug_remote_buf_get(struct udebug *ctx, uint32_t id)
{
	(void)ctx;
	(void)id;
	return NULL;
}

int udebug_remote_buf_map(struct udebug *ctx, struct udebug_remote_buf *rb, uint32_t id)
{
	(void)ctx;
	(void)rb;
	(void)id;
	return 0;
}

void udebug_remote_buf_unmap(struct udebug *ctx, struct udebug_remote_buf *rb)
{
	(void)ctx;
	(void)rb;
}

int udebug_remote_buf_set_poll(struct udebug *ctx,
			      struct udebug_remote_buf *rb, bool val)
{
	(void)ctx;
	(void)rb;
	(void)val;
	return 0;
}

void udebug_remote_buf_set_flags(struct udebug_remote_buf *rb,
				 uint64_t mask, uint64_t set)
{
	(void)rb;
	(void)mask;
	(void)set;
}

struct udebug_snapshot *
udebug_remote_buf_snapshot(struct udebug_remote_buf *rb)
{
	(void)rb;
	return NULL;
}

bool udebug_snapshot_get_entry(struct udebug_snapshot *s,
			       struct udebug_iter *it,
			       unsigned int entry)
{
	(void)s;
	(void)it;
	(void)entry;
	return false;
}

void udebug_remote_buf_set_start_time(struct udebug_remote_buf *rb, uint64_t ts)
{
	(void)rb;
	(void)ts;
}

void udebug_remote_buf_set_start_offset(struct udebug_remote_buf *rb, uint32_t idx)
{
	(void)rb;
	(void)idx;
}

/* ---- iterator ---- */

void udebug_iter_start(struct udebug_iter *it,
		       struct udebug_snapshot **s, size_t n)
{
	(void)it;
	(void)s;
	(void)n;
}

bool udebug_iter_next(struct udebug_iter *it)
{
	(void)it;
	return false;
}

/* ---- context / connection ---- */

void udebug_init(struct udebug *ctx)
{
	if (!ctx)
		return;

	memset(ctx, 0, sizeof(*ctx));
	ctx->fd.fd = -1;
}

int udebug_connect(struct udebug *ctx, const char *path)
{
	(void)ctx;
	(void)path;
	return -1;
}

void udebug_auto_connect(struct udebug *ctx, const char *path)
{
	(void)ctx;
	(void)path;
}

void udebug_add_uloop(struct udebug *ctx)
{
	(void)ctx;
}

void udebug_poll(struct udebug *ctx)
{
	(void)ctx;
}

void udebug_free(struct udebug *ctx)
{
	(void)ctx;
}

/* ---- helpers ---- */

int udebug_id_cmp(const void *k1, const void *k2, void *ptr)
{
	(void)ptr;

	if (k1 == k2)
		return 0;
	return (k1 < k2) ? -1 : 1;
}