/*
 * Copyright (c) 2021 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"

#ifdef HAVE_SELINUX_RUNTIME

# include <assert.h>
# include <errno.h>
# include <stdlib.h>
# include <string.h>
# include <sys/stat.h>
# include <unistd.h>
# include <selinux/selinux.h>
# include <selinux/label.h>

# include "xmalloc.h"

# define TEST_SECONTEXT
# include "secontext.h"

ATTRIBUTE_FORMAT((printf, 2, 0)) ATTRIBUTE_MALLOC
static char *
secontext_format(char *context, const char *fmt)
{
	int saved_errno = errno;
	char *res = context ? xasprintf(fmt, context) : xstrdup("");
	free(context);
	errno = saved_errno;
	return res;
}

# define FORMAT_SPACE_BEFORE(string)	secontext_format(string, " [%s]")
# define FORMAT_SPACE_AFTER(string)	secontext_format(string, "[%s] ")

static char *
strip_trailing_newlines(char *context)
{
	/*
	 * On the CI at least, the context may have a trailing \n,
	 * let's remove it just in case.
	 */
	size_t len = strlen(context);
	for (; len > 0; --len) {
		if (context[len - 1] != '\n')
			break;
	}
	context[len] = '\0';
	return context;
}

char *
get_secontext_field(const char *full_context, enum secontext_field field)
{
	int saved_errno = errno;

	if (!full_context)
		return NULL;

	char *saveptr = NULL;
	const char *token;
	unsigned int i;

	char *ctx_copy = xstrdup(full_context);
	char *context = NULL;
	for (token = strtok_r(ctx_copy, ":", &saveptr), i = 0;
	     token; token = strtok_r(NULL, ":", &saveptr), i++) {
		if (i == field) {
			context = xstrdup(token);
			break;
		}
	}
	if (!context)
		context = xstrdup(full_context);
	free(ctx_copy);

	errno = saved_errno;
	return context;
}

static char *
raw_expected_secontext_full_file(const char *filename)
{
	int saved_errno = errno;
	char *secontext;

	static struct selabel_handle *hdl;
	if (!hdl) {
		hdl = selabel_open(SELABEL_CTX_FILE, NULL, 0);
		if (!hdl)
			perror_msg_and_skip("selabel_open");
	}

	char *resolved = realpath(filename, NULL);
	if (!resolved)
		perror_msg_and_fail("realpath: %s", filename);

	struct stat statbuf;
	if (stat(resolved, &statbuf) < 0)
		perror_msg_and_fail("stat: %s", resolved);

	if (selabel_lookup(hdl, &secontext, resolved, statbuf.st_mode) < 0)
		perror_msg_and_skip("selabel_lookup: %s", resolved);
	free(resolved);

	char *full_secontext = xstrdup(secontext);
	freecon(secontext);
	errno = saved_errno;
	return full_secontext;
}

static char *
raw_expected_secontext_short_file(const char *filename)
{
	int saved_errno = errno;

	char *ctx = raw_expected_secontext_full_file(filename);
	char *type = get_secontext_field(ctx, SECONTEXT_TYPE);
	free(ctx);

	errno = saved_errno;
	return type;
}

static char *
raw_secontext_full_file(const char *filename)
{
	int saved_errno = errno;
	char *full_secontext = NULL;
	char *secontext;

	if (getfilecon(filename, &secontext) >= 0) {
		full_secontext = strip_trailing_newlines(xstrdup(secontext));
		freecon(secontext);
	}
	errno = saved_errno;
	return full_secontext;
}

char *
get_secontext_field_file(const char *file, enum secontext_field field)
{
	char *ctx = raw_secontext_full_file(file);
	char *type =  get_secontext_field(ctx, field);
	free(ctx);

	return type;
}

static char *
raw_secontext_short_file(const char *filename)
{
	return get_secontext_field_file(filename, SECONTEXT_TYPE);
}

static char *
raw_secontext_full_pid(pid_t pid)
{
	int saved_errno = errno;
	char *full_secontext = NULL;
	char *secontext;

	if (getpidcon(pid, &secontext) == 0) {
		full_secontext = strip_trailing_newlines(xstrdup(secontext));
		freecon(secontext);
	}
	errno = saved_errno;
	return full_secontext;
}

static char *
raw_secontext_short_pid(pid_t pid)
{
	int saved_errno = errno;

	char *ctx = raw_secontext_full_pid(pid);
	char *type = get_secontext_field(ctx, SECONTEXT_TYPE);
	free(ctx);

	errno = saved_errno;
	return type;
}

char *
secontext_full_file(const char *filename, bool mismatch)
{
	int saved_errno = errno;
	char *context = raw_secontext_full_file(filename);
	if (context && mismatch) {
		char *expected = raw_expected_secontext_full_file(filename);
		if (expected && strcmp(context, expected)) {
			char *context_mismatch =
				xasprintf("%s!!%s", context, expected);
			free(context);
			context = context_mismatch;
		}
		free(expected);
	}
	errno = saved_errno;
	return FORMAT_SPACE_BEFORE(context);
}

char *
secontext_full_pid(pid_t pid)
{
	return FORMAT_SPACE_AFTER(raw_secontext_full_pid(pid));
}

char *
secontext_short_file(const char *filename, bool mismatch)
{
	int saved_errno = errno;
	char *context = raw_secontext_short_file(filename);
	if (context && mismatch) {
		char *expected = raw_expected_secontext_short_file(filename);
		if (expected && strcmp(context, expected)) {
			char *context_mismatch =
				xasprintf("%s!!%s", context, expected);
			free(context);
			context = context_mismatch;
		}
		free(expected);
	}
	errno = saved_errno;
	return FORMAT_SPACE_BEFORE(context);
}

char *
secontext_short_pid(pid_t pid)
{
	return FORMAT_SPACE_AFTER(raw_secontext_short_pid(pid));
}

void reset_secontext_file(const char *file)
{
	char *proper_ctx = raw_expected_secontext_full_file(file);
	(void) setfilecon(file, proper_ctx);
	free(proper_ctx);
}

void
update_secontext_field(const char *file, enum secontext_field field,
		       const char *newvalue)
{
	int saved_errno = errno;
	assert(field >= SECONTEXT_USER && field <= SECONTEXT_TYPE);

	char *ctx = raw_secontext_full_file(file);
	if (ctx == NULL)
		return;

	char *saveptr = NULL;
	char *token;
	int nfields;
	char *split[4];

	for (token = strtok_r(ctx, ":", &saveptr), nfields = 0;
	     token; token = strtok_r(NULL, ":", &saveptr), nfields++) {
		assert(nfields < 4);
		split[nfields] = token;
	}
	assert(nfields == 4);

	split[field] = (char *)newvalue;

	char *newcontext = xasprintf("%s:%s:%s:%s", split[0], split[1],
				     split[2], split[3]);

	(void) setfilecon(file, newcontext);

	free(newcontext);
	free(ctx);
	errno = saved_errno;
}

#endif /* HAVE_SELINUX_RUNTIME */
