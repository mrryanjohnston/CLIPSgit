   /*******************************************************/
   /*      "C" Language Integrated Production System      */
   /*                                                     */
   /*            CLIPS Version 6.40  07/30/16             */
   /*                                                     */
   /*                USER FUNCTIONS MODULE                */
   /*******************************************************/

/*************************************************************/
/* Purpose: CLIPSgit. The git-* functions give CLIPS        */
/*          programs access to the libgit2 library.          */
/*                                                           */
/* Principal Programmer(s):                                  */
/*      Gary D. Riley                                        */
/*                                                           */
/* Contributing Programmer(s):                               */
/*      Ryan Johnston                                        */
/*                                                           */
/*************************************************************/

/***************************************************************************/
/*                                                                         */
/* Permission is hereby granted, free of charge, to any person obtaining   */
/* a copy of this software and associated documentation files (the         */
/* "Software"), to deal in the Software without restriction, including     */
/* without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, and/or sell copies of the Software, and to permit persons   */
/* to whom the Software is furnished to do so.                             */
/*                                                                         */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS */
/* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF              */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT   */
/* OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY  */
/* CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES */
/* WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN   */
/* ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF */
/* OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.          */
/*                                                                         */
/***************************************************************************/

#include "clips.h"
#include <git2.h>
#include <git2/sys/credential.h>
#include <git2/sys/errors.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void UserFunctions(Environment *);

/***************************************************************************/
/*                                                                         */
/* The libgit2 version.                                                    */
/*                                                                         */
/* libgit2 1.8 is the oldest release that this file supports. The 1.9      */
/* series has the same C API. Version 1.9 added some feature flags. This   */
/* file uses those flags only if the headers define them. The version in   */
/* the headers controls that. The function git-built-version reports that  */
/* version to the program.                                                 */
/*                                                                         */
/***************************************************************************/

#define CLIPSGIT_LIBGIT2_VERSION_NUM \
	(LIBGIT2_VER_MAJOR * 10000 + LIBGIT2_VER_MINOR * 100 + LIBGIT2_VER_REVISION)

#if CLIPSGIT_LIBGIT2_VERSION_NUM < 10800
#error "this libgit2 is too old for CLIPSgit: libgit2 1.8 or later is needed. Build the vendored library with 'make', or point at a newer installation."
#endif

#define CLIPSGIT_HAVE_1_9 (CLIPSGIT_LIBGIT2_VERSION_NUM >= 10900)

static void GitFail(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  const char *msg)
{
	WriteString(theEnv, STDERR, fname);
	WriteString(theEnv, STDERR, ": ");
	WriteString(theEnv, STDERR, msg);
	WriteString(theEnv, STDERR, "\n");
	returnValue->lexemeValue = FalseSymbol(theEnv);
}

struct GitEnumEntry
{
	const char *name;
	long long value;
};

static const char *GitEnumName(const struct GitEnumEntry *, long long);
static const struct GitEnumEntry GitErrorCodeNames[];
static void GitCredentialReset(void);

static void GitFailError(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  int code)
{
	const git_error *err = git_error_last();

	WriteString(theEnv, STDERR, fname);
	WriteString(theEnv, STDERR, ": ");

	if (err == NULL || err->message == NULL || err->message[0] == '\0')
	{
		WriteString(theEnv, STDERR, "the call failed and libgit2 reports no message");
	}
	else
	{
		WriteString(theEnv, STDERR, err->message);
	}

	if (code != 0)
	{
		WriteString(theEnv, STDERR, " (");
		WriteString(theEnv, STDERR, GitEnumName(GitErrorCodeNames, code));
		WriteString(theEnv, STDERR, ")");
	}

	WriteString(theEnv, STDERR, "\n");
	returnValue->lexemeValue = FalseSymbol(theEnv);
}

enum GitKind
{
	GIT_K_REPOSITORY,
	GIT_K_COMMIT,
	GIT_K_TREE,
	GIT_K_BLOB,
	GIT_K_TAG,
	GIT_K_TREE_ENTRY,
	GIT_K_TREEBUILDER,
	GIT_K_REFERENCE,
	GIT_K_INDEX,
	GIT_K_CONFIG,
	GIT_K_REMOTE,
	GIT_K_SIGNATURE,
	GIT_K_REVWALK,
	GIT_K_DIFF,
	GIT_K_PATCH,
	GIT_K_STATUS_LIST,
	GIT_K_ANNOTATED_COMMIT,
	GIT_K_BLAME,
	GIT_K_WORKTREE,
	GIT_K_SUBMODULE,
	GIT_K_ODB,
	GIT_K_REFSPEC,
	GIT_K_COUNT
};

#define GIT_KM(kind) (1u << (kind))

#define GIT_KM_OBJECT (GIT_KM(GIT_K_COMMIT) | GIT_KM(GIT_K_TREE) | GIT_KM(GIT_K_BLOB) | GIT_KM(GIT_K_TAG))

#define GIT_KM_TREEISH (GIT_KM(GIT_K_COMMIT) | GIT_KM(GIT_K_TREE) | GIT_KM(GIT_K_TAG))

static const char *GitKindNames[GIT_K_COUNT] = {
	"repository", "commit", "tree", "blob", "tag", "tree entry",
	"treebuilder", "reference", "index", "config", "remote", "signature",
	"revwalk", "diff", "patch", "status list", "annotated commit", "blame",
	"worktree", "submodule", "odb", "refspec"
};

struct GitHandle
{
	void *ptr;
	enum GitKind kind;
	git_repository *repo;   /* the repository of the handle, or NULL */
	void *parent;           /* the handle that owns it, or NULL */
	unsigned int refs;      /* the number of frees that the caller must still make */
	struct GitHandle *next;
};

#define GIT_HANDLE_BUCKETS 256

static struct GitHandle *GitHandles[GIT_HANDLE_BUCKETS];
static size_t GitHandleCount = 0;
static Environment *GitEnv = NULL;

static unsigned int GitHandleBucket(
  const void *ptr)
{
	size_t v = (size_t) ptr;
	return (unsigned int) ((v >> 4) ^ (v >> 12) ^ (v >> 20)) % GIT_HANDLE_BUCKETS;
}

static struct GitHandle *GitHandleFind(
  const void *ptr)
{
	struct GitHandle *h;

	for (h = GitHandles[GitHandleBucket(ptr)]; h != NULL; h = h->next)
	{
		if (h->ptr == ptr)
		{
			return h;
		}
	}

	return NULL;
}

/* Records a pointer. If the pointer is already in the table, adds one more
   free to its count. A borrowed pointer is recorded one time and is not
   counted. */
static struct GitHandle *GitHandleRegister(
  void *ptr,
  enum GitKind kind,
  git_repository *repo,
  void *parent)
{
	struct GitHandle *h = GitHandleFind(ptr);
	unsigned int bucket;

	if (h != NULL)
	{
		if (h->parent == NULL && parent == NULL)
		{
			h->refs++;
		}
		return h;
	}

	h = (struct GitHandle *) calloc(1, sizeof *h);
	if (h == NULL)
	{
		return NULL;
	}

	h->ptr = ptr;
	h->kind = kind;
	h->repo = repo;
	h->parent = parent;
	h->refs = (parent == NULL) ? 1 : 0;

	bucket = GitHandleBucket(ptr);
	h->next = GitHandles[bucket];
	GitHandles[bucket] = h;
	GitHandleCount++;

	return h;
}

static void GitHandleUnlink(
  struct GitHandle *dead)
{
	struct GitHandle **link = &GitHandles[GitHandleBucket(dead->ptr)];

	while (*link != NULL)
	{
		if (*link == dead)
		{
			*link = dead->next;
			if (GitEnv != NULL)
			{
				CreateCExternalAddress(GitEnv, dead->ptr)->contents = NULL;
			}
			free(dead);
			GitHandleCount--;
			return;
		}
		link = &(*link)->next;
	}
}

/* Unlinks all the handles that are borrowed from a pointer, before that
   pointer is released. */
static void GitHandleForgetBorrowed(
  void *parent)
{
	unsigned int b;
	struct GitHandle *h, *next;

	for (b = 0; b < GIT_HANDLE_BUCKETS; b++)
	{
		for (h = GitHandles[b]; h != NULL; h = next)
		{
			next = h->next;
			if (h->parent == parent)
			{
				GitHandleForgetBorrowed(h->ptr);
				GitHandleUnlink(h);
			}
		}
	}
}

static void GitFreeByKind(
  enum GitKind kind,
  void *ptr)
{
	switch (kind)
	{
		case GIT_K_REPOSITORY:       git_repository_free((git_repository *) ptr); break;
		case GIT_K_COMMIT:
		case GIT_K_TREE:
		case GIT_K_BLOB:
		case GIT_K_TAG:              git_object_free((git_object *) ptr); break;
		case GIT_K_TREE_ENTRY:       git_tree_entry_free((git_tree_entry *) ptr); break;
		case GIT_K_TREEBUILDER:      git_treebuilder_free((git_treebuilder *) ptr); break;
		case GIT_K_REFERENCE:        git_reference_free((git_reference *) ptr); break;
		case GIT_K_INDEX:            git_index_free((git_index *) ptr); break;
		case GIT_K_CONFIG:           git_config_free((git_config *) ptr); break;
		case GIT_K_REMOTE:           git_remote_free((git_remote *) ptr); break;
		case GIT_K_SIGNATURE:        git_signature_free((git_signature *) ptr); break;
		case GIT_K_REVWALK:          git_revwalk_free((git_revwalk *) ptr); break;
		case GIT_K_DIFF:             git_diff_free((git_diff *) ptr); break;
		case GIT_K_PATCH:            git_patch_free((git_patch *) ptr); break;
		case GIT_K_STATUS_LIST:      git_status_list_free((git_status_list *) ptr); break;
		case GIT_K_ANNOTATED_COMMIT: git_annotated_commit_free((git_annotated_commit *) ptr); break;
		case GIT_K_BLAME:            git_blame_free((git_blame *) ptr); break;
		case GIT_K_WORKTREE:         git_worktree_free((git_worktree *) ptr); break;
		case GIT_K_SUBMODULE:        git_submodule_free((git_submodule *) ptr); break;
		case GIT_K_ODB:              git_odb_free((git_odb *) ptr); break;
		case GIT_K_REFSPEC:          git_refspec_free((git_refspec *) ptr); break;
		case GIT_K_COUNT:            break;
	}
}

/* One free of an owned handle. The last free removes the pointer from the
   table, together with all the handles that are borrowed from it. */
static void GitHandleRelease(
  struct GitHandle *h)
{
	if (h->refs > 1)
	{
		h->refs--;
		GitFreeByKind(h->kind, h->ptr);
		return;
	}

	GitHandleForgetBorrowed(h->ptr);
	GitFreeByKind(h->kind, h->ptr);
	GitHandleUnlink(h);
}

/* Makes every free that the caller must still make on a handle. */
static void GitHandleReleaseAll(
  struct GitHandle *h)
{
	while (h->refs > 1)
	{
		h->refs--;
		GitFreeByKind(h->kind, h->ptr);
	}
	GitHandleRelease(h);
}

/* Releases all the handles that came from a repository, and then the
   repository. */
static void GitRepositoryRelease(
  struct GitHandle *repoHandle)
{
	git_repository *repo = (git_repository *) repoHandle->ptr;
	unsigned int b;
	struct GitHandle *h;
	bool again = true;

	/* The release of one handle can unlink other handles (the handles
	   that are borrowed from it). So each pass starts its bucket again
	   after a release. */
	while (again)
	{
		again = false;
		for (b = 0; b < GIT_HANDLE_BUCKETS && !again; b++)
		{
			for (h = GitHandles[b]; h != NULL; h = h->next)
			{
				if (h->repo == repo && h != repoHandle && h->parent == NULL)
				{
					GitHandleReleaseAll(h);
					again = true;
					break;
				}
			}
		}
	}

	GitHandleReleaseAll(repoHandle);
}

/* Releases every handle in the table, for git-libgit2-shutdown. */
static void GitHandlesReleaseEverything(void)
{
	unsigned int b;
	struct GitHandle *h;
	bool again = true;

	/* First the handles that are not repositories, then the
	   repositories. */
	while (again)
	{
		again = false;
		for (b = 0; b < GIT_HANDLE_BUCKETS && !again; b++)
		{
			for (h = GitHandles[b]; h != NULL; h = h->next)
			{
				if (h->kind != GIT_K_REPOSITORY && h->parent == NULL)
				{
					GitHandleReleaseAll(h);
					again = true;
					break;
				}
			}
		}
	}

	again = true;
	while (again)
	{
		again = false;
		for (b = 0; b < GIT_HANDLE_BUCKETS && !again; b++)
		{
			for (h = GitHandles[b]; h != NULL; h = h->next)
			{
				if (h->parent == NULL)
				{
					GitRepositoryRelease(h);
					again = true;
					break;
				}
			}
		}
	}

	/* The handles that are borrowed from a pointer that was not
	   recorded. */
	for (b = 0; b < GIT_HANDLE_BUCKETS; b++)
	{
		while (GitHandles[b] != NULL)
		{
			GitHandleUnlink(GitHandles[b]);
		}
	}
}

/***************************************************************************/
/*                                                                         */
/* Arguments.                                                              */
/*                                                                         */
/* CLIPS checks literal arguments against the restriction string that      */
/* AddUDF receives. CLIPS does not check a value that comes to a wrapper   */
/* through a variable. So this file checks each argument again. The        */
/* helpers return NULL or false after they write the message and set the   */
/* return value. Then the guard in a wrapper is one line for each          */
/* argument.                                                               */
/*                                                                         */
/***************************************************************************/

/* A handle of one of the kinds in the mask. The helper also returns the
   UDFValue, so a wrapper that frees a handle can make the value empty in
   place. */
static void *GitHandleArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  unsigned int kinds,
  const char *what,
  UDFValue *theArg)
{
	struct GitHandle *h;
	char msg[192];

	if (!UDFNextArgument(context, EXTERNAL_ADDRESS_BIT, theArg) ||
	    theArg->header->type != EXTERNAL_ADDRESS_TYPE)
	{
		snprintf(msg, sizeof msg, "expected a %s handle", what);
		GitFail(theEnv, returnValue, fname, msg);
		return NULL;
	}

	if (theArg->externalAddressValue->contents == NULL)
	{
		snprintf(msg, sizeof msg, "the %s handle has been freed", what);
		GitFail(theEnv, returnValue, fname, msg);
		return NULL;
	}

	h = GitHandleFind(theArg->externalAddressValue->contents);
	if (h == NULL)
	{
		snprintf(msg, sizeof msg,
		         "the %s handle is not one this library handed out, or has been freed", what);
		GitFail(theEnv, returnValue, fname, msg);
		return NULL;
	}

	if ((kinds & GIT_KM(h->kind)) == 0)
	{
		snprintf(msg, sizeof msg, "expected a %s handle, got a %s", what, GitKindNames[h->kind]);
		GitFail(theEnv, returnValue, fname, msg);
		return NULL;
	}

	return h->ptr;
}

/* The same, but the symbol nil is also accepted. For nil, the helper
   returns NULL and writes no message. */
static bool GitHandleOrNilArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  unsigned int kinds,
  const char *what,
  void **out)
{
	UDFValue theArg;
	struct GitHandle *h;
	char msg[192];

	*out = NULL;

	if (!UDFNextArgument(context, EXTERNAL_ADDRESS_BIT | SYMBOL_BIT, &theArg))
	{
		snprintf(msg, sizeof msg, "expected a %s handle or nil", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type == SYMBOL_TYPE)
	{
		if (strcmp(theArg.lexemeValue->contents, "nil") == 0)
		{
			return true;
		}
		snprintf(msg, sizeof msg, "expected a %s handle or nil", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.externalAddressValue->contents == NULL)
	{
		snprintf(msg, sizeof msg, "the %s handle has been freed", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	h = GitHandleFind(theArg.externalAddressValue->contents);
	if (h == NULL)
	{
		snprintf(msg, sizeof msg,
		         "the %s handle is not one this library handed out, or has been freed", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if ((kinds & GIT_KM(h->kind)) == 0)
	{
		snprintf(msg, sizeof msg, "expected a %s handle, got a %s", what, GitKindNames[h->kind]);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = h->ptr;
	return true;
}

#define GitRepoArgument(env, ctx, ret, fname, arg) \
	((git_repository *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_REPOSITORY), "repository", arg))
#define GitCommitArgument(env, ctx, ret, fname, arg) \
	((git_commit *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_COMMIT), "commit", arg))
#define GitTreeArgument(env, ctx, ret, fname, arg) \
	((git_tree *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_TREE), "tree", arg))
#define GitBlobArgument(env, ctx, ret, fname, arg) \
	((git_blob *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_BLOB), "blob", arg))
#define GitTagArgument(env, ctx, ret, fname, arg) \
	((git_tag *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_TAG), "tag", arg))
#define GitObjectArgument(env, ctx, ret, fname, arg) \
	((git_object *) GitHandleArgument(env, ctx, ret, fname, GIT_KM_OBJECT, "object", arg))
#define GitTreeishArgument(env, ctx, ret, fname, arg) \
	((git_object *) GitHandleArgument(env, ctx, ret, fname, GIT_KM_TREEISH, "commit, tree or tag", arg))
#define GitTreeEntryArgument(env, ctx, ret, fname, arg) \
	((git_tree_entry *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_TREE_ENTRY), "tree entry", arg))
#define GitTreebuilderArgument(env, ctx, ret, fname, arg) \
	((git_treebuilder *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_TREEBUILDER), "treebuilder", arg))
#define GitReferenceArgument(env, ctx, ret, fname, arg) \
	((git_reference *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_REFERENCE), "reference", arg))
#define GitIndexArgument(env, ctx, ret, fname, arg) \
	((git_index *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_INDEX), "index", arg))
#define GitConfigArgument(env, ctx, ret, fname, arg) \
	((git_config *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_CONFIG), "config", arg))
#define GitRemoteArgument(env, ctx, ret, fname, arg) \
	((git_remote *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_REMOTE), "remote", arg))
#define GitSignatureArgument(env, ctx, ret, fname, arg) \
	((git_signature *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_SIGNATURE), "signature", arg))
#define GitRevwalkArgument(env, ctx, ret, fname, arg) \
	((git_revwalk *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_REVWALK), "revwalk", arg))
#define GitDiffArgument(env, ctx, ret, fname, arg) \
	((git_diff *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_DIFF), "diff", arg))
#define GitPatchArgument(env, ctx, ret, fname, arg) \
	((git_patch *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_PATCH), "patch", arg))
#define GitStatusListArgument(env, ctx, ret, fname, arg) \
	((git_status_list *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_STATUS_LIST), "status list", arg))
#define GitAnnotatedCommitArgument(env, ctx, ret, fname, arg) \
	((git_annotated_commit *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_ANNOTATED_COMMIT), "annotated commit", arg))
#define GitBlameArgument(env, ctx, ret, fname, arg) \
	((git_blame *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_BLAME), "blame", arg))
#define GitWorktreeArgument(env, ctx, ret, fname, arg) \
	((git_worktree *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_WORKTREE), "worktree", arg))
#define GitSubmoduleArgument(env, ctx, ret, fname, arg) \
	((git_submodule *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_SUBMODULE), "submodule", arg))
#define GitOdbArgument(env, ctx, ret, fname, arg) \
	((git_odb *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_ODB), "odb", arg))
#define GitRefspecArgument(env, ctx, ret, fname, arg) \
	((git_refspec *) GitHandleArgument(env, ctx, ret, fname, GIT_KM(GIT_K_REFSPEC), "refspec", arg))

/* A symbol or a string. A CLIPS program writes the symbol nil for a value
   that is not present. So this helper accepts nil where libgit2 accepts a
   NULL pointer. For nil, it returns NULL and writes no message. */
static bool GitTextArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  bool nilIsNull,
  const char **out)
{
	UDFValue theArg;
	char msg[128];

	if (!UDFNextArgument(context, LEXEME_BITS, &theArg) ||
	    (theArg.header->type != SYMBOL_TYPE && theArg.header->type != STRING_TYPE))
	{
		snprintf(msg, sizeof msg, "expected a symbol or a string for %s", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (nilIsNull &&
	    theArg.header->type == SYMBOL_TYPE &&
	    strcmp(theArg.lexemeValue->contents, "nil") == 0)
	{
		*out = NULL;
		return true;
	}

	*out = theArg.lexemeValue->contents;
	return true;
}

/* An optional text argument. If the argument is not present or is nil, the
   result is NULL. */
static bool GitOptionalTextArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const char **out)
{
	*out = NULL;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}
	return GitTextArgument(theEnv, context, returnValue, fname, what, true, out);
}

static bool GitIntegerArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  long long *out)
{
	UDFValue theArg;
	char msg[128];

	if (!UDFNextArgument(context, INTEGER_BIT, &theArg) ||
	    theArg.header->type != INTEGER_TYPE)
	{
		snprintf(msg, sizeof msg, "expected an integer for %s", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = theArg.integerValue->contents;
	return true;
}

/* An integer that is not negative, for the counts and positions that
   libgit2 receives as size_t. */
static bool GitSizeArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  size_t *out)
{
	long long value;
	char msg[128];

	if (!GitIntegerArgument(theEnv, context, returnValue, fname, what, &value))
	{
		return false;
	}

	if (value < 0)
	{
		snprintf(msg, sizeof msg, "%s cannot be negative", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = (size_t) value;
	return true;
}

static bool GitBooleanArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  bool *out)
{
	UDFValue theArg;
	char msg[128];

	if (!UDFNextArgument(context, LEXEME_BITS, &theArg) ||
	    theArg.header->type != SYMBOL_TYPE ||
	    (theArg.lexemeValue != TrueSymbol(theEnv) &&
	     theArg.lexemeValue != FalseSymbol(theEnv)))
	{
		snprintf(msg, sizeof msg, "expected TRUE or FALSE for %s", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = (theArg.lexemeValue == TrueSymbol(theEnv));
	return true;
}

/* An optional boolean. If the argument is not present, the result is false. */
static bool GitOptionalBooleanArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  bool *out)
{
	*out = false;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}
	return GitBooleanArgument(theEnv, context, returnValue, fname, what, out);
}

static bool GitMultifieldArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  UDFValue *theArg)
{
	char msg[128];

	if (!UDFNextArgument(context, MULTIFIELD_BIT, theArg) ||
	    theArg->header->type != MULTIFIELD_TYPE)
	{
		snprintf(msg, sizeof msg, "expected a multifield for %s", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	return true;
}

/* An object id in hexadecimal: 40 characters for SHA-1. The helper accepts
   symbols and strings. An id that comes from this library is a string. An
   id that a program contains is usually a symbol. */
static bool GitOidArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  git_oid *out)
{
	const char *text;
	char msg[192];

	if (!GitTextArgument(theEnv, context, returnValue, fname, what, false, &text))
	{
		return false;
	}

	if (git_oid_fromstr(out, text) != 0)
	{
		snprintf(msg, sizeof msg, "'%s' is not an object id: expected %d hex characters",
		         text, GIT_OID_SHA1_HEXSIZE);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	return true;
}

/* An object id, or a prefix of one that has only one match. The prefix must
   have four characters or more. */
static bool GitOidPrefixArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  git_oid *out,
  size_t *length)
{
	const char *text;
	char msg[192];

	if (!GitTextArgument(theEnv, context, returnValue, fname, what, false, &text))
	{
		return false;
	}

	*length = strlen(text);
	if (*length < GIT_OID_MINPREFIXLEN || *length > GIT_OID_SHA1_HEXSIZE ||
	    git_oid_fromstrn(out, text, *length) != 0)
	{
		snprintf(msg, sizeof msg,
		         "'%s' is not an object id prefix: expected %d to %d hex characters",
		         text, GIT_OID_MINPREFIXLEN, GIT_OID_SHA1_HEXSIZE);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	return true;
}

/* A multifield of symbols and strings, as a git_strarray. The strings
   belong to CLIPS and remain for the duration of the call. The helper
   allocates only the array, and GitStrarrayDispose releases it. An empty
   multifield gives an empty array. */
static bool GitStrarrayArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  git_strarray *out)
{
	UDFValue theArg;
	size_t i;
	char msg[160];

	out->strings = NULL;
	out->count = 0;

	if (!GitMultifieldArgument(theEnv, context, returnValue, fname, what, &theArg))
	{
		return false;
	}

	if (theArg.multifieldValue->length == 0)
	{
		return true;
	}

	out->strings = (char **) calloc(theArg.multifieldValue->length, sizeof(char *));
	if (out->strings == NULL)
	{
		GitFail(theEnv, returnValue, fname, "out of memory");
		return false;
	}

	for (i = 0; i < theArg.multifieldValue->length; i++)
	{
		CLIPSValue *v = &theArg.multifieldValue->contents[i];

		if (v->header->type != SYMBOL_TYPE && v->header->type != STRING_TYPE)
		{
			snprintf(msg, sizeof msg, "every element of %s must be a symbol or a string", what);
			GitFail(theEnv, returnValue, fname, msg);
			free(out->strings);
			out->strings = NULL;
			return false;
		}
		out->strings[i] = (char *) v->lexemeValue->contents;
	}

	out->count = theArg.multifieldValue->length;
	return true;
}

/* An optional strarray. If the argument is not present, the array is empty. */
static bool GitOptionalStrarrayArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  git_strarray *out)
{
	out->strings = NULL;
	out->count = 0;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}
	return GitStrarrayArgument(theEnv, context, returnValue, fname, what, out);
}

static void GitStrarrayDispose(
  git_strarray *array)
{
	free(array->strings);
	array->strings = NULL;
	array->count = 0;
}

/* A multifield of handles that all match one kind mask, for example the
   parents of a commit or the heads of a merge. The helper allocates the
   array, and the caller frees it. */
static bool GitHandlesArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  unsigned int kinds,
  const char *what,
  void ***out,
  size_t *count)
{
	UDFValue theArg;
	size_t i;
	char msg[192];

	*out = NULL;
	*count = 0;

	if (!GitMultifieldArgument(theEnv, context, returnValue, fname, what, &theArg))
	{
		return false;
	}

	if (theArg.multifieldValue->length == 0)
	{
		return true;
	}

	*out = (void **) calloc(theArg.multifieldValue->length, sizeof(void *));
	if (*out == NULL)
	{
		GitFail(theEnv, returnValue, fname, "out of memory");
		return false;
	}

	for (i = 0; i < theArg.multifieldValue->length; i++)
	{
		CLIPSValue *v = &theArg.multifieldValue->contents[i];
		struct GitHandle *h;

		if (v->header->type != EXTERNAL_ADDRESS_TYPE || v->externalAddressValue->contents == NULL)
		{
			snprintf(msg, sizeof msg, "every element of %s must be a live handle", what);
			GitFail(theEnv, returnValue, fname, msg);
			free(*out);
			*out = NULL;
			return false;
		}

		h = GitHandleFind(v->externalAddressValue->contents);
		if (h == NULL || (kinds & GIT_KM(h->kind)) == 0)
		{
			snprintf(msg, sizeof msg, "element %d of %s is not a handle of the right kind",
			         (int) i + 1, what);
			GitFail(theEnv, returnValue, fname, msg);
			free(*out);
			*out = NULL;
			return false;
		}

		(*out)[i] = h->ptr;
	}

	*count = theArg.multifieldValue->length;
	return true;
}

/***************************************************************************/
/*                                                                         */
/* Enumerations.                                                           */
/*                                                                         */
/* Each enumeration value that libgit2 returns goes into CLIPS as the      */
/* symbol with the C name of the value. Each enumeration value that        */
/* libgit2 receives is accepted as that symbol or as the integer of the    */
/* value. The tables are the complete map. A name that is not in a table   */
/* is a name that this libgit2 does not have.                              */
/*                                                                         */
/***************************************************************************/

static const struct GitEnumEntry GitErrorCodeNames[] = {
	{ "GIT_OK",              GIT_OK },
	{ "GIT_ERROR",           GIT_ERROR },
	{ "GIT_ENOTFOUND",       GIT_ENOTFOUND },
	{ "GIT_EEXISTS",         GIT_EEXISTS },
	{ "GIT_EAMBIGUOUS",      GIT_EAMBIGUOUS },
	{ "GIT_EBUFS",           GIT_EBUFS },
	{ "GIT_EUSER",           GIT_EUSER },
	{ "GIT_EBAREREPO",       GIT_EBAREREPO },
	{ "GIT_EUNBORNBRANCH",   GIT_EUNBORNBRANCH },
	{ "GIT_EUNMERGED",       GIT_EUNMERGED },
	{ "GIT_ENONFASTFORWARD", GIT_ENONFASTFORWARD },
	{ "GIT_EINVALIDSPEC",    GIT_EINVALIDSPEC },
	{ "GIT_ECONFLICT",       GIT_ECONFLICT },
	{ "GIT_ELOCKED",         GIT_ELOCKED },
	{ "GIT_EMODIFIED",       GIT_EMODIFIED },
	{ "GIT_EAUTH",           GIT_EAUTH },
	{ "GIT_ECERTIFICATE",    GIT_ECERTIFICATE },
	{ "GIT_EAPPLIED",        GIT_EAPPLIED },
	{ "GIT_EPEEL",           GIT_EPEEL },
	{ "GIT_EEOF",            GIT_EEOF },
	{ "GIT_EINVALID",        GIT_EINVALID },
	{ "GIT_EUNCOMMITTED",    GIT_EUNCOMMITTED },
	{ "GIT_EDIRECTORY",      GIT_EDIRECTORY },
	{ "GIT_EMERGECONFLICT",  GIT_EMERGECONFLICT },
	{ "GIT_PASSTHROUGH",     GIT_PASSTHROUGH },
	{ "GIT_ITEROVER",        GIT_ITEROVER },
	{ "GIT_RETRY",           GIT_RETRY },
	{ "GIT_EMISMATCH",       GIT_EMISMATCH },
	{ "GIT_EINDEXDIRTY",     GIT_EINDEXDIRTY },
	{ "GIT_EAPPLYFAIL",      GIT_EAPPLYFAIL },
	{ "GIT_EOWNER",          GIT_EOWNER },
	{ "GIT_TIMEOUT",         GIT_TIMEOUT },
	{ "GIT_EUNCHANGED",      GIT_EUNCHANGED },
	{ "GIT_ENOTSUPPORTED",   GIT_ENOTSUPPORTED },
	{ "GIT_EREADONLY",       GIT_EREADONLY },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitErrorClassNames[] = {
	{ "GIT_ERROR_NONE",       GIT_ERROR_NONE },
	{ "GIT_ERROR_NOMEMORY",   GIT_ERROR_NOMEMORY },
	{ "GIT_ERROR_OS",         GIT_ERROR_OS },
	{ "GIT_ERROR_INVALID",    GIT_ERROR_INVALID },
	{ "GIT_ERROR_REFERENCE",  GIT_ERROR_REFERENCE },
	{ "GIT_ERROR_ZLIB",       GIT_ERROR_ZLIB },
	{ "GIT_ERROR_REPOSITORY", GIT_ERROR_REPOSITORY },
	{ "GIT_ERROR_CONFIG",     GIT_ERROR_CONFIG },
	{ "GIT_ERROR_REGEX",      GIT_ERROR_REGEX },
	{ "GIT_ERROR_ODB",        GIT_ERROR_ODB },
	{ "GIT_ERROR_INDEX",      GIT_ERROR_INDEX },
	{ "GIT_ERROR_OBJECT",     GIT_ERROR_OBJECT },
	{ "GIT_ERROR_NET",        GIT_ERROR_NET },
	{ "GIT_ERROR_TAG",        GIT_ERROR_TAG },
	{ "GIT_ERROR_TREE",       GIT_ERROR_TREE },
	{ "GIT_ERROR_INDEXER",    GIT_ERROR_INDEXER },
	{ "GIT_ERROR_SSL",        GIT_ERROR_SSL },
	{ "GIT_ERROR_SUBMODULE",  GIT_ERROR_SUBMODULE },
	{ "GIT_ERROR_THREAD",     GIT_ERROR_THREAD },
	{ "GIT_ERROR_STASH",      GIT_ERROR_STASH },
	{ "GIT_ERROR_CHECKOUT",   GIT_ERROR_CHECKOUT },
	{ "GIT_ERROR_FETCHHEAD",  GIT_ERROR_FETCHHEAD },
	{ "GIT_ERROR_MERGE",      GIT_ERROR_MERGE },
	{ "GIT_ERROR_SSH",        GIT_ERROR_SSH },
	{ "GIT_ERROR_FILTER",     GIT_ERROR_FILTER },
	{ "GIT_ERROR_REVERT",     GIT_ERROR_REVERT },
	{ "GIT_ERROR_CALLBACK",   GIT_ERROR_CALLBACK },
	{ "GIT_ERROR_CHERRYPICK", GIT_ERROR_CHERRYPICK },
	{ "GIT_ERROR_DESCRIBE",   GIT_ERROR_DESCRIBE },
	{ "GIT_ERROR_REBASE",     GIT_ERROR_REBASE },
	{ "GIT_ERROR_FILESYSTEM", GIT_ERROR_FILESYSTEM },
	{ "GIT_ERROR_PATCH",      GIT_ERROR_PATCH },
	{ "GIT_ERROR_WORKTREE",   GIT_ERROR_WORKTREE },
	{ "GIT_ERROR_SHA",        GIT_ERROR_SHA },
	{ "GIT_ERROR_HTTP",       GIT_ERROR_HTTP },
	{ "GIT_ERROR_INTERNAL",   GIT_ERROR_INTERNAL },
	{ "GIT_ERROR_GRAFTS",     GIT_ERROR_GRAFTS },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitFeatureNames[] = {
	{ "GIT_FEATURE_THREADS",        GIT_FEATURE_THREADS },
	{ "GIT_FEATURE_HTTPS",          GIT_FEATURE_HTTPS },
	{ "GIT_FEATURE_SSH",            GIT_FEATURE_SSH },
	{ "GIT_FEATURE_NSEC",           GIT_FEATURE_NSEC },
#if CLIPSGIT_HAVE_1_9
	/* libgit2 1.9 reports which of its optional parts are compiled in. */
	{ "GIT_FEATURE_HTTP_PARSER",    GIT_FEATURE_HTTP_PARSER },
	{ "GIT_FEATURE_REGEX",          GIT_FEATURE_REGEX },
	{ "GIT_FEATURE_I18N",           GIT_FEATURE_I18N },
	{ "GIT_FEATURE_AUTH_NTLM",      GIT_FEATURE_AUTH_NTLM },
	{ "GIT_FEATURE_AUTH_NEGOTIATE", GIT_FEATURE_AUTH_NEGOTIATE },
	{ "GIT_FEATURE_COMPRESSION",    GIT_FEATURE_COMPRESSION },
	{ "GIT_FEATURE_SHA1",           GIT_FEATURE_SHA1 },
	{ "GIT_FEATURE_SHA256",         GIT_FEATURE_SHA256 },
#endif
	{ NULL, 0 }
};

static const struct GitEnumEntry GitObjectTypeNames[] = {
	{ "GIT_OBJECT_ANY",     GIT_OBJECT_ANY },
	{ "GIT_OBJECT_INVALID", GIT_OBJECT_INVALID },
	{ "GIT_OBJECT_COMMIT",  GIT_OBJECT_COMMIT },
	{ "GIT_OBJECT_TREE",    GIT_OBJECT_TREE },
	{ "GIT_OBJECT_BLOB",    GIT_OBJECT_BLOB },
	{ "GIT_OBJECT_TAG",     GIT_OBJECT_TAG },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitReferenceTypeNames[] = {
	{ "GIT_REFERENCE_INVALID",  GIT_REFERENCE_INVALID },
	{ "GIT_REFERENCE_DIRECT",   GIT_REFERENCE_DIRECT },
	{ "GIT_REFERENCE_SYMBOLIC", GIT_REFERENCE_SYMBOLIC },
	{ "GIT_REFERENCE_ALL",      GIT_REFERENCE_ALL },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitBranchTypeNames[] = {
	{ "GIT_BRANCH_LOCAL",  GIT_BRANCH_LOCAL },
	{ "GIT_BRANCH_REMOTE", GIT_BRANCH_REMOTE },
	{ "GIT_BRANCH_ALL",    GIT_BRANCH_ALL },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitFilemodeNames[] = {
	{ "GIT_FILEMODE_UNREADABLE",      GIT_FILEMODE_UNREADABLE },
	{ "GIT_FILEMODE_TREE",            GIT_FILEMODE_TREE },
	{ "GIT_FILEMODE_BLOB",            GIT_FILEMODE_BLOB },
	{ "GIT_FILEMODE_BLOB_EXECUTABLE", GIT_FILEMODE_BLOB_EXECUTABLE },
	{ "GIT_FILEMODE_LINK",            GIT_FILEMODE_LINK },
	{ "GIT_FILEMODE_COMMIT",          GIT_FILEMODE_COMMIT },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitResetTypeNames[] = {
	{ "GIT_RESET_SOFT",  GIT_RESET_SOFT },
	{ "GIT_RESET_MIXED", GIT_RESET_MIXED },
	{ "GIT_RESET_HARD",  GIT_RESET_HARD },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitSortNames[] = {
	{ "GIT_SORT_NONE",        GIT_SORT_NONE },
	{ "GIT_SORT_TOPOLOGICAL", GIT_SORT_TOPOLOGICAL },
	{ "GIT_SORT_TIME",        GIT_SORT_TIME },
	{ "GIT_SORT_REVERSE",     GIT_SORT_REVERSE },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitStatusNames[] = {
	{ "GIT_STATUS_CURRENT",          GIT_STATUS_CURRENT },
	{ "GIT_STATUS_INDEX_NEW",        GIT_STATUS_INDEX_NEW },
	{ "GIT_STATUS_INDEX_MODIFIED",   GIT_STATUS_INDEX_MODIFIED },
	{ "GIT_STATUS_INDEX_DELETED",    GIT_STATUS_INDEX_DELETED },
	{ "GIT_STATUS_INDEX_RENAMED",    GIT_STATUS_INDEX_RENAMED },
	{ "GIT_STATUS_INDEX_TYPECHANGE", GIT_STATUS_INDEX_TYPECHANGE },
	{ "GIT_STATUS_WT_NEW",           GIT_STATUS_WT_NEW },
	{ "GIT_STATUS_WT_MODIFIED",      GIT_STATUS_WT_MODIFIED },
	{ "GIT_STATUS_WT_DELETED",       GIT_STATUS_WT_DELETED },
	{ "GIT_STATUS_WT_TYPECHANGE",    GIT_STATUS_WT_TYPECHANGE },
	{ "GIT_STATUS_WT_RENAMED",       GIT_STATUS_WT_RENAMED },
	{ "GIT_STATUS_WT_UNREADABLE",    GIT_STATUS_WT_UNREADABLE },
	{ "GIT_STATUS_IGNORED",          GIT_STATUS_IGNORED },
	{ "GIT_STATUS_CONFLICTED",       GIT_STATUS_CONFLICTED },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitStatusShowNames[] = {
	{ "GIT_STATUS_SHOW_INDEX_AND_WORKDIR", GIT_STATUS_SHOW_INDEX_AND_WORKDIR },
	{ "GIT_STATUS_SHOW_INDEX_ONLY",        GIT_STATUS_SHOW_INDEX_ONLY },
	{ "GIT_STATUS_SHOW_WORKDIR_ONLY",      GIT_STATUS_SHOW_WORKDIR_ONLY },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitStatusOptNames[] = {
	{ "GIT_STATUS_OPT_INCLUDE_UNTRACKED",              GIT_STATUS_OPT_INCLUDE_UNTRACKED },
	{ "GIT_STATUS_OPT_INCLUDE_IGNORED",                GIT_STATUS_OPT_INCLUDE_IGNORED },
	{ "GIT_STATUS_OPT_INCLUDE_UNMODIFIED",             GIT_STATUS_OPT_INCLUDE_UNMODIFIED },
	{ "GIT_STATUS_OPT_EXCLUDE_SUBMODULES",             GIT_STATUS_OPT_EXCLUDE_SUBMODULES },
	{ "GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS",         GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS },
	{ "GIT_STATUS_OPT_DISABLE_PATHSPEC_MATCH",         GIT_STATUS_OPT_DISABLE_PATHSPEC_MATCH },
	{ "GIT_STATUS_OPT_RECURSE_IGNORED_DIRS",           GIT_STATUS_OPT_RECURSE_IGNORED_DIRS },
	{ "GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX",          GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX },
	{ "GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR",       GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR },
	{ "GIT_STATUS_OPT_SORT_CASE_SENSITIVELY",          GIT_STATUS_OPT_SORT_CASE_SENSITIVELY },
	{ "GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY",        GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY },
	{ "GIT_STATUS_OPT_RENAMES_FROM_REWRITES",          GIT_STATUS_OPT_RENAMES_FROM_REWRITES },
	{ "GIT_STATUS_OPT_NO_REFRESH",                     GIT_STATUS_OPT_NO_REFRESH },
	{ "GIT_STATUS_OPT_UPDATE_INDEX",                   GIT_STATUS_OPT_UPDATE_INDEX },
	{ "GIT_STATUS_OPT_INCLUDE_UNREADABLE",             GIT_STATUS_OPT_INCLUDE_UNREADABLE },
	{ "GIT_STATUS_OPT_INCLUDE_UNREADABLE_AS_UNTRACKED", GIT_STATUS_OPT_INCLUDE_UNREADABLE_AS_UNTRACKED },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitCheckoutNames[] = {
	{ "GIT_CHECKOUT_NONE",                    GIT_CHECKOUT_NONE },
	{ "GIT_CHECKOUT_SAFE",                    GIT_CHECKOUT_SAFE },
	{ "GIT_CHECKOUT_FORCE",                   GIT_CHECKOUT_FORCE },
	{ "GIT_CHECKOUT_RECREATE_MISSING",        GIT_CHECKOUT_RECREATE_MISSING },
	{ "GIT_CHECKOUT_ALLOW_CONFLICTS",         GIT_CHECKOUT_ALLOW_CONFLICTS },
	{ "GIT_CHECKOUT_REMOVE_UNTRACKED",        GIT_CHECKOUT_REMOVE_UNTRACKED },
	{ "GIT_CHECKOUT_REMOVE_IGNORED",          GIT_CHECKOUT_REMOVE_IGNORED },
	{ "GIT_CHECKOUT_UPDATE_ONLY",             GIT_CHECKOUT_UPDATE_ONLY },
	{ "GIT_CHECKOUT_DONT_UPDATE_INDEX",       GIT_CHECKOUT_DONT_UPDATE_INDEX },
	{ "GIT_CHECKOUT_NO_REFRESH",              GIT_CHECKOUT_NO_REFRESH },
	{ "GIT_CHECKOUT_SKIP_UNMERGED",           GIT_CHECKOUT_SKIP_UNMERGED },
	{ "GIT_CHECKOUT_USE_OURS",                GIT_CHECKOUT_USE_OURS },
	{ "GIT_CHECKOUT_USE_THEIRS",              GIT_CHECKOUT_USE_THEIRS },
	{ "GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH",  GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH },
	{ "GIT_CHECKOUT_SKIP_LOCKED_DIRECTORIES", GIT_CHECKOUT_SKIP_LOCKED_DIRECTORIES },
	{ "GIT_CHECKOUT_DONT_OVERWRITE_IGNORED",  GIT_CHECKOUT_DONT_OVERWRITE_IGNORED },
	{ "GIT_CHECKOUT_CONFLICT_STYLE_MERGE",    GIT_CHECKOUT_CONFLICT_STYLE_MERGE },
	{ "GIT_CHECKOUT_CONFLICT_STYLE_DIFF3",    GIT_CHECKOUT_CONFLICT_STYLE_DIFF3 },
	{ "GIT_CHECKOUT_DONT_REMOVE_EXISTING",    GIT_CHECKOUT_DONT_REMOVE_EXISTING },
	{ "GIT_CHECKOUT_DONT_WRITE_INDEX",        GIT_CHECKOUT_DONT_WRITE_INDEX },
#if CLIPSGIT_HAVE_1_9
	{ "GIT_CHECKOUT_DRY_RUN",                 GIT_CHECKOUT_DRY_RUN },
#endif
	{ "GIT_CHECKOUT_CONFLICT_STYLE_ZDIFF3",   GIT_CHECKOUT_CONFLICT_STYLE_ZDIFF3 },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitDeltaNames[] = {
	{ "GIT_DELTA_UNMODIFIED", GIT_DELTA_UNMODIFIED },
	{ "GIT_DELTA_ADDED",      GIT_DELTA_ADDED },
	{ "GIT_DELTA_DELETED",    GIT_DELTA_DELETED },
	{ "GIT_DELTA_MODIFIED",   GIT_DELTA_MODIFIED },
	{ "GIT_DELTA_RENAMED",    GIT_DELTA_RENAMED },
	{ "GIT_DELTA_COPIED",     GIT_DELTA_COPIED },
	{ "GIT_DELTA_IGNORED",    GIT_DELTA_IGNORED },
	{ "GIT_DELTA_UNTRACKED",  GIT_DELTA_UNTRACKED },
	{ "GIT_DELTA_TYPECHANGE", GIT_DELTA_TYPECHANGE },
	{ "GIT_DELTA_UNREADABLE", GIT_DELTA_UNREADABLE },
	{ "GIT_DELTA_CONFLICTED", GIT_DELTA_CONFLICTED },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitDiffFormatNames[] = {
	{ "GIT_DIFF_FORMAT_PATCH",        GIT_DIFF_FORMAT_PATCH },
	{ "GIT_DIFF_FORMAT_PATCH_HEADER", GIT_DIFF_FORMAT_PATCH_HEADER },
	{ "GIT_DIFF_FORMAT_RAW",          GIT_DIFF_FORMAT_RAW },
	{ "GIT_DIFF_FORMAT_NAME_ONLY",    GIT_DIFF_FORMAT_NAME_ONLY },
	{ "GIT_DIFF_FORMAT_NAME_STATUS",  GIT_DIFF_FORMAT_NAME_STATUS },
	{ "GIT_DIFF_FORMAT_PATCH_ID",     GIT_DIFF_FORMAT_PATCH_ID },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitDiffOptionNames[] = {
	{ "GIT_DIFF_NORMAL",                       GIT_DIFF_NORMAL },
	{ "GIT_DIFF_REVERSE",                      GIT_DIFF_REVERSE },
	{ "GIT_DIFF_INCLUDE_IGNORED",              GIT_DIFF_INCLUDE_IGNORED },
	{ "GIT_DIFF_RECURSE_IGNORED_DIRS",         GIT_DIFF_RECURSE_IGNORED_DIRS },
	{ "GIT_DIFF_INCLUDE_UNTRACKED",            GIT_DIFF_INCLUDE_UNTRACKED },
	{ "GIT_DIFF_RECURSE_UNTRACKED_DIRS",       GIT_DIFF_RECURSE_UNTRACKED_DIRS },
	{ "GIT_DIFF_INCLUDE_UNMODIFIED",           GIT_DIFF_INCLUDE_UNMODIFIED },
	{ "GIT_DIFF_INCLUDE_TYPECHANGE",           GIT_DIFF_INCLUDE_TYPECHANGE },
	{ "GIT_DIFF_INCLUDE_TYPECHANGE_TREES",     GIT_DIFF_INCLUDE_TYPECHANGE_TREES },
	{ "GIT_DIFF_IGNORE_FILEMODE",              GIT_DIFF_IGNORE_FILEMODE },
	{ "GIT_DIFF_IGNORE_SUBMODULES",            GIT_DIFF_IGNORE_SUBMODULES },
	{ "GIT_DIFF_IGNORE_CASE",                  GIT_DIFF_IGNORE_CASE },
	{ "GIT_DIFF_INCLUDE_CASECHANGE",           GIT_DIFF_INCLUDE_CASECHANGE },
	{ "GIT_DIFF_DISABLE_PATHSPEC_MATCH",       GIT_DIFF_DISABLE_PATHSPEC_MATCH },
	{ "GIT_DIFF_SKIP_BINARY_CHECK",            GIT_DIFF_SKIP_BINARY_CHECK },
	{ "GIT_DIFF_ENABLE_FAST_UNTRACKED_DIRS",   GIT_DIFF_ENABLE_FAST_UNTRACKED_DIRS },
	{ "GIT_DIFF_UPDATE_INDEX",                 GIT_DIFF_UPDATE_INDEX },
	{ "GIT_DIFF_INCLUDE_UNREADABLE",           GIT_DIFF_INCLUDE_UNREADABLE },
	{ "GIT_DIFF_INCLUDE_UNREADABLE_AS_UNTRACKED", GIT_DIFF_INCLUDE_UNREADABLE_AS_UNTRACKED },
	{ "GIT_DIFF_INDENT_HEURISTIC",             GIT_DIFF_INDENT_HEURISTIC },
	{ "GIT_DIFF_IGNORE_BLANK_LINES",           GIT_DIFF_IGNORE_BLANK_LINES },
	{ "GIT_DIFF_FORCE_TEXT",                   GIT_DIFF_FORCE_TEXT },
	{ "GIT_DIFF_FORCE_BINARY",                 GIT_DIFF_FORCE_BINARY },
	{ "GIT_DIFF_IGNORE_WHITESPACE",            GIT_DIFF_IGNORE_WHITESPACE },
	{ "GIT_DIFF_IGNORE_WHITESPACE_CHANGE",     GIT_DIFF_IGNORE_WHITESPACE_CHANGE },
	{ "GIT_DIFF_IGNORE_WHITESPACE_EOL",        GIT_DIFF_IGNORE_WHITESPACE_EOL },
	{ "GIT_DIFF_SHOW_UNTRACKED_CONTENT",       GIT_DIFF_SHOW_UNTRACKED_CONTENT },
	{ "GIT_DIFF_SHOW_UNMODIFIED",              GIT_DIFF_SHOW_UNMODIFIED },
	{ "GIT_DIFF_PATIENCE",                     GIT_DIFF_PATIENCE },
	{ "GIT_DIFF_MINIMAL",                      GIT_DIFF_MINIMAL },
	{ "GIT_DIFF_SHOW_BINARY",                  GIT_DIFF_SHOW_BINARY },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitDiffFindNames[] = {
	{ "GIT_DIFF_FIND_BY_CONFIG",                  GIT_DIFF_FIND_BY_CONFIG },
	{ "GIT_DIFF_FIND_RENAMES",                    GIT_DIFF_FIND_RENAMES },
	{ "GIT_DIFF_FIND_RENAMES_FROM_REWRITES",      GIT_DIFF_FIND_RENAMES_FROM_REWRITES },
	{ "GIT_DIFF_FIND_COPIES",                     GIT_DIFF_FIND_COPIES },
	{ "GIT_DIFF_FIND_COPIES_FROM_UNMODIFIED",     GIT_DIFF_FIND_COPIES_FROM_UNMODIFIED },
	{ "GIT_DIFF_FIND_REWRITES",                   GIT_DIFF_FIND_REWRITES },
	{ "GIT_DIFF_BREAK_REWRITES",                  GIT_DIFF_BREAK_REWRITES },
	{ "GIT_DIFF_FIND_AND_BREAK_REWRITES",         GIT_DIFF_FIND_AND_BREAK_REWRITES },
	{ "GIT_DIFF_FIND_FOR_UNTRACKED",              GIT_DIFF_FIND_FOR_UNTRACKED },
	{ "GIT_DIFF_FIND_ALL",                        GIT_DIFF_FIND_ALL },
	{ "GIT_DIFF_FIND_IGNORE_LEADING_WHITESPACE",  GIT_DIFF_FIND_IGNORE_LEADING_WHITESPACE },
	{ "GIT_DIFF_FIND_IGNORE_WHITESPACE",          GIT_DIFF_FIND_IGNORE_WHITESPACE },
	{ "GIT_DIFF_FIND_DONT_IGNORE_WHITESPACE",     GIT_DIFF_FIND_DONT_IGNORE_WHITESPACE },
	{ "GIT_DIFF_FIND_EXACT_MATCH_ONLY",           GIT_DIFF_FIND_EXACT_MATCH_ONLY },
	{ "GIT_DIFF_BREAK_REWRITES_FOR_RENAMES_ONLY", GIT_DIFF_BREAK_REWRITES_FOR_RENAMES_ONLY },
	{ "GIT_DIFF_FIND_REMOVE_UNMODIFIED",          GIT_DIFF_FIND_REMOVE_UNMODIFIED },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitDiffStatsFormatNames[] = {
	{ "GIT_DIFF_STATS_NONE",            GIT_DIFF_STATS_NONE },
	{ "GIT_DIFF_STATS_FULL",            GIT_DIFF_STATS_FULL },
	{ "GIT_DIFF_STATS_SHORT",           GIT_DIFF_STATS_SHORT },
	{ "GIT_DIFF_STATS_NUMBER",          GIT_DIFF_STATS_NUMBER },
	{ "GIT_DIFF_STATS_INCLUDE_SUMMARY", GIT_DIFF_STATS_INCLUDE_SUMMARY },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitMergeAnalysisNames[] = {
	{ "GIT_MERGE_ANALYSIS_NONE",        GIT_MERGE_ANALYSIS_NONE },
	{ "GIT_MERGE_ANALYSIS_NORMAL",      GIT_MERGE_ANALYSIS_NORMAL },
	{ "GIT_MERGE_ANALYSIS_UP_TO_DATE",  GIT_MERGE_ANALYSIS_UP_TO_DATE },
	{ "GIT_MERGE_ANALYSIS_FASTFORWARD", GIT_MERGE_ANALYSIS_FASTFORWARD },
	{ "GIT_MERGE_ANALYSIS_UNBORN",      GIT_MERGE_ANALYSIS_UNBORN },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitMergePreferenceNames[] = {
	{ "GIT_MERGE_PREFERENCE_NONE",             GIT_MERGE_PREFERENCE_NONE },
	{ "GIT_MERGE_PREFERENCE_NO_FASTFORWARD",   GIT_MERGE_PREFERENCE_NO_FASTFORWARD },
	{ "GIT_MERGE_PREFERENCE_FASTFORWARD_ONLY", GIT_MERGE_PREFERENCE_FASTFORWARD_ONLY },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitMergeFlagNames[] = {
	{ "GIT_MERGE_FIND_RENAMES",     GIT_MERGE_FIND_RENAMES },
	{ "GIT_MERGE_FAIL_ON_CONFLICT", GIT_MERGE_FAIL_ON_CONFLICT },
	{ "GIT_MERGE_SKIP_REUC",        GIT_MERGE_SKIP_REUC },
	{ "GIT_MERGE_NO_RECURSIVE",     GIT_MERGE_NO_RECURSIVE },
	{ "GIT_MERGE_VIRTUAL_BASE",     GIT_MERGE_VIRTUAL_BASE },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitRepositoryStateNames[] = {
	{ "GIT_REPOSITORY_STATE_NONE",                    GIT_REPOSITORY_STATE_NONE },
	{ "GIT_REPOSITORY_STATE_MERGE",                   GIT_REPOSITORY_STATE_MERGE },
	{ "GIT_REPOSITORY_STATE_REVERT",                  GIT_REPOSITORY_STATE_REVERT },
	{ "GIT_REPOSITORY_STATE_REVERT_SEQUENCE",         GIT_REPOSITORY_STATE_REVERT_SEQUENCE },
	{ "GIT_REPOSITORY_STATE_CHERRYPICK",              GIT_REPOSITORY_STATE_CHERRYPICK },
	{ "GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE",     GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE },
	{ "GIT_REPOSITORY_STATE_BISECT",                  GIT_REPOSITORY_STATE_BISECT },
	{ "GIT_REPOSITORY_STATE_REBASE",                  GIT_REPOSITORY_STATE_REBASE },
	{ "GIT_REPOSITORY_STATE_REBASE_INTERACTIVE",      GIT_REPOSITORY_STATE_REBASE_INTERACTIVE },
	{ "GIT_REPOSITORY_STATE_REBASE_MERGE",            GIT_REPOSITORY_STATE_REBASE_MERGE },
	{ "GIT_REPOSITORY_STATE_APPLY_MAILBOX",           GIT_REPOSITORY_STATE_APPLY_MAILBOX },
	{ "GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE", GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitRepositoryInitNames[] = {
	{ "GIT_REPOSITORY_INIT_BARE",              GIT_REPOSITORY_INIT_BARE },
	{ "GIT_REPOSITORY_INIT_NO_REINIT",         GIT_REPOSITORY_INIT_NO_REINIT },
	{ "GIT_REPOSITORY_INIT_NO_DOTGIT_DIR",     GIT_REPOSITORY_INIT_NO_DOTGIT_DIR },
	{ "GIT_REPOSITORY_INIT_MKDIR",             GIT_REPOSITORY_INIT_MKDIR },
	{ "GIT_REPOSITORY_INIT_MKPATH",            GIT_REPOSITORY_INIT_MKPATH },
	{ "GIT_REPOSITORY_INIT_EXTERNAL_TEMPLATE", GIT_REPOSITORY_INIT_EXTERNAL_TEMPLATE },
	{ "GIT_REPOSITORY_INIT_RELATIVE_GITLINK",  GIT_REPOSITORY_INIT_RELATIVE_GITLINK },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitRepositoryOpenNames[] = {
	{ "GIT_REPOSITORY_OPEN_NO_SEARCH", GIT_REPOSITORY_OPEN_NO_SEARCH },
	{ "GIT_REPOSITORY_OPEN_CROSS_FS",  GIT_REPOSITORY_OPEN_CROSS_FS },
	{ "GIT_REPOSITORY_OPEN_BARE",      GIT_REPOSITORY_OPEN_BARE },
	{ "GIT_REPOSITORY_OPEN_NO_DOTGIT", GIT_REPOSITORY_OPEN_NO_DOTGIT },
	{ "GIT_REPOSITORY_OPEN_FROM_ENV",  GIT_REPOSITORY_OPEN_FROM_ENV },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitRepositoryItemNames[] = {
	{ "GIT_REPOSITORY_ITEM_GITDIR",     GIT_REPOSITORY_ITEM_GITDIR },
	{ "GIT_REPOSITORY_ITEM_WORKDIR",    GIT_REPOSITORY_ITEM_WORKDIR },
	{ "GIT_REPOSITORY_ITEM_COMMONDIR",  GIT_REPOSITORY_ITEM_COMMONDIR },
	{ "GIT_REPOSITORY_ITEM_INDEX",      GIT_REPOSITORY_ITEM_INDEX },
	{ "GIT_REPOSITORY_ITEM_OBJECTS",    GIT_REPOSITORY_ITEM_OBJECTS },
	{ "GIT_REPOSITORY_ITEM_REFS",       GIT_REPOSITORY_ITEM_REFS },
	{ "GIT_REPOSITORY_ITEM_PACKED_REFS", GIT_REPOSITORY_ITEM_PACKED_REFS },
	{ "GIT_REPOSITORY_ITEM_REMOTES",    GIT_REPOSITORY_ITEM_REMOTES },
	{ "GIT_REPOSITORY_ITEM_CONFIG",     GIT_REPOSITORY_ITEM_CONFIG },
	{ "GIT_REPOSITORY_ITEM_INFO",       GIT_REPOSITORY_ITEM_INFO },
	{ "GIT_REPOSITORY_ITEM_HOOKS",      GIT_REPOSITORY_ITEM_HOOKS },
	{ "GIT_REPOSITORY_ITEM_LOGS",       GIT_REPOSITORY_ITEM_LOGS },
	{ "GIT_REPOSITORY_ITEM_MODULES",    GIT_REPOSITORY_ITEM_MODULES },
	{ "GIT_REPOSITORY_ITEM_WORKTREES",  GIT_REPOSITORY_ITEM_WORKTREES },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitConfigLevelNames[] = {
	{ "GIT_CONFIG_LEVEL_PROGRAMDATA", GIT_CONFIG_LEVEL_PROGRAMDATA },
	{ "GIT_CONFIG_LEVEL_SYSTEM",      GIT_CONFIG_LEVEL_SYSTEM },
	{ "GIT_CONFIG_LEVEL_XDG",         GIT_CONFIG_LEVEL_XDG },
	{ "GIT_CONFIG_LEVEL_GLOBAL",      GIT_CONFIG_LEVEL_GLOBAL },
	{ "GIT_CONFIG_LEVEL_LOCAL",       GIT_CONFIG_LEVEL_LOCAL },
	{ "GIT_CONFIG_LEVEL_WORKTREE",    GIT_CONFIG_LEVEL_WORKTREE },
	{ "GIT_CONFIG_LEVEL_APP",         GIT_CONFIG_LEVEL_APP },
	{ "GIT_CONFIG_HIGHEST_LEVEL",     GIT_CONFIG_HIGHEST_LEVEL },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitStashFlagNames[] = {
	{ "GIT_STASH_DEFAULT",           GIT_STASH_DEFAULT },
	{ "GIT_STASH_KEEP_INDEX",        GIT_STASH_KEEP_INDEX },
	{ "GIT_STASH_INCLUDE_UNTRACKED", GIT_STASH_INCLUDE_UNTRACKED },
	{ "GIT_STASH_INCLUDE_IGNORED",   GIT_STASH_INCLUDE_IGNORED },
	{ "GIT_STASH_KEEP_ALL",          GIT_STASH_KEEP_ALL },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitStashApplyFlagNames[] = {
	{ "GIT_STASH_APPLY_DEFAULT",         GIT_STASH_APPLY_DEFAULT },
	{ "GIT_STASH_APPLY_REINSTATE_INDEX", GIT_STASH_APPLY_REINSTATE_INDEX },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitApplyLocationNames[] = {
	{ "GIT_APPLY_LOCATION_WORKDIR", GIT_APPLY_LOCATION_WORKDIR },
	{ "GIT_APPLY_LOCATION_INDEX",   GIT_APPLY_LOCATION_INDEX },
	{ "GIT_APPLY_LOCATION_BOTH",    GIT_APPLY_LOCATION_BOTH },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitDescribeStrategyNames[] = {
	{ "GIT_DESCRIBE_DEFAULT", GIT_DESCRIBE_DEFAULT },
	{ "GIT_DESCRIBE_TAGS",    GIT_DESCRIBE_TAGS },
	{ "GIT_DESCRIBE_ALL",     GIT_DESCRIBE_ALL },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitIndexAddOptionNames[] = {
	{ "GIT_INDEX_ADD_DEFAULT",                GIT_INDEX_ADD_DEFAULT },
	{ "GIT_INDEX_ADD_FORCE",                  GIT_INDEX_ADD_FORCE },
	{ "GIT_INDEX_ADD_DISABLE_PATHSPEC_MATCH", GIT_INDEX_ADD_DISABLE_PATHSPEC_MATCH },
	{ "GIT_INDEX_ADD_CHECK_PATHSPEC",         GIT_INDEX_ADD_CHECK_PATHSPEC },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitDirectionNames[] = {
	{ "GIT_DIRECTION_FETCH", GIT_DIRECTION_FETCH },
	{ "GIT_DIRECTION_PUSH",  GIT_DIRECTION_PUSH },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitRemoteAutotagNames[] = {
	{ "GIT_REMOTE_DOWNLOAD_TAGS_UNSPECIFIED", GIT_REMOTE_DOWNLOAD_TAGS_UNSPECIFIED },
	{ "GIT_REMOTE_DOWNLOAD_TAGS_AUTO",        GIT_REMOTE_DOWNLOAD_TAGS_AUTO },
	{ "GIT_REMOTE_DOWNLOAD_TAGS_NONE",        GIT_REMOTE_DOWNLOAD_TAGS_NONE },
	{ "GIT_REMOTE_DOWNLOAD_TAGS_ALL",         GIT_REMOTE_DOWNLOAD_TAGS_ALL },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitSubmoduleStatusNames[] = {
	{ "GIT_SUBMODULE_STATUS_IN_HEAD",           GIT_SUBMODULE_STATUS_IN_HEAD },
	{ "GIT_SUBMODULE_STATUS_IN_INDEX",          GIT_SUBMODULE_STATUS_IN_INDEX },
	{ "GIT_SUBMODULE_STATUS_IN_CONFIG",         GIT_SUBMODULE_STATUS_IN_CONFIG },
	{ "GIT_SUBMODULE_STATUS_IN_WD",             GIT_SUBMODULE_STATUS_IN_WD },
	{ "GIT_SUBMODULE_STATUS_INDEX_ADDED",       GIT_SUBMODULE_STATUS_INDEX_ADDED },
	{ "GIT_SUBMODULE_STATUS_INDEX_DELETED",     GIT_SUBMODULE_STATUS_INDEX_DELETED },
	{ "GIT_SUBMODULE_STATUS_INDEX_MODIFIED",    GIT_SUBMODULE_STATUS_INDEX_MODIFIED },
	{ "GIT_SUBMODULE_STATUS_WD_UNINITIALIZED",  GIT_SUBMODULE_STATUS_WD_UNINITIALIZED },
	{ "GIT_SUBMODULE_STATUS_WD_ADDED",          GIT_SUBMODULE_STATUS_WD_ADDED },
	{ "GIT_SUBMODULE_STATUS_WD_DELETED",        GIT_SUBMODULE_STATUS_WD_DELETED },
	{ "GIT_SUBMODULE_STATUS_WD_MODIFIED",       GIT_SUBMODULE_STATUS_WD_MODIFIED },
	{ "GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED", GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED },
	{ "GIT_SUBMODULE_STATUS_WD_WD_MODIFIED",    GIT_SUBMODULE_STATUS_WD_WD_MODIFIED },
	{ "GIT_SUBMODULE_STATUS_WD_UNTRACKED",      GIT_SUBMODULE_STATUS_WD_UNTRACKED },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitSubmoduleIgnoreNames[] = {
	{ "GIT_SUBMODULE_IGNORE_UNSPECIFIED", GIT_SUBMODULE_IGNORE_UNSPECIFIED },
	{ "GIT_SUBMODULE_IGNORE_NONE",        GIT_SUBMODULE_IGNORE_NONE },
	{ "GIT_SUBMODULE_IGNORE_UNTRACKED",   GIT_SUBMODULE_IGNORE_UNTRACKED },
	{ "GIT_SUBMODULE_IGNORE_DIRTY",       GIT_SUBMODULE_IGNORE_DIRTY },
	{ "GIT_SUBMODULE_IGNORE_ALL",         GIT_SUBMODULE_IGNORE_ALL },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitWorktreePruneNames[] = {
	{ "GIT_WORKTREE_PRUNE_VALID",       GIT_WORKTREE_PRUNE_VALID },
	{ "GIT_WORKTREE_PRUNE_LOCKED",      GIT_WORKTREE_PRUNE_LOCKED },
	{ "GIT_WORKTREE_PRUNE_WORKING_TREE", GIT_WORKTREE_PRUNE_WORKING_TREE },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitBlameFlagNames[] = {
	{ "GIT_BLAME_NORMAL",                          GIT_BLAME_NORMAL },
	{ "GIT_BLAME_TRACK_COPIES_SAME_FILE",          GIT_BLAME_TRACK_COPIES_SAME_FILE },
	{ "GIT_BLAME_TRACK_COPIES_SAME_COMMIT_MOVES",  GIT_BLAME_TRACK_COPIES_SAME_COMMIT_MOVES },
	{ "GIT_BLAME_TRACK_COPIES_SAME_COMMIT_COPIES", GIT_BLAME_TRACK_COPIES_SAME_COMMIT_COPIES },
	{ "GIT_BLAME_TRACK_COPIES_ANY_COMMIT_COPIES",  GIT_BLAME_TRACK_COPIES_ANY_COMMIT_COPIES },
	{ "GIT_BLAME_FIRST_PARENT",                    GIT_BLAME_FIRST_PARENT },
	{ "GIT_BLAME_USE_MAILMAP",                     GIT_BLAME_USE_MAILMAP },
	{ "GIT_BLAME_IGNORE_WHITESPACE",               GIT_BLAME_IGNORE_WHITESPACE },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitRevspecNames[] = {
	{ "GIT_REVSPEC_SINGLE",     GIT_REVSPEC_SINGLE },
	{ "GIT_REVSPEC_RANGE",      GIT_REVSPEC_RANGE },
	{ "GIT_REVSPEC_MERGE_BASE", GIT_REVSPEC_MERGE_BASE },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitTreewalkModeNames[] = {
	{ "GIT_TREEWALK_PRE",  GIT_TREEWALK_PRE },
	{ "GIT_TREEWALK_POST", GIT_TREEWALK_POST },
	{ NULL, 0 }
};

/* The C name of a value. The result is "<unknown>" for a value that libgit2
   added after these tables were written. */
static const char *GitEnumName(
  const struct GitEnumEntry *table,
  long long value)
{
	int i;

	for (i = 0; table[i].name != NULL; i++)
	{
		if (table[i].value == value)
		{
			return table[i].name;
		}
	}

	return "<unknown>";
}

/* The name of a value, as a CLIPS symbol. */
static void GitReturnEnum(
  Environment *theEnv,
  UDFValue *returnValue,
  const struct GitEnumEntry *table,
  long long value)
{
	returnValue->lexemeValue = CreateSymbol(theEnv, GitEnumName(table, value));
}

/* The value for a name. The caller can also give the value as the integer. */
static bool GitEnumArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const struct GitEnumEntry *table,
  long long *out)
{
	UDFValue theArg;
	char msg[192];
	int i;

	if (!UDFNextArgument(context, INTEGER_BIT | LEXEME_BITS, &theArg))
	{
		snprintf(msg, sizeof msg, "expected a symbol or an integer for %s", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type == INTEGER_TYPE)
	{
		*out = theArg.integerValue->contents;
		return true;
	}

	if (theArg.header->type != SYMBOL_TYPE && theArg.header->type != STRING_TYPE)
	{
		snprintf(msg, sizeof msg, "expected a symbol or an integer for %s", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	for (i = 0; table[i].name != NULL; i++)
	{
		if (strcmp(table[i].name, theArg.lexemeValue->contents) == 0)
		{
			*out = table[i].value;
			return true;
		}
	}

	snprintf(msg, sizeof msg, "%s is not a name this libgit2 has for %s",
	         theArg.lexemeValue->contents, what);
	GitFail(theEnv, returnValue, fname, msg);
	return false;
}

/* An optional enumeration. If the argument is not present, the result is
   the given default. */
static bool GitOptionalEnumArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const struct GitEnumEntry *table,
  long long defaultValue,
  long long *out)
{
	*out = defaultValue;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}
	return GitEnumArgument(theEnv, context, returnValue, fname, what, table, out);
}

/* A set of flags: an integer, or a multifield of the symbols that name the
   flags. A multifield is the readable form. An integer is the form that a
   program calculates. So both forms are accepted for all flag arguments. A
   single symbol is also accepted, as a multifield of one element. */
static bool GitFlagsArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const struct GitEnumEntry *table,
  unsigned int *out)
{
	UDFValue theArg;
	char msg[192];
	size_t i;
	int j;

	*out = 0;

	if (!UDFNextArgument(context, INTEGER_BIT | MULTIFIELD_BIT | LEXEME_BITS, &theArg))
	{
		snprintf(msg, sizeof msg, "expected an integer, a symbol or a multifield for %s", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type == INTEGER_TYPE)
	{
		if (theArg.integerValue->contents < 0)
		{
			snprintf(msg, sizeof msg, "%s cannot be negative", what);
			GitFail(theEnv, returnValue, fname, msg);
			return false;
		}
		*out = (unsigned int) theArg.integerValue->contents;
		return true;
	}

	if (theArg.header->type == SYMBOL_TYPE || theArg.header->type == STRING_TYPE)
	{
		for (j = 0; table[j].name != NULL; j++)
		{
			if (strcmp(table[j].name, theArg.lexemeValue->contents) == 0)
			{
				*out = (unsigned int) table[j].value;
				return true;
			}
		}
		snprintf(msg, sizeof msg, "%s is not a name this libgit2 has for %s",
		         theArg.lexemeValue->contents, what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	if (theArg.header->type != MULTIFIELD_TYPE)
	{
		snprintf(msg, sizeof msg, "expected an integer, a symbol or a multifield for %s", what);
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	for (i = 0; i < theArg.multifieldValue->length; i++)
	{
		CLIPSValue *v = &theArg.multifieldValue->contents[i];
		bool found = false;

		if (v->header->type == INTEGER_TYPE)
		{
			*out |= (unsigned int) v->integerValue->contents;
			continue;
		}

		if (v->header->type != SYMBOL_TYPE && v->header->type != STRING_TYPE)
		{
			snprintf(msg, sizeof msg, "every element of %s must be a symbol or an integer", what);
			GitFail(theEnv, returnValue, fname, msg);
			return false;
		}

		for (j = 0; table[j].name != NULL; j++)
		{
			if (strcmp(table[j].name, v->lexemeValue->contents) == 0)
			{
				*out |= (unsigned int) table[j].value;
				found = true;
				break;
			}
		}

		if (!found)
		{
			snprintf(msg, sizeof msg, "%s is not a name this libgit2 has for %s",
			         v->lexemeValue->contents, what);
			GitFail(theEnv, returnValue, fname, msg);
			return false;
		}
	}

	return true;
}

/* Optional flags. If the argument is not present, the result is the given
   default. */
static bool GitOptionalFlagsArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  const struct GitEnumEntry *table,
  unsigned int defaultValue,
  unsigned int *out)
{
	*out = defaultValue;
	if (!UDFHasNextArgument(context))
	{
		return true;
	}
	return GitFlagsArgument(theEnv, context, returnValue, fname, what, table, out);
}

/* A set of flags as a multifield of the symbols that name the bits that are
   set. A bit that has no name is added as its integer. */
static void GitReturnFlags(
  Environment *theEnv,
  UDFValue *returnValue,
  const struct GitEnumEntry *table,
  unsigned int value)
{
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, 8);
	unsigned int covered = 0;
	int i;

	/* In most of these tables, the value with no bits set has a name of
	   its own, for example GIT_STATUS_CURRENT or GIT_SORT_NONE. That
	   name is more useful than an empty multifield. */
	if (value == 0)
	{
		for (i = 0; table[i].name != NULL; i++)
		{
			if (table[i].value == 0)
			{
				MBAppendSymbol(mb, table[i].name);
				break;
			}
		}
	}

	for (i = 0; table[i].name != NULL; i++)
	{
		unsigned int bit = (unsigned int) table[i].value;

		if (bit != 0 && (value & bit) == bit)
		{
			MBAppendSymbol(mb, table[i].name);
			covered |= bit;
		}
	}

	if ((value & ~covered) != 0)
	{
		MBAppendInteger(mb, (long long) (value & ~covered));
	}

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/***************************************************************************/
/*                                                                         */
/* Return values.                                                          */
/*                                                                         */
/***************************************************************************/

/* A new handle. The helper records it and returns it. */
static void GitReturnHandle(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  void *ptr,
  enum GitKind kind,
  git_repository *repo,
  void *parent)
{
	if (GitHandleRegister(ptr, kind, repo, parent) == NULL)
	{
		if (parent == NULL)
		{
			GitFreeByKind(kind, ptr);
		}
		GitFail(theEnv, returnValue, fname, "out of memory recording the handle");
		return;
	}

	returnValue->externalAddressValue = CreateCExternalAddress(theEnv, ptr);
}

/* An object. The helper records it as the kind that it is: commit, tree,
   blob, or tag. */
static void GitReturnObject(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  git_object *object,
  git_repository *repo)
{
	enum GitKind kind;

	switch (git_object_type(object))
	{
		case GIT_OBJECT_COMMIT: kind = GIT_K_COMMIT; break;
		case GIT_OBJECT_TREE:   kind = GIT_K_TREE; break;
		case GIT_OBJECT_BLOB:   kind = GIT_K_BLOB; break;
		case GIT_OBJECT_TAG:    kind = GIT_K_TAG; break;
		default:
			git_object_free(object);
			GitFail(theEnv, returnValue, fname, "the object is of a type this library does not handle");
			return;
	}

	GitReturnHandle(theEnv, returnValue, fname, object, kind, repo, NULL);
}

/* A borrowed signature, for example the author of a commit or the tagger of
   a tag. The helper records it against the object that owns it. */
static void GitReturnBorrowedSignature(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  const git_signature *sig,
  void *parent)
{
	if (sig == NULL)
	{
		returnValue->lexemeValue = CreateSymbol(theEnv, "nil");
		return;
	}
	GitReturnHandle(theEnv, returnValue, fname, (void *) sig, GIT_K_SIGNATURE, NULL, parent);
}

static void GitReturnOid(
  Environment *theEnv,
  UDFValue *returnValue,
  const git_oid *oid)
{
	char hex[GIT_OID_MAX_HEXSIZE + 1];

	git_oid_tostr(hex, sizeof hex, oid);
	returnValue->lexemeValue = CreateString(theEnv, hex);
}

/* Text that can be NULL. NULL returns as the symbol nil. */
static void GitReturnText(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *text)
{
	if (text == NULL)
	{
		returnValue->lexemeValue = CreateSymbol(theEnv, "nil");
	}
	else
	{
		returnValue->lexemeValue = CreateString(theEnv, text);
	}
}

/* Bytes with no NUL terminator, as a string. A NUL inside the bytes ends
   the string, because a CLIPS string cannot hold a NUL. */
static void GitReturnBytes(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  const void *data,
  size_t size)
{
	char *copy = (char *) malloc(size + 1);

	if (copy == NULL)
	{
		GitFail(theEnv, returnValue, fname, "out of memory copying the content");
		return;
	}

	if (size > 0)
	{
		memcpy(copy, data, size);
	}
	copy[size] = '\0';
	returnValue->lexemeValue = CreateString(theEnv, copy);
	free(copy);
}

/* A git_buf as a string. The helper disposes of the git_buf. */
static void GitReturnBuf(
  Environment *theEnv,
  UDFValue *returnValue,
  git_buf *buf)
{
	returnValue->lexemeValue = CreateString(theEnv, (buf->ptr == NULL) ? "" : buf->ptr);
	git_buf_dispose(buf);
}

/* A git_strarray as a multifield of strings. The helper disposes of the
   git_strarray. */
static void GitReturnStrarray(
  Environment *theEnv,
  UDFValue *returnValue,
  git_strarray *array)
{
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, array->count);
	size_t i;

	for (i = 0; i < array->count; i++)
	{
		MBAppendString(mb, array->strings[i]);
	}

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
	git_strarray_dispose(array);
}

static void GitReturnBool(
  Environment *theEnv,
  UDFValue *returnValue,
  int value)
{
	returnValue->lexemeValue = value ? TrueSymbol(theEnv) : FalseSymbol(theEnv);
}

static void GitReturnNil(
  Environment *theEnv,
  UDFValue *returnValue)
{
	returnValue->lexemeValue = CreateSymbol(theEnv, "nil");
}

/* A call to a C function that returns zero for success. Zero returns as
   TRUE. A different value returns as a rejected call with the libgit2
   message. */
static void GitReturnStatus(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  int error)
{
	if (error != 0)
	{
		GitFailError(theEnv, returnValue, fname, error);
		return;
	}
	returnValue->lexemeValue = TrueSymbol(theEnv);
}

/***************************************************************************/
/*                                                                         */
/* Common wrapper forms.                                                   */
/*                                                                         */
/* Most libgit2 functions are getters: one handle in, one value out. These */
/* macros write a complete wrapper of that form. The inputs are the name,  */
/* the kinds of handle that the wrapper accepts, and the expression that   */
/* calculates the value. The handle is in scope as h.                      */
/*                                                                         */
/***************************************************************************/

#define GIT_GETTER(Cname, fname, KINDS, what, body) \
void Cname(Environment *theEnv, UDFContext *context, UDFValue *returnValue) \
{ \
	UDFValue theArg; \
	void *h = GitHandleArgument(theEnv, context, returnValue, fname, KINDS, what, &theArg); \
	if (h == NULL) { return; } \
	body \
}

#define GIT_GETTER_TEXT(Cname, fname, KINDS, what, expr) \
	GIT_GETTER(Cname, fname, KINDS, what, { GitReturnText(theEnv, returnValue, (expr)); })

#define GIT_GETTER_OID(Cname, fname, KINDS, what, expr) \
	GIT_GETTER(Cname, fname, KINDS, what, { \
		const git_oid *oid = (expr); \
		if (oid == NULL) { GitReturnNil(theEnv, returnValue); } \
		else { GitReturnOid(theEnv, returnValue, oid); } })

#define GIT_GETTER_INTEGER(Cname, fname, KINDS, what, expr) \
	GIT_GETTER(Cname, fname, KINDS, what, { returnValue->integerValue = CreateInteger(theEnv, (long long) (expr)); })

#define GIT_GETTER_BOOL(Cname, fname, KINDS, what, expr) \
	GIT_GETTER(Cname, fname, KINDS, what, { GitReturnBool(theEnv, returnValue, (expr)); })

#define GIT_GETTER_STATUS(Cname, fname, KINDS, what, expr) \
	GIT_GETTER(Cname, fname, KINDS, what, { GitReturnStatus(theEnv, returnValue, fname, (expr)); })

#define GIT_GETTER_ENUM(Cname, fname, KINDS, what, table, expr) \
	GIT_GETTER(Cname, fname, KINDS, what, { GitReturnEnum(theEnv, returnValue, table, (long long) (expr)); })

/* A wrapper that frees the handle that it receives. It makes every free
   that the caller must still make on the handle, and makes the external
   address empty. Then a second use gives a message. */
static void GitFreeHandle(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  unsigned int kinds,
  const char *what)
{
	UDFValue theArg;
	struct GitHandle *h;
	void *ptr = GitHandleArgument(theEnv, context, returnValue, fname, kinds, what, &theArg);
	char msg[160];

	if (ptr == NULL)
	{
		return;
	}

	h = GitHandleFind(ptr);
	if (h->parent != NULL)
	{
		struct GitHandle *owner = GitHandleFind(h->parent);

		snprintf(msg, sizeof msg, "this %s is borrowed from the %s it came from and is freed with it",
		         what, (owner != NULL) ? GitKindNames[owner->kind] : "handle");
		GitFail(theEnv, returnValue, fname, msg);
		return;
	}

	/* The last free removes the handle from the table and makes the
	   CLIPS value empty. An earlier free keeps both, because the
	   pointer is still valid until the last free. */
	if (h->kind == GIT_K_REPOSITORY)
	{
		GitRepositoryRelease(h);
	}
	else
	{
		GitHandleRelease(h);
	}

	returnValue->lexemeValue = TrueSymbol(theEnv);
}

#define GIT_FREE(Cname, fname, KINDS, what) \
void Cname(Environment *theEnv, UDFContext *context, UDFValue *returnValue) \
{ \
	GitFreeHandle(theEnv, context, returnValue, fname, KINDS, what); \
}

/* A signature argument that can be nil. Nil means the default signature of
   the repository, from user.name and user.email. The signature that returns
   through *owned is one that this call made. The caller must free it. */
static bool GitSignatureOrDefaultArgument(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  git_repository *repo,
  git_signature **out,
  git_signature **owned)
{
	void *sig;
	int error;
	char msg[192];

	*out = NULL;
	*owned = NULL;

	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_SIGNATURE), what, &sig))
	{
		return false;
	}

	if (sig != NULL)
	{
		*out = (git_signature *) sig;
		return true;
	}

	error = git_signature_default(owned, repo);
	if (error != 0)
	{
		const git_error *err = git_error_last();
		snprintf(msg, sizeof msg, "%s is nil and no default signature is configured: %s",
		         what, (err != NULL && err->message != NULL) ? err->message : "set user.name and user.email");
		GitFail(theEnv, returnValue, fname, msg);
		return false;
	}

	*out = *owned;
	return true;
}

/* The repository of an object, to record the handles that come from it. */
#define GitObjectRepo(obj) git_object_owner((const git_object *) (obj))

/***************************************************************************/
/*                                                                         */
/* The library.                                                            */
/*                                                                         */
/***************************************************************************/

void GitLibgit2InitFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	int count = git_libgit2_init();

	if (count < 0)
	{
		GitFailError(theEnv, returnValue, "git-libgit2-init", count);
		return;
	}
	returnValue->integerValue = CreateInteger(theEnv, count);
}

/* First, the call frees all the open handles. libgit2 must not shut down
   while a handle is open. After the shutdown, no handle is usable. */
void GitLibgit2ShutdownFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	int count;

	GitHandlesReleaseEverything();
	GitCredentialReset();
	count = git_libgit2_shutdown();
	if (count < 0)
	{
		GitFailError(theEnv, returnValue, "git-libgit2-shutdown", count);
		return;
	}
	returnValue->integerValue = CreateInteger(theEnv, count);
}

void GitLibgit2VersionFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	int major, minor, rev;
	char text[64];

	git_libgit2_version(&major, &minor, &rev);
	snprintf(text, sizeof text, "%d.%d.%d", major, minor, rev);
	returnValue->lexemeValue = CreateString(theEnv, text);
}

void GitLibgit2VersionNumberFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	int major, minor, rev;

	git_libgit2_version(&major, &minor, &rev);
	returnValue->integerValue = CreateInteger(theEnv, major * 10000 + minor * 100 + rev);
}

void GitBuiltVersionFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	returnValue->integerValue = CreateInteger(theEnv, CLIPSGIT_LIBGIT2_VERSION_NUM);
}

void GitLibgit2PrereleaseFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitReturnText(theEnv, returnValue, git_libgit2_prerelease());
}

void GitLibgit2FeaturesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitReturnFlags(theEnv, returnValue, GitFeatureNames, (unsigned int) git_libgit2_features());
}

void GitHandleCountFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	returnValue->integerValue = CreateInteger(theEnv, (long long) GitHandleCount);
}

/* The kind of each open handle, as a multifield of symbols. Use it to find
   the handles that a program did not free. A borrowed handle shows the kind
   of its owner in parentheses after its own kind. */
void GitHandleKindsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, GitHandleCount);
	unsigned int b;
	struct GitHandle *h;
	char name[96];

	for (b = 0; b < GIT_HANDLE_BUCKETS; b++)
	{
		for (h = GitHandles[b]; h != NULL; h = h->next)
		{
			if (h->parent == NULL)
			{
				snprintf(name, sizeof name, "%s", GitKindNames[h->kind]);
			}
			else
			{
				struct GitHandle *owner = GitHandleFind(h->parent);
				snprintf(name, sizeof name, "%s (%s)", GitKindNames[h->kind],
				         (owner != NULL) ? GitKindNames[owner->kind] : "?");
			}
			MBAppendString(mb, name);
		}
	}

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitErrorLastFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const git_error *err = git_error_last();

	if (err == NULL || err->klass == GIT_ERROR_NONE || err->message == NULL)
	{
		GitReturnNil(theEnv, returnValue);
		return;
	}
	returnValue->lexemeValue = CreateString(theEnv, err->message);
}

void GitErrorLastClassFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const git_error *err = git_error_last();

	GitReturnEnum(theEnv, returnValue, GitErrorClassNames, (err == NULL) ? GIT_ERROR_NONE : err->klass);
}

/***************************************************************************/
/*                                                                         */
/* Repositories.                                                           */
/*                                                                         */
/***************************************************************************/

void GitRepositoryOpenFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-open";
	const char *path;
	git_repository *repo = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_repository_open(&repo, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, repo, GIT_K_REPOSITORY, repo, NULL);
}

void GitRepositoryOpenExtFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-open-ext";
	const char *path;
	const char *ceiling;
	unsigned int flags;
	git_repository *repo = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", true, &path)) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the open flags",
	                              GitRepositoryOpenNames, 0, &flags)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the ceiling directories", &ceiling)) { return; }

	error = git_repository_open_ext(&repo, path, flags, ceiling);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, repo, GIT_K_REPOSITORY, repo, NULL);
}

void GitRepositoryOpenBareFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-open-bare";
	const char *path;
	git_repository *repo = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_repository_open_bare(&repo, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, repo, GIT_K_REPOSITORY, repo, NULL);
}

void GitRepositoryInitFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-init";
	const char *path;
	bool bare;
	git_repository *repo = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "bare", &bare)) { return; }

	error = git_repository_init(&repo, path, bare ? 1 : 0);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, repo, GIT_K_REPOSITORY, repo, NULL);
}

void GitRepositoryInitExtFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-init-ext";
	const char *path;
	unsigned int flags;
	const char *description, *initialHead, *originUrl, *workdir;
	git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
	git_repository *repo = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the init flags",
	                              GitRepositoryInitNames, GIT_REPOSITORY_INIT_MKPATH, &flags)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the initial head", &initialHead)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the description", &description)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the origin url", &originUrl)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the working directory", &workdir)) { return; }

	opts.flags = flags;
	opts.initial_head = initialHead;
	opts.description = description;
	opts.origin_url = originUrl;
	opts.workdir_path = workdir;

	error = git_repository_init_ext(&repo, path, &opts);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, repo, GIT_K_REPOSITORY, repo, NULL);
}

void GitRepositoryDiscoverFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-discover";
	const char *start, *ceiling;
	bool acrossFs;
	git_buf out = GIT_BUF_INIT;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the start path", false, &start)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "across-fs", &acrossFs)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the ceiling directories", &ceiling)) { return; }

	error = git_repository_discover(&out, start, acrossFs ? 1 : 0, ceiling);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

GIT_FREE(GitRepositoryFreeFunction, "git-repository-free", GIT_KM(GIT_K_REPOSITORY), "repository")

GIT_GETTER_TEXT(GitRepositoryPathFunction, "git-repository-path", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_path((git_repository *) h))
GIT_GETTER_TEXT(GitRepositoryWorkdirFunction, "git-repository-workdir", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_workdir((git_repository *) h))
GIT_GETTER_TEXT(GitRepositoryCommondirFunction, "git-repository-commondir", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_commondir((git_repository *) h))
GIT_GETTER_BOOL(GitRepositoryIsBareFunction, "git-repository-is-bare", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_is_bare((git_repository *) h))
GIT_GETTER_BOOL(GitRepositoryIsEmptyFunction, "git-repository-is-empty", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_is_empty((git_repository *) h) == 1)
GIT_GETTER_BOOL(GitRepositoryIsShallowFunction, "git-repository-is-shallow", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_is_shallow((git_repository *) h))
GIT_GETTER_BOOL(GitRepositoryIsWorktreeFunction, "git-repository-is-worktree", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_is_worktree((git_repository *) h))
GIT_GETTER_BOOL(GitRepositoryHeadDetachedFunction, "git-repository-head-detached", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_head_detached((git_repository *) h) == 1)
GIT_GETTER_BOOL(GitRepositoryHeadUnbornFunction, "git-repository-head-unborn", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_head_unborn((git_repository *) h) == 1)
GIT_GETTER_ENUM(GitRepositoryStateFunction, "git-repository-state", GIT_KM(GIT_K_REPOSITORY), "repository",
                GitRepositoryStateNames, git_repository_state((git_repository *) h))
GIT_GETTER_STATUS(GitRepositoryStateCleanupFunction, "git-repository-state-cleanup", GIT_KM(GIT_K_REPOSITORY), "repository",
                  git_repository_state_cleanup((git_repository *) h))
GIT_GETTER_STATUS(GitRepositoryDetachHeadFunction, "git-repository-detach-head", GIT_KM(GIT_K_REPOSITORY), "repository",
                  git_repository_detach_head((git_repository *) h))
GIT_GETTER_STATUS(GitRepositoryMessageRemoveFunction, "git-repository-message-remove", GIT_KM(GIT_K_REPOSITORY), "repository",
                  git_repository_message_remove((git_repository *) h))
GIT_GETTER_TEXT(GitRepositoryGetNamespaceFunction, "git-repository-get-namespace", GIT_KM(GIT_K_REPOSITORY), "repository",
                git_repository_get_namespace((git_repository *) h))

GIT_GETTER(GitRepositoryHeadFunction, "git-repository-head", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_reference *ref = NULL;
	int error = git_repository_head(&ref, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-repository-head", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-repository-head", ref, GIT_K_REFERENCE, (git_repository *) h, NULL);
})

GIT_GETTER(GitRepositoryConfigFunction, "git-repository-config", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_config *cfg = NULL;
	int error = git_repository_config(&cfg, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-repository-config", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-repository-config", cfg, GIT_K_CONFIG, (git_repository *) h, NULL);
})

GIT_GETTER(GitRepositoryConfigSnapshotFunction, "git-repository-config-snapshot", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_config *cfg = NULL;
	int error = git_repository_config_snapshot(&cfg, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-repository-config-snapshot", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-repository-config-snapshot", cfg, GIT_K_CONFIG, (git_repository *) h, NULL);
})

GIT_GETTER(GitRepositoryIndexFunction, "git-repository-index", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_index *index = NULL;
	int error = git_repository_index(&index, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-repository-index", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-repository-index", index, GIT_K_INDEX, (git_repository *) h, NULL);
})

GIT_GETTER(GitRepositoryOdbFunction, "git-repository-odb", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_odb *odb = NULL;
	int error = git_repository_odb(&odb, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-repository-odb", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-repository-odb", odb, GIT_K_ODB, (git_repository *) h, NULL);
})

/* The message that a merge or a revert in progress left, or nil if there is
   no message. */
GIT_GETTER(GitRepositoryMessageFunction, "git-repository-message", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_buf out = GIT_BUF_INIT;
	int error = git_repository_message(&out, (git_repository *) h);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, "git-repository-message", error); return; }
	GitReturnBuf(theEnv, returnValue, &out);
})

/* The identity that the repository uses for reflogs, as a multifield of two
   elements: name and email. An element that is not set is nil. */
GIT_GETTER(GitRepositoryIdentFunction, "git-repository-ident", GIT_KM(GIT_K_REPOSITORY), "repository", {
	const char *name = NULL;
	const char *email = NULL;
	MultifieldBuilder *mb;
	int error = git_repository_ident(&name, &email, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-repository-ident", error); return; }
	mb = CreateMultifieldBuilder(theEnv, 2);
	if (name == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, name); }
	if (email == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, email); }
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
})

void GitRepositorySetIdentFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-set-ident";
	UDFValue theArg;
	git_repository *repo;
	const char *name, *email;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", true, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the email", true, &email)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_repository_set_ident(repo, name, email));
}

void GitRepositorySetHeadFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-set-head";
	UDFValue theArg;
	git_repository *repo;
	const char *refname;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &refname)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_repository_set_head(repo, refname));
}

void GitRepositorySetHeadDetachedFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-set-head-detached";
	UDFValue theArg;
	git_repository *repo;
	git_oid oid;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the commit id", &oid)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_repository_set_head_detached(repo, &oid));
}

void GitRepositorySetWorkdirFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-set-workdir";
	UDFValue theArg;
	git_repository *repo;
	const char *workdir;
	bool update;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the working directory", false, &workdir)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "update-gitlink", &update)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_repository_set_workdir(repo, workdir, update ? 1 : 0));
}

void GitRepositorySetNamespaceFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-set-namespace";
	UDFValue theArg;
	git_repository *repo;
	const char *ns;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the namespace", true, &ns)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_repository_set_namespace(repo, ns));
}

void GitRepositoryItemPathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-item-path";
	UDFValue theArg;
	git_repository *repo;
	long long item;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the item", GitRepositoryItemNames, &item)) { return; }

	error = git_repository_item_path(&out, repo, (git_repository_item_t) item);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

void GitRepositoryHashfileFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-hashfile";
	UDFValue theArg;
	git_repository *repo;
	const char *path, *asPath;
	long long type;
	git_oid oid;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the object type",
	                             GitObjectTypeNames, GIT_OBJECT_BLOB, &type)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the as-path", &asPath)) { return; }

	error = git_repository_hashfile(&oid, repo, path, (git_object_t) type, asPath);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

void GitRepositoryOpenFromWorktreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-repository-open-from-worktree";
	UDFValue theArg;
	git_worktree *wt;
	git_repository *repo = NULL;
	int error;

	if ((wt = GitWorktreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = git_repository_open_from_worktree(&repo, wt);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, repo, GIT_K_REPOSITORY, repo, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Objects.                                                                */
/*                                                                         */
/* An object handle is one of four kinds: commit, tree, blob, or tag. The  */
/* git-object-* functions accept all four kinds. The typed lookups below   */
/* reject an id that names a different kind.                               */
/*                                                                         */
/***************************************************************************/

static void GitLookupObject(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  git_object_t type,
  bool prefix)
{
	UDFValue theArg;
	git_repository *repo;
	git_oid oid;
	size_t length = 0;
	git_object *object = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	if (prefix)
	{
		if (!GitOidPrefixArgument(theEnv, context, returnValue, fname, "the object id", &oid, &length)) { return; }
	}
	else
	{
		if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }
	}

	if (type == GIT_OBJECT_ANY && UDFHasNextArgument(context))
	{
		long long t;
		if (!GitEnumArgument(theEnv, context, returnValue, fname, "the object type", GitObjectTypeNames, &t)) { return; }
		type = (git_object_t) t;
	}

	error = prefix ? git_object_lookup_prefix(&object, repo, &oid, length, type)
	               : git_object_lookup(&object, repo, &oid, type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnObject(theEnv, returnValue, fname, object, repo);
}

void GitObjectLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-object-lookup", GIT_OBJECT_ANY, false);
}

void GitObjectLookupPrefixFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-object-lookup-prefix", GIT_OBJECT_ANY, true);
}

GIT_GETTER_OID(GitObjectIdFunction, "git-object-id", GIT_KM_OBJECT, "object", git_object_id((git_object *) h))
GIT_GETTER_ENUM(GitObjectTypeFunction, "git-object-type", GIT_KM_OBJECT, "object", GitObjectTypeNames,
                git_object_type((git_object *) h))
GIT_FREE(GitObjectFreeFunction, "git-object-free", GIT_KM_OBJECT, "object")

GIT_GETTER(GitObjectShortIdFunction, "git-object-short-id", GIT_KM_OBJECT, "object", {
	git_buf out = GIT_BUF_INIT;
	int error = git_object_short_id(&out, (git_object *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-object-short-id", error); return; }
	GitReturnBuf(theEnv, returnValue, &out);
})

void GitObjectPeelFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-object-peel";
	UDFValue theArg;
	git_object *object, *peeled = NULL;
	long long type;
	int error;

	if ((object = GitObjectArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the target type", GitObjectTypeNames, &type)) { return; }

	error = git_object_peel(&peeled, object, (git_object_t) type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnObject(theEnv, returnValue, fname, peeled, GitObjectRepo(object));
}

void GitObjectType2stringFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	long long type;

	if (!GitEnumArgument(theEnv, context, returnValue, "git-object-type2string", "the object type",
	                     GitObjectTypeNames, &type)) { return; }
	returnValue->lexemeValue = CreateString(theEnv, git_object_type2string((git_object_t) type));
}

void GitObjectString2typeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *text;

	if (!GitTextArgument(theEnv, context, returnValue, "git-object-string2type", "the type name", false, &text)) { return; }
	GitReturnEnum(theEnv, returnValue, GitObjectTypeNames, git_object_string2type(text));
}

void GitObjectRawcontentIsValidFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-object-rawcontent-is-valid";
	const char *content;
	long long type;
	int valid = 0;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the content", false, &content)) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the object type", GitObjectTypeNames, &type)) { return; }

	error = git_object_rawcontent_is_valid(&valid, content, strlen(content), (git_object_t) type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBool(theEnv, returnValue, valid);
}

/***************************************************************************/
/*                                                                         */
/* Signatures.                                                             */
/*                                                                         */
/***************************************************************************/

void GitSignatureNewFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-signature-new";
	const char *name, *email;
	long long time, offset;
	git_signature *sig = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the email", false, &email)) { return; }
	if (!GitIntegerArgument(theEnv, context, returnValue, fname, "the time", &time)) { return; }
	if (!GitIntegerArgument(theEnv, context, returnValue, fname, "the offset in minutes", &offset)) { return; }

	error = git_signature_new(&sig, name, email, (git_time_t) time, (int) offset);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, sig, GIT_K_SIGNATURE, NULL, NULL);
}

void GitSignatureNowFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-signature-now";
	const char *name, *email;
	git_signature *sig = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the email", false, &email)) { return; }

	error = git_signature_now(&sig, name, email);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, sig, GIT_K_SIGNATURE, NULL, NULL);
}

GIT_GETTER(GitSignatureDefaultFunction, "git-signature-default", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_signature *sig = NULL;
	int error = git_signature_default(&sig, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-signature-default", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-signature-default", sig, GIT_K_SIGNATURE, NULL, NULL);
})

void GitSignatureFromBufferFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-signature-from-buffer";
	const char *text;
	git_signature *sig = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the signature text", false, &text)) { return; }

	error = git_signature_from_buffer(&sig, text);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, sig, GIT_K_SIGNATURE, NULL, NULL);
}

GIT_GETTER(GitSignatureDupFunction, "git-signature-dup", GIT_KM(GIT_K_SIGNATURE), "signature", {
	git_signature *copy = NULL;
	int error = git_signature_dup(&copy, (const git_signature *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-signature-dup", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-signature-dup", copy, GIT_K_SIGNATURE, NULL, NULL);
})

GIT_GETTER_TEXT(GitSignatureNameFunction, "git-signature-name", GIT_KM(GIT_K_SIGNATURE), "signature",
                ((git_signature *) h)->name)
GIT_GETTER_TEXT(GitSignatureEmailFunction, "git-signature-email", GIT_KM(GIT_K_SIGNATURE), "signature",
                ((git_signature *) h)->email)
GIT_GETTER_INTEGER(GitSignatureTimeFunction, "git-signature-time", GIT_KM(GIT_K_SIGNATURE), "signature",
                   ((git_signature *) h)->when.time)
GIT_GETTER_INTEGER(GitSignatureOffsetFunction, "git-signature-offset", GIT_KM(GIT_K_SIGNATURE), "signature",
                   ((git_signature *) h)->when.offset)
GIT_FREE(GitSignatureFreeFunction, "git-signature-free", GIT_KM(GIT_K_SIGNATURE), "signature")

/***************************************************************************/
/*                                                                         */
/* Commits.                                                                */
/*                                                                         */
/***************************************************************************/

void GitCommitLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-commit-lookup", GIT_OBJECT_COMMIT, false);
}

void GitCommitLookupPrefixFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-commit-lookup-prefix", GIT_OBJECT_COMMIT, true);
}

#define GIT_KC GIT_KM(GIT_K_COMMIT)

GIT_GETTER_OID(GitCommitIdFunction, "git-commit-id", GIT_KC, "commit", git_commit_id((git_commit *) h))
GIT_GETTER_TEXT(GitCommitMessageFunction, "git-commit-message", GIT_KC, "commit", git_commit_message((git_commit *) h))
GIT_GETTER_TEXT(GitCommitMessageRawFunction, "git-commit-message-raw", GIT_KC, "commit", git_commit_message_raw((git_commit *) h))
GIT_GETTER_TEXT(GitCommitSummaryFunction, "git-commit-summary", GIT_KC, "commit", git_commit_summary((git_commit *) h))
GIT_GETTER_TEXT(GitCommitBodyFunction, "git-commit-body", GIT_KC, "commit", git_commit_body((git_commit *) h))
GIT_GETTER_TEXT(GitCommitMessageEncodingFunction, "git-commit-message-encoding", GIT_KC, "commit", git_commit_message_encoding((git_commit *) h))
GIT_GETTER_TEXT(GitCommitRawHeaderFunction, "git-commit-raw-header", GIT_KC, "commit", git_commit_raw_header((git_commit *) h))
GIT_GETTER_INTEGER(GitCommitTimeFunction, "git-commit-time", GIT_KC, "commit", git_commit_time((git_commit *) h))
GIT_GETTER_INTEGER(GitCommitTimeOffsetFunction, "git-commit-time-offset", GIT_KC, "commit", git_commit_time_offset((git_commit *) h))
GIT_GETTER_INTEGER(GitCommitParentcountFunction, "git-commit-parentcount", GIT_KC, "commit", git_commit_parentcount((git_commit *) h))
GIT_GETTER_OID(GitCommitTreeIdFunction, "git-commit-tree-id", GIT_KC, "commit", git_commit_tree_id((git_commit *) h))
GIT_FREE(GitCommitFreeFunction, "git-commit-free", GIT_KC, "commit")

GIT_GETTER(GitCommitAuthorFunction, "git-commit-author", GIT_KC, "commit", {
	GitReturnBorrowedSignature(theEnv, returnValue, "git-commit-author", git_commit_author((git_commit *) h), h);
})

GIT_GETTER(GitCommitCommitterFunction, "git-commit-committer", GIT_KC, "commit", {
	GitReturnBorrowedSignature(theEnv, returnValue, "git-commit-committer", git_commit_committer((git_commit *) h), h);
})

GIT_GETTER(GitCommitTreeFunction, "git-commit-tree", GIT_KC, "commit", {
	git_tree *tree = NULL;
	int error = git_commit_tree(&tree, (git_commit *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-commit-tree", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-commit-tree", tree, GIT_K_TREE, GitObjectRepo(h), NULL);
})

void GitCommitParentFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-parent";
	UDFValue theArg;
	git_commit *commit, *parent = NULL;
	size_t n;
	int error;

	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the parent number", &n)) { return; }

	error = git_commit_parent(&parent, commit, (unsigned int) n);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, parent, GIT_K_COMMIT, GitObjectRepo(commit), NULL);
}

void GitCommitParentIdFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-parent-id";
	UDFValue theArg;
	git_commit *commit;
	size_t n;
	const git_oid *oid;

	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the parent number", &n)) { return; }

	oid = git_commit_parent_id(commit, (unsigned int) n);
	if (oid == NULL) { GitFail(theEnv, returnValue, fname, "the commit has no parent by that number"); return; }

	GitReturnOid(theEnv, returnValue, oid);
}

void GitCommitNthGenAncestorFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-nth-gen-ancestor";
	UDFValue theArg;
	git_commit *commit, *ancestor = NULL;
	size_t n;
	int error;

	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the generation", &n)) { return; }

	error = git_commit_nth_gen_ancestor(&ancestor, commit, (unsigned int) n);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, ancestor, GIT_K_COMMIT, GitObjectRepo(commit), NULL);
}

void GitCommitHeaderFieldFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-header-field";
	UDFValue theArg;
	git_commit *commit;
	const char *field;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the field", false, &field)) { return; }

	error = git_commit_header_field(&out, commit, field);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

void GitCommitCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-create";
	UDFValue theArg;
	git_repository *repo;
	const char *updateRef, *message;
	git_signature *author, *committer, *ownedAuthor = NULL, *ownedCommitter = NULL;
	git_tree *tree;
	void **parents = NULL;
	size_t parentCount = 0;
	git_oid oid;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference to update", true, &updateRef)) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the author", repo, &author, &ownedAuthor)) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the committer", repo, &committer, &ownedCommitter))
	{
		git_signature_free(ownedAuthor);
		return;
	}
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the message", false, &message) ||
	    (tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL ||
	    !GitHandlesArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_COMMIT), "the parents", &parents, &parentCount))
	{
		git_signature_free(ownedAuthor);
		git_signature_free(ownedCommitter);
		return;
	}

	error = git_commit_create(&oid, repo, updateRef, author, committer, NULL, message, tree,
	                          parentCount, (const git_commit **) parents);
	free(parents);
	git_signature_free(ownedAuthor);
	git_signature_free(ownedCommitter);

	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	GitReturnOid(theEnv, returnValue, &oid);
}

void GitCommitAmendFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-amend";
	UDFValue theArg;
	git_commit *commit;
	const char *updateRef, *message;
	void *author, *committer, *tree;
	git_oid oid;
	int error;

	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference to update", true, &updateRef)) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_SIGNATURE), "the author", &author)) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_SIGNATURE), "the committer", &committer)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the message", true, &message)) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_TREE), "the tree", &tree)) { return; }

	error = git_commit_amend(&oid, commit, updateRef, (git_signature *) author, (git_signature *) committer,
	                         NULL, message, (git_tree *) tree);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

/* The bytes of a commit in the form that libgit2 writes. The call does not
   write the commit. */
void GitCommitCreateBufferFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-create-buffer";
	UDFValue theArg;
	git_repository *repo;
	const char *message;
	git_signature *author, *committer, *ownedAuthor = NULL, *ownedCommitter = NULL;
	git_tree *tree;
	void **parents = NULL;
	size_t parentCount = 0;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the author", repo, &author, &ownedAuthor)) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the committer", repo, &committer, &ownedCommitter))
	{
		git_signature_free(ownedAuthor);
		return;
	}
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the message", false, &message) ||
	    (tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL ||
	    !GitHandlesArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_COMMIT), "the parents", &parents, &parentCount))
	{
		git_signature_free(ownedAuthor);
		git_signature_free(ownedCommitter);
		return;
	}

	error = git_commit_create_buffer(&out, repo, author, committer, NULL, message, tree,
	                                 parentCount, (const git_commit **) parents);
	free(parents);
	git_signature_free(ownedAuthor);
	git_signature_free(ownedCommitter);

	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	GitReturnBuf(theEnv, returnValue, &out);
}

/***************************************************************************/
/*                                                                         */
/* Commits as facts and instances.                                         */
/*                                                                         */
/* These are not libgit2 functions. They write the parts of a commit into  */
/* the slots of a deftemplate or a defclass. Only the slots with these     */
/* names are filled: id, tree, parents, summary, message, body, author-    */
/* name, author-email, author-time, author-offset, committer-name,         */
/* committer-email, committer-time, committer-offset. A template with only */
/* some of these slots gets only those. So one commit template can serve   */
/* many programs.                                                          */
/*                                                                         */
/***************************************************************************/

struct GitCommitParts
{
	const char *id;
	const char *tree;
	const char *summary;
	const char *message;
	const char *body;
	const git_signature *author;
	const git_signature *committer;
	size_t parentCount;
	git_commit *commit;
};

static void GitCommitPartsFill(
  struct GitCommitParts *parts,
  git_commit *commit,
  char *idBuf,
  char *treeBuf)
{
	git_oid_tostr(idBuf, GIT_OID_MAX_HEXSIZE + 1, git_commit_id(commit));
	git_oid_tostr(treeBuf, GIT_OID_MAX_HEXSIZE + 1, git_commit_tree_id(commit));
	parts->id = idBuf;
	parts->tree = treeBuf;
	parts->summary = git_commit_summary(commit);
	parts->message = git_commit_message(commit);
	parts->body = git_commit_body(commit);
	parts->author = git_commit_author(commit);
	parts->committer = git_commit_committer(commit);
	parts->parentCount = git_commit_parentcount(commit);
	parts->commit = commit;
}

static Multifield *GitCommitParentsMultifield(
  Environment *theEnv,
  git_commit *commit)
{
	size_t n = git_commit_parentcount(commit), i;
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, n);
	Multifield *mf;
	char hex[GIT_OID_MAX_HEXSIZE + 1];

	for (i = 0; i < n; i++)
	{
		git_oid_tostr(hex, sizeof hex, git_commit_parent_id(commit, (unsigned int) i));
		MBAppendString(mb, hex);
	}

	mf = MBCreate(mb);
	MBDispose(mb);
	return mf;
}

static const char *GitPutSlotErrorText(
  PutSlotError e)
{
	switch (e)
	{
		case PSE_NO_ERROR:          return "no error";
		case PSE_NULL_POINTER_ERROR: return "a NULL pointer";
		case PSE_INVALID_TARGET_ERROR: return "the target is invalid";
		case PSE_SLOT_NOT_FOUND_ERROR: return "there is no such slot";
		case PSE_TYPE_ERROR:        return "the value is not of a type the slot allows";
		case PSE_RANGE_ERROR:       return "the value is outside the slot's range";
		case PSE_ALLOWED_VALUES_ERROR: return "the value is not one the slot allows";
		case PSE_CARDINALITY_ERROR: return "the value has the wrong cardinality for the slot";
		case PSE_ALLOWED_CLASSES_ERROR: return "the value is not of a class the slot allows";
		case PSE_EVALUATION_ERROR:  return "an evaluation error";
		case PSE_RULE_NETWORK_ERROR: return "an error in the rule network";
	}
	return "an error";
}

/* One slot, if the template has it. If the slot is present but does not
   accept the value, the call is rejected. If the slot is not present, the
   call skips the slot. */
#define GIT_PUT_TEXT_SLOT(exists, put, target, slot, value) \
	if ((exists) && (value) != NULL) { \
		PutSlotError e = put(target, slot, value); \
		if (e != PSE_NO_ERROR) { GitFailSlot(theEnv, returnValue, fname, slot, e); goto fail; } }

#define GIT_PUT_INTEGER_SLOT(exists, put, target, slot, value) \
	if (exists) { \
		PutSlotError e = put(target, slot, (long long) (value)); \
		if (e != PSE_NO_ERROR) { GitFailSlot(theEnv, returnValue, fname, slot, e); goto fail; } }

static void GitFailSlot(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  const char *slot,
  PutSlotError e)
{
	char msg[256];

	snprintf(msg, sizeof msg, "slot %s could not be written: %s", slot, GitPutSlotErrorText(e));
	GitFail(theEnv, returnValue, fname, msg);
}

void GitCommitToFactFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-to-fact";
	UDFValue theArg;
	git_commit *commit;
	const char *templateName;
	Deftemplate *dt;
	FactBuilder *fb = NULL;
	Fact *fact;
	struct GitCommitParts p;
	char idBuf[GIT_OID_MAX_HEXSIZE + 1], treeBuf[GIT_OID_MAX_HEXSIZE + 1];

	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!UDFNextArgument(context, SYMBOL_BIT, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		GitFail(theEnv, returnValue, fname, "expected a symbol for the deftemplate name");
		return;
	}
	templateName = theArg.lexemeValue->contents;

	dt = FindDeftemplate(theEnv, templateName);
	if (dt == NULL) { GitFail(theEnv, returnValue, fname, "there is no deftemplate by that name"); return; }

	fb = CreateFactBuilder(theEnv, templateName);
	if (FBError(theEnv) != FBE_NO_ERROR)
	{
		GitFail(theEnv, returnValue, fname, "the FactBuilder could not be created");
		FBDispose(fb);
		return;
	}

	GitCommitPartsFill(&p, commit, idBuf, treeBuf);

#define S(name) DeftemplateSlotExistP(dt, name)
	GIT_PUT_TEXT_SLOT(S("id"), FBPutSlotString, fb, "id", p.id)
	GIT_PUT_TEXT_SLOT(S("tree"), FBPutSlotString, fb, "tree", p.tree)
	GIT_PUT_TEXT_SLOT(S("summary"), FBPutSlotString, fb, "summary", p.summary)
	GIT_PUT_TEXT_SLOT(S("message"), FBPutSlotString, fb, "message", p.message)
	GIT_PUT_TEXT_SLOT(S("body"), FBPutSlotString, fb, "body", p.body)
	if (p.author != NULL)
	{
		GIT_PUT_TEXT_SLOT(S("author-name"), FBPutSlotString, fb, "author-name", p.author->name)
		GIT_PUT_TEXT_SLOT(S("author-email"), FBPutSlotString, fb, "author-email", p.author->email)
		GIT_PUT_INTEGER_SLOT(S("author-time"), FBPutSlotInteger, fb, "author-time", p.author->when.time)
		GIT_PUT_INTEGER_SLOT(S("author-offset"), FBPutSlotInteger, fb, "author-offset", p.author->when.offset)
	}
	if (p.committer != NULL)
	{
		GIT_PUT_TEXT_SLOT(S("committer-name"), FBPutSlotString, fb, "committer-name", p.committer->name)
		GIT_PUT_TEXT_SLOT(S("committer-email"), FBPutSlotString, fb, "committer-email", p.committer->email)
		GIT_PUT_INTEGER_SLOT(S("committer-time"), FBPutSlotInteger, fb, "committer-time", p.committer->when.time)
		GIT_PUT_INTEGER_SLOT(S("committer-offset"), FBPutSlotInteger, fb, "committer-offset", p.committer->when.offset)
	}
	if (S("parents"))
	{
		PutSlotError e = FBPutSlotMultifield(fb, "parents", GitCommitParentsMultifield(theEnv, commit));
		if (e != PSE_NO_ERROR) { GitFailSlot(theEnv, returnValue, fname, "parents", e); goto fail; }
	}
#undef S

	fact = FBAssert(fb);
	if (fact == NULL)
	{
		GitFail(theEnv, returnValue, fname, "the fact could not be asserted: it may already exist");
		goto fail;
	}

	returnValue->factValue = fact;
	FBDispose(fb);
	return;

fail:
	FBDispose(fb);
}

void GitCommitToInstanceFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-commit-to-instance";
	UDFValue theArg;
	git_commit *commit;
	const char *className, *instanceName = NULL;
	Defclass *dc;
	InstanceBuilder *ib = NULL;
	Instance *instance;
	struct GitCommitParts p;
	char idBuf[GIT_OID_MAX_HEXSIZE + 1], treeBuf[GIT_OID_MAX_HEXSIZE + 1];

	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!UDFNextArgument(context, SYMBOL_BIT, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		GitFail(theEnv, returnValue, fname, "expected a symbol for the defclass name");
		return;
	}
	className = theArg.lexemeValue->contents;
	if (UDFHasNextArgument(context))
	{
		if (!UDFNextArgument(context, SYMBOL_BIT | INSTANCE_NAME_BIT, &theArg) ||
		    (theArg.header->type != SYMBOL_TYPE && theArg.header->type != INSTANCE_NAME_TYPE))
		{
			GitFail(theEnv, returnValue, fname, "expected a symbol for the instance name");
			return;
		}
		if (!(theArg.header->type == SYMBOL_TYPE && strcmp(theArg.lexemeValue->contents, "nil") == 0))
		{
			instanceName = theArg.lexemeValue->contents;
		}
	}

	dc = FindDefclass(theEnv, className);
	if (dc == NULL) { GitFail(theEnv, returnValue, fname, "there is no defclass by that name"); return; }

	ib = CreateInstanceBuilder(theEnv, className);
	if (IBError(theEnv) != IBE_NO_ERROR)
	{
		GitFail(theEnv, returnValue, fname, "the InstanceBuilder could not be created");
		IBDispose(ib);
		return;
	}

	GitCommitPartsFill(&p, commit, idBuf, treeBuf);

#define S(name) SlotExistP(dc, name, true)
	GIT_PUT_TEXT_SLOT(S("id"), IBPutSlotString, ib, "id", p.id)
	GIT_PUT_TEXT_SLOT(S("tree"), IBPutSlotString, ib, "tree", p.tree)
	GIT_PUT_TEXT_SLOT(S("summary"), IBPutSlotString, ib, "summary", p.summary)
	GIT_PUT_TEXT_SLOT(S("message"), IBPutSlotString, ib, "message", p.message)
	GIT_PUT_TEXT_SLOT(S("body"), IBPutSlotString, ib, "body", p.body)
	if (p.author != NULL)
	{
		GIT_PUT_TEXT_SLOT(S("author-name"), IBPutSlotString, ib, "author-name", p.author->name)
		GIT_PUT_TEXT_SLOT(S("author-email"), IBPutSlotString, ib, "author-email", p.author->email)
		GIT_PUT_INTEGER_SLOT(S("author-time"), IBPutSlotInteger, ib, "author-time", p.author->when.time)
		GIT_PUT_INTEGER_SLOT(S("author-offset"), IBPutSlotInteger, ib, "author-offset", p.author->when.offset)
	}
	if (p.committer != NULL)
	{
		GIT_PUT_TEXT_SLOT(S("committer-name"), IBPutSlotString, ib, "committer-name", p.committer->name)
		GIT_PUT_TEXT_SLOT(S("committer-email"), IBPutSlotString, ib, "committer-email", p.committer->email)
		GIT_PUT_INTEGER_SLOT(S("committer-time"), IBPutSlotInteger, ib, "committer-time", p.committer->when.time)
		GIT_PUT_INTEGER_SLOT(S("committer-offset"), IBPutSlotInteger, ib, "committer-offset", p.committer->when.offset)
	}
	if (S("parents"))
	{
		PutSlotError e = IBPutSlotMultifield(ib, "parents", GitCommitParentsMultifield(theEnv, commit));
		if (e != PSE_NO_ERROR) { GitFailSlot(theEnv, returnValue, fname, "parents", e); goto fail; }
	}
#undef S

	instance = IBMake(ib, instanceName);
	if (instance == NULL)
	{
		GitFail(theEnv, returnValue, fname, "the instance could not be created");
		goto fail;
	}

	returnValue->instanceValue = instance;
	IBDispose(ib);
	return;

fail:
	IBDispose(ib);
}

/***************************************************************************/
/*                                                                         */
/* History walks.                                                          */
/*                                                                         */
/***************************************************************************/

GIT_GETTER(GitRevwalkNewFunction, "git-revwalk-new", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_revwalk *walk = NULL;
	int error = git_revwalk_new(&walk, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-revwalk-new", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-revwalk-new", walk, GIT_K_REVWALK, (git_repository *) h, NULL);
})

#define GIT_KW GIT_KM(GIT_K_REVWALK)

GIT_GETTER_STATUS(GitRevwalkPushHeadFunction, "git-revwalk-push-head", GIT_KW, "revwalk", git_revwalk_push_head((git_revwalk *) h))
GIT_GETTER_STATUS(GitRevwalkHideHeadFunction, "git-revwalk-hide-head", GIT_KW, "revwalk", git_revwalk_hide_head((git_revwalk *) h))
GIT_GETTER_STATUS(GitRevwalkResetFunction, "git-revwalk-reset", GIT_KW, "revwalk", git_revwalk_reset((git_revwalk *) h))
GIT_GETTER_STATUS(GitRevwalkSimplifyFirstParentFunction, "git-revwalk-simplify-first-parent", GIT_KW, "revwalk",
                  git_revwalk_simplify_first_parent((git_revwalk *) h))
GIT_FREE(GitRevwalkFreeFunction, "git-revwalk-free", GIT_KW, "revwalk")

/* The next commit id. If the walk is complete, the result is FALSE with no
   message. */
GIT_GETTER(GitRevwalkNextFunction, "git-revwalk-next", GIT_KW, "revwalk", {
	git_oid oid;
	int error = git_revwalk_next(&oid, (git_revwalk *) h);
	if (error == GIT_ITEROVER) { returnValue->lexemeValue = FalseSymbol(theEnv); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, "git-revwalk-next", error); return; }
	GitReturnOid(theEnv, returnValue, &oid);
})

static void GitRevwalkOid(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_revwalk *, const git_oid *))
{
	UDFValue theArg;
	git_revwalk *walk;
	git_oid oid;

	if ((walk = GitRevwalkArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the commit id", &oid)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, fn(walk, &oid));
}

static void GitRevwalkText(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  int (*fn)(git_revwalk *, const char *))
{
	UDFValue theArg;
	git_revwalk *walk;
	const char *text;

	if ((walk = GitRevwalkArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, what, false, &text)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, fn(walk, text));
}

void GitRevwalkPushFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRevwalkOid(theEnv, context, returnValue, "git-revwalk-push", git_revwalk_push); }
void GitRevwalkHideFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRevwalkOid(theEnv, context, returnValue, "git-revwalk-hide", git_revwalk_hide); }
void GitRevwalkPushRefFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRevwalkText(theEnv, context, returnValue, "git-revwalk-push-ref", "the reference name", git_revwalk_push_ref); }
void GitRevwalkHideRefFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRevwalkText(theEnv, context, returnValue, "git-revwalk-hide-ref", "the reference name", git_revwalk_hide_ref); }
void GitRevwalkPushGlobFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRevwalkText(theEnv, context, returnValue, "git-revwalk-push-glob", "the glob", git_revwalk_push_glob); }
void GitRevwalkHideGlobFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRevwalkText(theEnv, context, returnValue, "git-revwalk-hide-glob", "the glob", git_revwalk_hide_glob); }
void GitRevwalkPushRangeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRevwalkText(theEnv, context, returnValue, "git-revwalk-push-range", "the range", git_revwalk_push_range); }

void GitRevwalkSortingFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-revwalk-sorting";
	UDFValue theArg;
	git_revwalk *walk;
	unsigned int flags;

	if ((walk = GitRevwalkArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitFlagsArgument(theEnv, context, returnValue, fname, "the sort flags", GitSortNames, &flags)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_revwalk_sorting(walk, flags));
}

/***************************************************************************/
/*                                                                         */
/* Trees and tree entries.                                                 */
/*                                                                         */
/* A tree entry that you get by index or by name is borrowed from its      */
/* tree. It is released with the tree. A tree entry that you get by path   */
/* belongs to the caller. Release it with git-tree-entry-free. The record  */
/* has the type of each entry.                                             */
/*                                                                         */
/***************************************************************************/

void GitTreeLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-tree-lookup", GIT_OBJECT_TREE, false);
}

void GitTreeLookupPrefixFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-tree-lookup-prefix", GIT_OBJECT_TREE, true);
}

#define GIT_KT GIT_KM(GIT_K_TREE)
#define GIT_KE GIT_KM(GIT_K_TREE_ENTRY)

GIT_GETTER_OID(GitTreeIdFunction, "git-tree-id", GIT_KT, "tree", git_tree_id((git_tree *) h))
GIT_GETTER_INTEGER(GitTreeEntrycountFunction, "git-tree-entrycount", GIT_KT, "tree", git_tree_entrycount((git_tree *) h))
GIT_FREE(GitTreeFreeFunction, "git-tree-free", GIT_KT, "tree")

void GitTreeEntryByindexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tree-entry-byindex";
	UDFValue theArg;
	git_tree *tree;
	size_t idx;
	const git_tree_entry *entry;

	if ((tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the index", &idx)) { return; }

	entry = git_tree_entry_byindex(tree, idx);
	if (entry == NULL) { GitFail(theEnv, returnValue, fname, "the index is out of range"); return; }

	GitReturnHandle(theEnv, returnValue, fname, (void *) entry, GIT_K_TREE_ENTRY, GitObjectRepo(tree), tree);
}

void GitTreeEntryBynameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tree-entry-byname";
	UDFValue theArg;
	git_tree *tree;
	const char *name;
	const git_tree_entry *entry;

	if ((tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }

	entry = git_tree_entry_byname(tree, name);
	if (entry == NULL) { GitFail(theEnv, returnValue, fname, "the tree has no entry by that name"); return; }

	GitReturnHandle(theEnv, returnValue, fname, (void *) entry, GIT_K_TREE_ENTRY, GitObjectRepo(tree), tree);
}

void GitTreeEntryByidFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tree-entry-byid";
	UDFValue theArg;
	git_tree *tree;
	git_oid oid;
	const git_tree_entry *entry;

	if ((tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }

	entry = git_tree_entry_byid(tree, &oid);
	if (entry == NULL) { GitFail(theEnv, returnValue, fname, "the tree has no entry with that id"); return; }

	GitReturnHandle(theEnv, returnValue, fname, (void *) entry, GIT_K_TREE_ENTRY, GitObjectRepo(tree), tree);
}

void GitTreeEntryBypathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tree-entry-bypath";
	UDFValue theArg;
	git_tree *tree;
	const char *path;
	git_tree_entry *entry = NULL;
	int error;

	if ((tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_tree_entry_bypath(&entry, tree, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, entry, GIT_K_TREE_ENTRY, GitObjectRepo(tree), NULL);
}

GIT_GETTER_TEXT(GitTreeEntryNameFunction, "git-tree-entry-name", GIT_KE, "tree entry", git_tree_entry_name((git_tree_entry *) h))
GIT_GETTER_OID(GitTreeEntryIdFunction, "git-tree-entry-id", GIT_KE, "tree entry", git_tree_entry_id((git_tree_entry *) h))
GIT_GETTER_ENUM(GitTreeEntryTypeFunction, "git-tree-entry-type", GIT_KE, "tree entry", GitObjectTypeNames,
                git_tree_entry_type((git_tree_entry *) h))
GIT_GETTER_ENUM(GitTreeEntryFilemodeFunction, "git-tree-entry-filemode", GIT_KE, "tree entry", GitFilemodeNames,
                git_tree_entry_filemode((git_tree_entry *) h))
GIT_GETTER_ENUM(GitTreeEntryFilemodeRawFunction, "git-tree-entry-filemode-raw", GIT_KE, "tree entry", GitFilemodeNames,
                git_tree_entry_filemode_raw((git_tree_entry *) h))
GIT_FREE(GitTreeEntryFreeFunction, "git-tree-entry-free", GIT_KE, "tree entry")

GIT_GETTER(GitTreeEntryDupFunction, "git-tree-entry-dup", GIT_KE, "tree entry", {
	git_tree_entry *copy = NULL;
	struct GitHandle *rec = GitHandleFind(h);
	int error = git_tree_entry_dup(&copy, (const git_tree_entry *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-tree-entry-dup", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-tree-entry-dup", copy, GIT_K_TREE_ENTRY, rec->repo, NULL);
})

void GitTreeEntryToObjectFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tree-entry-to-object";
	UDFValue theArg;
	git_repository *repo;
	git_tree_entry *entry;
	git_object *object = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((entry = GitTreeEntryArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = git_tree_entry_to_object(&object, repo, entry);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnObject(theEnv, returnValue, fname, object, repo);
}

void GitTreeEntryCmpFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tree-entry-cmp";
	UDFValue theArg;
	git_tree_entry *a, *b;

	if ((a = GitTreeEntryArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((b = GitTreeEntryArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	returnValue->integerValue = CreateInteger(theEnv, git_tree_entry_cmp(a, b));
}

/* Every entry of a tree, recursively, as a multifield of paths. A subtree
   has a slash at the end. git_tree_walk takes a callback, and this is the
   one use that a CLIPS program has for it. The mode argument selects pre-
   order (the default) or post-order. */
struct GitTreeWalkState
{
	MultifieldBuilder *mb;
};

static int GitTreeWalkCallback(
  const char *root,
  const git_tree_entry *entry,
  void *payload)
{
	struct GitTreeWalkState *state = (struct GitTreeWalkState *) payload;
	size_t rootLen = strlen(root), nameLen = strlen(git_tree_entry_name(entry));
	bool isTree = (git_tree_entry_type(entry) == GIT_OBJECT_TREE);
	char *path = (char *) malloc(rootLen + nameLen + 2);

	if (path == NULL) { return -1; }
	memcpy(path, root, rootLen);
	memcpy(path + rootLen, git_tree_entry_name(entry), nameLen + 1);
	/* A subtree gets a slash at the end. This shows the difference
	   between a subtree and a file with the same name in the list. */
	if (isTree)
	{
		path[rootLen + nameLen] = '/';
		path[rootLen + nameLen + 1] = '\0';
	}
	MBAppendString(state->mb, path);
	free(path);
	return 0;
}

void GitTreeWalkFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tree-walk";
	UDFValue theArg;
	git_tree *tree;
	long long mode;
	struct GitTreeWalkState state;
	int error;

	if ((tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the walk mode", GitTreewalkModeNames,
	                             GIT_TREEWALK_PRE, &mode)) { return; }

	state.mb = CreateMultifieldBuilder(theEnv, 16);
	error = git_tree_walk(tree, (git_treewalk_mode) mode, GitTreeWalkCallback, &state);
	if (error != 0)
	{
		MBDispose(state.mb);
		GitFailError(theEnv, returnValue, fname, error);
		return;
	}

	returnValue->multifieldValue = MBCreate(state.mb);
	MBDispose(state.mb);
}

/* Treebuilders */

void GitTreebuilderNewFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-treebuilder-new";
	UDFValue theArg;
	git_repository *repo;
	void *source = NULL;
	git_treebuilder *bld = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (UDFHasNextArgument(context) &&
	    !GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_TREE), "the source tree", &source)) { return; }

	error = git_treebuilder_new(&bld, repo, (const git_tree *) source);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, bld, GIT_K_TREEBUILDER, repo, NULL);
}

#define GIT_KB GIT_KM(GIT_K_TREEBUILDER)

GIT_GETTER_STATUS(GitTreebuilderClearFunction, "git-treebuilder-clear", GIT_KB, "treebuilder", git_treebuilder_clear((git_treebuilder *) h))
GIT_GETTER_INTEGER(GitTreebuilderEntrycountFunction, "git-treebuilder-entrycount", GIT_KB, "treebuilder", git_treebuilder_entrycount((git_treebuilder *) h))
GIT_FREE(GitTreebuilderFreeFunction, "git-treebuilder-free", GIT_KB, "treebuilder")

GIT_GETTER(GitTreebuilderWriteFunction, "git-treebuilder-write", GIT_KB, "treebuilder", {
	git_oid oid;
	int error = git_treebuilder_write(&oid, (git_treebuilder *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-treebuilder-write", error); return; }
	GitReturnOid(theEnv, returnValue, &oid);
})

void GitTreebuilderGetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-treebuilder-get";
	UDFValue theArg;
	git_treebuilder *bld;
	const char *name;
	const git_tree_entry *entry;
	struct GitHandle *rec;

	if ((bld = GitTreebuilderArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }

	entry = git_treebuilder_get(bld, name);
	if (entry == NULL) { GitFail(theEnv, returnValue, fname, "the treebuilder has no entry by that name"); return; }

	rec = GitHandleFind(bld);
	GitReturnHandle(theEnv, returnValue, fname, (void *) entry, GIT_K_TREE_ENTRY, rec->repo, bld);
}

void GitTreebuilderInsertFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-treebuilder-insert";
	UDFValue theArg;
	git_treebuilder *bld;
	const char *name;
	git_oid oid;
	long long mode;
	const git_tree_entry *entry = NULL;
	struct GitHandle *rec;
	int error;

	if ((bld = GitTreebuilderArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the filemode", GitFilemodeNames, &mode)) { return; }

	/* An insert replaces an entry that has the same name. The pointer
	   to the old entry then becomes invalid. */
	entry = git_treebuilder_get(bld, name);
	if (entry != NULL)
	{
		struct GitHandle *old = GitHandleFind((void *) entry);
		if (old != NULL) { GitHandleUnlink(old); }
	}

	error = git_treebuilder_insert(&entry, bld, name, &oid, (git_filemode_t) mode);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	rec = GitHandleFind(bld);
	GitReturnHandle(theEnv, returnValue, fname, (void *) entry, GIT_K_TREE_ENTRY, rec->repo, bld);
}

void GitTreebuilderRemoveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-treebuilder-remove";
	UDFValue theArg;
	git_treebuilder *bld;
	const char *name;
	const git_tree_entry *entry;

	if ((bld = GitTreebuilderArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }

	entry = git_treebuilder_get(bld, name);
	if (entry != NULL)
	{
		struct GitHandle *old = GitHandleFind((void *) entry);
		if (old != NULL) { GitHandleUnlink(old); }
	}

	GitReturnStatus(theEnv, returnValue, fname, git_treebuilder_remove(bld, name));
}

/***************************************************************************/
/*                                                                         */
/* Blobs.                                                                  */
/*                                                                         */
/***************************************************************************/

void GitBlobLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-blob-lookup", GIT_OBJECT_BLOB, false);
}

void GitBlobLookupPrefixFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-blob-lookup-prefix", GIT_OBJECT_BLOB, true);
}

#define GIT_KL GIT_KM(GIT_K_BLOB)

GIT_GETTER_OID(GitBlobIdFunction, "git-blob-id", GIT_KL, "blob", git_blob_id((git_blob *) h))
GIT_GETTER_INTEGER(GitBlobRawsizeFunction, "git-blob-rawsize", GIT_KL, "blob", git_blob_rawsize((git_blob *) h))
GIT_GETTER_BOOL(GitBlobIsBinaryFunction, "git-blob-is-binary", GIT_KL, "blob", git_blob_is_binary((git_blob *) h))
GIT_FREE(GitBlobFreeFunction, "git-blob-free", GIT_KL, "blob")

GIT_GETTER(GitBlobRawcontentFunction, "git-blob-rawcontent", GIT_KL, "blob", {
	GitReturnBytes(theEnv, returnValue, "git-blob-rawcontent",
	               git_blob_rawcontent((git_blob *) h), (size_t) git_blob_rawsize((git_blob *) h));
})

void GitBlobFilterFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-blob-filter";
	UDFValue theArg;
	git_blob *blob;
	const char *asPath;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((blob = GitBlobArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the as-path", false, &asPath)) { return; }

	error = git_blob_filter(&out, blob, asPath, NULL);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

void GitBlobCreateFromBufferFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-blob-create-from-buffer";
	UDFValue theArg;
	git_repository *repo;
	const char *content;
	git_oid oid;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the content", false, &content)) { return; }

	error = git_blob_create_from_buffer(&oid, repo, content, strlen(content));
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

static void GitBlobCreateFromPath(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_oid *, git_repository *, const char *))
{
	UDFValue theArg;
	git_repository *repo;
	const char *path;
	git_oid oid;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = fn(&oid, repo, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

void GitBlobCreateFromWorkdirFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitBlobCreateFromPath(theEnv, context, returnValue, "git-blob-create-from-workdir", git_blob_create_from_workdir); }
void GitBlobCreateFromDiskFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitBlobCreateFromPath(theEnv, context, returnValue, "git-blob-create-from-disk", git_blob_create_from_disk); }

/***************************************************************************/
/*                                                                         */
/* Tags.                                                                   */
/*                                                                         */
/***************************************************************************/

void GitTagLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-tag-lookup", GIT_OBJECT_TAG, false);
}

void GitTagLookupPrefixFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	GitLookupObject(theEnv, context, returnValue, "git-tag-lookup-prefix", GIT_OBJECT_TAG, true);
}

#define GIT_KG GIT_KM(GIT_K_TAG)

GIT_GETTER_OID(GitTagIdFunction, "git-tag-id", GIT_KG, "tag", git_tag_id((git_tag *) h))
GIT_GETTER_OID(GitTagTargetIdFunction, "git-tag-target-id", GIT_KG, "tag", git_tag_target_id((git_tag *) h))
GIT_GETTER_ENUM(GitTagTargetTypeFunction, "git-tag-target-type", GIT_KG, "tag", GitObjectTypeNames, git_tag_target_type((git_tag *) h))
GIT_GETTER_TEXT(GitTagNameFunction, "git-tag-name", GIT_KG, "tag", git_tag_name((git_tag *) h))
GIT_GETTER_TEXT(GitTagMessageFunction, "git-tag-message", GIT_KG, "tag", git_tag_message((git_tag *) h))
GIT_FREE(GitTagFreeFunction, "git-tag-free", GIT_KG, "tag")

GIT_GETTER(GitTagTaggerFunction, "git-tag-tagger", GIT_KG, "tag", {
	GitReturnBorrowedSignature(theEnv, returnValue, "git-tag-tagger", git_tag_tagger((git_tag *) h), h);
})

GIT_GETTER(GitTagTargetFunction, "git-tag-target", GIT_KG, "tag", {
	git_object *target = NULL;
	int error = git_tag_target(&target, (git_tag *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-tag-target", error); return; }
	GitReturnObject(theEnv, returnValue, "git-tag-target", target, GitObjectRepo(h));
})

GIT_GETTER(GitTagPeelFunction, "git-tag-peel", GIT_KG, "tag", {
	git_object *target = NULL;
	int error = git_tag_peel(&target, (git_tag *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-tag-peel", error); return; }
	GitReturnObject(theEnv, returnValue, "git-tag-peel", target, GitObjectRepo(h));
})

static void GitTagCreateCommon(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  bool lightweight,
  bool annotation)
{
	UDFValue theArg;
	git_repository *repo;
	const char *name, *message = NULL;
	git_object *target;
	git_signature *tagger = NULL, *owned = NULL;
	bool force;
	git_oid oid;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the tag name", false, &name)) { return; }
	if ((target = GitObjectArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	if (lightweight)
	{
		if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)) { return; }
		error = git_tag_create_lightweight(&oid, repo, name, target, force ? 1 : 0);
	}
	else
	{
		if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the tagger", repo, &tagger, &owned)) { return; }
		if (!GitTextArgument(theEnv, context, returnValue, fname, "the message", false, &message) ||
		    (!annotation && !GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)))
		{
			git_signature_free(owned);
			return;
		}
		error = annotation ? git_tag_annotation_create(&oid, repo, name, target, tagger, message)
		                   : git_tag_create(&oid, repo, name, target, tagger, message, force ? 1 : 0);
		git_signature_free(owned);
	}

	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	GitReturnOid(theEnv, returnValue, &oid);
}

void GitTagCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitTagCreateCommon(theEnv, context, returnValue, "git-tag-create", false, false); }
void GitTagCreateLightweightFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitTagCreateCommon(theEnv, context, returnValue, "git-tag-create-lightweight", true, false); }
void GitTagAnnotationCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitTagCreateCommon(theEnv, context, returnValue, "git-tag-annotation-create", false, true); }

void GitTagDeleteFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tag-delete";
	UDFValue theArg;
	git_repository *repo;
	const char *name;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the tag name", false, &name)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_tag_delete(repo, name));
}

void GitTagListFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-tag-list";
	UDFValue theArg;
	git_repository *repo;
	const char *pattern;
	git_strarray names = { NULL, 0 };
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the pattern", &pattern)) { return; }

	error = (pattern == NULL) ? git_tag_list(&names, repo) : git_tag_list_match(&names, pattern, repo);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnStrarray(theEnv, returnValue, &names);
}

/***************************************************************************/
/*                                                                         */
/* References.                                                             */
/*                                                                         */
/***************************************************************************/

static void GitReferenceByName(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_reference **, git_repository *, const char *))
{
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_reference *ref = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	error = fn(&ref, repo, name);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, ref, GIT_K_REFERENCE, repo, NULL);
}

void GitReferenceLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitReferenceByName(theEnv, context, returnValue, "git-reference-lookup", git_reference_lookup); }
void GitReferenceDwimFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitReferenceByName(theEnv, context, returnValue, "git-reference-dwim", git_reference_dwim); }

void GitReferenceNameToIdFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-name-to-id";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_oid oid;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	error = git_reference_name_to_id(&oid, repo, name);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

#define GIT_KR GIT_KM(GIT_K_REFERENCE)

GIT_GETTER_TEXT(GitReferenceNameFunction, "git-reference-name", GIT_KR, "reference", git_reference_name((git_reference *) h))
GIT_GETTER_TEXT(GitReferenceShorthandFunction, "git-reference-shorthand", GIT_KR, "reference", git_reference_shorthand((git_reference *) h))
GIT_GETTER_TEXT(GitReferenceSymbolicTargetFunction, "git-reference-symbolic-target", GIT_KR, "reference", git_reference_symbolic_target((git_reference *) h))
GIT_GETTER_OID(GitReferenceTargetFunction, "git-reference-target", GIT_KR, "reference", git_reference_target((git_reference *) h))
GIT_GETTER_OID(GitReferenceTargetPeelFunction, "git-reference-target-peel", GIT_KR, "reference", git_reference_target_peel((git_reference *) h))
GIT_GETTER_ENUM(GitReferenceTypeFunction, "git-reference-type", GIT_KR, "reference", GitReferenceTypeNames, git_reference_type((git_reference *) h))
GIT_GETTER_BOOL(GitReferenceIsBranchFunction, "git-reference-is-branch", GIT_KR, "reference", git_reference_is_branch((git_reference *) h))
GIT_GETTER_BOOL(GitReferenceIsRemoteFunction, "git-reference-is-remote", GIT_KR, "reference", git_reference_is_remote((git_reference *) h))
GIT_GETTER_BOOL(GitReferenceIsTagFunction, "git-reference-is-tag", GIT_KR, "reference", git_reference_is_tag((git_reference *) h))
GIT_GETTER_BOOL(GitReferenceIsNoteFunction, "git-reference-is-note", GIT_KR, "reference", git_reference_is_note((git_reference *) h))
GIT_GETTER_STATUS(GitReferenceDeleteFunction, "git-reference-delete", GIT_KR, "reference", git_reference_delete((git_reference *) h))
GIT_FREE(GitReferenceFreeFunction, "git-reference-free", GIT_KR, "reference")

GIT_GETTER(GitReferenceResolveFunction, "git-reference-resolve", GIT_KR, "reference", {
	git_reference *out = NULL;
	int error = git_reference_resolve(&out, (git_reference *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-reference-resolve", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-reference-resolve", out, GIT_K_REFERENCE, git_reference_owner((git_reference *) h), NULL);
})

GIT_GETTER(GitReferenceDupFunction, "git-reference-dup", GIT_KR, "reference", {
	git_reference *out = NULL;
	int error = git_reference_dup(&out, (git_reference *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-reference-dup", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-reference-dup", out, GIT_K_REFERENCE, git_reference_owner((git_reference *) h), NULL);
})

void GitReferencePeelFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-peel";
	UDFValue theArg;
	git_reference *ref;
	long long type;
	git_object *object = NULL;
	int error;

	if ((ref = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the target type", GitObjectTypeNames,
	                             GIT_OBJECT_ANY, &type)) { return; }

	error = git_reference_peel(&object, ref, (git_object_t) type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnObject(theEnv, returnValue, fname, object, git_reference_owner(ref));
}

void GitReferenceCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-create";
	UDFValue theArg;
	git_repository *repo;
	const char *name, *logMessage;
	git_oid oid;
	bool force;
	git_reference *ref = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the target id", &oid)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &logMessage)) { return; }

	error = git_reference_create(&ref, repo, name, &oid, force ? 1 : 0, logMessage);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, ref, GIT_K_REFERENCE, repo, NULL);
}

void GitReferenceSymbolicCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-symbolic-create";
	UDFValue theArg;
	git_repository *repo;
	const char *name, *target, *logMessage;
	bool force;
	git_reference *ref = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the target name", false, &target)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &logMessage)) { return; }

	error = git_reference_symbolic_create(&ref, repo, name, target, force ? 1 : 0, logMessage);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, ref, GIT_K_REFERENCE, repo, NULL);
}

void GitReferenceSetTargetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-set-target";
	UDFValue theArg;
	git_reference *ref, *out = NULL;
	git_oid oid;
	const char *logMessage;
	int error;

	if ((ref = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the target id", &oid)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &logMessage)) { return; }

	error = git_reference_set_target(&out, ref, &oid, logMessage);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_REFERENCE, git_reference_owner(ref), NULL);
}

void GitReferenceSymbolicSetTargetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-symbolic-set-target";
	UDFValue theArg;
	git_reference *ref, *out = NULL;
	const char *target, *logMessage;
	int error;

	if ((ref = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the target name", false, &target)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &logMessage)) { return; }

	error = git_reference_symbolic_set_target(&out, ref, target, logMessage);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_REFERENCE, git_reference_owner(ref), NULL);
}

void GitReferenceRenameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-rename";
	UDFValue theArg;
	git_reference *ref, *out = NULL;
	const char *newName, *logMessage;
	bool force;
	int error;

	if ((ref = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the new name", false, &newName)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the log message", &logMessage)) { return; }

	error = git_reference_rename(&out, ref, newName, force ? 1 : 0, logMessage);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_REFERENCE, git_reference_owner(ref), NULL);
}

void GitReferenceListFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-list";
	UDFValue theArg;
	git_repository *repo;
	git_strarray names = { NULL, 0 };
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = git_reference_list(&names, repo);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnStrarray(theEnv, returnValue, &names);
}

/* The reference names that match a glob, as a multifield.
   git_reference_foreach_glob takes a callback. This is the use that a CLIPS
   program has for it. */
static int GitCollectNameCallback(
  const char *name,
  void *payload)
{
	MBAppendString((MultifieldBuilder *) payload, name);
	return 0;
}

void GitReferenceListGlobFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-list-glob";
	UDFValue theArg;
	git_repository *repo;
	const char *glob;
	MultifieldBuilder *mb;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the glob", false, &glob)) { return; }

	mb = CreateMultifieldBuilder(theEnv, 16);
	error = git_reference_foreach_glob(repo, glob, GitCollectNameCallback, mb);
	if (error != 0) { MBDispose(mb); GitFailError(theEnv, returnValue, fname, error); return; }

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitReferenceRemoveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-remove";
	UDFValue theArg;
	git_repository *repo;
	const char *name;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_reference_remove(repo, name));
}

void GitReferenceHasLogFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-has-log";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	int r;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	r = git_reference_has_log(repo, name);
	if (r < 0) { GitFailError(theEnv, returnValue, fname, r); return; }
	GitReturnBool(theEnv, returnValue, r);
}

void GitReferenceIsValidNameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *name;
	int valid = 0;

	if (!GitTextArgument(theEnv, context, returnValue, "git-reference-is-valid-name", "the reference name", false, &name)) { return; }
	git_reference_name_is_valid(&valid, name);
	GitReturnBool(theEnv, returnValue, valid);
}

void GitReferenceCmpFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reference-cmp";
	UDFValue theArg;
	git_reference *a, *b;

	if ((a = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((b = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	returnValue->integerValue = CreateInteger(theEnv, git_reference_cmp(a, b));
}

/***************************************************************************/
/*                                                                         */
/* Branches.                                                               */
/*                                                                         */
/***************************************************************************/

void GitBranchCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-branch-create";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_commit *target;
	bool force;
	git_reference *ref = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the branch name", false, &name)) { return; }
	if ((target = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)) { return; }

	error = git_branch_create(&ref, repo, name, target, force ? 1 : 0);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, ref, GIT_K_REFERENCE, repo, NULL);
}

void GitBranchCreateFromAnnotatedFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-branch-create-from-annotated";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_annotated_commit *target;
	bool force;
	git_reference *ref = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the branch name", false, &name)) { return; }
	if ((target = GitAnnotatedCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)) { return; }

	error = git_branch_create_from_annotated(&ref, repo, name, target, force ? 1 : 0);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, ref, GIT_K_REFERENCE, repo, NULL);
}

void GitBranchLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-branch-lookup";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	long long type;
	git_reference *ref = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the branch name", false, &name)) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the branch type", GitBranchTypeNames,
	                             GIT_BRANCH_LOCAL, &type)) { return; }

	error = git_branch_lookup(&ref, repo, name, (git_branch_t) type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, ref, GIT_K_REFERENCE, repo, NULL);
}

GIT_GETTER_STATUS(GitBranchDeleteFunction, "git-branch-delete", GIT_KR, "reference", git_branch_delete((git_reference *) h))
GIT_GETTER_BOOL(GitBranchIsHeadFunction, "git-branch-is-head", GIT_KR, "reference", git_branch_is_head((git_reference *) h) == 1)
GIT_GETTER_BOOL(GitBranchIsCheckedOutFunction, "git-branch-is-checked-out", GIT_KR, "reference", git_branch_is_checked_out((git_reference *) h) == 1)

GIT_GETTER(GitBranchNameFunction, "git-branch-name", GIT_KR, "reference", {
	const char *name = NULL;
	int error = git_branch_name(&name, (git_reference *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-branch-name", error); return; }
	GitReturnText(theEnv, returnValue, name);
})

/* The upstream branch, or nil if no upstream is configured. */
GIT_GETTER(GitBranchUpstreamFunction, "git-branch-upstream", GIT_KR, "reference", {
	git_reference *out = NULL;
	int error = git_branch_upstream(&out, (git_reference *) h);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, "git-branch-upstream", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-branch-upstream", out, GIT_K_REFERENCE, git_reference_owner((git_reference *) h), NULL);
})

void GitBranchSetUpstreamFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-branch-set-upstream";
	UDFValue theArg;
	git_reference *ref;
	const char *name;

	if ((ref = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the upstream name", true, &name)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_branch_set_upstream(ref, name));
}

void GitBranchMoveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-branch-move";
	UDFValue theArg;
	git_reference *ref, *out = NULL;
	const char *name;
	bool force;
	int error;

	if ((ref = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the new name", false, &name)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)) { return; }

	error = git_branch_move(&out, ref, name, force ? 1 : 0);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_REFERENCE, git_reference_owner(ref), NULL);
}

static void GitBranchNameQuery(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_buf *, git_repository *, const char *))
{
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	error = fn(&out, repo, name);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

void GitBranchUpstreamNameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitBranchNameQuery(theEnv, context, returnValue, "git-branch-upstream-name", git_branch_upstream_name); }
void GitBranchUpstreamRemoteFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitBranchNameQuery(theEnv, context, returnValue, "git-branch-upstream-remote", git_branch_upstream_remote); }
void GitBranchUpstreamMergeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitBranchNameQuery(theEnv, context, returnValue, "git-branch-upstream-merge", git_branch_upstream_merge); }
void GitBranchRemoteNameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitBranchNameQuery(theEnv, context, returnValue, "git-branch-remote-name", git_branch_remote_name); }

/* Every branch of one kind, as a multifield of names. The iterator API is
   one call here, because a CLIPS program has no useful place to keep an
   iterator handle. */
void GitBranchListFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-branch-list";
	UDFValue theArg;
	git_repository *repo;
	long long type;
	git_branch_iterator *iter = NULL;
	git_reference *ref = NULL;
	git_branch_t outType;
	MultifieldBuilder *mb;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the branch type", GitBranchTypeNames,
	                             GIT_BRANCH_ALL, &type)) { return; }

	error = git_branch_iterator_new(&iter, repo, (git_branch_t) type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	mb = CreateMultifieldBuilder(theEnv, 16);
	while ((error = git_branch_next(&ref, &outType, iter)) == 0)
	{
		const char *name = NULL;
		if (git_branch_name(&name, ref) == 0 && name != NULL)
		{
			MBAppendString(mb, name);
		}
		git_reference_free(ref);
	}
	git_branch_iterator_free(iter);

	if (error != GIT_ITEROVER)
	{
		MBDispose(mb);
		GitFailError(theEnv, returnValue, fname, error);
		return;
	}

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitBranchIsValidNameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *name;
	int valid = 0;

	if (!GitTextArgument(theEnv, context, returnValue, "git-branch-is-valid-name", "the branch name", false, &name)) { return; }
	git_branch_name_is_valid(&valid, name);
	GitReturnBool(theEnv, returnValue, valid);
}

/***************************************************************************/
/*                                                                         */
/* The index.                                                              */
/*                                                                         */
/***************************************************************************/

#define GIT_KI GIT_KM(GIT_K_INDEX)

void GitIndexOpenFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-open";
	const char *path;
	git_index *index = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_index_open(&index, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, index, GIT_K_INDEX, NULL, NULL);
}

void GitIndexNewFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-new";
	git_index *index = NULL;
	int error = git_index_new(&index);

	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	GitReturnHandle(theEnv, returnValue, fname, index, GIT_K_INDEX, NULL, NULL);
}

GIT_GETTER_TEXT(GitIndexPathFunction, "git-index-path", GIT_KI, "index", git_index_path((git_index *) h))
GIT_GETTER_INTEGER(GitIndexEntrycountFunction, "git-index-entrycount", GIT_KI, "index", git_index_entrycount((git_index *) h))
GIT_GETTER_INTEGER(GitIndexVersionFunction, "git-index-version", GIT_KI, "index", git_index_version((git_index *) h))
GIT_GETTER_BOOL(GitIndexHasConflictsFunction, "git-index-has-conflicts", GIT_KI, "index", git_index_has_conflicts((git_index *) h))
GIT_GETTER_STATUS(GitIndexReadFunction, "git-index-read", GIT_KI, "index", git_index_read((git_index *) h, 0))
GIT_GETTER_STATUS(GitIndexWriteFunction, "git-index-write", GIT_KI, "index", git_index_write((git_index *) h))
GIT_GETTER_STATUS(GitIndexClearFunction, "git-index-clear", GIT_KI, "index", git_index_clear((git_index *) h))
GIT_GETTER_STATUS(GitIndexConflictCleanupFunction, "git-index-conflict-cleanup", GIT_KI, "index", git_index_conflict_cleanup((git_index *) h))
GIT_GETTER_OID(GitIndexChecksumFunction, "git-index-checksum", GIT_KI, "index", git_index_checksum((git_index *) h))
GIT_FREE(GitIndexFreeFunction, "git-index-free", GIT_KI, "index")

GIT_GETTER(GitIndexOwnerFunction, "git-index-owner", GIT_KI, "index", {
	git_repository *repo = git_index_owner((git_index *) h);
	struct GitHandle *rec;
	if (repo == NULL) { GitReturnNil(theEnv, returnValue); return; }
	rec = GitHandleFind(repo);
	if (rec == NULL) { GitFail(theEnv, returnValue, "git-index-owner", "the owning repository has no handle"); return; }
	returnValue->externalAddressValue = CreateCExternalAddress(theEnv, repo);
})

GIT_GETTER(GitIndexWriteTreeFunction, "git-index-write-tree", GIT_KI, "index", {
	git_oid oid;
	int error = git_index_write_tree(&oid, (git_index *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-index-write-tree", error); return; }
	GitReturnOid(theEnv, returnValue, &oid);
})

void GitIndexWriteTreeToFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-write-tree-to";
	UDFValue theArg;
	git_index *index;
	git_repository *repo;
	git_oid oid;
	int error;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = git_index_write_tree_to(&oid, index, repo);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

void GitIndexReadTreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-read-tree";
	UDFValue theArg;
	git_index *index;
	git_tree *tree;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_index_read_tree(index, tree));
}

void GitIndexSetVersionFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-set-version";
	UDFValue theArg;
	git_index *index;
	long long version;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitIntegerArgument(theEnv, context, returnValue, fname, "the version", &version)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_index_set_version(index, (unsigned int) version));
}

static void GitIndexPath(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_index *, const char *))
{
	UDFValue theArg;
	git_index *index;
	const char *path;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, fn(index, path));
}

void GitIndexAddBypathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitIndexPath(theEnv, context, returnValue, "git-index-add-bypath", git_index_add_bypath); }
void GitIndexRemoveBypathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitIndexPath(theEnv, context, returnValue, "git-index-remove-bypath", git_index_remove_bypath); }
void GitIndexConflictRemoveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitIndexPath(theEnv, context, returnValue, "git-index-conflict-remove", git_index_conflict_remove); }

void GitIndexRemoveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-remove";
	UDFValue theArg;
	git_index *index;
	const char *path;
	long long stage;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (!GitIntegerArgument(theEnv, context, returnValue, fname, "the stage", &stage)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_index_remove(index, path, (int) stage));
}

void GitIndexRemoveDirectoryFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-remove-directory";
	UDFValue theArg;
	git_index *index;
	const char *path;
	long long stage;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the directory", false, &path)) { return; }
	if (!GitIntegerArgument(theEnv, context, returnValue, fname, "the stage", &stage)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_index_remove_directory(index, path, (int) stage));
}

void GitIndexAddAllFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-add-all";
	UDFValue theArg;
	git_index *index;
	git_strarray pathspec;
	unsigned int flags;
	int error;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitStrarrayArgument(theEnv, context, returnValue, fname, "the pathspec", &pathspec)) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the add flags", GitIndexAddOptionNames,
	                              GIT_INDEX_ADD_DEFAULT, &flags))
	{
		GitStrarrayDispose(&pathspec);
		return;
	}

	error = git_index_add_all(index, &pathspec, flags, NULL, NULL);
	GitStrarrayDispose(&pathspec);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

static void GitIndexPathspec(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_index *, const git_strarray *, git_index_matched_path_cb, void *))
{
	UDFValue theArg;
	git_index *index;
	git_strarray pathspec;
	int error;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitStrarrayArgument(theEnv, context, returnValue, fname, "the pathspec", &pathspec)) { return; }

	error = fn(index, &pathspec, NULL, NULL);
	GitStrarrayDispose(&pathspec);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitIndexRemoveAllFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitIndexPathspec(theEnv, context, returnValue, "git-index-remove-all", git_index_remove_all); }
void GitIndexUpdateAllFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitIndexPathspec(theEnv, context, returnValue, "git-index-update-all", git_index_update_all); }

void GitIndexFindFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-find";
	UDFValue theArg;
	git_index *index;
	const char *path;
	size_t pos = 0;
	int error;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_index_find(&pos, index, path);
	if (error == GIT_ENOTFOUND) { returnValue->lexemeValue = FalseSymbol(theEnv); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	returnValue->integerValue = CreateInteger(theEnv, (long long) pos);
}

/* An index entry as a multifield: path, id, mode, file size, stage. The
   entry is a struct inside the index, and a later change to the index can
   move it. So the entry goes to CLIPS as a value, not as a handle. */
static void GitReturnIndexEntry(
  Environment *theEnv,
  UDFValue *returnValue,
  const git_index_entry *entry)
{
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, 5);
	char hex[GIT_OID_MAX_HEXSIZE + 1];

	git_oid_tostr(hex, sizeof hex, &entry->id);
	MBAppendString(mb, entry->path);
	MBAppendString(mb, hex);
	MBAppendSymbol(mb, GitEnumName(GitFilemodeNames, entry->mode));
	MBAppendInteger(mb, (long long) entry->file_size);
	MBAppendInteger(mb, (long long) git_index_entry_stage(entry));
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitIndexGetByindexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-get-byindex";
	UDFValue theArg;
	git_index *index;
	size_t n;
	const git_index_entry *entry;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the position", &n)) { return; }

	entry = git_index_get_byindex(index, n);
	if (entry == NULL) { GitFail(theEnv, returnValue, fname, "the position is out of range"); return; }

	GitReturnIndexEntry(theEnv, returnValue, entry);
}

void GitIndexGetBypathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-get-bypath";
	UDFValue theArg;
	git_index *index;
	const char *path;
	long long stage;
	const git_index_entry *entry;

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	stage = 0;
	if (UDFHasNextArgument(context) &&
	    !GitIntegerArgument(theEnv, context, returnValue, fname, "the stage", &stage)) { return; }

	entry = git_index_get_bypath(index, path, (int) stage);
	if (entry == NULL) { GitFail(theEnv, returnValue, fname, "the index has no entry for that path"); return; }

	GitReturnIndexEntry(theEnv, returnValue, entry);
}

/* One entry as a fact of the named deftemplate. Only the slots that the
   template has are filled: path, id, mode, size, stage. If the position is
   after the last entry, the result is FALSE. */
void GitIndexEntryToFactFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-index-entry-to-fact";
	UDFValue theArg;
	git_index *index;
	size_t n;
	const git_index_entry *entry;
	const char *templateName;
	Deftemplate *dt;
	FactBuilder *fb;
	Fact *fact;
	char hex[GIT_OID_MAX_HEXSIZE + 1];

	if ((index = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the position", &n)) { return; }
	if (!UDFNextArgument(context, SYMBOL_BIT, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		GitFail(theEnv, returnValue, fname, "expected a symbol for the deftemplate name");
		return;
	}
	templateName = theArg.lexemeValue->contents;

	entry = git_index_get_byindex(index, n);
	if (entry == NULL) { returnValue->lexemeValue = FalseSymbol(theEnv); return; }

	dt = FindDeftemplate(theEnv, templateName);
	if (dt == NULL) { GitFail(theEnv, returnValue, fname, "there is no deftemplate by that name"); return; }

	fb = CreateFactBuilder(theEnv, templateName);
	if (FBError(theEnv) != FBE_NO_ERROR)
	{
		GitFail(theEnv, returnValue, fname, "the FactBuilder could not be created");
		FBDispose(fb);
		return;
	}

	git_oid_tostr(hex, sizeof hex, &entry->id);
#define S(name) DeftemplateSlotExistP(dt, name)
	GIT_PUT_TEXT_SLOT(S("path"), FBPutSlotString, fb, "path", entry->path)
	GIT_PUT_TEXT_SLOT(S("id"), FBPutSlotString, fb, "id", hex)
	GIT_PUT_TEXT_SLOT(S("mode"), FBPutSlotSymbol, fb, "mode", GitEnumName(GitFilemodeNames, entry->mode))
	GIT_PUT_INTEGER_SLOT(S("size"), FBPutSlotInteger, fb, "size", entry->file_size)
	GIT_PUT_INTEGER_SLOT(S("stage"), FBPutSlotInteger, fb, "stage", git_index_entry_stage(entry))
#undef S

	fact = FBAssert(fb);
	if (fact == NULL)
	{
		GitFail(theEnv, returnValue, fname, "the fact could not be asserted: it may already exist");
		goto fail;
	}
	returnValue->factValue = fact;
	FBDispose(fb);
	return;

fail:
	FBDispose(fb);
}

/***************************************************************************/
/*                                                                         */
/* Configuration.                                                          */
/*                                                                         */
/***************************************************************************/

#define GIT_KF GIT_KM(GIT_K_CONFIG)

void GitConfigOpenDefaultFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-open-default";
	git_config *cfg = NULL;
	int error = git_config_open_default(&cfg);

	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	GitReturnHandle(theEnv, returnValue, fname, cfg, GIT_K_CONFIG, NULL, NULL);
}

void GitConfigNewFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-new";
	git_config *cfg = NULL;
	int error = git_config_new(&cfg);

	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	GitReturnHandle(theEnv, returnValue, fname, cfg, GIT_K_CONFIG, NULL, NULL);
}

void GitConfigOpenOndiskFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-open-ondisk";
	const char *path;
	git_config *cfg = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_config_open_ondisk(&cfg, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, cfg, GIT_K_CONFIG, NULL, NULL);
}

void GitConfigOpenLevelFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-open-level";
	UDFValue theArg;
	git_config *parent, *cfg = NULL;
	long long level;
	int error;

	if ((parent = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the level", GitConfigLevelNames, &level)) { return; }

	error = git_config_open_level(&cfg, parent, (git_config_level_t) level);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, cfg, GIT_K_CONFIG, NULL, NULL);
}

void GitConfigAddFileOndiskFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-add-file-ondisk";
	UDFValue theArg;
	git_config *cfg;
	const char *path;
	long long level;
	bool force;
	void *repo = NULL;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the level", GitConfigLevelNames, &level)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force)) { return; }
	if (UDFHasNextArgument(context) &&
	    !GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_REPOSITORY), "the repository", &repo)) { return; }

	GitReturnStatus(theEnv, returnValue, fname,
	                git_config_add_file_ondisk(cfg, path, (git_config_level_t) level, (git_repository *) repo, force ? 1 : 0));
}

GIT_GETTER(GitConfigSnapshotFunction, "git-config-snapshot", GIT_KF, "config", {
	git_config *out = NULL;
	int error = git_config_snapshot(&out, (git_config *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-config-snapshot", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-config-snapshot", out, GIT_K_CONFIG, NULL, NULL);
})

GIT_FREE(GitConfigFreeFunction, "git-config-free", GIT_KF, "config")

static void GitConfigFindPath(
  Environment *theEnv,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_buf *))
{
	git_buf out = GIT_BUF_INIT;
	int error = fn(&out);

	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	GitReturnBuf(theEnv, returnValue, &out);
}

void GitConfigFindGlobalFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitConfigFindPath(theEnv, returnValue, "git-config-find-global", git_config_find_global); }
void GitConfigFindXdgFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitConfigFindPath(theEnv, returnValue, "git-config-find-xdg", git_config_find_xdg); }
void GitConfigFindSystemFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitConfigFindPath(theEnv, returnValue, "git-config-find-system", git_config_find_system); }
void GitConfigFindProgramdataFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitConfigFindPath(theEnv, returnValue, "git-config-find-programdata", git_config_find_programdata); }

/* A value that is not set returns nil, not a rejected call. A program asks
   for a value to find out if it is set. */
void GitConfigGetStringFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-get-string";
	UDFValue theArg;
	git_config *cfg;
	const char *name;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }

	error = git_config_get_string_buf(&out, cfg, name);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

void GitConfigGetPathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-get-path";
	UDFValue theArg;
	git_config *cfg;
	const char *name;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }

	error = git_config_get_path(&out, cfg, name);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

void GitConfigGetIntFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-get-int";
	UDFValue theArg;
	git_config *cfg;
	const char *name;
	int64_t value = 0;
	int error;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }

	error = git_config_get_int64(&value, cfg, name);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	returnValue->integerValue = CreateInteger(theEnv, (long long) value);
}

void GitConfigGetBoolFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-get-bool";
	UDFValue theArg;
	git_config *cfg;
	const char *name;
	int value = 0;
	int error;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }

	error = git_config_get_bool(&value, cfg, name);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBool(theEnv, returnValue, value);
}

/* Every value of a multivar, as a multifield of strings. */
static int GitConfigCollectCallback(
  const git_config_entry *entry,
  void *payload)
{
	MBAppendString((MultifieldBuilder *) payload, entry->value);
	return 0;
}

void GitConfigGetMultivarFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-get-multivar";
	UDFValue theArg;
	git_config *cfg;
	const char *name, *regexp;
	MultifieldBuilder *mb;
	int error;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the regexp", &regexp)) { return; }

	mb = CreateMultifieldBuilder(theEnv, 8);
	error = git_config_get_multivar_foreach(cfg, name, regexp, GitConfigCollectCallback, mb);
	if (error != 0 && error != GIT_ENOTFOUND) { MBDispose(mb); GitFailError(theEnv, returnValue, fname, error); return; }

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/* Every entry, as a multifield of name and value pairs. */
static int GitConfigCollectPairCallback(
  const git_config_entry *entry,
  void *payload)
{
	MBAppendString((MultifieldBuilder *) payload, entry->name);
	MBAppendString((MultifieldBuilder *) payload, entry->value);
	return 0;
}

void GitConfigEntriesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-entries";
	UDFValue theArg;
	git_config *cfg;
	const char *regexp;
	MultifieldBuilder *mb;
	int error;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the regexp", &regexp)) { return; }

	mb = CreateMultifieldBuilder(theEnv, 32);
	error = (regexp == NULL) ? git_config_foreach(cfg, GitConfigCollectPairCallback, mb)
	                         : git_config_foreach_match(cfg, regexp, GitConfigCollectPairCallback, mb);
	if (error != 0) { MBDispose(mb); GitFailError(theEnv, returnValue, fname, error); return; }

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitConfigSetStringFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-set-string";
	UDFValue theArg;
	git_config *cfg;
	const char *name, *value;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the value", false, &value)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_config_set_string(cfg, name, value));
}

void GitConfigSetIntFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-set-int";
	UDFValue theArg;
	git_config *cfg;
	const char *name;
	long long value;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitIntegerArgument(theEnv, context, returnValue, fname, "the value", &value)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_config_set_int64(cfg, name, (int64_t) value));
}

void GitConfigSetBoolFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-set-bool";
	UDFValue theArg;
	git_config *cfg;
	const char *name;
	bool value;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitBooleanArgument(theEnv, context, returnValue, fname, "the value", &value)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_config_set_bool(cfg, name, value ? 1 : 0));
}

void GitConfigSetMultivarFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-set-multivar";
	UDFValue theArg;
	git_config *cfg;
	const char *name, *regexp, *value;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the regexp", false, &regexp)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the value", false, &value)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_config_set_multivar(cfg, name, regexp, value));
}

void GitConfigDeleteEntryFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-delete-entry";
	UDFValue theArg;
	git_config *cfg;
	const char *name;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_config_delete_entry(cfg, name));
}

void GitConfigDeleteMultivarFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-config-delete-multivar";
	UDFValue theArg;
	git_config *cfg;
	const char *name, *regexp;

	if ((cfg = GitConfigArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the regexp", false, &regexp)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_config_delete_multivar(cfg, name, regexp));
}

static void GitConfigParse(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int which)
{
	const char *text;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the value", false, &text)) { return; }

	if (which == 0)
	{
		int out = 0;
		error = git_config_parse_bool(&out, text);
		if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
		GitReturnBool(theEnv, returnValue, out);
	}
	else if (which == 1)
	{
		int64_t out = 0;
		error = git_config_parse_int64(&out, text);
		if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
		returnValue->integerValue = CreateInteger(theEnv, (long long) out);
	}
	else
	{
		git_buf out = GIT_BUF_INIT;
		error = git_config_parse_path(&out, text);
		if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
		GitReturnBuf(theEnv, returnValue, &out);
	}
}

void GitConfigParseBoolFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitConfigParse(theEnv, context, returnValue, "git-config-parse-bool", 0); }
void GitConfigParseIntFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitConfigParse(theEnv, context, returnValue, "git-config-parse-int", 1); }
void GitConfigParsePathFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitConfigParse(theEnv, context, returnValue, "git-config-parse-path", 2); }

/***************************************************************************/
/*                                                                         */
/* Credentials.                                                            */
/*                                                                         */
/* A remote that needs a credential asks for it through a libgit2          */
/* callback. This library installs one callback on each call that can use  */
/* the network. The callback gets the credential from one of two sources:  */
/*                                                                         */
/*   1. The hook. A CLIPS function that the program names with             */
/*      git-credential-hook. The hook selects the kind of credential and   */
/*      the username. It also names the logical name that the secret is    */
/*      read from. The hook does not carry the secret.                     */
/*   2. The prompt. The same prompt that git shows. The username is read   */
/*      from the logical name stdin. The secret is read from the logical   */
/*      name git-secret, which does not echo on a terminal.                */
/*                                                                         */
/* This library stores no secret. The secret is read into a buffer on the  */
/* stack, given to libgit2, and set to zero. It is not made into a CLIPS   */
/* value. This is important: CLIPS keeps each string in its symbol table   */
/* until garbage collection. libgit2 sets a password and a passphrase to   */
/* zero when it frees the credential. So libgit2 has the only copy.        */
/*                                                                         */
/* The router "gitsecret" serves the logical name git-secret. It reads     */
/* from the logical name stdin, so a batch file or a different stdin       */
/* router continues to work. If stdin is a terminal, it sets the echo flag */
/* of the terminal off for one line. It sets the flag on again at the end  */
/* of the line, at the end of the file, at a halt, and at exit.            */
/*                                                                         */
/***************************************************************************/

#define GIT_SECRET_LOGICAL_NAME "git-secret"
#define GIT_SECRET_ROUTER_NAME "gitsecret"
#define GIT_SECRET_LINE_MAX 4096
#define GIT_CREDENTIAL_NAME_MAX 512
#define GIT_CREDENTIAL_PATH_MAX 1024
#define GIT_CREDENTIAL_MAX_ATTEMPTS 3

enum GitPromptMode
{
	GIT_PROMPT_AUTO,
	GIT_PROMPT_ALWAYS,
	GIT_PROMPT_NEVER
};

static const struct GitEnumEntry GitPromptModeNames[] = {
	{ "auto",   GIT_PROMPT_AUTO },
	{ "always", GIT_PROMPT_ALWAYS },
	{ "never",  GIT_PROMPT_NEVER },
	{ NULL, 0 }
};

static const struct GitEnumEntry GitCredentialTypeNames[] = {
	{ "GIT_CREDENTIAL_USERPASS_PLAINTEXT", GIT_CREDENTIAL_USERPASS_PLAINTEXT },
	{ "GIT_CREDENTIAL_SSH_KEY",            GIT_CREDENTIAL_SSH_KEY },
	{ "GIT_CREDENTIAL_SSH_CUSTOM",         GIT_CREDENTIAL_SSH_CUSTOM },
	{ "GIT_CREDENTIAL_DEFAULT",            GIT_CREDENTIAL_DEFAULT },
	{ "GIT_CREDENTIAL_SSH_INTERACTIVE",    GIT_CREDENTIAL_SSH_INTERACTIVE },
	{ "GIT_CREDENTIAL_USERNAME",           GIT_CREDENTIAL_USERNAME },
	{ "GIT_CREDENTIAL_SSH_MEMORY",         GIT_CREDENTIAL_SSH_MEMORY },
	{ NULL, 0 }
};

/* The name of the hook function, or NULL. The name is a copy on the heap,
   because CLIPS can collect the symbol. */
static char *GitCredentialHook = NULL;
static enum GitPromptMode GitPromptMode = GIT_PROMPT_AUTO;

struct GitCredentialPayload
{
	Environment *env;
	int attempts;
};

/* Sets a buffer to zero. The volatile pointer prevents the compiler from
   removing the writes to a buffer that is not read again. */
static void GitWipe(
  void *p,
  size_t n)
{
	volatile unsigned char *v = (volatile unsigned char *) p;

	while (n-- > 0)
	{
		*v++ = 0;
	}
}

/* --- The terminal ------------------------------------------------------ */

#ifdef _WIN32
#include <io.h>
#include <windows.h>

static struct
{
	bool lineOpen;
	bool echoOff;
	DWORD saved;
} GitSecret;

static bool GitSecretIsTerminal(void)
{
	return _isatty(_fileno(stdin)) != 0;
}

static void GitSecretEchoOff(void)
{
	HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode;

	if (GitSecret.echoOff || !GitSecretIsTerminal()) { return; }
	if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) { return; }
	GitSecret.saved = mode;
	if (SetConsoleMode(h, mode & ~ENABLE_ECHO_INPUT)) { GitSecret.echoOff = true; }
}

static void GitSecretEchoOn(void)
{
	if (!GitSecret.echoOff) { return; }
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), GitSecret.saved);
	GitSecret.echoOff = false;
}

#else
#include <termios.h>
#include <unistd.h>

static struct
{
	bool lineOpen;
	bool echoOff;
	struct termios saved;
} GitSecret;

static bool GitSecretIsTerminal(void)
{
	return isatty(STDIN_FILENO) != 0;
}

static void GitSecretEchoOff(void)
{
	struct termios t;

	if (GitSecret.echoOff || !GitSecretIsTerminal()) { return; }
	if (tcgetattr(STDIN_FILENO, &GitSecret.saved) != 0) { return; }
	t = GitSecret.saved;
	t.c_lflag &= (tcflag_t) ~ECHO;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) == 0) { GitSecret.echoOff = true; }
}

static void GitSecretEchoOn(void)
{
	if (!GitSecret.echoOff) { return; }
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &GitSecret.saved);
	GitSecret.echoOff = false;
}
#endif

/* --- The git-secret router --------------------------------------------- */

/* The end of a line. The terminal shows nothing while echo is off, not
   even the Enter. So a newline is written when echo was off. */
static void GitSecretLineDone(
  Environment *theEnv)
{
	bool wasOff = GitSecret.echoOff;

	GitSecretEchoOn();
	GitSecret.lineOpen = false;
	if (wasOff)
	{
		WriteString(theEnv, STDOUT, "\n");
	}
}

static bool GitSecretQuery(
  Environment *theEnv,
  const char *logicalName,
  void *context)
{
	return strcmp(logicalName, GIT_SECRET_LOGICAL_NAME) == 0;
}

static int GitSecretRead(
  Environment *theEnv,
  const char *logicalName,
  void *context)
{
	int ch;

	if (!GitSecret.lineOpen)
	{
		GitSecret.lineOpen = true;
		GitSecretEchoOff();
	}

	if (GetHaltExecution(theEnv))
	{
		GitSecretLineDone(theEnv);
		return EOF;
	}

	ch = ReadRouter(theEnv, STDIN);
	if (ch == '\n' || ch == EOF)
	{
		GitSecretLineDone(theEnv);
	}

	return ch;
}

static int GitSecretUnread(
  Environment *theEnv,
  const char *logicalName,
  int ch,
  void *context)
{
	return UnreadRouter(theEnv, STDIN, ch);
}

static void GitSecretExit(
  Environment *theEnv,
  int code,
  void *context)
{
	GitSecretEchoOn();
}

/* --- Reading a line ---------------------------------------------------- */

/* Reads one line from a logical name into a buffer, without the newline.
   The result is false at the end of the file with nothing read, and false
   if the logical name is not known to the routers. A line that is longer
   than the buffer is cut. The caller sets the buffer to zero after use. */
static bool GitReadLine(
  Environment *theEnv,
  const char *logicalName,
  char *buf,
  size_t size)
{
	size_t n = 0;
	int ch;

	for (;;)
	{
		ch = ReadRouter(theEnv, logicalName);
		if (ch == EOF || ch == '\n') { break; }
		if (ch == '\r') { continue; }
		if (n + 1 < size) { buf[n++] = (char) ch; }
	}
	buf[n] = '\0';

	return !(ch == EOF && n == 0);
}

/* The part of a URL before the path: "https://github.com". This is the
   text of the prompt. A URL without "://" is shown whole. */
static void GitUrlOrigin(
  const char *url,
  char *buf,
  size_t size)
{
	const char *sep = strstr(url, "://");
	const char *end;
	size_t n;

	end = (sep == NULL) ? NULL : strchr(sep + 3, '/');
	n = (end == NULL) ? strlen(url) : (size_t) (end - url);
	if (n >= size) { n = size - 1; }
	memcpy(buf, url, n);
	buf[n] = '\0';
}

/* --- Building a credential --------------------------------------------- */

/* A rejection. The message goes to libgit2. The wrapper that made the call
   reports it, with the name of the wrapper in front. */
static int GitCredentialRefuse(
  const char *msg)
{
	git_error_set_str(GIT_ERROR_CALLBACK, msg);
	return GIT_EUSER;
}

/* A username and a secret. The secret is one line from a logical name. */
static int GitCredentialUserpass(
  git_credential **out,
  Environment *theEnv,
  const char *user,
  const char *from)
{
	char secret[GIT_SECRET_LINE_MAX];
	int error;

	if (!GitReadLine(theEnv, from, secret, sizeof secret))
	{
		GitWipe(secret, sizeof secret);
		return GitCredentialRefuse("no secret could be read for the credential");
	}

	error = git_credential_userpass_plaintext_new(out, user, secret);
	GitWipe(secret, sizeof secret);
	return error;
}

/* A key pair on disk. The passphrase, if the hook named a logical name for
   it, is one line from that name. */
static int GitCredentialSshKey(
  git_credential **out,
  Environment *theEnv,
  const char *user,
  const char *publicKey,
  const char *privateKey,
  const char *from)
{
	char passphrase[GIT_SECRET_LINE_MAX];
	int error;

	if (from == NULL)
	{
		return git_credential_ssh_key_new(out, user, publicKey, privateKey, NULL);
	}

	if (!GitReadLine(theEnv, from, passphrase, sizeof passphrase))
	{
		GitWipe(passphrase, sizeof passphrase);
		return GitCredentialRefuse("no passphrase could be read for the ssh key");
	}

	error = git_credential_ssh_key_new(out, user, publicKey, privateKey, passphrase);
	GitWipe(passphrase, sizeof passphrase);
	return error;
}

static bool GitPromptAllowed(void)
{
	const char *setting;

	switch (GitPromptMode)
	{
		case GIT_PROMPT_ALWAYS: return true;
		case GIT_PROMPT_NEVER:  return false;
		default: break;
	}

	setting = getenv("GIT_TERMINAL_PROMPT");
	if (setting != NULL && strcmp(setting, "0") == 0) { return false; }

	return GitSecretIsTerminal();
}

/* The prompt that git shows: the username, unless the URL has one, then
   the password. The remote must accept a username and a password. A remote
   that accepts only a username gets that. */
static int GitCredentialPrompt(
  git_credential **out,
  Environment *theEnv,
  const char *url,
  const char *userFromUrl,
  unsigned int allowed)
{
	char origin[GIT_CREDENTIAL_PATH_MAX];
	char user[GIT_CREDENTIAL_NAME_MAX];
	char line[GIT_CREDENTIAL_PATH_MAX + GIT_CREDENTIAL_NAME_MAX + 32];
	const char *scheme;

	if ((allowed & (GIT_CREDENTIAL_USERPASS_PLAINTEXT | GIT_CREDENTIAL_USERNAME)) == 0)
	{
		return GitCredentialRefuse("the remote accepts no credential that a prompt can give; name a hook with git-credential-hook");
	}

	GitUrlOrigin(url, origin, sizeof origin);

	if (userFromUrl != NULL && userFromUrl[0] != '\0')
	{
		snprintf(user, sizeof user, "%s", userFromUrl);
	}
	else
	{
		snprintf(line, sizeof line, "Username for '%s': ", origin);
		WriteString(theEnv, STDOUT, line);
		if (!GitReadLine(theEnv, STDIN, user, sizeof user) || user[0] == '\0')
		{
			return GitCredentialRefuse("no username was given at the prompt");
		}
	}

	if ((allowed & GIT_CREDENTIAL_USERPASS_PLAINTEXT) == 0)
	{
		return git_credential_username_new(out, user);
	}

	/* "https://alice@github.com", as git shows it. */
	scheme = strstr(origin, "://");
	if (scheme != NULL)
	{
		snprintf(line, sizeof line, "Password for '%.*s%s@%s': ",
		         (int) (scheme + 3 - origin), origin, user, scheme + 3);
	}
	else
	{
		snprintf(line, sizeof line, "Password for '%s@%s': ", user, origin);
	}
	WriteString(theEnv, STDOUT, line);

	return GitCredentialUserpass(out, theEnv, user, GIT_SECRET_LOGICAL_NAME);
}

/* One element of the answer of the hook as text, or NULL if the element is
   not present or is not a symbol or a string. */
static const char *GitReplyText(
  Multifield *reply,
  size_t i)
{
	CLIPSValue *v;

	if (i >= reply->length) { return NULL; }
	v = &reply->contents[i];
	if (v->header->type != SYMBOL_TYPE && v->header->type != STRING_TYPE) { return NULL; }
	return v->lexemeValue->contents;
}

/* Copies text that belongs to CLIPS into a buffer of the caller. The text
   is valid only while the answer of the hook is valid. libgit2 gets a copy
   that does not depend on that answer. */
static bool GitReplyCopy(
  const char *text,
  char *buf,
  size_t size)
{
	if (text == NULL || strlen(text) >= size) { return false; }
	memcpy(buf, text, strlen(text) + 1);
	return true;
}

/* Calls the hook and builds the credential that it selects. */
static int GitCredentialFromHook(
  git_credential **out,
  Environment *theEnv,
  const char *url,
  const char *userFromUrl,
  unsigned int allowed,
  bool *wantsPrompt)
{
	FunctionCallBuilder *fcb;
	FunctionCallBuilderError fe;
	UDFValue kinds;
	CLIPSValue reply;
	Multifield *mf;
	const char *tag;
	char user[GIT_CREDENTIAL_NAME_MAX];
	char from[GIT_CREDENTIAL_NAME_MAX];
	char publicKey[GIT_CREDENTIAL_PATH_MAX];
	char privateKey[GIT_CREDENTIAL_PATH_MAX];
	char msg[256];

	*wantsPrompt = false;

	fcb = CreateFunctionCallBuilder(theEnv, 3);
	FCBAppendString(fcb, url);
	if (userFromUrl != NULL && userFromUrl[0] != '\0')
	{
		FCBAppendString(fcb, userFromUrl);
	}
	else
	{
		FCBAppendSymbol(fcb, "nil");
	}
	GitReturnFlags(theEnv, &kinds, GitCredentialTypeNames, allowed);
	FCBAppendMultifield(fcb, kinds.multifieldValue);

	fe = FCBCall(fcb, GitCredentialHook, &reply);
	FCBDispose(fcb);

	switch (fe)
	{
		case FCBE_NO_ERROR:
			break;
		case FCBE_FUNCTION_NOT_FOUND_ERROR:
			snprintf(msg, sizeof msg, "the credential hook %s is not a function", GitCredentialHook);
			return GitCredentialRefuse(msg);
		case FCBE_ARGUMENT_COUNT_ERROR:
		case FCBE_ARGUMENT_TYPE_ERROR:
			snprintf(msg, sizeof msg, "the credential hook %s must take a url, a username, and a multifield", GitCredentialHook);
			return GitCredentialRefuse(msg);
		default:
			snprintf(msg, sizeof msg, "the credential hook %s could not be called", GitCredentialHook);
			return GitCredentialRefuse(msg);
	}

	if (GetHaltExecution(theEnv) || GetEvaluationError(theEnv))
	{
		snprintf(msg, sizeof msg, "the credential hook %s halted", GitCredentialHook);
		return GitCredentialRefuse(msg);
	}

	if (reply.header->type != MULTIFIELD_TYPE)
	{
		if (reply.header->type == SYMBOL_TYPE && reply.lexemeValue == FalseSymbol(theEnv))
		{
			snprintf(msg, sizeof msg, "the credential hook %s refused", GitCredentialHook);
			return GitCredentialRefuse(msg);
		}
		snprintf(msg, sizeof msg, "the credential hook %s did not answer a multifield or FALSE", GitCredentialHook);
		return GitCredentialRefuse(msg);
	}

	mf = reply.multifieldValue;
	tag = GitReplyText(mf, 0);
	if (tag == NULL)
	{
		snprintf(msg, sizeof msg, "the credential hook %s did not answer a tag", GitCredentialHook);
		return GitCredentialRefuse(msg);
	}

	if (strcmp(tag, "prompt") == 0)
	{
		*wantsPrompt = true;
		return 0;
	}

	if (strcmp(tag, "userpass") == 0)
	{
		if ((allowed & GIT_CREDENTIAL_USERPASS_PLAINTEXT) == 0)
		{
			return GitCredentialRefuse("the remote does not accept a username and a password");
		}
		if (!GitReplyCopy(GitReplyText(mf, 1), user, sizeof user))
		{
			return GitCredentialRefuse("the credential hook answered userpass without a username");
		}
		if (mf->length < 3)
		{
			snprintf(from, sizeof from, "%s", GIT_SECRET_LOGICAL_NAME);
		}
		else if (!GitReplyCopy(GitReplyText(mf, 2), from, sizeof from))
		{
			return GitCredentialRefuse("the credential hook answered userpass with a bad logical name");
		}
		return GitCredentialUserpass(out, theEnv, user, from);
	}

	if (strcmp(tag, "username") == 0)
	{
		if ((allowed & GIT_CREDENTIAL_USERNAME) == 0)
		{
			return GitCredentialRefuse("the remote does not ask for a username on its own");
		}
		if (!GitReplyCopy(GitReplyText(mf, 1), user, sizeof user))
		{
			return GitCredentialRefuse("the credential hook answered username without a username");
		}
		return git_credential_username_new(out, user);
	}

	if (strcmp(tag, "ssh-agent") == 0)
	{
		if ((allowed & GIT_CREDENTIAL_SSH_KEY) == 0)
		{
			return GitCredentialRefuse("the remote does not accept an ssh key");
		}
		if (!GitReplyCopy(GitReplyText(mf, 1), user, sizeof user))
		{
			return GitCredentialRefuse("the credential hook answered ssh-agent without a username");
		}
		return git_credential_ssh_key_from_agent(out, user);
	}

	if (strcmp(tag, "ssh-key") == 0)
	{
		if ((allowed & GIT_CREDENTIAL_SSH_KEY) == 0)
		{
			return GitCredentialRefuse("the remote does not accept an ssh key");
		}
		if (!GitReplyCopy(GitReplyText(mf, 1), user, sizeof user) ||
		    !GitReplyCopy(GitReplyText(mf, 2), publicKey, sizeof publicKey) ||
		    !GitReplyCopy(GitReplyText(mf, 3), privateKey, sizeof privateKey))
		{
			return GitCredentialRefuse("the credential hook answered ssh-key without a username and two key paths");
		}
		if (mf->length < 5)
		{
			return GitCredentialSshKey(out, theEnv, user, publicKey, privateKey, NULL);
		}
		if (!GitReplyCopy(GitReplyText(mf, 4), from, sizeof from))
		{
			return GitCredentialRefuse("the credential hook answered ssh-key with a bad logical name");
		}
		return GitCredentialSshKey(out, theEnv, user, publicKey, privateKey, from);
	}

	snprintf(msg, sizeof msg, "the credential hook %s answered the tag %s, which this library does not know", GitCredentialHook, tag);
	return GitCredentialRefuse(msg);
}

/* The callback that libgit2 calls. libgit2 calls it again after the remote
   rejects a credential. After three credentials, the callback rejects the
   request. */
static int GitCredentialCallback(
  git_credential **out,
  const char *url,
  const char *userFromUrl,
  unsigned int allowed,
  void *payload)
{
	struct GitCredentialPayload *p = (struct GitCredentialPayload *) payload;
	Environment *theEnv = p->env;
	bool wantsPrompt = false;
	char msg[GIT_CREDENTIAL_PATH_MAX + 256];
	char origin[GIT_CREDENTIAL_PATH_MAX];
	int error;

	if (++p->attempts > GIT_CREDENTIAL_MAX_ATTEMPTS)
	{
		return GitCredentialRefuse("the remote rejected the credential three times");
	}

	if (GitCredentialHook != NULL)
	{
		error = GitCredentialFromHook(out, theEnv, url, userFromUrl, allowed, &wantsPrompt);
		if (!wantsPrompt) { return error; }
	}

	if (GitPromptAllowed())
	{
		return GitCredentialPrompt(out, theEnv, url, userFromUrl, allowed);
	}

	GitUrlOrigin(url, origin, sizeof origin);
	snprintf(msg, sizeof msg, "%s needs a credential and no prompt is permitted; name a hook with git-credential-hook, or permit a prompt with git-credential-prompt", origin);
	return GitCredentialRefuse(msg);
}

/* Fills the callbacks of a network call. The payload is on the stack of
   the wrapper that makes the call, so it is valid for the duration of the
   call. */
static void GitRemoteCallbacksInit(
  git_remote_callbacks *callbacks,
  struct GitCredentialPayload *payload,
  Environment *theEnv)
{
	git_remote_callbacks init = GIT_REMOTE_CALLBACKS_INIT;

	*callbacks = init;
	payload->env = theEnv;
	payload->attempts = 0;
	callbacks->credentials = GitCredentialCallback;
	callbacks->payload = payload;
}

/* --- The functions ----------------------------------------------------- */

static void GitCredentialReset(void)
{
	free(GitCredentialHook);
	GitCredentialHook = NULL;
	GitPromptMode = GIT_PROMPT_AUTO;
}

/* With an argument: names the hook, or clears it with nil. Without one:
   answers the name, or nil. */
void GitCredentialHookFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-credential-hook";
	UDFValue theArg;
	const char *name;

	if (!UDFHasNextArgument(context))
	{
		returnValue->lexemeValue = CreateSymbol(theEnv, (GitCredentialHook == NULL) ? "nil" : GitCredentialHook);
		return;
	}

	if (!UDFNextArgument(context, SYMBOL_BIT, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		GitFail(theEnv, returnValue, fname, "expected a symbol for the name of the hook function, or nil");
		return;
	}

	name = theArg.lexemeValue->contents;
	free(GitCredentialHook);
	GitCredentialHook = NULL;
	if (strcmp(name, "nil") != 0)
	{
		GitCredentialHook = malloc(strlen(name) + 1);
		if (GitCredentialHook == NULL)
		{
			GitFail(theEnv, returnValue, fname, "out of memory");
			return;
		}
		memcpy(GitCredentialHook, name, strlen(name) + 1);
	}

	returnValue->lexemeValue = TrueSymbol(theEnv);
}

/* With an argument: sets the mode, one of auto, always, and never. Without
   one: answers the mode. */
void GitCredentialPromptFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-credential-prompt";
	UDFValue theArg;
	int i;

	if (!UDFHasNextArgument(context))
	{
		GitReturnEnum(theEnv, returnValue, GitPromptModeNames, GitPromptMode);
		return;
	}

	if (UDFNextArgument(context, SYMBOL_BIT, &theArg) && theArg.header->type == SYMBOL_TYPE)
	{
		for (i = 0; GitPromptModeNames[i].name != NULL; i++)
		{
			if (strcmp(GitPromptModeNames[i].name, theArg.lexemeValue->contents) == 0)
			{
				GitPromptMode = (enum GitPromptMode) GitPromptModeNames[i].value;
				returnValue->lexemeValue = TrueSymbol(theEnv);
				return;
			}
		}
	}

	GitFail(theEnv, returnValue, fname, "the prompt mode is one of the symbols auto, always, and never");
}

/* Asks for a credential as a fetch would, and answers the tag and the
   username of the credential that the sources gave. The secret is not
   answered. A program uses this to check its hook before it goes to the
   network. */
void GitCredentialAskFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-credential-ask";
	const char *url, *user;
	unsigned int allowed;
	struct GitCredentialPayload payload;
	git_credential *cred = NULL;
	MultifieldBuilder *mb;
	const char *tag;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the url", false, &url)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the username", &user)) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the kinds the remote accepts",
	                              GitCredentialTypeNames, GIT_CREDENTIAL_USERPASS_PLAINTEXT, &allowed)) { return; }

	payload.env = theEnv;
	payload.attempts = 0;
	error = GitCredentialCallback(&cred, url, user, allowed, &payload);
	if (error != 0 || cred == NULL)
	{
		GitFailError(theEnv, returnValue, fname, error);
		return;
	}

	switch (cred->credtype)
	{
		case GIT_CREDENTIAL_USERPASS_PLAINTEXT: tag = "userpass"; break;
		case GIT_CREDENTIAL_USERNAME:           tag = "username"; break;
		case GIT_CREDENTIAL_SSH_KEY:            tag = "ssh-key"; break;
		default:                                tag = GitEnumName(GitCredentialTypeNames, cred->credtype); break;
	}

	mb = CreateMultifieldBuilder(theEnv, 2);
	MBAppendSymbol(mb, tag);
	if (git_credential_has_username(cred))
	{
		MBAppendString(mb, git_credential_get_username(cred));
	}
	else
	{
		MBAppendSymbol(mb, "nil");
	}
	git_credential_free(cred);

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/***************************************************************************/
/*                                                                         */
/* Remotes.                                                                */
/*                                                                         */
/* The functions that create, list, rename, and inspect a remote work on   */
/* any repository. Connect, ls, fetch, push, and clone use the network.    */
/* The build reports the transports that it has: git-libgit2-features      */
/* answers GIT_FEATURE_HTTPS and GIT_FEATURE_SSH when they are compiled    */
/* in. A remote that needs a credential gets one through the callback of   */
/* the Credentials section above.                                          */
/*                                                                         */
/***************************************************************************/

#define GIT_KN GIT_KM(GIT_K_REMOTE)

void GitRemoteCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-create";
	UDFValue theArg;
	git_repository *repo;
	const char *name, *url, *fetch;
	git_remote *remote = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the remote name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the url", false, &url)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the fetch refspec", &fetch)) { return; }

	error = (fetch == NULL) ? git_remote_create(&remote, repo, name, url)
	                        : git_remote_create_with_fetchspec(&remote, repo, name, url, fetch);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, remote, GIT_K_REMOTE, repo, NULL);
}

void GitRemoteCreateAnonymousFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-create-anonymous";
	UDFValue theArg;
	git_repository *repo;
	const char *url;
	git_remote *remote = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the url", false, &url)) { return; }

	error = git_remote_create_anonymous(&remote, repo, url);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, remote, GIT_K_REMOTE, repo, NULL);
}

void GitRemoteCreateDetachedFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-create-detached";
	const char *url;
	git_remote *remote = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the url", false, &url)) { return; }

	error = git_remote_create_detached(&remote, url);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, remote, GIT_K_REMOTE, NULL, NULL);
}

void GitRemoteLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-lookup";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_remote *remote = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the remote name", false, &name)) { return; }

	error = git_remote_lookup(&remote, repo, name);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, remote, GIT_K_REMOTE, repo, NULL);
}

GIT_GETTER_TEXT(GitRemoteNameFunction, "git-remote-name", GIT_KN, "remote", git_remote_name((git_remote *) h))
GIT_GETTER_TEXT(GitRemoteUrlFunction, "git-remote-url", GIT_KN, "remote", git_remote_url((git_remote *) h))
GIT_GETTER_TEXT(GitRemotePushurlFunction, "git-remote-pushurl", GIT_KN, "remote", git_remote_pushurl((git_remote *) h))
GIT_GETTER_INTEGER(GitRemoteRefspecCountFunction, "git-remote-refspec-count", GIT_KN, "remote", git_remote_refspec_count((git_remote *) h))
GIT_GETTER_BOOL(GitRemoteConnectedFunction, "git-remote-connected", GIT_KN, "remote", git_remote_connected((git_remote *) h))
GIT_GETTER_STATUS(GitRemoteDisconnectFunction, "git-remote-disconnect", GIT_KN, "remote", git_remote_disconnect((git_remote *) h))
GIT_GETTER_STATUS(GitRemoteStopFunction, "git-remote-stop", GIT_KN, "remote", git_remote_stop((git_remote *) h))
GIT_GETTER_ENUM(GitRemoteAutotagFunction, "git-remote-autotag", GIT_KN, "remote", GitRemoteAutotagNames, git_remote_autotag((git_remote *) h))
GIT_GETTER_BOOL(GitRemotePruneRefsFunction, "git-remote-prune-refs", GIT_KN, "remote", git_remote_prune_refs((git_remote *) h))
GIT_FREE(GitRemoteFreeFunction, "git-remote-free", GIT_KN, "remote")

GIT_GETTER(GitRemoteDupFunction, "git-remote-dup", GIT_KN, "remote", {
	git_remote *out = NULL;
	struct GitHandle *rec = GitHandleFind(h);
	int error = git_remote_dup(&out, (git_remote *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-remote-dup", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-remote-dup", out, GIT_K_REMOTE, rec->repo, NULL);
})

static void GitRemoteRefspecs(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_strarray *, const git_remote *))
{
	UDFValue theArg;
	git_remote *remote;
	git_strarray out = { NULL, 0 };
	int error;

	if ((remote = GitRemoteArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = fn(&out, remote);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnStrarray(theEnv, returnValue, &out);
}

void GitRemoteGetFetchRefspecsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRemoteRefspecs(theEnv, context, returnValue, "git-remote-get-fetch-refspecs", git_remote_get_fetch_refspecs); }
void GitRemoteGetPushRefspecsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRemoteRefspecs(theEnv, context, returnValue, "git-remote-get-push-refspecs", git_remote_get_push_refspecs); }

void GitRemoteGetRefspecFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-get-refspec";
	UDFValue theArg;
	git_remote *remote;
	size_t n;
	const git_refspec *spec;

	if ((remote = GitRemoteArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the refspec number", &n)) { return; }

	spec = git_remote_get_refspec(remote, n);
	if (spec == NULL) { GitFail(theEnv, returnValue, fname, "the remote has no refspec by that number"); return; }

	GitReturnHandle(theEnv, returnValue, fname, (void *) spec, GIT_K_REFSPEC, NULL, remote);
}

void GitRemoteListFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-list";
	UDFValue theArg;
	git_repository *repo;
	git_strarray out = { NULL, 0 };
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = git_remote_list(&out, repo);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnStrarray(theEnv, returnValue, &out);
}

static void GitRemoteNameAndText(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  int (*fn)(git_repository *, const char *, const char *))
{
	UDFValue theArg;
	git_repository *repo;
	const char *name, *text;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the remote name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, what, false, &text)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, fn(repo, name, text));
}

void GitRemoteSetUrlFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRemoteNameAndText(theEnv, context, returnValue, "git-remote-set-url", "the url", git_remote_set_url); }
void GitRemoteSetPushurlFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRemoteNameAndText(theEnv, context, returnValue, "git-remote-set-pushurl", "the url", git_remote_set_pushurl); }
void GitRemoteAddFetchFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRemoteNameAndText(theEnv, context, returnValue, "git-remote-add-fetch", "the refspec", git_remote_add_fetch); }
void GitRemoteAddPushFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRemoteNameAndText(theEnv, context, returnValue, "git-remote-add-push", "the refspec", git_remote_add_push); }

void GitRemoteDeleteFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-delete";
	UDFValue theArg;
	git_repository *repo;
	const char *name;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the remote name", false, &name)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_remote_delete(repo, name));
}

/* Renames a remote. The refspecs that libgit2 could not rename return as a
   multifield. The multifield is empty if libgit2 renamed all of them. */
void GitRemoteRenameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-rename";
	UDFValue theArg;
	git_repository *repo;
	const char *name, *newName;
	git_strarray problems = { NULL, 0 };
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the remote name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the new name", false, &newName)) { return; }

	error = git_remote_rename(&problems, repo, name, newName);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnStrarray(theEnv, returnValue, &problems);
}

void GitRemoteIsValidNameFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *name;
	int valid = 0;

	if (!GitTextArgument(theEnv, context, returnValue, "git-remote-is-valid-name", "the remote name", false, &name)) { return; }
	git_remote_name_is_valid(&valid, name);
	GitReturnBool(theEnv, returnValue, valid);
}

void GitRemoteConnectFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-connect";
	UDFValue theArg;
	git_remote *remote;
	long long direction;
	git_remote_callbacks callbacks;
	struct GitCredentialPayload payload;

	if ((remote = GitRemoteArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the direction", GitDirectionNames,
	                             GIT_DIRECTION_FETCH, &direction)) { return; }

	GitRemoteCallbacksInit(&callbacks, &payload, theEnv);
	GitReturnStatus(theEnv, returnValue, fname, git_remote_connect(remote, (git_direction) direction, &callbacks, NULL, NULL));
}

/* The references that the remote advertises, as a multifield of name and id
   pairs. The call connects to the remote if it is not connected. */
void GitRemoteLsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-ls";
	UDFValue theArg;
	git_remote *remote;
	const git_remote_head **heads = NULL;
	size_t count = 0, i;
	MultifieldBuilder *mb;
	char hex[GIT_OID_MAX_HEXSIZE + 1];
	git_remote_callbacks callbacks;
	struct GitCredentialPayload payload;
	int error;

	if ((remote = GitRemoteArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	if (!git_remote_connected(remote))
	{
		GitRemoteCallbacksInit(&callbacks, &payload, theEnv);
		error = git_remote_connect(remote, GIT_DIRECTION_FETCH, &callbacks, NULL, NULL);
		if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	}

	error = git_remote_ls(&heads, &count, remote);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	mb = CreateMultifieldBuilder(theEnv, count * 2);
	for (i = 0; i < count; i++)
	{
		git_oid_tostr(hex, sizeof hex, &heads[i]->oid);
		MBAppendString(mb, heads[i]->name);
		MBAppendString(mb, hex);
	}
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitRemoteFetchFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-fetch";
	UDFValue theArg;
	git_remote *remote;
	git_strarray refspecs;
	const char *reflog;
	git_fetch_options opts = GIT_FETCH_OPTIONS_INIT;
	struct GitCredentialPayload payload;
	int error;

	if ((remote = GitRemoteArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalStrarrayArgument(theEnv, context, returnValue, fname, "the refspecs", &refspecs)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the reflog message", &reflog))
	{
		GitStrarrayDispose(&refspecs);
		return;
	}

	GitRemoteCallbacksInit(&opts.callbacks, &payload, theEnv);
	error = git_remote_fetch(remote, (refspecs.count > 0) ? &refspecs : NULL, &opts, reflog);
	GitStrarrayDispose(&refspecs);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitRemotePushFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-push";
	UDFValue theArg;
	git_remote *remote;
	git_strarray refspecs;
	git_push_options opts = GIT_PUSH_OPTIONS_INIT;
	struct GitCredentialPayload payload;
	int error;

	if ((remote = GitRemoteArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalStrarrayArgument(theEnv, context, returnValue, fname, "the refspecs", &refspecs)) { return; }

	GitRemoteCallbacksInit(&opts.callbacks, &payload, theEnv);
	error = git_remote_push(remote, (refspecs.count > 0) ? &refspecs : NULL, &opts);
	GitStrarrayDispose(&refspecs);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitRemotePruneFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-prune";
	UDFValue theArg;
	git_remote *remote;

	if ((remote = GitRemoteArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	GitReturnStatus(theEnv, returnValue, fname, git_remote_prune(remote, NULL));
}

void GitRemoteUpdateTipsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-remote-update-tips";
	UDFValue theArg;
	git_remote *remote;
	const char *reflog;

	if ((remote = GitRemoteArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the reflog message", &reflog)) { return; }

#if CLIPSGIT_HAVE_1_9
	GitReturnStatus(theEnv, returnValue, fname, git_remote_update_tips(remote, NULL, GIT_REMOTE_UPDATE_FETCHHEAD, GIT_REMOTE_DOWNLOAD_TAGS_AUTO, reflog));
#else
	GitReturnStatus(theEnv, returnValue, fname, git_remote_update_tips(remote, NULL, GIT_REMOTE_UPDATE_FETCHHEAD, GIT_REMOTE_DOWNLOAD_TAGS_AUTO, reflog));
#endif
}

/* Refspecs */

#define GIT_KS GIT_KM(GIT_K_REFSPEC)

void GitRefspecParseFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-refspec-parse";
	const char *text;
	bool isFetch;
	git_refspec *spec = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the refspec", false, &text)) { return; }
	if (!GitBooleanArgument(theEnv, context, returnValue, fname, "is-fetch", &isFetch)) { return; }

	error = git_refspec_parse(&spec, text, isFetch ? 1 : 0);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, spec, GIT_K_REFSPEC, NULL, NULL);
}

GIT_GETTER_TEXT(GitRefspecSrcFunction, "git-refspec-src", GIT_KS, "refspec", git_refspec_src((git_refspec *) h))
GIT_GETTER_TEXT(GitRefspecDstFunction, "git-refspec-dst", GIT_KS, "refspec", git_refspec_dst((git_refspec *) h))
GIT_GETTER_TEXT(GitRefspecStringFunction, "git-refspec-string", GIT_KS, "refspec", git_refspec_string((git_refspec *) h))
GIT_GETTER_BOOL(GitRefspecForceFunction, "git-refspec-force", GIT_KS, "refspec", git_refspec_force((git_refspec *) h))
GIT_GETTER_ENUM(GitRefspecDirectionFunction, "git-refspec-direction", GIT_KS, "refspec", GitDirectionNames, git_refspec_direction((git_refspec *) h))
GIT_FREE(GitRefspecFreeFunction, "git-refspec-free", GIT_KS, "refspec")

static void GitRefspecMatch(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(const git_refspec *, const char *))
{
	UDFValue theArg;
	git_refspec *spec;
	const char *name;

	if ((spec = GitRefspecArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	GitReturnBool(theEnv, returnValue, fn(spec, name));
}

void GitRefspecSrcMatchesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRefspecMatch(theEnv, context, returnValue, "git-refspec-src-matches", git_refspec_src_matches); }
void GitRefspecDstMatchesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRefspecMatch(theEnv, context, returnValue, "git-refspec-dst-matches", git_refspec_dst_matches); }

static void GitRefspecTransformCommon(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_buf *, const git_refspec *, const char *))
{
	UDFValue theArg;
	git_refspec *spec;
	const char *name;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((spec = GitRefspecArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	error = fn(&out, spec, name);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

void GitRefspecTransformFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRefspecTransformCommon(theEnv, context, returnValue, "git-refspec-transform", git_refspec_transform); }
void GitRefspecRtransformFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitRefspecTransformCommon(theEnv, context, returnValue, "git-refspec-rtransform", git_refspec_rtransform); }

/* Clone */

void GitCloneFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-clone";
	const char *url, *path, *branch;
	bool bare;
	git_clone_options opts = GIT_CLONE_OPTIONS_INIT;
	struct GitCredentialPayload payload;
	git_repository *repo = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the url", false, &url)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "bare", &bare)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the branch", &branch)) { return; }

	opts.bare = bare ? 1 : 0;
	opts.checkout_branch = branch;
	GitRemoteCallbacksInit(&opts.fetch_opts.callbacks, &payload, theEnv);

	error = git_clone(&repo, url, path, &opts);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, repo, GIT_K_REPOSITORY, repo, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Status.                                                                 */
/*                                                                         */
/***************************************************************************/

#define GIT_KA GIT_KM(GIT_K_STATUS_LIST)

void GitStatusListNewFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-status-list-new";
	UDFValue theArg;
	git_repository *repo;
	unsigned int flags;
	long long show;
	git_strarray pathspec;
	git_status_options opts = GIT_STATUS_OPTIONS_INIT;
	git_status_list *list = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the status flags", GitStatusOptNames,
	                              GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX |
	                              GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR, &flags)) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "what to show", GitStatusShowNames,
	                             GIT_STATUS_SHOW_INDEX_AND_WORKDIR, &show)) { return; }
	if (!GitOptionalStrarrayArgument(theEnv, context, returnValue, fname, "the pathspec", &pathspec)) { return; }

	opts.flags = flags;
	opts.show = (git_status_show_t) show;
	opts.pathspec = pathspec;

	error = git_status_list_new(&list, repo, &opts);
	GitStrarrayDispose(&pathspec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, list, GIT_K_STATUS_LIST, repo, NULL);
}

GIT_GETTER_INTEGER(GitStatusListEntrycountFunction, "git-status-list-entrycount", GIT_KA, "status list", git_status_list_entrycount((git_status_list *) h))
GIT_FREE(GitStatusListFreeFunction, "git-status-list-free", GIT_KA, "status list")

/* One entry of a status list, as a multifield: the path, the old path if
   the file was renamed (or nil), and then the status flags. The flags are
   last, because a CLIPS multifield cannot hold another multifield, and the
   number of flags varies. */
static const char *GitStatusEntryPath(
  const git_status_entry *entry,
  const char **oldPath)
{
	const git_diff_delta *d = (entry->index_to_workdir != NULL) ? entry->index_to_workdir : entry->head_to_index;

	*oldPath = NULL;
	if (d == NULL) { return NULL; }
	if (d->status == GIT_DELTA_RENAMED && d->old_file.path != NULL && d->new_file.path != NULL &&
	    strcmp(d->old_file.path, d->new_file.path) != 0)
	{
		*oldPath = d->old_file.path;
	}
	return (d->new_file.path != NULL) ? d->new_file.path : d->old_file.path;
}

void GitStatusByindexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-status-byindex";
	UDFValue theArg;
	git_status_list *list;
	size_t n;
	const git_status_entry *entry;
	const char *path, *oldPath;
	MultifieldBuilder *mb;
	UDFValue flags;

	if ((list = GitStatusListArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the position", &n)) { return; }

	entry = git_status_byindex(list, n);
	if (entry == NULL) { GitFail(theEnv, returnValue, fname, "the position is out of range"); return; }

	path = GitStatusEntryPath(entry, &oldPath);
	GitReturnFlags(theEnv, &flags, GitStatusNames, entry->status);

	mb = CreateMultifieldBuilder(theEnv, 4);
	if (path == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, path); }
	if (oldPath == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, oldPath); }
	MBAppendMultifield(mb, flags.multifieldValue);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/* One entry as a fact. Only the slots that the deftemplate has are filled:
   path (a string), status (a multislot of the flag symbols), and old-path.
   If the position is after the last entry, the result is FALSE. */
void GitStatusEntryToFactFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-status-entry-to-fact";
	UDFValue theArg;
	git_status_list *list;
	size_t n;
	const git_status_entry *entry;
	const char *templateName, *path, *oldPath;
	Deftemplate *dt;
	FactBuilder *fb;
	Fact *fact;
	UDFValue flags;

	if ((list = GitStatusListArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the position", &n)) { return; }
	if (!UDFNextArgument(context, SYMBOL_BIT, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		GitFail(theEnv, returnValue, fname, "expected a symbol for the deftemplate name");
		return;
	}
	templateName = theArg.lexemeValue->contents;

	entry = git_status_byindex(list, n);
	if (entry == NULL) { returnValue->lexemeValue = FalseSymbol(theEnv); return; }

	dt = FindDeftemplate(theEnv, templateName);
	if (dt == NULL) { GitFail(theEnv, returnValue, fname, "there is no deftemplate by that name"); return; }

	fb = CreateFactBuilder(theEnv, templateName);
	if (FBError(theEnv) != FBE_NO_ERROR)
	{
		GitFail(theEnv, returnValue, fname, "the FactBuilder could not be created");
		FBDispose(fb);
		return;
	}

	path = GitStatusEntryPath(entry, &oldPath);
#define S(name) DeftemplateSlotExistP(dt, name)
	GIT_PUT_TEXT_SLOT(S("path"), FBPutSlotString, fb, "path", path)
	GIT_PUT_TEXT_SLOT(S("old-path"), FBPutSlotString, fb, "old-path", oldPath)
	if (S("status"))
	{
		PutSlotError e;
		GitReturnFlags(theEnv, &flags, GitStatusNames, entry->status);
		e = FBPutSlotMultifield(fb, "status", flags.multifieldValue);
		if (e != PSE_NO_ERROR) { GitFailSlot(theEnv, returnValue, fname, "status", e); goto fail; }
	}
#undef S

	fact = FBAssert(fb);
	if (fact == NULL)
	{
		GitFail(theEnv, returnValue, fname, "the fact could not be asserted: it may already exist");
		goto fail;
	}
	returnValue->factValue = fact;
	FBDispose(fb);
	return;

fail:
	FBDispose(fb);
}

void GitStatusFileFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-status-file";
	UDFValue theArg;
	git_repository *repo;
	const char *path;
	unsigned int flags = 0;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_status_file(&flags, repo, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnFlags(theEnv, returnValue, GitStatusNames, flags);
}

void GitStatusShouldIgnoreFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-status-should-ignore";
	UDFValue theArg;
	git_repository *repo;
	const char *path;
	int ignored = 0;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_status_should_ignore(&ignored, repo, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBool(theEnv, returnValue, ignored);
}

/* Ignore rules */

void GitIgnoreAddRuleFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-ignore-add-rule";
	UDFValue theArg;
	git_repository *repo;
	const char *rules;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the rules", false, &rules)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_ignore_add_rule(repo, rules));
}

GIT_GETTER_STATUS(GitIgnoreClearInternalRulesFunction, "git-ignore-clear-internal-rules", GIT_KM(GIT_K_REPOSITORY), "repository",
                  git_ignore_clear_internal_rules((git_repository *) h))

void GitIgnorePathIsIgnoredFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-ignore-path-is-ignored";
	UDFValue theArg;
	git_repository *repo;
	const char *path;
	int ignored = 0;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }

	error = git_ignore_path_is_ignored(&ignored, repo, path);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBool(theEnv, returnValue, ignored);
}

/***************************************************************************/
/*                                                                         */
/* Diffs and patches.                                                      */
/*                                                                         */
/***************************************************************************/

#define GIT_KD GIT_KM(GIT_K_DIFF)
#define GIT_KP GIT_KM(GIT_K_PATCH)

/* The diff options that every diff accepts: the flags, then a pathspec,
   then the context and interhunk line counts. All of them are optional. */
static bool GitDiffOptionsArguments(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  git_diff_options *opts,
  git_strarray *pathspec)
{
	unsigned int flags;
	size_t n;

	pathspec->strings = NULL;
	pathspec->count = 0;

	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the diff flags", GitDiffOptionNames,
	                              GIT_DIFF_NORMAL, &flags)) { return false; }
	opts->flags = flags;

	if (!GitOptionalStrarrayArgument(theEnv, context, returnValue, fname, "the pathspec", pathspec)) { return false; }
	opts->pathspec = *pathspec;

	if (UDFHasNextArgument(context))
	{
		if (!GitSizeArgument(theEnv, context, returnValue, fname, "the context lines", &n))
		{
			GitStrarrayDispose(pathspec);
			return false;
		}
		opts->context_lines = (uint32_t) n;
	}
	if (UDFHasNextArgument(context))
	{
		if (!GitSizeArgument(theEnv, context, returnValue, fname, "the interhunk lines", &n))
		{
			GitStrarrayDispose(pathspec);
			return false;
		}
		opts->interhunk_lines = (uint32_t) n;
	}

	return true;
}

void GitDiffTreeToTreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-tree-to-tree";
	UDFValue theArg;
	git_repository *repo;
	void *oldTree, *newTree;
	git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
	git_strarray pathspec;
	git_diff *diff = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_TREE), "the old tree", &oldTree)) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_TREE), "the new tree", &newTree)) { return; }
	if (!GitDiffOptionsArguments(theEnv, context, returnValue, fname, &opts, &pathspec)) { return; }

	error = git_diff_tree_to_tree(&diff, repo, (git_tree *) oldTree, (git_tree *) newTree, &opts);
	GitStrarrayDispose(&pathspec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, diff, GIT_K_DIFF, repo, NULL);
}

void GitDiffTreeToIndexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-tree-to-index";
	UDFValue theArg;
	git_repository *repo;
	void *oldTree, *index;
	git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
	git_strarray pathspec;
	git_diff *diff = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_TREE), "the old tree", &oldTree)) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_INDEX), "the index", &index)) { return; }
	if (!GitDiffOptionsArguments(theEnv, context, returnValue, fname, &opts, &pathspec)) { return; }

	error = git_diff_tree_to_index(&diff, repo, (git_tree *) oldTree, (git_index *) index, &opts);
	GitStrarrayDispose(&pathspec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, diff, GIT_K_DIFF, repo, NULL);
}

void GitDiffIndexToWorkdirFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-index-to-workdir";
	UDFValue theArg;
	git_repository *repo;
	void *index;
	git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
	git_strarray pathspec;
	git_diff *diff = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_INDEX), "the index", &index)) { return; }
	if (!GitDiffOptionsArguments(theEnv, context, returnValue, fname, &opts, &pathspec)) { return; }

	error = git_diff_index_to_workdir(&diff, repo, (git_index *) index, &opts);
	GitStrarrayDispose(&pathspec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, diff, GIT_K_DIFF, repo, NULL);
}

static void GitDiffTreeToWorkdirCommon(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  int (*fn)(git_diff **, git_repository *, git_tree *, const git_diff_options *))
{
	UDFValue theArg;
	git_repository *repo;
	void *oldTree;
	git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
	git_strarray pathspec;
	git_diff *diff = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_TREE), "the old tree", &oldTree)) { return; }
	if (!GitDiffOptionsArguments(theEnv, context, returnValue, fname, &opts, &pathspec)) { return; }

	error = fn(&diff, repo, (git_tree *) oldTree, &opts);
	GitStrarrayDispose(&pathspec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, diff, GIT_K_DIFF, repo, NULL);
}

void GitDiffTreeToWorkdirFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitDiffTreeToWorkdirCommon(theEnv, context, returnValue, "git-diff-tree-to-workdir", git_diff_tree_to_workdir); }
void GitDiffTreeToWorkdirWithIndexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitDiffTreeToWorkdirCommon(theEnv, context, returnValue, "git-diff-tree-to-workdir-with-index", git_diff_tree_to_workdir_with_index); }

void GitDiffIndexToIndexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-index-to-index";
	UDFValue theArg;
	git_repository *repo;
	git_index *oldIndex, *newIndex;
	git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
	git_strarray pathspec;
	git_diff *diff = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((oldIndex = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((newIndex = GitIndexArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitDiffOptionsArguments(theEnv, context, returnValue, fname, &opts, &pathspec)) { return; }

	error = git_diff_index_to_index(&diff, repo, oldIndex, newIndex, &opts);
	GitStrarrayDispose(&pathspec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, diff, GIT_K_DIFF, repo, NULL);
}

void GitDiffFromBufferFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-from-buffer";
	const char *content;
	git_diff *diff = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the patch text", false, &content)) { return; }

	error = git_diff_from_buffer(&diff, content, strlen(content));
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, diff, GIT_K_DIFF, NULL, NULL);
}

GIT_GETTER_INTEGER(GitDiffNumDeltasFunction, "git-diff-num-deltas", GIT_KD, "diff", git_diff_num_deltas((git_diff *) h))
GIT_GETTER_BOOL(GitDiffIsSortedIcaseFunction, "git-diff-is-sorted-icase", GIT_KD, "diff", git_diff_is_sorted_icase((git_diff *) h))
GIT_FREE(GitDiffFreeFunction, "git-diff-free", GIT_KD, "diff")

void GitDiffNumDeltasOfTypeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-num-deltas-of-type";
	UDFValue theArg;
	git_diff *diff;
	long long type;

	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the delta type", GitDeltaNames, &type)) { return; }

	returnValue->integerValue = CreateInteger(theEnv, (long long) git_diff_num_deltas_of_type(diff, (git_delta_t) type));
}

void GitDiffMergeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-merge";
	UDFValue theArg;
	git_diff *onto, *from;

	if ((onto = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((from = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_diff_merge(onto, from));
}

void GitDiffFindSimilarFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-find-similar";
	UDFValue theArg;
	git_diff *diff;
	unsigned int flags;
	git_diff_find_options opts = GIT_DIFF_FIND_OPTIONS_INIT;

	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the find flags", GitDiffFindNames,
	                              GIT_DIFF_FIND_BY_CONFIG, &flags)) { return; }
	opts.flags = flags;

	GitReturnStatus(theEnv, returnValue, fname, git_diff_find_similar(diff, &opts));
}

/* A delta as a multifield: the status, the old path, the new path, the old
   id, the new id, and a boolean that shows if the delta is binary. */
static void GitReturnDelta(
  Environment *theEnv,
  UDFValue *returnValue,
  const git_diff_delta *d)
{
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, 6);
	char hex[GIT_OID_MAX_HEXSIZE + 1];

	MBAppendSymbol(mb, GitEnumName(GitDeltaNames, d->status));
	if (d->old_file.path == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, d->old_file.path); }
	if (d->new_file.path == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, d->new_file.path); }
	git_oid_tostr(hex, sizeof hex, &d->old_file.id);
	MBAppendString(mb, hex);
	git_oid_tostr(hex, sizeof hex, &d->new_file.id);
	MBAppendString(mb, hex);
	MBAppendSymbol(mb, (d->flags & GIT_DIFF_FLAG_BINARY) ? "TRUE" : "FALSE");
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitDiffGetDeltaFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-get-delta";
	UDFValue theArg;
	git_diff *diff;
	size_t n;
	const git_diff_delta *d;

	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the delta number", &n)) { return; }

	d = git_diff_get_delta(diff, n);
	if (d == NULL) { GitFail(theEnv, returnValue, fname, "the delta number is out of range"); return; }

	GitReturnDelta(theEnv, returnValue, d);
}

/* One delta as a fact. Only the slots that the deftemplate has are filled:
   status, old-path, new-path, old-id, new-id, binary. If the position is
   after the last delta, the result is FALSE. */
void GitDiffDeltaToFactFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-delta-to-fact";
	UDFValue theArg;
	git_diff *diff;
	size_t n;
	const git_diff_delta *d;
	const char *templateName;
	Deftemplate *dt;
	FactBuilder *fb;
	Fact *fact;
	char oldHex[GIT_OID_MAX_HEXSIZE + 1], newHex[GIT_OID_MAX_HEXSIZE + 1];

	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the delta number", &n)) { return; }
	if (!UDFNextArgument(context, SYMBOL_BIT, &theArg) || theArg.header->type != SYMBOL_TYPE)
	{
		GitFail(theEnv, returnValue, fname, "expected a symbol for the deftemplate name");
		return;
	}
	templateName = theArg.lexemeValue->contents;

	d = git_diff_get_delta(diff, n);
	if (d == NULL) { returnValue->lexemeValue = FalseSymbol(theEnv); return; }

	dt = FindDeftemplate(theEnv, templateName);
	if (dt == NULL) { GitFail(theEnv, returnValue, fname, "there is no deftemplate by that name"); return; }

	fb = CreateFactBuilder(theEnv, templateName);
	if (FBError(theEnv) != FBE_NO_ERROR)
	{
		GitFail(theEnv, returnValue, fname, "the FactBuilder could not be created");
		FBDispose(fb);
		return;
	}

	git_oid_tostr(oldHex, sizeof oldHex, &d->old_file.id);
	git_oid_tostr(newHex, sizeof newHex, &d->new_file.id);
#define S(name) DeftemplateSlotExistP(dt, name)
	GIT_PUT_TEXT_SLOT(S("status"), FBPutSlotSymbol, fb, "status", GitEnumName(GitDeltaNames, d->status))
	GIT_PUT_TEXT_SLOT(S("old-path"), FBPutSlotString, fb, "old-path", d->old_file.path)
	GIT_PUT_TEXT_SLOT(S("new-path"), FBPutSlotString, fb, "new-path", d->new_file.path)
	GIT_PUT_TEXT_SLOT(S("old-id"), FBPutSlotString, fb, "old-id", oldHex)
	GIT_PUT_TEXT_SLOT(S("new-id"), FBPutSlotString, fb, "new-id", newHex)
	GIT_PUT_TEXT_SLOT(S("binary"), FBPutSlotSymbol, fb, "binary", (d->flags & GIT_DIFF_FLAG_BINARY) ? "TRUE" : "FALSE")
#undef S

	fact = FBAssert(fb);
	if (fact == NULL)
	{
		GitFail(theEnv, returnValue, fname, "the fact could not be asserted: it may already exist");
		goto fail;
	}
	returnValue->factValue = fact;
	FBDispose(fb);
	return;

fail:
	FBDispose(fb);
}

void GitDiffToBufFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-to-buf";
	UDFValue theArg;
	git_diff *diff;
	long long format;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the format", GitDiffFormatNames,
	                             GIT_DIFF_FORMAT_PATCH, &format)) { return; }

	error = git_diff_to_buf(&out, diff, (git_diff_format_t) format);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

/* The statistics of a diff, as a multifield of three integers: files
   changed, insertions, deletions. */
GIT_GETTER(GitDiffStatsFunction, "git-diff-stats", GIT_KD, "diff", {
	git_diff_stats *stats = NULL;
	MultifieldBuilder *mb;
	int error = git_diff_get_stats(&stats, (git_diff *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-diff-stats", error); return; }
	mb = CreateMultifieldBuilder(theEnv, 3);
	MBAppendInteger(mb, (long long) git_diff_stats_files_changed(stats));
	MBAppendInteger(mb, (long long) git_diff_stats_insertions(stats));
	MBAppendInteger(mb, (long long) git_diff_stats_deletions(stats));
	git_diff_stats_free(stats);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
})

void GitDiffStatsToBufFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-diff-stats-to-buf";
	UDFValue theArg;
	git_diff *diff;
	unsigned int format;
	long long width;
	git_diff_stats *stats = NULL;
	git_buf out = GIT_BUF_INIT;
	int error;

	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the stats format", GitDiffStatsFormatNames,
	                              GIT_DIFF_STATS_FULL, &format)) { return; }
	width = 80;
	if (UDFHasNextArgument(context) &&
	    !GitIntegerArgument(theEnv, context, returnValue, fname, "the width", &width)) { return; }

	error = git_diff_get_stats(&stats, diff);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	error = git_diff_stats_to_buf(&out, stats, (git_diff_stats_format_t) format, (size_t) width);
	git_diff_stats_free(stats);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

GIT_GETTER(GitDiffPatchidFunction, "git-diff-patchid", GIT_KD, "diff", {
	git_oid oid;
	int error = git_diff_patchid(&oid, (git_diff *) h, NULL);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-diff-patchid", error); return; }
	GitReturnOid(theEnv, returnValue, &oid);
})

/* Patches */

void GitPatchFromDiffFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-patch-from-diff";
	UDFValue theArg;
	git_diff *diff;
	size_t n;
	git_patch *patch = NULL;
	struct GitHandle *rec;
	int error;

	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the delta number", &n)) { return; }

	error = git_patch_from_diff(&patch, diff, n);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }
	if (patch == NULL) { GitReturnNil(theEnv, returnValue); return; }

	rec = GitHandleFind(diff);
	GitReturnHandle(theEnv, returnValue, fname, patch, GIT_K_PATCH, rec->repo, NULL);
}

void GitPatchFromBlobsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-patch-from-blobs";
	void *oldBlob, *newBlob;
	const char *oldPath, *newPath;
	git_patch *patch = NULL;
	int error;

	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_BLOB), "the old blob", &oldBlob)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the old path", true, &oldPath)) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_BLOB), "the new blob", &newBlob)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the new path", true, &newPath)) { return; }

	error = git_patch_from_blobs(&patch, (git_blob *) oldBlob, oldPath, (git_blob *) newBlob, newPath, NULL);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, patch, GIT_K_PATCH, NULL, NULL);
}

void GitPatchFromBuffersFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-patch-from-buffers";
	const char *oldText, *oldPath, *newText, *newPath;
	git_patch *patch = NULL;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the old text", true, &oldText)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the old path", true, &oldPath)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the new text", true, &newText)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the new path", true, &newPath)) { return; }

	error = git_patch_from_buffers(&patch, oldText, (oldText == NULL) ? 0 : strlen(oldText), oldPath,
	                               newText, (newText == NULL) ? 0 : strlen(newText), newPath, NULL);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, patch, GIT_K_PATCH, NULL, NULL);
}

GIT_GETTER_INTEGER(GitPatchNumHunksFunction, "git-patch-num-hunks", GIT_KP, "patch", git_patch_num_hunks((git_patch *) h))
GIT_GETTER_INTEGER(GitPatchSizeFunction, "git-patch-size", GIT_KP, "patch", git_patch_size((git_patch *) h, 1, 1, 1))
GIT_FREE(GitPatchFreeFunction, "git-patch-free", GIT_KP, "patch")

GIT_GETTER(GitPatchGetDeltaFunction, "git-patch-get-delta", GIT_KP, "patch", {
	GitReturnDelta(theEnv, returnValue, git_patch_get_delta((git_patch *) h));
})

GIT_GETTER(GitPatchToBufFunction, "git-patch-to-buf", GIT_KP, "patch", {
	git_buf out = GIT_BUF_INIT;
	int error = git_patch_to_buf(&out, (git_patch *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-patch-to-buf", error); return; }
	GitReturnBuf(theEnv, returnValue, &out);
})

/* The number of lines added, deleted, and of context, as a multifield of
   three integers. */
GIT_GETTER(GitPatchLineStatsFunction, "git-patch-line-stats", GIT_KP, "patch", {
	size_t ctx = 0;
	size_t add = 0;
	size_t del = 0;
	MultifieldBuilder *mb;
	int error = git_patch_line_stats(&ctx, &add, &del, (git_patch *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-patch-line-stats", error); return; }
	mb = CreateMultifieldBuilder(theEnv, 3);
	MBAppendInteger(mb, (long long) ctx);
	MBAppendInteger(mb, (long long) add);
	MBAppendInteger(mb, (long long) del);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
})

/* A hunk as a multifield: old start, old lines, new start, new lines, the
   header, and the line count. */
void GitPatchGetHunkFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-patch-get-hunk";
	UDFValue theArg;
	git_patch *patch;
	size_t n, lines = 0;
	const git_diff_hunk *hunk = NULL;
	MultifieldBuilder *mb;
	int error;

	if ((patch = GitPatchArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the hunk number", &n)) { return; }

	error = git_patch_get_hunk(&hunk, &lines, patch, n);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	mb = CreateMultifieldBuilder(theEnv, 6);
	MBAppendInteger(mb, hunk->old_start);
	MBAppendInteger(mb, hunk->old_lines);
	MBAppendInteger(mb, hunk->new_start);
	MBAppendInteger(mb, hunk->new_lines);
	GitReturnBytes(theEnv, &theArg, fname, hunk->header, hunk->header_len);
	MBAppendUDFValue(mb, &theArg);
	MBAppendInteger(mb, (long long) lines);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitPatchNumLinesInHunkFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-patch-num-lines-in-hunk";
	UDFValue theArg;
	git_patch *patch;
	size_t n;
	int r;

	if ((patch = GitPatchArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the hunk number", &n)) { return; }

	r = git_patch_num_lines_in_hunk(patch, n);
	if (r < 0) { GitFailError(theEnv, returnValue, fname, r); return; }
	returnValue->integerValue = CreateInteger(theEnv, r);
}

/* A line as a multifield: the origin character (" ", "+", "-"), the old
   line number (or -1), the new line number (or -1), and the content. */
void GitPatchGetLineInHunkFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-patch-get-line-in-hunk";
	UDFValue theArg;
	git_patch *patch;
	size_t hunkNo, lineNo;
	const git_diff_line *line = NULL;
	MultifieldBuilder *mb;
	char origin[2];
	int error;

	if ((patch = GitPatchArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the hunk number", &hunkNo)) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the line number", &lineNo)) { return; }

	error = git_patch_get_line_in_hunk(&line, patch, hunkNo, lineNo);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	origin[0] = line->origin;
	origin[1] = '\0';
	mb = CreateMultifieldBuilder(theEnv, 4);
	MBAppendString(mb, origin);
	MBAppendInteger(mb, line->old_lineno);
	MBAppendInteger(mb, line->new_lineno);
	GitReturnBytes(theEnv, &theArg, fname, line->content, line->content_len);
	MBAppendUDFValue(mb, &theArg);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/* Apply */

void GitApplyFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-apply";
	UDFValue theArg;
	git_repository *repo;
	git_diff *diff;
	long long location;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the location", GitApplyLocationNames,
	                             GIT_APPLY_LOCATION_WORKDIR, &location)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_apply(repo, diff, (git_apply_location_t) location, NULL));
}

void GitApplyToTreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-apply-to-tree";
	UDFValue theArg;
	git_repository *repo;
	git_tree *tree;
	git_diff *diff;
	git_index *out = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((tree = GitTreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((diff = GitDiffArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = git_apply_to_tree(&out, repo, tree, diff, NULL);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_INDEX, repo, NULL);
}

/***************************************************************************/
/*                                                                         */
/* Checkout and reset.                                                     */
/*                                                                         */
/***************************************************************************/

static bool GitCheckoutOptionsArguments(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  git_checkout_options *opts,
  git_strarray *paths)
{
	unsigned int strategy;

	paths->strings = NULL;
	paths->count = 0;

	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the checkout strategy", GitCheckoutNames,
	                              GIT_CHECKOUT_SAFE, &strategy)) { return false; }
	opts->checkout_strategy = strategy;

	if (!GitOptionalStrarrayArgument(theEnv, context, returnValue, fname, "the paths", paths)) { return false; }
	opts->paths = *paths;
	return true;
}

void GitCheckoutHeadFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-checkout-head";
	UDFValue theArg;
	git_repository *repo;
	git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
	git_strarray paths;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitCheckoutOptionsArguments(theEnv, context, returnValue, fname, &opts, &paths)) { return; }

	error = git_checkout_head(repo, &opts);
	GitStrarrayDispose(&paths);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitCheckoutIndexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-checkout-index";
	UDFValue theArg;
	git_repository *repo;
	void *index;
	git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
	git_strarray paths;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_INDEX), "the index", &index)) { return; }
	if (!GitCheckoutOptionsArguments(theEnv, context, returnValue, fname, &opts, &paths)) { return; }

	error = git_checkout_index(repo, (git_index *) index, &opts);
	GitStrarrayDispose(&paths);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitCheckoutTreeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-checkout-tree";
	UDFValue theArg;
	git_repository *repo;
	git_object *treeish;
	git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
	git_strarray paths;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((treeish = GitTreeishArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitCheckoutOptionsArguments(theEnv, context, returnValue, fname, &opts, &paths)) { return; }

	error = git_checkout_tree(repo, treeish, &opts);
	GitStrarrayDispose(&paths);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitResetFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reset";
	UDFValue theArg;
	git_repository *repo;
	git_object *target;
	long long type;
	git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((target = GitTreeishArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the reset type", GitResetTypeNames, &type)) { return; }

	opts.checkout_strategy = GIT_CHECKOUT_SAFE;
	GitReturnStatus(theEnv, returnValue, fname, git_reset(repo, target, (git_reset_t) type, &opts));
}

void GitResetDefaultFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reset-default";
	UDFValue theArg;
	git_repository *repo;
	void *target;
	git_strarray paths;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM_TREEISH, "the target", &target)) { return; }
	if (!GitStrarrayArgument(theEnv, context, returnValue, fname, "the paths", &paths)) { return; }

	error = git_reset_default(repo, (git_object *) target, &paths);
	GitStrarrayDispose(&paths);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

/***************************************************************************/
/*                                                                         */
/* Revision specs.                                                         */
/*                                                                         */
/***************************************************************************/

void GitRevparseSingleFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-revparse-single";
	UDFValue theArg;
	git_repository *repo;
	const char *spec;
	git_object *object = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the revspec", false, &spec)) { return; }

	error = git_revparse_single(&object, repo, spec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnObject(theEnv, returnValue, fname, object, repo);
}

/* The object and the reference that a spec names, as a multifield of two
   elements. The reference is nil if the spec did not use a reference. */
void GitRevparseExtFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-revparse-ext";
	UDFValue theArg, objectValue;
	git_repository *repo;
	const char *spec;
	git_object *object = NULL;
	git_reference *ref = NULL;
	MultifieldBuilder *mb;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the revspec", false, &spec)) { return; }

	error = git_revparse_ext(&object, &ref, repo, spec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnObject(theEnv, &objectValue, fname, object, repo);
	if (objectValue.header->type != EXTERNAL_ADDRESS_TYPE)
	{
		if (ref != NULL) { git_reference_free(ref); }
		returnValue->lexemeValue = FalseSymbol(theEnv);
		return;
	}

	mb = CreateMultifieldBuilder(theEnv, 2);
	MBAppendUDFValue(mb, &objectValue);
	if (ref == NULL)
	{
		MBAppendSymbol(mb, "nil");
	}
	else
	{
		GitReturnHandle(theEnv, &theArg, fname, ref, GIT_K_REFERENCE, repo, NULL);
		MBAppendUDFValue(mb, &theArg);
	}
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/* A range spec as a multifield: the from object, the to object (or nil),
   and then the flags. The flags are last, one element for each flag. */
void GitRevparseFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-revparse";
	UDFValue theArg, v;
	git_repository *repo;
	const char *spec;
	git_revspec rs;
	MultifieldBuilder *mb;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the revspec", false, &spec)) { return; }

	error = git_revparse(&rs, repo, spec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	mb = CreateMultifieldBuilder(theEnv, 3);
	if (rs.from == NULL) { MBAppendSymbol(mb, "nil"); }
	else { GitReturnObject(theEnv, &v, fname, rs.from, repo); MBAppendUDFValue(mb, &v); }
	if (rs.to == NULL) { MBAppendSymbol(mb, "nil"); }
	else { GitReturnObject(theEnv, &v, fname, rs.to, repo); MBAppendUDFValue(mb, &v); }
	GitReturnFlags(theEnv, &v, GitRevspecNames, rs.flags);
	MBAppendMultifield(mb, v.multifieldValue);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/***************************************************************************/
/*                                                                         */
/* Annotated commits, merge, cherry-pick, and revert.                      */
/*                                                                         */
/***************************************************************************/

#define GIT_KAC GIT_KM(GIT_K_ANNOTATED_COMMIT)

void GitAnnotatedCommitFromRefFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-annotated-commit-from-ref";
	UDFValue theArg;
	git_repository *repo;
	git_reference *ref;
	git_annotated_commit *out = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((ref = GitReferenceArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = git_annotated_commit_from_ref(&out, repo, ref);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_ANNOTATED_COMMIT, repo, NULL);
}

void GitAnnotatedCommitLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-annotated-commit-lookup";
	UDFValue theArg;
	git_repository *repo;
	git_oid oid;
	git_annotated_commit *out = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the commit id", &oid)) { return; }

	error = git_annotated_commit_lookup(&out, repo, &oid);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_ANNOTATED_COMMIT, repo, NULL);
}

void GitAnnotatedCommitFromRevspecFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-annotated-commit-from-revspec";
	UDFValue theArg;
	git_repository *repo;
	const char *spec;
	git_annotated_commit *out = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the revspec", false, &spec)) { return; }

	error = git_annotated_commit_from_revspec(&out, repo, spec);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_ANNOTATED_COMMIT, repo, NULL);
}

GIT_GETTER_OID(GitAnnotatedCommitIdFunction, "git-annotated-commit-id", GIT_KAC, "annotated commit", git_annotated_commit_id((git_annotated_commit *) h))
GIT_GETTER_TEXT(GitAnnotatedCommitRefFunction, "git-annotated-commit-ref", GIT_KAC, "annotated commit", git_annotated_commit_ref((git_annotated_commit *) h))
GIT_FREE(GitAnnotatedCommitFreeFunction, "git-annotated-commit-free", GIT_KAC, "annotated commit")

/* The result of a merge analysis: the analysis flags, then the preference.
   A CLIPS multifield cannot hold another multifield. So the flags are one
   element each, and the preference is the last element. */
void GitMergeAnalysisFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-merge-analysis";
	UDFValue theArg, v;
	git_repository *repo;
	void **heads = NULL;
	size_t count = 0;
	git_merge_analysis_t analysis;
	git_merge_preference_t preference;
	MultifieldBuilder *mb;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitHandlesArgument(theEnv, context, returnValue, fname, GIT_KAC, "the heads", &heads, &count)) { return; }
	if (count == 0) { GitFail(theEnv, returnValue, fname, "at least one head is needed"); return; }

	error = git_merge_analysis(&analysis, &preference, repo, (const git_annotated_commit **) heads, count);
	free(heads);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	mb = CreateMultifieldBuilder(theEnv, 4);
	GitReturnFlags(theEnv, &v, GitMergeAnalysisNames, analysis);
	MBAppendMultifield(mb, v.multifieldValue);
	MBAppendSymbol(mb, GitEnumName(GitMergePreferenceNames, preference));
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitMergeFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-merge";
	UDFValue theArg;
	git_repository *repo;
	void **heads = NULL;
	size_t count = 0;
	unsigned int mergeFlags, strategy;
	git_merge_options mopts = GIT_MERGE_OPTIONS_INIT;
	git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitHandlesArgument(theEnv, context, returnValue, fname, GIT_KAC, "the heads", &heads, &count)) { return; }
	if (count == 0) { GitFail(theEnv, returnValue, fname, "at least one head is needed"); return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the merge flags", GitMergeFlagNames,
	                              GIT_MERGE_FIND_RENAMES, &mergeFlags) ||
	    !GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the checkout strategy", GitCheckoutNames,
	                              GIT_CHECKOUT_SAFE | GIT_CHECKOUT_ALLOW_CONFLICTS, &strategy))
	{
		free(heads);
		return;
	}

	mopts.flags = mergeFlags;
	copts.checkout_strategy = strategy;

	error = git_merge(repo, (const git_annotated_commit **) heads, count, &mopts, &copts);
	free(heads);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitMergeBaseFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-merge-base";
	UDFValue theArg;
	git_repository *repo;
	git_oid one, two, out;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the first commit id", &one)) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the second commit id", &two)) { return; }

	error = git_merge_base(&out, repo, &one, &two);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &out);
}

static void GitMergeTreesOrCommits(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  bool trees)
{
	UDFValue theArg;
	git_repository *repo;
	void *ancestor = NULL, *ours, *theirs;
	unsigned int flags;
	git_merge_options opts = GIT_MERGE_OPTIONS_INIT;
	git_index *out = NULL;
	unsigned int kind = trees ? GIT_KM(GIT_K_TREE) : GIT_KM(GIT_K_COMMIT);
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (trees && !GitHandleOrNilArgument(theEnv, context, returnValue, fname, kind, "the ancestor tree", &ancestor)) { return; }
	if ((ours = GitHandleArgument(theEnv, context, returnValue, fname, kind, trees ? "our tree" : "our commit", &theArg)) == NULL) { return; }
	if ((theirs = GitHandleArgument(theEnv, context, returnValue, fname, kind, trees ? "their tree" : "their commit", &theArg)) == NULL) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the merge flags", GitMergeFlagNames,
	                              GIT_MERGE_FIND_RENAMES, &flags)) { return; }
	opts.flags = flags;

	error = trees ? git_merge_trees(&out, repo, (git_tree *) ancestor, (git_tree *) ours, (git_tree *) theirs, &opts)
	              : git_merge_commits(&out, repo, (git_commit *) ours, (git_commit *) theirs, &opts);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_INDEX, repo, NULL);
}

void GitMergeTreesFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitMergeTreesOrCommits(theEnv, context, returnValue, "git-merge-trees", true); }
void GitMergeCommitsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitMergeTreesOrCommits(theEnv, context, returnValue, "git-merge-commits", false); }

static void GitCherrypickOrRevert(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  bool cherrypick)
{
	UDFValue theArg;
	git_repository *repo;
	git_commit *commit;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }

	error = cherrypick ? git_cherrypick(repo, commit, NULL) : git_revert(repo, commit, NULL);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitCherrypickFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitCherrypickOrRevert(theEnv, context, returnValue, "git-cherrypick", true); }
void GitRevertFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitCherrypickOrRevert(theEnv, context, returnValue, "git-revert", false); }

static void GitCherrypickOrRevertCommit(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  bool cherrypick)
{
	UDFValue theArg;
	git_repository *repo;
	git_commit *commit, *our;
	long long mainline;
	git_index *out = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((commit = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if ((our = GitCommitArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	mainline = 0;
	if (UDFHasNextArgument(context) &&
	    !GitIntegerArgument(theEnv, context, returnValue, fname, "the mainline", &mainline)) { return; }

	error = cherrypick ? git_cherrypick_commit(&out, repo, commit, our, (unsigned int) mainline, NULL)
	                   : git_revert_commit(&out, repo, commit, our, (unsigned int) mainline, NULL);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, out, GIT_K_INDEX, repo, NULL);
}

void GitCherrypickCommitFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitCherrypickOrRevertCommit(theEnv, context, returnValue, "git-cherrypick-commit", true); }
void GitRevertCommitFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitCherrypickOrRevertCommit(theEnv, context, returnValue, "git-revert-commit", false); }

/* Graph queries */

void GitGraphAheadBehindFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-graph-ahead-behind";
	UDFValue theArg;
	git_repository *repo;
	git_oid local, upstream;
	size_t ahead = 0, behind = 0;
	MultifieldBuilder *mb;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the local commit id", &local)) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the upstream commit id", &upstream)) { return; }

	error = git_graph_ahead_behind(&ahead, &behind, repo, &local, &upstream);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	mb = CreateMultifieldBuilder(theEnv, 2);
	MBAppendInteger(mb, (long long) ahead);
	MBAppendInteger(mb, (long long) behind);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitGraphDescendantOfFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-graph-descendant-of";
	UDFValue theArg;
	git_repository *repo;
	git_oid commit, ancestor;
	int r;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the commit id", &commit)) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the ancestor id", &ancestor)) { return; }

	r = git_graph_descendant_of(repo, &commit, &ancestor);
	if (r < 0) { GitFailError(theEnv, returnValue, fname, r); return; }
	GitReturnBool(theEnv, returnValue, r);
}

/***************************************************************************/
/*                                                                         */
/* Stashes.                                                                */
/*                                                                         */
/***************************************************************************/

void GitStashSaveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-stash-save";
	UDFValue theArg;
	git_repository *repo;
	git_signature *stasher, *owned = NULL;
	const char *message;
	unsigned int flags;
	git_oid oid;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the stasher", repo, &stasher, &owned)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the message", &message) ||
	    !GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the stash flags", GitStashFlagNames,
	                              GIT_STASH_DEFAULT, &flags))
	{
		git_signature_free(owned);
		return;
	}

	error = git_stash_save(&oid, repo, stasher, message, flags);
	git_signature_free(owned);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

static void GitStashApplyOrPop(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  bool pop)
{
	UDFValue theArg;
	git_repository *repo;
	size_t n;
	unsigned int flags;
	git_stash_apply_options opts = GIT_STASH_APPLY_OPTIONS_INIT;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	n = 0;
	if (UDFHasNextArgument(context) && !GitSizeArgument(theEnv, context, returnValue, fname, "the stash number", &n)) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the apply flags", GitStashApplyFlagNames,
	                              GIT_STASH_APPLY_DEFAULT, &flags)) { return; }
	opts.flags = flags;

	error = pop ? git_stash_pop(repo, n, &opts) : git_stash_apply(repo, n, &opts);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

void GitStashApplyFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitStashApplyOrPop(theEnv, context, returnValue, "git-stash-apply", false); }
void GitStashPopFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitStashApplyOrPop(theEnv, context, returnValue, "git-stash-pop", true); }

void GitStashDropFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-stash-drop";
	UDFValue theArg;
	git_repository *repo;
	size_t n;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	n = 0;
	if (UDFHasNextArgument(context) && !GitSizeArgument(theEnv, context, returnValue, fname, "the stash number", &n)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_stash_drop(repo, n));
}

/* Every stash, as a multifield of message and id pairs. */
static int GitStashCollectCallback(
  size_t index,
  const char *message,
  const git_oid *stashId,
  void *payload)
{
	char hex[GIT_OID_MAX_HEXSIZE + 1];

	(void) index;
	git_oid_tostr(hex, sizeof hex, stashId);
	MBAppendString((MultifieldBuilder *) payload, message);
	MBAppendString((MultifieldBuilder *) payload, hex);
	return 0;
}

GIT_GETTER(GitStashListFunction, "git-stash-list", GIT_KM(GIT_K_REPOSITORY), "repository", {
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, 8);
	int error = git_stash_foreach((git_repository *) h, GitStashCollectCallback, mb);
	if (error != 0) { MBDispose(mb); GitFailError(theEnv, returnValue, "git-stash-list", error); return; }
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
})

/***************************************************************************/
/*                                                                         */
/* Describe.                                                               */
/*                                                                         */
/***************************************************************************/

static void GitDescribeCommon(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  bool workdir)
{
	UDFValue theArg;
	git_repository *repo = NULL;
	git_object *object = NULL;
	long long strategy;
	bool alwaysLong;
	git_describe_options opts = GIT_DESCRIBE_OPTIONS_INIT;
	git_describe_format_options fopts = GIT_DESCRIBE_FORMAT_OPTIONS_INIT;
	git_describe_result *result = NULL;
	git_buf out = GIT_BUF_INIT;
	int error;

	if (workdir)
	{
		if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	}
	else
	{
		if ((object = GitTreeishArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	}
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "the strategy", GitDescribeStrategyNames,
	                             GIT_DESCRIBE_DEFAULT, &strategy)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "always-long", &alwaysLong)) { return; }

	opts.describe_strategy = (unsigned int) strategy;
	opts.show_commit_oid_as_fallback = 1;
	fopts.always_use_long_format = alwaysLong ? 1 : 0;
	if (workdir) { fopts.dirty_suffix = "-dirty"; }

	error = workdir ? git_describe_workdir(&result, repo, &opts) : git_describe_commit(&result, object, &opts);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	error = git_describe_format(&out, result, &fopts);
	git_describe_result_free(result);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBuf(theEnv, returnValue, &out);
}

void GitDescribeCommitFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitDescribeCommon(theEnv, context, returnValue, "git-describe-commit", false); }
void GitDescribeWorkdirFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitDescribeCommon(theEnv, context, returnValue, "git-describe-workdir", true); }

/***************************************************************************/
/*                                                                         */
/* Blame.                                                                  */
/*                                                                         */
/***************************************************************************/

#define GIT_KBL GIT_KM(GIT_K_BLAME)

void GitBlameFileFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-blame-file";
	UDFValue theArg;
	git_repository *repo;
	const char *path;
	unsigned int flags;
	git_blame_options opts = GIT_BLAME_OPTIONS_INIT;
	git_blame *blame = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the blame flags", GitBlameFlagNames,
	                              GIT_BLAME_NORMAL, &flags)) { return; }
	opts.flags = flags;

	error = git_blame_file(&blame, repo, path, &opts);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, blame, GIT_K_BLAME, repo, NULL);
}

GIT_GETTER_INTEGER(GitBlameHunkcountFunction, "git-blame-hunkcount", GIT_KBL, "blame", git_blame_get_hunk_count((git_blame *) h))
GIT_FREE(GitBlameFreeFunction, "git-blame-free", GIT_KBL, "blame")

/* A hunk as a multifield: the number of lines in it, the final commit id,
   the final start line, the name of the author, the email of the author,
   the original commit id, the original path, and the original start line. */
static void GitReturnBlameHunk(
  Environment *theEnv,
  UDFValue *returnValue,
  const git_blame_hunk *hunk)
{
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, 8);
	char hex[GIT_OID_MAX_HEXSIZE + 1];

	MBAppendInteger(mb, (long long) hunk->lines_in_hunk);
	git_oid_tostr(hex, sizeof hex, &hunk->final_commit_id);
	MBAppendString(mb, hex);
	MBAppendInteger(mb, (long long) hunk->final_start_line_number);
	if (hunk->final_signature == NULL) { MBAppendSymbol(mb, "nil"); MBAppendSymbol(mb, "nil"); }
	else { MBAppendString(mb, hunk->final_signature->name); MBAppendString(mb, hunk->final_signature->email); }
	git_oid_tostr(hex, sizeof hex, &hunk->orig_commit_id);
	MBAppendString(mb, hex);
	if (hunk->orig_path == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, hunk->orig_path); }
	MBAppendInteger(mb, (long long) hunk->orig_start_line_number);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitBlameGetHunkByindexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-blame-get-hunk-byindex";
	UDFValue theArg;
	git_blame *blame;
	size_t n;
	const git_blame_hunk *hunk;

	if ((blame = GitBlameArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the hunk number", &n)) { return; }

	hunk = git_blame_get_hunk_byindex(blame, (uint32_t) n);
	if (hunk == NULL) { GitFail(theEnv, returnValue, fname, "the hunk number is out of range"); return; }

	GitReturnBlameHunk(theEnv, returnValue, hunk);
}

void GitBlameGetHunkBylineFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-blame-get-hunk-byline";
	UDFValue theArg;
	git_blame *blame;
	size_t n;
	const git_blame_hunk *hunk;

	if ((blame = GitBlameArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitSizeArgument(theEnv, context, returnValue, fname, "the line number", &n)) { return; }

	hunk = git_blame_get_hunk_byline(blame, n);
	if (hunk == NULL) { GitFail(theEnv, returnValue, fname, "no hunk covers that line"); return; }

	GitReturnBlameHunk(theEnv, returnValue, hunk);
}

/***************************************************************************/
/*                                                                         */
/* Reflogs and notes.                                                      */
/*                                                                         */
/***************************************************************************/

/* A reflog as a flat multifield with four elements for each entry, newest
   entry first: the old id, the new id, the name of the committer, and the
   message. A CLIPS multifield cannot hold another multifield, so the
   entries follow each other. The entry count is (length$) divided by four.
   The call reads and frees the reflog. It does not return a handle. */
void GitReflogReadFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reflog-read";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_reflog *reflog = NULL;
	MultifieldBuilder *mb;
	size_t n, i;
	char hex[GIT_OID_MAX_HEXSIZE + 1];
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	error = git_reflog_read(&reflog, repo, name);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	n = git_reflog_entrycount(reflog);
	mb = CreateMultifieldBuilder(theEnv, n * 4);
	for (i = 0; i < n; i++)
	{
		const git_reflog_entry *e = git_reflog_entry_byindex(reflog, i);
		const git_signature *who = git_reflog_entry_committer(e);
		const char *msg = git_reflog_entry_message(e);

		git_oid_tostr(hex, sizeof hex, git_reflog_entry_id_old(e));
		MBAppendString(mb, hex);
		git_oid_tostr(hex, sizeof hex, git_reflog_entry_id_new(e));
		MBAppendString(mb, hex);
		if (who == NULL || who->name == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, who->name); }
		if (msg == NULL) { MBAppendSymbol(mb, "nil"); } else { MBAppendString(mb, msg); }
	}
	git_reflog_free(reflog);

	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

void GitReflogDeleteFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-reflog-delete";
	UDFValue theArg;
	git_repository *repo;
	const char *name;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the reference name", false, &name)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_reflog_delete(repo, name));
}

void GitNoteReadFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-note-read";
	UDFValue theArg;
	git_repository *repo;
	const char *notesRef;
	git_oid oid;
	git_note *note = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the notes reference", &notesRef)) { return; }

	error = git_note_read(&note, repo, notesRef, &oid);
	if (error == GIT_ENOTFOUND) { GitReturnNil(theEnv, returnValue); return; }
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnText(theEnv, returnValue, git_note_message(note));
	git_note_free(note);
}

void GitNoteCreateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-note-create";
	UDFValue theArg;
	git_repository *repo;
	git_oid oid, out;
	const char *message, *notesRef;
	git_signature *author, *committer, *ownedAuthor = NULL, *ownedCommitter = NULL;
	bool force;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the message", false, &message)) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the author", repo, &author, &ownedAuthor)) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the committer", repo, &committer, &ownedCommitter))
	{
		git_signature_free(ownedAuthor);
		return;
	}
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the notes reference", &notesRef) ||
	    !GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "force", &force))
	{
		git_signature_free(ownedAuthor);
		git_signature_free(ownedCommitter);
		return;
	}

	error = git_note_create(&out, repo, notesRef, author, committer, &oid, message, force ? 1 : 0);
	git_signature_free(ownedAuthor);
	git_signature_free(ownedCommitter);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &out);
}

void GitNoteRemoveFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-note-remove";
	UDFValue theArg;
	git_repository *repo;
	git_oid oid;
	const char *notesRef;
	git_signature *author, *committer, *ownedAuthor = NULL, *ownedCommitter = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the author", repo, &author, &ownedAuthor)) { return; }
	if (!GitSignatureOrDefaultArgument(theEnv, context, returnValue, fname, "the committer", repo, &committer, &ownedCommitter))
	{
		git_signature_free(ownedAuthor);
		return;
	}
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the notes reference", &notesRef))
	{
		git_signature_free(ownedAuthor);
		git_signature_free(ownedCommitter);
		return;
	}

	error = git_note_remove(repo, notesRef, author, committer, &oid);
	git_signature_free(ownedAuthor);
	git_signature_free(ownedCommitter);
	GitReturnStatus(theEnv, returnValue, fname, error);
}

GIT_GETTER(GitNoteDefaultRefFunction, "git-note-default-ref", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_buf out = GIT_BUF_INIT;
	int error = git_note_default_ref(&out, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-note-default-ref", error); return; }
	GitReturnBuf(theEnv, returnValue, &out);
})

/***************************************************************************/
/*                                                                         */
/* Worktrees.                                                              */
/*                                                                         */
/***************************************************************************/

#define GIT_KWT GIT_KM(GIT_K_WORKTREE)

GIT_GETTER(GitWorktreeListFunction, "git-worktree-list", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_strarray names;
	int error;
	names.strings = NULL;
	names.count = 0;
	error = git_worktree_list(&names, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-worktree-list", error); return; }
	GitReturnStrarray(theEnv, returnValue, &names);
})

void GitWorktreeLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-worktree-lookup";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_worktree *wt = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the worktree name", false, &name)) { return; }

	error = git_worktree_lookup(&wt, repo, name);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, wt, GIT_K_WORKTREE, repo, NULL);
}

GIT_GETTER(GitWorktreeOpenFromRepositoryFunction, "git-worktree-open-from-repository", GIT_KM(GIT_K_REPOSITORY), "repository", {
	git_worktree *wt = NULL;
	int error = git_worktree_open_from_repository(&wt, (git_repository *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-worktree-open-from-repository", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-worktree-open-from-repository", wt, GIT_K_WORKTREE, (git_repository *) h, NULL);
})

void GitWorktreeAddFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-worktree-add";
	UDFValue theArg;
	git_repository *repo;
	const char *name, *path;
	void *ref = NULL;
	bool lock;
	git_worktree_add_options opts = GIT_WORKTREE_ADD_OPTIONS_INIT;
	git_worktree *wt = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the worktree name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (UDFHasNextArgument(context) &&
	    !GitHandleOrNilArgument(theEnv, context, returnValue, fname, GIT_KM(GIT_K_REFERENCE), "the reference", &ref)) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "lock", &lock)) { return; }

	opts.ref = (git_reference *) ref;
	opts.lock = lock ? 1 : 0;

	error = git_worktree_add(&wt, repo, name, path, &opts);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, wt, GIT_K_WORKTREE, repo, NULL);
}

GIT_GETTER_TEXT(GitWorktreeNameFunction, "git-worktree-name", GIT_KWT, "worktree", git_worktree_name((git_worktree *) h))
GIT_GETTER_TEXT(GitWorktreePathFunction, "git-worktree-path", GIT_KWT, "worktree", git_worktree_path((git_worktree *) h))
GIT_GETTER_STATUS(GitWorktreeValidateFunction, "git-worktree-validate", GIT_KWT, "worktree", git_worktree_validate((git_worktree *) h))
GIT_GETTER_STATUS(GitWorktreeUnlockFunction, "git-worktree-unlock", GIT_KWT, "worktree", git_worktree_unlock((git_worktree *) h))
GIT_FREE(GitWorktreeFreeFunction, "git-worktree-free", GIT_KWT, "worktree")

void GitWorktreeLockFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-worktree-lock";
	UDFValue theArg;
	git_worktree *wt;
	const char *reason;

	if ((wt = GitWorktreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalTextArgument(theEnv, context, returnValue, fname, "the reason", &reason)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_worktree_lock(wt, reason));
}

/* Shows if the worktree is locked. If it is locked, the result is the
   reason as a string. If it is not locked, the result is FALSE. */
GIT_GETTER(GitWorktreeIsLockedFunction, "git-worktree-is-locked", GIT_KWT, "worktree", {
	git_buf reason = GIT_BUF_INIT;
	int r = git_worktree_is_locked(&reason, (git_worktree *) h);
	if (r < 0) { GitFailError(theEnv, returnValue, "git-worktree-is-locked", r); return; }
	if (r == 0) { git_buf_dispose(&reason); returnValue->lexemeValue = FalseSymbol(theEnv); return; }
	GitReturnBuf(theEnv, returnValue, &reason);
})

void GitWorktreeIsPrunableFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-worktree-is-prunable";
	UDFValue theArg;
	git_worktree *wt;
	unsigned int flags;
	git_worktree_prune_options opts = GIT_WORKTREE_PRUNE_OPTIONS_INIT;
	int r;

	if ((wt = GitWorktreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the prune flags", GitWorktreePruneNames, 0, &flags)) { return; }
	opts.flags = flags;

	r = git_worktree_is_prunable(wt, &opts);
	if (r < 0) { GitFailError(theEnv, returnValue, fname, r); return; }
	GitReturnBool(theEnv, returnValue, r);
}

void GitWorktreePruneFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-worktree-prune";
	UDFValue theArg;
	git_worktree *wt;
	unsigned int flags;
	git_worktree_prune_options opts = GIT_WORKTREE_PRUNE_OPTIONS_INIT;

	if ((wt = GitWorktreeArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalFlagsArgument(theEnv, context, returnValue, fname, "the prune flags", GitWorktreePruneNames, 0, &flags)) { return; }
	opts.flags = flags;

	GitReturnStatus(theEnv, returnValue, fname, git_worktree_prune(wt, &opts));
}

/***************************************************************************/
/*                                                                         */
/* Submodules.                                                             */
/*                                                                         */
/***************************************************************************/

#define GIT_KSM GIT_KM(GIT_K_SUBMODULE)

void GitSubmoduleLookupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-submodule-lookup";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	git_submodule *sm = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the submodule name", false, &name)) { return; }

	error = git_submodule_lookup(&sm, repo, name);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, sm, GIT_K_SUBMODULE, repo, NULL);
}

static int GitSubmoduleCollectCallback(
  git_submodule *sm,
  const char *name,
  void *payload)
{
	(void) sm;
	MBAppendString((MultifieldBuilder *) payload, name);
	return 0;
}

GIT_GETTER(GitSubmoduleListFunction, "git-submodule-list", GIT_KM(GIT_K_REPOSITORY), "repository", {
	MultifieldBuilder *mb = CreateMultifieldBuilder(theEnv, 8);
	int error = git_submodule_foreach((git_repository *) h, GitSubmoduleCollectCallback, mb);
	if (error != 0) { MBDispose(mb); GitFailError(theEnv, returnValue, "git-submodule-list", error); return; }
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
})

void GitSubmoduleAddSetupFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-submodule-add-setup";
	UDFValue theArg;
	git_repository *repo;
	const char *url, *path;
	bool useGitlink;
	git_submodule *sm = NULL;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the url", false, &url)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	useGitlink = true;
	if (UDFHasNextArgument(context) &&
	    !GitBooleanArgument(theEnv, context, returnValue, fname, "use-gitlink", &useGitlink)) { return; }

	error = git_submodule_add_setup(&sm, repo, url, path, useGitlink ? 1 : 0);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, sm, GIT_K_SUBMODULE, repo, NULL);
}

GIT_GETTER_TEXT(GitSubmoduleNameFunction, "git-submodule-name", GIT_KSM, "submodule", git_submodule_name((git_submodule *) h))
GIT_GETTER_TEXT(GitSubmodulePathFunction, "git-submodule-path", GIT_KSM, "submodule", git_submodule_path((git_submodule *) h))
GIT_GETTER_TEXT(GitSubmoduleUrlFunction, "git-submodule-url", GIT_KSM, "submodule", git_submodule_url((git_submodule *) h))
GIT_GETTER_TEXT(GitSubmoduleBranchFunction, "git-submodule-branch", GIT_KSM, "submodule", git_submodule_branch((git_submodule *) h))
GIT_GETTER_OID(GitSubmoduleHeadIdFunction, "git-submodule-head-id", GIT_KSM, "submodule", git_submodule_head_id((git_submodule *) h))
GIT_GETTER_OID(GitSubmoduleIndexIdFunction, "git-submodule-index-id", GIT_KSM, "submodule", git_submodule_index_id((git_submodule *) h))
GIT_GETTER_OID(GitSubmoduleWdIdFunction, "git-submodule-wd-id", GIT_KSM, "submodule", git_submodule_wd_id((git_submodule *) h))
GIT_GETTER_ENUM(GitSubmoduleIgnoreFunction, "git-submodule-ignore", GIT_KSM, "submodule", GitSubmoduleIgnoreNames, git_submodule_ignore((git_submodule *) h))
GIT_GETTER_STATUS(GitSubmoduleAddFinalizeFunction, "git-submodule-add-finalize", GIT_KSM, "submodule", git_submodule_add_finalize((git_submodule *) h))
GIT_GETTER_STATUS(GitSubmoduleSyncFunction, "git-submodule-sync", GIT_KSM, "submodule", git_submodule_sync((git_submodule *) h))
GIT_FREE(GitSubmoduleFreeFunction, "git-submodule-free", GIT_KSM, "submodule")

/* Clones the submodule into its location in the working directory. This
   step must occur after git_submodule_add_setup and before
   git_submodule_add_finalize. */
GIT_GETTER(GitSubmoduleCloneFunction, "git-submodule-clone", GIT_KSM, "submodule", {
	git_repository *repo = NULL;
	git_submodule_update_options opts = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
	struct GitCredentialPayload payload;
	int error;
	GitRemoteCallbacksInit(&opts.fetch_opts.callbacks, &payload, theEnv);
	error = git_submodule_clone(&repo, (git_submodule *) h, &opts);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-submodule-clone", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-submodule-clone", repo, GIT_K_REPOSITORY, repo, NULL);
})

void GitSubmoduleRepoInitFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-submodule-repo-init";
	UDFValue theArg;
	git_submodule *sm;
	bool useGitlink;
	git_repository *repo = NULL;
	int error;

	if ((sm = GitSubmoduleArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	useGitlink = true;
	if (UDFHasNextArgument(context) &&
	    !GitBooleanArgument(theEnv, context, returnValue, fname, "use-gitlink", &useGitlink)) { return; }

	error = git_submodule_repo_init(&repo, sm, useGitlink ? 1 : 0);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnHandle(theEnv, returnValue, fname, repo, GIT_K_REPOSITORY, repo, NULL);
}

GIT_GETTER(GitSubmoduleOpenFunction, "git-submodule-open", GIT_KSM, "submodule", {
	git_repository *repo = NULL;
	int error = git_submodule_open(&repo, (git_submodule *) h);
	if (error != 0) { GitFailError(theEnv, returnValue, "git-submodule-open", error); return; }
	GitReturnHandle(theEnv, returnValue, "git-submodule-open", repo, GIT_K_REPOSITORY, repo, NULL);
})

static void GitSubmoduleFlag(
  Environment *theEnv,
  UDFContext *context,
  UDFValue *returnValue,
  const char *fname,
  const char *what,
  int (*fn)(git_submodule *, int))
{
	UDFValue theArg;
	git_submodule *sm;
	bool flag;

	if ((sm = GitSubmoduleArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, what, &flag)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, fn(sm, flag ? 1 : 0));
}

void GitSubmoduleInitFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitSubmoduleFlag(theEnv, context, returnValue, "git-submodule-init", "overwrite", git_submodule_init); }
void GitSubmoduleReloadFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitSubmoduleFlag(theEnv, context, returnValue, "git-submodule-reload", "force", git_submodule_reload); }
void GitSubmoduleAddToIndexFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{ GitSubmoduleFlag(theEnv, context, returnValue, "git-submodule-add-to-index", "write-index", git_submodule_add_to_index); }

void GitSubmoduleUpdateFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-submodule-update";
	UDFValue theArg;
	git_submodule *sm;
	bool init;
	git_submodule_update_options opts = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
	struct GitCredentialPayload payload;

	if ((sm = GitSubmoduleArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOptionalBooleanArgument(theEnv, context, returnValue, fname, "init", &init)) { return; }

	GitRemoteCallbacksInit(&opts.fetch_opts.callbacks, &payload, theEnv);
	GitReturnStatus(theEnv, returnValue, fname, git_submodule_update(sm, init ? 1 : 0, &opts));
}

void GitSubmoduleStatusFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-submodule-status";
	UDFValue theArg;
	git_repository *repo;
	const char *name;
	long long ignore;
	unsigned int status = 0;
	int error;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the submodule name", false, &name)) { return; }
	if (!GitOptionalEnumArgument(theEnv, context, returnValue, fname, "what to ignore", GitSubmoduleIgnoreNames,
	                             GIT_SUBMODULE_IGNORE_UNSPECIFIED, &ignore)) { return; }

	error = git_submodule_status(&status, repo, name, (git_submodule_ignore_t) ignore);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnFlags(theEnv, returnValue, GitSubmoduleStatusNames, status);
}

void GitSubmoduleSetUrlFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-submodule-set-url";
	UDFValue theArg;
	git_repository *repo;
	const char *name, *url;

	if ((repo = GitRepoArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the submodule name", false, &name)) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the url", false, &url)) { return; }

	GitReturnStatus(theEnv, returnValue, fname, git_submodule_set_url(repo, name, url));
}

/***************************************************************************/
/*                                                                         */
/* The object database.                                                    */
/*                                                                         */
/***************************************************************************/

#define GIT_KO GIT_KM(GIT_K_ODB)

GIT_FREE(GitOdbFreeFunction, "git-odb-free", GIT_KO, "odb")
GIT_GETTER_STATUS(GitOdbRefreshFunction, "git-odb-refresh", GIT_KO, "odb", git_odb_refresh((git_odb *) h))

void GitOdbExistsFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-odb-exists";
	UDFValue theArg;
	git_odb *odb;
	git_oid oid;

	if ((odb = GitOdbArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }

	GitReturnBool(theEnv, returnValue, git_odb_exists(odb, &oid));
}

/* The type and size of an object, as a multifield of two elements. The call
   does not read the object. */
void GitOdbReadHeaderFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-odb-read-header";
	UDFValue theArg;
	git_odb *odb;
	git_oid oid;
	size_t len = 0;
	git_object_t type = GIT_OBJECT_INVALID;
	MultifieldBuilder *mb;
	int error;

	if ((odb = GitOdbArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }

	error = git_odb_read_header(&len, &type, odb, &oid);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	mb = CreateMultifieldBuilder(theEnv, 2);
	MBAppendSymbol(mb, GitEnumName(GitObjectTypeNames, type));
	MBAppendInteger(mb, (long long) len);
	returnValue->multifieldValue = MBCreate(mb);
	MBDispose(mb);
}

/* The raw bytes of an object. */
void GitOdbReadFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-odb-read";
	UDFValue theArg;
	git_odb *odb;
	git_oid oid;
	git_odb_object *object = NULL;
	int error;

	if ((odb = GitOdbArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, fname, "the object id", &oid)) { return; }

	error = git_odb_read(&object, odb, &oid);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnBytes(theEnv, returnValue, fname, git_odb_object_data(object), git_odb_object_size(object));
	git_odb_object_free(object);
}

void GitOdbWriteFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-odb-write";
	UDFValue theArg;
	git_odb *odb;
	const char *data;
	long long type;
	git_oid oid;
	int error;

	if ((odb = GitOdbArgument(theEnv, context, returnValue, fname, &theArg)) == NULL) { return; }
	if (!GitTextArgument(theEnv, context, returnValue, fname, "the content", false, &data)) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the object type", GitObjectTypeNames, &type)) { return; }

	error = git_odb_write(&oid, odb, data, strlen(data), (git_object_t) type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

void GitOdbHashFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-odb-hash";
	const char *data;
	long long type;
	git_oid oid;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the content", false, &data)) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the object type", GitObjectTypeNames, &type)) { return; }

	error = git_odb_hash(&oid, data, strlen(data), (git_object_t) type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

void GitOdbHashfileFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *fname = "git-odb-hashfile";
	const char *path;
	long long type;
	git_oid oid;
	int error;

	if (!GitTextArgument(theEnv, context, returnValue, fname, "the path", false, &path)) { return; }
	if (!GitEnumArgument(theEnv, context, returnValue, fname, "the object type", GitObjectTypeNames, &type)) { return; }

	error = git_odb_hashfile(&oid, path, (git_object_t) type);
	if (error != 0) { GitFailError(theEnv, returnValue, fname, error); return; }

	GitReturnOid(theEnv, returnValue, &oid);
}

/* Object ids as text */

void GitOidIsZeroFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	git_oid oid;

	if (!GitOidArgument(theEnv, context, returnValue, "git-oid-is-zero", "the object id", &oid)) { return; }
	GitReturnBool(theEnv, returnValue, git_oid_is_zero(&oid));
}

void GitOidCmpFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	git_oid a, b;

	if (!GitOidArgument(theEnv, context, returnValue, "git-oid-cmp", "the first object id", &a)) { return; }
	if (!GitOidArgument(theEnv, context, returnValue, "git-oid-cmp", "the second object id", &b)) { return; }
	returnValue->integerValue = CreateInteger(theEnv, git_oid_cmp(&a, &b));
}

void GitOidIsValidFunction(Environment *theEnv, UDFContext *context, UDFValue *returnValue)
{
	const char *text;
	git_oid oid;

	if (!GitTextArgument(theEnv, context, returnValue, "git-oid-is-valid", "the text", false, &text)) { return; }
	GitReturnBool(theEnv, returnValue, git_oid_fromstr(&oid, text) == 0);
}

/*********************************************************/
/*                                                       */
/* UserFunctions.                                        */
/*                                                       */
/* Tells the expert system environment about the user-   */
/* defined functions. This file registers the git-*      */
/* functions.                                            */
/*                                                       */
/*********************************************************/
void UserFunctions(
  Environment *env)
{
	/* libgit2 keeps its own initialization count, and the environment
	   is not usable without one initialization. A program that needs to
	   release it can use git-libgit2-shutdown. */
	GitEnv = env;
	git_libgit2_init();

	/* The logical name git-secret. See the Credentials section. */
	AddRouter(env, GIT_SECRET_ROUTER_NAME, 30, GitSecretQuery, NULL, GitSecretRead, GitSecretUnread, GitSecretExit, NULL);

	/* doc-group: The library */
	AddUDF(env,"git-libgit2-init","bl",0,0,NULL,GitLibgit2InitFunction,"GitLibgit2InitFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"git-libgit2-shutdown","bl",0,0,NULL,GitLibgit2ShutdownFunction,"GitLibgit2ShutdownFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"git-libgit2-version","s",0,0,NULL,GitLibgit2VersionFunction,"GitLibgit2VersionFunction",NULL);  /* doc: -> string */
	AddUDF(env,"git-libgit2-version-number","l",0,0,NULL,GitLibgit2VersionNumberFunction,"GitLibgit2VersionNumberFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"git-built-version","l",0,0,NULL,GitBuiltVersionFunction,"GitBuiltVersionFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"git-libgit2-prerelease","sy",0,0,NULL,GitLibgit2PrereleaseFunction,"GitLibgit2PrereleaseFunction",NULL);  /* doc: -> string */
	AddUDF(env,"git-libgit2-features","m",0,0,NULL,GitLibgit2FeaturesFunction,"GitLibgit2FeaturesFunction",NULL);  /* doc: -> multifield */
	AddUDF(env,"git-handle-count","l",0,0,NULL,GitHandleCountFunction,"GitHandleCountFunction",NULL);  /* doc: -> integer */
	AddUDF(env,"git-handle-kinds","m",0,0,NULL,GitHandleKindsFunction,"GitHandleKindsFunction",NULL);  /* doc: -> multifield */
	AddUDF(env,"git-error-last","sy",0,0,NULL,GitErrorLastFunction,"GitErrorLastFunction",NULL);  /* doc: -> string */
	AddUDF(env,"git-error-last-class","y",0,0,NULL,GitErrorLastClassFunction,"GitErrorLastClassFunction",NULL);  /* doc: -> symbol */

	/* doc-group: Repositories */
	AddUDF(env,"git-repository-open","be",1,1,";sy",GitRepositoryOpenFunction,"GitRepositoryOpenFunction",NULL);  /* doc: path -> handle */
	AddUDF(env,"git-repository-open-ext","be",1,3,";sy;lmsy;sy",GitRepositoryOpenExtFunction,"GitRepositoryOpenExtFunction",NULL);  /* doc: path flags? ceiling-dirs? -> handle */
	AddUDF(env,"git-repository-open-bare","be",1,1,";sy",GitRepositoryOpenBareFunction,"GitRepositoryOpenBareFunction",NULL);  /* doc: path -> handle */
	AddUDF(env,"git-repository-open-from-worktree","be",1,1,";e",GitRepositoryOpenFromWorktreeFunction,"GitRepositoryOpenFromWorktreeFunction",NULL);  /* doc: worktree -> handle */
	AddUDF(env,"git-repository-init","be",1,2,";sy;b",GitRepositoryInitFunction,"GitRepositoryInitFunction",NULL);  /* doc: path bare? -> handle */
	AddUDF(env,"git-repository-init-ext","be",1,6,";sy;lmsy;sy;sy;sy;sy",GitRepositoryInitExtFunction,"GitRepositoryInitExtFunction",NULL);  /* doc: path flags? initial-head? description? origin-url? workdir? -> handle */
	AddUDF(env,"git-repository-discover","bs",1,3,";sy;b;sy",GitRepositoryDiscoverFunction,"GitRepositoryDiscoverFunction",NULL);  /* doc: start-path across-fs? ceiling-dirs? -> string */
	AddUDF(env,"git-clone","be",2,4,";sy;sy;b;sy",GitCloneFunction,"GitCloneFunction",NULL);  /* doc: url path bare? branch? -> handle */
	AddUDF(env,"git-repository-free","b",1,1,";e",GitRepositoryFreeFunction,"GitRepositoryFreeFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-path","bsy",1,1,";e",GitRepositoryPathFunction,"GitRepositoryPathFunction",NULL);  /* doc: repo -> string */
	AddUDF(env,"git-repository-workdir","bsy",1,1,";e",GitRepositoryWorkdirFunction,"GitRepositoryWorkdirFunction",NULL);  /* doc: repo -> string */
	AddUDF(env,"git-repository-commondir","bsy",1,1,";e",GitRepositoryCommondirFunction,"GitRepositoryCommondirFunction",NULL);  /* doc: repo -> string */
	AddUDF(env,"git-repository-is-bare","b",1,1,";e",GitRepositoryIsBareFunction,"GitRepositoryIsBareFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-is-empty","b",1,1,";e",GitRepositoryIsEmptyFunction,"GitRepositoryIsEmptyFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-is-shallow","b",1,1,";e",GitRepositoryIsShallowFunction,"GitRepositoryIsShallowFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-is-worktree","b",1,1,";e",GitRepositoryIsWorktreeFunction,"GitRepositoryIsWorktreeFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-head","be",1,1,";e",GitRepositoryHeadFunction,"GitRepositoryHeadFunction",NULL);  /* doc: repo -> handle */
	AddUDF(env,"git-repository-head-detached","b",1,1,";e",GitRepositoryHeadDetachedFunction,"GitRepositoryHeadDetachedFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-head-unborn","b",1,1,";e",GitRepositoryHeadUnbornFunction,"GitRepositoryHeadUnbornFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-set-head","b",2,2,";e;sy",GitRepositorySetHeadFunction,"GitRepositorySetHeadFunction",NULL);  /* doc: repo refname -> boolean */
	AddUDF(env,"git-repository-set-head-detached","b",2,2,";e;sy",GitRepositorySetHeadDetachedFunction,"GitRepositorySetHeadDetachedFunction",NULL);  /* doc: repo commit-id -> boolean */
	AddUDF(env,"git-repository-detach-head","b",1,1,";e",GitRepositoryDetachHeadFunction,"GitRepositoryDetachHeadFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-state","by",1,1,";e",GitRepositoryStateFunction,"GitRepositoryStateFunction",NULL);  /* doc: repo -> symbol */
	AddUDF(env,"git-repository-state-cleanup","b",1,1,";e",GitRepositoryStateCleanupFunction,"GitRepositoryStateCleanupFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-message","bsy",1,1,";e",GitRepositoryMessageFunction,"GitRepositoryMessageFunction",NULL);  /* doc: repo -> string */
	AddUDF(env,"git-repository-message-remove","b",1,1,";e",GitRepositoryMessageRemoveFunction,"GitRepositoryMessageRemoveFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-repository-config","be",1,1,";e",GitRepositoryConfigFunction,"GitRepositoryConfigFunction",NULL);  /* doc: repo -> handle */
	AddUDF(env,"git-repository-config-snapshot","be",1,1,";e",GitRepositoryConfigSnapshotFunction,"GitRepositoryConfigSnapshotFunction",NULL);  /* doc: repo -> handle */
	AddUDF(env,"git-repository-index","be",1,1,";e",GitRepositoryIndexFunction,"GitRepositoryIndexFunction",NULL);  /* doc: repo -> handle */
	AddUDF(env,"git-repository-odb","be",1,1,";e",GitRepositoryOdbFunction,"GitRepositoryOdbFunction",NULL);  /* doc: repo -> handle */
	AddUDF(env,"git-repository-ident","bm",1,1,";e",GitRepositoryIdentFunction,"GitRepositoryIdentFunction",NULL);  /* doc: repo -> multifield */
	AddUDF(env,"git-repository-set-ident","b",3,3,";e;sy;sy",GitRepositorySetIdentFunction,"GitRepositorySetIdentFunction",NULL);  /* doc: repo name email -> boolean */
	AddUDF(env,"git-repository-set-workdir","b",2,3,";e;sy;b",GitRepositorySetWorkdirFunction,"GitRepositorySetWorkdirFunction",NULL);  /* doc: repo workdir update-gitlink? -> boolean */
	AddUDF(env,"git-repository-get-namespace","bsy",1,1,";e",GitRepositoryGetNamespaceFunction,"GitRepositoryGetNamespaceFunction",NULL);  /* doc: repo -> string */
	AddUDF(env,"git-repository-set-namespace","b",2,2,";e;sy",GitRepositorySetNamespaceFunction,"GitRepositorySetNamespaceFunction",NULL);  /* doc: repo namespace -> boolean */
	AddUDF(env,"git-repository-item-path","bsy",2,2,";e;lsy",GitRepositoryItemPathFunction,"GitRepositoryItemPathFunction",NULL);  /* doc: repo item -> string */
	AddUDF(env,"git-repository-hashfile","bs",2,4,";e;sy;lsy;sy",GitRepositoryHashfileFunction,"GitRepositoryHashfileFunction",NULL);  /* doc: repo path type? as-path? -> string */

	/* doc-group: Objects */
	AddUDF(env,"git-object-lookup","be",2,3,";e;sy;lsy",GitObjectLookupFunction,"GitObjectLookupFunction",NULL);  /* doc: repo id type? -> handle */
	AddUDF(env,"git-object-lookup-prefix","be",2,3,";e;sy;lsy",GitObjectLookupPrefixFunction,"GitObjectLookupPrefixFunction",NULL);  /* doc: repo prefix type? -> handle */
	AddUDF(env,"git-object-id","bs",1,1,";e",GitObjectIdFunction,"GitObjectIdFunction",NULL);  /* doc: object -> string */
	AddUDF(env,"git-object-short-id","bs",1,1,";e",GitObjectShortIdFunction,"GitObjectShortIdFunction",NULL);  /* doc: object -> string */
	AddUDF(env,"git-object-type","by",1,1,";e",GitObjectTypeFunction,"GitObjectTypeFunction",NULL);  /* doc: object -> symbol */
	AddUDF(env,"git-object-peel","be",2,2,";e;lsy",GitObjectPeelFunction,"GitObjectPeelFunction",NULL);  /* doc: object type -> handle */
	AddUDF(env,"git-object-free","b",1,1,";e",GitObjectFreeFunction,"GitObjectFreeFunction",NULL);  /* doc: object -> boolean */
	AddUDF(env,"git-object-type2string","bs",1,1,";lsy",GitObjectType2stringFunction,"GitObjectType2stringFunction",NULL);  /* doc: type -> string */
	AddUDF(env,"git-object-string2type","by",1,1,";sy",GitObjectString2typeFunction,"GitObjectString2typeFunction",NULL);  /* doc: name -> symbol */
	AddUDF(env,"git-object-rawcontent-is-valid","b",2,2,";sy;lsy",GitObjectRawcontentIsValidFunction,"GitObjectRawcontentIsValidFunction",NULL);  /* doc: content type -> boolean */
	AddUDF(env,"git-oid-is-valid","b",1,1,";sy",GitOidIsValidFunction,"GitOidIsValidFunction",NULL);  /* doc: text -> boolean */
	AddUDF(env,"git-oid-is-zero","b",1,1,";sy",GitOidIsZeroFunction,"GitOidIsZeroFunction",NULL);  /* doc: id -> boolean */
	AddUDF(env,"git-oid-cmp","bl",2,2,";sy;sy",GitOidCmpFunction,"GitOidCmpFunction",NULL);  /* doc: id-a id-b -> integer */

	/* doc-group: Signatures */
	AddUDF(env,"git-signature-new","be",4,4,";sy;sy;l;l",GitSignatureNewFunction,"GitSignatureNewFunction",NULL);  /* doc: name email time offset -> handle */
	AddUDF(env,"git-signature-now","be",2,2,";sy;sy",GitSignatureNowFunction,"GitSignatureNowFunction",NULL);  /* doc: name email -> handle */
	AddUDF(env,"git-signature-default","be",1,1,";e",GitSignatureDefaultFunction,"GitSignatureDefaultFunction",NULL);  /* doc: repo -> handle */
	AddUDF(env,"git-signature-from-buffer","be",1,1,";sy",GitSignatureFromBufferFunction,"GitSignatureFromBufferFunction",NULL);  /* doc: text -> handle */
	AddUDF(env,"git-signature-dup","be",1,1,";e",GitSignatureDupFunction,"GitSignatureDupFunction",NULL);  /* doc: signature -> handle */
	AddUDF(env,"git-signature-name","bsy",1,1,";e",GitSignatureNameFunction,"GitSignatureNameFunction",NULL);  /* doc: signature -> string */
	AddUDF(env,"git-signature-email","bsy",1,1,";e",GitSignatureEmailFunction,"GitSignatureEmailFunction",NULL);  /* doc: signature -> string */
	AddUDF(env,"git-signature-time","bl",1,1,";e",GitSignatureTimeFunction,"GitSignatureTimeFunction",NULL);  /* doc: signature -> integer */
	AddUDF(env,"git-signature-offset","bl",1,1,";e",GitSignatureOffsetFunction,"GitSignatureOffsetFunction",NULL);  /* doc: signature -> integer */
	AddUDF(env,"git-signature-free","b",1,1,";e",GitSignatureFreeFunction,"GitSignatureFreeFunction",NULL);  /* doc: signature -> boolean */

	/* doc-group: Commits */
	AddUDF(env,"git-commit-lookup","be",2,2,";e;sy",GitCommitLookupFunction,"GitCommitLookupFunction",NULL);  /* doc: repo id -> handle */
	AddUDF(env,"git-commit-lookup-prefix","be",2,2,";e;sy",GitCommitLookupPrefixFunction,"GitCommitLookupPrefixFunction",NULL);  /* doc: repo prefix -> handle */
	AddUDF(env,"git-commit-create","bs",7,7,";e;sy;ey;ey;sy;e;m",GitCommitCreateFunction,"GitCommitCreateFunction",NULL);  /* doc: repo update-ref author committer message tree parents -> string */
	AddUDF(env,"git-commit-create-buffer","bs",6,6,";e;ey;ey;sy;e;m",GitCommitCreateBufferFunction,"GitCommitCreateBufferFunction",NULL);  /* doc: repo author committer message tree parents -> string */
	AddUDF(env,"git-commit-amend","bs",6,6,";e;sy;ey;ey;sy;ey",GitCommitAmendFunction,"GitCommitAmendFunction",NULL);  /* doc: commit update-ref author committer message tree -> string */
	AddUDF(env,"git-commit-id","bs",1,1,";e",GitCommitIdFunction,"GitCommitIdFunction",NULL);  /* doc: commit -> string */
	AddUDF(env,"git-commit-message","bsy",1,1,";e",GitCommitMessageFunction,"GitCommitMessageFunction",NULL);  /* doc: commit -> string */
	AddUDF(env,"git-commit-message-raw","bsy",1,1,";e",GitCommitMessageRawFunction,"GitCommitMessageRawFunction",NULL);  /* doc: commit -> string */
	AddUDF(env,"git-commit-message-encoding","bsy",1,1,";e",GitCommitMessageEncodingFunction,"GitCommitMessageEncodingFunction",NULL);  /* doc: commit -> string */
	AddUDF(env,"git-commit-summary","bsy",1,1,";e",GitCommitSummaryFunction,"GitCommitSummaryFunction",NULL);  /* doc: commit -> string */
	AddUDF(env,"git-commit-body","bsy",1,1,";e",GitCommitBodyFunction,"GitCommitBodyFunction",NULL);  /* doc: commit -> string */
	AddUDF(env,"git-commit-raw-header","bsy",1,1,";e",GitCommitRawHeaderFunction,"GitCommitRawHeaderFunction",NULL);  /* doc: commit -> string */
	AddUDF(env,"git-commit-header-field","bsy",2,2,";e;sy",GitCommitHeaderFieldFunction,"GitCommitHeaderFieldFunction",NULL);  /* doc: commit field -> string */
	AddUDF(env,"git-commit-time","bl",1,1,";e",GitCommitTimeFunction,"GitCommitTimeFunction",NULL);  /* doc: commit -> integer */
	AddUDF(env,"git-commit-time-offset","bl",1,1,";e",GitCommitTimeOffsetFunction,"GitCommitTimeOffsetFunction",NULL);  /* doc: commit -> integer */
	AddUDF(env,"git-commit-author","bey",1,1,";e",GitCommitAuthorFunction,"GitCommitAuthorFunction",NULL);  /* doc: commit -> handle */
	AddUDF(env,"git-commit-committer","bey",1,1,";e",GitCommitCommitterFunction,"GitCommitCommitterFunction",NULL);  /* doc: commit -> handle */
	AddUDF(env,"git-commit-tree","be",1,1,";e",GitCommitTreeFunction,"GitCommitTreeFunction",NULL);  /* doc: commit -> handle */
	AddUDF(env,"git-commit-tree-id","bs",1,1,";e",GitCommitTreeIdFunction,"GitCommitTreeIdFunction",NULL);  /* doc: commit -> string */
	AddUDF(env,"git-commit-parentcount","bl",1,1,";e",GitCommitParentcountFunction,"GitCommitParentcountFunction",NULL);  /* doc: commit -> integer */
	AddUDF(env,"git-commit-parent","be",2,2,";e;l",GitCommitParentFunction,"GitCommitParentFunction",NULL);  /* doc: commit n -> handle */
	AddUDF(env,"git-commit-parent-id","bs",2,2,";e;l",GitCommitParentIdFunction,"GitCommitParentIdFunction",NULL);  /* doc: commit n -> string */
	AddUDF(env,"git-commit-nth-gen-ancestor","be",2,2,";e;l",GitCommitNthGenAncestorFunction,"GitCommitNthGenAncestorFunction",NULL);  /* doc: commit n -> handle */
	AddUDF(env,"git-commit-to-fact","bf",2,2,";e;y",GitCommitToFactFunction,"GitCommitToFactFunction",NULL);  /* doc: commit deftemplate -> fact */
	AddUDF(env,"git-commit-to-instance","bi",2,3,";e;y;yn",GitCommitToInstanceFunction,"GitCommitToInstanceFunction",NULL);  /* doc: commit defclass instance-name? -> instance */
	AddUDF(env,"git-commit-free","b",1,1,";e",GitCommitFreeFunction,"GitCommitFreeFunction",NULL);  /* doc: commit -> boolean */

	/* doc-group: Walking history */
	AddUDF(env,"git-revwalk-new","be",1,1,";e",GitRevwalkNewFunction,"GitRevwalkNewFunction",NULL);  /* doc: repo -> handle */
	AddUDF(env,"git-revwalk-push","b",2,2,";e;sy",GitRevwalkPushFunction,"GitRevwalkPushFunction",NULL);  /* doc: walk id -> boolean */
	AddUDF(env,"git-revwalk-push-head","b",1,1,";e",GitRevwalkPushHeadFunction,"GitRevwalkPushHeadFunction",NULL);  /* doc: walk -> boolean */
	AddUDF(env,"git-revwalk-push-ref","b",2,2,";e;sy",GitRevwalkPushRefFunction,"GitRevwalkPushRefFunction",NULL);  /* doc: walk refname -> boolean */
	AddUDF(env,"git-revwalk-push-glob","b",2,2,";e;sy",GitRevwalkPushGlobFunction,"GitRevwalkPushGlobFunction",NULL);  /* doc: walk glob -> boolean */
	AddUDF(env,"git-revwalk-push-range","b",2,2,";e;sy",GitRevwalkPushRangeFunction,"GitRevwalkPushRangeFunction",NULL);  /* doc: walk range -> boolean */
	AddUDF(env,"git-revwalk-hide","b",2,2,";e;sy",GitRevwalkHideFunction,"GitRevwalkHideFunction",NULL);  /* doc: walk id -> boolean */
	AddUDF(env,"git-revwalk-hide-head","b",1,1,";e",GitRevwalkHideHeadFunction,"GitRevwalkHideHeadFunction",NULL);  /* doc: walk -> boolean */
	AddUDF(env,"git-revwalk-hide-ref","b",2,2,";e;sy",GitRevwalkHideRefFunction,"GitRevwalkHideRefFunction",NULL);  /* doc: walk refname -> boolean */
	AddUDF(env,"git-revwalk-hide-glob","b",2,2,";e;sy",GitRevwalkHideGlobFunction,"GitRevwalkHideGlobFunction",NULL);  /* doc: walk glob -> boolean */
	AddUDF(env,"git-revwalk-sorting","b",2,2,";e;lmsy",GitRevwalkSortingFunction,"GitRevwalkSortingFunction",NULL);  /* doc: walk flags -> boolean */
	AddUDF(env,"git-revwalk-simplify-first-parent","b",1,1,";e",GitRevwalkSimplifyFirstParentFunction,"GitRevwalkSimplifyFirstParentFunction",NULL);  /* doc: walk -> boolean */
	AddUDF(env,"git-revwalk-next","bs",1,1,";e",GitRevwalkNextFunction,"GitRevwalkNextFunction",NULL);  /* doc: walk -> string */
	AddUDF(env,"git-revwalk-reset","b",1,1,";e",GitRevwalkResetFunction,"GitRevwalkResetFunction",NULL);  /* doc: walk -> boolean */
	AddUDF(env,"git-revwalk-free","b",1,1,";e",GitRevwalkFreeFunction,"GitRevwalkFreeFunction",NULL);  /* doc: walk -> boolean */

	/* doc-group: Trees */
	AddUDF(env,"git-tree-lookup","be",2,2,";e;sy",GitTreeLookupFunction,"GitTreeLookupFunction",NULL);  /* doc: repo id -> handle */
	AddUDF(env,"git-tree-lookup-prefix","be",2,2,";e;sy",GitTreeLookupPrefixFunction,"GitTreeLookupPrefixFunction",NULL);  /* doc: repo prefix -> handle */
	AddUDF(env,"git-tree-id","bs",1,1,";e",GitTreeIdFunction,"GitTreeIdFunction",NULL);  /* doc: tree -> string */
	AddUDF(env,"git-tree-entrycount","bl",1,1,";e",GitTreeEntrycountFunction,"GitTreeEntrycountFunction",NULL);  /* doc: tree -> integer */
	AddUDF(env,"git-tree-entry-byindex","be",2,2,";e;l",GitTreeEntryByindexFunction,"GitTreeEntryByindexFunction",NULL);  /* doc: tree n -> handle */
	AddUDF(env,"git-tree-entry-byname","be",2,2,";e;sy",GitTreeEntryBynameFunction,"GitTreeEntryBynameFunction",NULL);  /* doc: tree name -> handle */
	AddUDF(env,"git-tree-entry-byid","be",2,2,";e;sy",GitTreeEntryByidFunction,"GitTreeEntryByidFunction",NULL);  /* doc: tree id -> handle */
	AddUDF(env,"git-tree-entry-bypath","be",2,2,";e;sy",GitTreeEntryBypathFunction,"GitTreeEntryBypathFunction",NULL);  /* doc: tree path -> handle */
	AddUDF(env,"git-tree-walk","bm",1,2,";e;lsy",GitTreeWalkFunction,"GitTreeWalkFunction",NULL);  /* doc: tree mode? -> multifield */
	AddUDF(env,"git-tree-free","b",1,1,";e",GitTreeFreeFunction,"GitTreeFreeFunction",NULL);  /* doc: tree -> boolean */
	AddUDF(env,"git-tree-entry-name","bsy",1,1,";e",GitTreeEntryNameFunction,"GitTreeEntryNameFunction",NULL);  /* doc: entry -> string */
	AddUDF(env,"git-tree-entry-id","bs",1,1,";e",GitTreeEntryIdFunction,"GitTreeEntryIdFunction",NULL);  /* doc: entry -> string */
	AddUDF(env,"git-tree-entry-type","by",1,1,";e",GitTreeEntryTypeFunction,"GitTreeEntryTypeFunction",NULL);  /* doc: entry -> symbol */
	AddUDF(env,"git-tree-entry-filemode","by",1,1,";e",GitTreeEntryFilemodeFunction,"GitTreeEntryFilemodeFunction",NULL);  /* doc: entry -> symbol */
	AddUDF(env,"git-tree-entry-filemode-raw","by",1,1,";e",GitTreeEntryFilemodeRawFunction,"GitTreeEntryFilemodeRawFunction",NULL);  /* doc: entry -> symbol */
	AddUDF(env,"git-tree-entry-to-object","be",2,2,";e;e",GitTreeEntryToObjectFunction,"GitTreeEntryToObjectFunction",NULL);  /* doc: repo entry -> handle */
	AddUDF(env,"git-tree-entry-cmp","bl",2,2,";e;e",GitTreeEntryCmpFunction,"GitTreeEntryCmpFunction",NULL);  /* doc: entry-a entry-b -> integer */
	AddUDF(env,"git-tree-entry-dup","be",1,1,";e",GitTreeEntryDupFunction,"GitTreeEntryDupFunction",NULL);  /* doc: entry -> handle */
	AddUDF(env,"git-tree-entry-free","b",1,1,";e",GitTreeEntryFreeFunction,"GitTreeEntryFreeFunction",NULL);  /* doc: entry -> boolean */
	AddUDF(env,"git-treebuilder-new","be",1,2,";e;ey",GitTreebuilderNewFunction,"GitTreebuilderNewFunction",NULL);  /* doc: repo source? -> handle */
	AddUDF(env,"git-treebuilder-insert","be",4,4,";e;sy;sy;lsy",GitTreebuilderInsertFunction,"GitTreebuilderInsertFunction",NULL);  /* doc: builder name id filemode -> handle */
	AddUDF(env,"git-treebuilder-get","be",2,2,";e;sy",GitTreebuilderGetFunction,"GitTreebuilderGetFunction",NULL);  /* doc: builder name -> handle */
	AddUDF(env,"git-treebuilder-remove","b",2,2,";e;sy",GitTreebuilderRemoveFunction,"GitTreebuilderRemoveFunction",NULL);  /* doc: builder name -> boolean */
	AddUDF(env,"git-treebuilder-clear","b",1,1,";e",GitTreebuilderClearFunction,"GitTreebuilderClearFunction",NULL);  /* doc: builder -> boolean */
	AddUDF(env,"git-treebuilder-entrycount","bl",1,1,";e",GitTreebuilderEntrycountFunction,"GitTreebuilderEntrycountFunction",NULL);  /* doc: builder -> integer */
	AddUDF(env,"git-treebuilder-write","bs",1,1,";e",GitTreebuilderWriteFunction,"GitTreebuilderWriteFunction",NULL);  /* doc: builder -> string */
	AddUDF(env,"git-treebuilder-free","b",1,1,";e",GitTreebuilderFreeFunction,"GitTreebuilderFreeFunction",NULL);  /* doc: builder -> boolean */

	/* doc-group: Blobs */
	AddUDF(env,"git-blob-lookup","be",2,2,";e;sy",GitBlobLookupFunction,"GitBlobLookupFunction",NULL);  /* doc: repo id -> handle */
	AddUDF(env,"git-blob-lookup-prefix","be",2,2,";e;sy",GitBlobLookupPrefixFunction,"GitBlobLookupPrefixFunction",NULL);  /* doc: repo prefix -> handle */
	AddUDF(env,"git-blob-id","bs",1,1,";e",GitBlobIdFunction,"GitBlobIdFunction",NULL);  /* doc: blob -> string */
	AddUDF(env,"git-blob-rawsize","bl",1,1,";e",GitBlobRawsizeFunction,"GitBlobRawsizeFunction",NULL);  /* doc: blob -> integer */
	AddUDF(env,"git-blob-rawcontent","bs",1,1,";e",GitBlobRawcontentFunction,"GitBlobRawcontentFunction",NULL);  /* doc: blob -> string */
	AddUDF(env,"git-blob-is-binary","b",1,1,";e",GitBlobIsBinaryFunction,"GitBlobIsBinaryFunction",NULL);  /* doc: blob -> boolean */
	AddUDF(env,"git-blob-filter","bs",2,2,";e;sy",GitBlobFilterFunction,"GitBlobFilterFunction",NULL);  /* doc: blob as-path -> string */
	AddUDF(env,"git-blob-create-from-buffer","bs",2,2,";e;sy",GitBlobCreateFromBufferFunction,"GitBlobCreateFromBufferFunction",NULL);  /* doc: repo content -> string */
	AddUDF(env,"git-blob-create-from-workdir","bs",2,2,";e;sy",GitBlobCreateFromWorkdirFunction,"GitBlobCreateFromWorkdirFunction",NULL);  /* doc: repo relative-path -> string */
	AddUDF(env,"git-blob-create-from-disk","bs",2,2,";e;sy",GitBlobCreateFromDiskFunction,"GitBlobCreateFromDiskFunction",NULL);  /* doc: repo path -> string */
	AddUDF(env,"git-blob-free","b",1,1,";e",GitBlobFreeFunction,"GitBlobFreeFunction",NULL);  /* doc: blob -> boolean */

	/* doc-group: Tags */
	AddUDF(env,"git-tag-lookup","be",2,2,";e;sy",GitTagLookupFunction,"GitTagLookupFunction",NULL);  /* doc: repo id -> handle */
	AddUDF(env,"git-tag-lookup-prefix","be",2,2,";e;sy",GitTagLookupPrefixFunction,"GitTagLookupPrefixFunction",NULL);  /* doc: repo prefix -> handle */
	AddUDF(env,"git-tag-create","bs",5,6,";e;sy;e;ey;sy;b",GitTagCreateFunction,"GitTagCreateFunction",NULL);  /* doc: repo name target tagger message force? -> string */
	AddUDF(env,"git-tag-create-lightweight","bs",3,4,";e;sy;e;b",GitTagCreateLightweightFunction,"GitTagCreateLightweightFunction",NULL);  /* doc: repo name target force? -> string */
	AddUDF(env,"git-tag-annotation-create","bs",5,5,";e;sy;e;ey;sy",GitTagAnnotationCreateFunction,"GitTagAnnotationCreateFunction",NULL);  /* doc: repo name target tagger message -> string */
	AddUDF(env,"git-tag-delete","b",2,2,";e;sy",GitTagDeleteFunction,"GitTagDeleteFunction",NULL);  /* doc: repo name -> boolean */
	AddUDF(env,"git-tag-list","bm",1,2,";e;sy",GitTagListFunction,"GitTagListFunction",NULL);  /* doc: repo pattern? -> multifield */
	AddUDF(env,"git-tag-id","bs",1,1,";e",GitTagIdFunction,"GitTagIdFunction",NULL);  /* doc: tag -> string */
	AddUDF(env,"git-tag-name","bsy",1,1,";e",GitTagNameFunction,"GitTagNameFunction",NULL);  /* doc: tag -> string */
	AddUDF(env,"git-tag-message","bsy",1,1,";e",GitTagMessageFunction,"GitTagMessageFunction",NULL);  /* doc: tag -> string */
	AddUDF(env,"git-tag-tagger","bey",1,1,";e",GitTagTaggerFunction,"GitTagTaggerFunction",NULL);  /* doc: tag -> handle */
	AddUDF(env,"git-tag-target","be",1,1,";e",GitTagTargetFunction,"GitTagTargetFunction",NULL);  /* doc: tag -> handle */
	AddUDF(env,"git-tag-target-id","bs",1,1,";e",GitTagTargetIdFunction,"GitTagTargetIdFunction",NULL);  /* doc: tag -> string */
	AddUDF(env,"git-tag-target-type","by",1,1,";e",GitTagTargetTypeFunction,"GitTagTargetTypeFunction",NULL);  /* doc: tag -> symbol */
	AddUDF(env,"git-tag-peel","be",1,1,";e",GitTagPeelFunction,"GitTagPeelFunction",NULL);  /* doc: tag -> handle */
	AddUDF(env,"git-tag-free","b",1,1,";e",GitTagFreeFunction,"GitTagFreeFunction",NULL);  /* doc: tag -> boolean */

	/* doc-group: References */
	AddUDF(env,"git-reference-lookup","be",2,2,";e;sy",GitReferenceLookupFunction,"GitReferenceLookupFunction",NULL);  /* doc: repo name -> handle */
	AddUDF(env,"git-reference-dwim","be",2,2,";e;sy",GitReferenceDwimFunction,"GitReferenceDwimFunction",NULL);  /* doc: repo shorthand -> handle */
	AddUDF(env,"git-reference-name-to-id","bs",2,2,";e;sy",GitReferenceNameToIdFunction,"GitReferenceNameToIdFunction",NULL);  /* doc: repo name -> string */
	AddUDF(env,"git-reference-create","be",3,5,";e;sy;sy;b;sy",GitReferenceCreateFunction,"GitReferenceCreateFunction",NULL);  /* doc: repo name id force? log-message? -> handle */
	AddUDF(env,"git-reference-symbolic-create","be",3,5,";e;sy;sy;b;sy",GitReferenceSymbolicCreateFunction,"GitReferenceSymbolicCreateFunction",NULL);  /* doc: repo name target force? log-message? -> handle */
	AddUDF(env,"git-reference-list","bm",1,1,";e",GitReferenceListFunction,"GitReferenceListFunction",NULL);  /* doc: repo -> multifield */
	AddUDF(env,"git-reference-list-glob","bm",2,2,";e;sy",GitReferenceListGlobFunction,"GitReferenceListGlobFunction",NULL);  /* doc: repo glob -> multifield */
	AddUDF(env,"git-reference-remove","b",2,2,";e;sy",GitReferenceRemoveFunction,"GitReferenceRemoveFunction",NULL);  /* doc: repo name -> boolean */
	AddUDF(env,"git-reference-has-log","b",2,2,";e;sy",GitReferenceHasLogFunction,"GitReferenceHasLogFunction",NULL);  /* doc: repo name -> boolean */
	AddUDF(env,"git-reference-is-valid-name","b",1,1,";sy",GitReferenceIsValidNameFunction,"GitReferenceIsValidNameFunction",NULL);  /* doc: name -> boolean */
	AddUDF(env,"git-reference-name","bsy",1,1,";e",GitReferenceNameFunction,"GitReferenceNameFunction",NULL);  /* doc: ref -> string */
	AddUDF(env,"git-reference-shorthand","bsy",1,1,";e",GitReferenceShorthandFunction,"GitReferenceShorthandFunction",NULL);  /* doc: ref -> string */
	AddUDF(env,"git-reference-type","by",1,1,";e",GitReferenceTypeFunction,"GitReferenceTypeFunction",NULL);  /* doc: ref -> symbol */
	AddUDF(env,"git-reference-target","bsy",1,1,";e",GitReferenceTargetFunction,"GitReferenceTargetFunction",NULL);  /* doc: ref -> string */
	AddUDF(env,"git-reference-target-peel","bsy",1,1,";e",GitReferenceTargetPeelFunction,"GitReferenceTargetPeelFunction",NULL);  /* doc: ref -> string */
	AddUDF(env,"git-reference-symbolic-target","bsy",1,1,";e",GitReferenceSymbolicTargetFunction,"GitReferenceSymbolicTargetFunction",NULL);  /* doc: ref -> string */
	AddUDF(env,"git-reference-resolve","be",1,1,";e",GitReferenceResolveFunction,"GitReferenceResolveFunction",NULL);  /* doc: ref -> handle */
	AddUDF(env,"git-reference-peel","be",1,2,";e;lsy",GitReferencePeelFunction,"GitReferencePeelFunction",NULL);  /* doc: ref type? -> handle */
	AddUDF(env,"git-reference-set-target","be",2,3,";e;sy;sy",GitReferenceSetTargetFunction,"GitReferenceSetTargetFunction",NULL);  /* doc: ref id log-message? -> handle */
	AddUDF(env,"git-reference-symbolic-set-target","be",2,3,";e;sy;sy",GitReferenceSymbolicSetTargetFunction,"GitReferenceSymbolicSetTargetFunction",NULL);  /* doc: ref target log-message? -> handle */
	AddUDF(env,"git-reference-rename","be",2,4,";e;sy;b;sy",GitReferenceRenameFunction,"GitReferenceRenameFunction",NULL);  /* doc: ref new-name force? log-message? -> handle */
	AddUDF(env,"git-reference-delete","b",1,1,";e",GitReferenceDeleteFunction,"GitReferenceDeleteFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-reference-is-branch","b",1,1,";e",GitReferenceIsBranchFunction,"GitReferenceIsBranchFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-reference-is-remote","b",1,1,";e",GitReferenceIsRemoteFunction,"GitReferenceIsRemoteFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-reference-is-tag","b",1,1,";e",GitReferenceIsTagFunction,"GitReferenceIsTagFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-reference-is-note","b",1,1,";e",GitReferenceIsNoteFunction,"GitReferenceIsNoteFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-reference-cmp","bl",2,2,";e;e",GitReferenceCmpFunction,"GitReferenceCmpFunction",NULL);  /* doc: ref-a ref-b -> integer */
	AddUDF(env,"git-reference-dup","be",1,1,";e",GitReferenceDupFunction,"GitReferenceDupFunction",NULL);  /* doc: ref -> handle */
	AddUDF(env,"git-reference-free","b",1,1,";e",GitReferenceFreeFunction,"GitReferenceFreeFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-reflog-read","bm",2,2,";e;sy",GitReflogReadFunction,"GitReflogReadFunction",NULL);  /* doc: repo name -> multifield */
	AddUDF(env,"git-reflog-delete","b",2,2,";e;sy",GitReflogDeleteFunction,"GitReflogDeleteFunction",NULL);  /* doc: repo name -> boolean */

	/* doc-group: Branches */
	AddUDF(env,"git-branch-create","be",3,4,";e;sy;e;b",GitBranchCreateFunction,"GitBranchCreateFunction",NULL);  /* doc: repo name target force? -> handle */
	AddUDF(env,"git-branch-create-from-annotated","be",3,4,";e;sy;e;b",GitBranchCreateFromAnnotatedFunction,"GitBranchCreateFromAnnotatedFunction",NULL);  /* doc: repo name target force? -> handle */
	AddUDF(env,"git-branch-lookup","be",2,3,";e;sy;lsy",GitBranchLookupFunction,"GitBranchLookupFunction",NULL);  /* doc: repo name type? -> handle */
	AddUDF(env,"git-branch-list","bm",1,2,";e;lsy",GitBranchListFunction,"GitBranchListFunction",NULL);  /* doc: repo type? -> multifield */
	AddUDF(env,"git-branch-delete","b",1,1,";e",GitBranchDeleteFunction,"GitBranchDeleteFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-branch-move","be",2,3,";e;sy;b",GitBranchMoveFunction,"GitBranchMoveFunction",NULL);  /* doc: ref new-name force? -> handle */
	AddUDF(env,"git-branch-name","bsy",1,1,";e",GitBranchNameFunction,"GitBranchNameFunction",NULL);  /* doc: ref -> string */
	AddUDF(env,"git-branch-is-head","b",1,1,";e",GitBranchIsHeadFunction,"GitBranchIsHeadFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-branch-is-checked-out","b",1,1,";e",GitBranchIsCheckedOutFunction,"GitBranchIsCheckedOutFunction",NULL);  /* doc: ref -> boolean */
	AddUDF(env,"git-branch-upstream","bey",1,1,";e",GitBranchUpstreamFunction,"GitBranchUpstreamFunction",NULL);  /* doc: ref -> handle */
	AddUDF(env,"git-branch-set-upstream","b",2,2,";e;sy",GitBranchSetUpstreamFunction,"GitBranchSetUpstreamFunction",NULL);  /* doc: ref upstream -> boolean */
	AddUDF(env,"git-branch-upstream-name","bsy",2,2,";e;sy",GitBranchUpstreamNameFunction,"GitBranchUpstreamNameFunction",NULL);  /* doc: repo refname -> string */
	AddUDF(env,"git-branch-upstream-remote","bsy",2,2,";e;sy",GitBranchUpstreamRemoteFunction,"GitBranchUpstreamRemoteFunction",NULL);  /* doc: repo refname -> string */
	AddUDF(env,"git-branch-upstream-merge","bsy",2,2,";e;sy",GitBranchUpstreamMergeFunction,"GitBranchUpstreamMergeFunction",NULL);  /* doc: repo refname -> string */
	AddUDF(env,"git-branch-remote-name","bsy",2,2,";e;sy",GitBranchRemoteNameFunction,"GitBranchRemoteNameFunction",NULL);  /* doc: repo refname -> string */
	AddUDF(env,"git-branch-is-valid-name","b",1,1,";sy",GitBranchIsValidNameFunction,"GitBranchIsValidNameFunction",NULL);  /* doc: name -> boolean */

	/* doc-group: The index */
	AddUDF(env,"git-index-open","be",1,1,";sy",GitIndexOpenFunction,"GitIndexOpenFunction",NULL);  /* doc: path -> handle */
	AddUDF(env,"git-index-new","be",0,0,NULL,GitIndexNewFunction,"GitIndexNewFunction",NULL);  /* doc: -> handle */
	AddUDF(env,"git-index-owner","bey",1,1,";e",GitIndexOwnerFunction,"GitIndexOwnerFunction",NULL);  /* doc: index -> handle */
	AddUDF(env,"git-index-path","bsy",1,1,";e",GitIndexPathFunction,"GitIndexPathFunction",NULL);  /* doc: index -> string */
	AddUDF(env,"git-index-read","b",1,1,";e",GitIndexReadFunction,"GitIndexReadFunction",NULL);  /* doc: index -> boolean */
	AddUDF(env,"git-index-write","b",1,1,";e",GitIndexWriteFunction,"GitIndexWriteFunction",NULL);  /* doc: index -> boolean */
	AddUDF(env,"git-index-read-tree","b",2,2,";e;e",GitIndexReadTreeFunction,"GitIndexReadTreeFunction",NULL);  /* doc: index tree -> boolean */
	AddUDF(env,"git-index-write-tree","bs",1,1,";e",GitIndexWriteTreeFunction,"GitIndexWriteTreeFunction",NULL);  /* doc: index -> string */
	AddUDF(env,"git-index-write-tree-to","bs",2,2,";e;e",GitIndexWriteTreeToFunction,"GitIndexWriteTreeToFunction",NULL);  /* doc: index repo -> string */
	AddUDF(env,"git-index-entrycount","bl",1,1,";e",GitIndexEntrycountFunction,"GitIndexEntrycountFunction",NULL);  /* doc: index -> integer */
	AddUDF(env,"git-index-clear","b",1,1,";e",GitIndexClearFunction,"GitIndexClearFunction",NULL);  /* doc: index -> boolean */
	AddUDF(env,"git-index-version","bl",1,1,";e",GitIndexVersionFunction,"GitIndexVersionFunction",NULL);  /* doc: index -> integer */
	AddUDF(env,"git-index-set-version","b",2,2,";e;l",GitIndexSetVersionFunction,"GitIndexSetVersionFunction",NULL);  /* doc: index version -> boolean */
	AddUDF(env,"git-index-checksum","bsy",1,1,";e",GitIndexChecksumFunction,"GitIndexChecksumFunction",NULL);  /* doc: index -> string */
	AddUDF(env,"git-index-add-bypath","b",2,2,";e;sy",GitIndexAddBypathFunction,"GitIndexAddBypathFunction",NULL);  /* doc: index path -> boolean */
	AddUDF(env,"git-index-add-all","b",2,3,";e;m;lmsy",GitIndexAddAllFunction,"GitIndexAddAllFunction",NULL);  /* doc: index pathspec flags? -> boolean */
	AddUDF(env,"git-index-remove","b",3,3,";e;sy;l",GitIndexRemoveFunction,"GitIndexRemoveFunction",NULL);  /* doc: index path stage -> boolean */
	AddUDF(env,"git-index-remove-bypath","b",2,2,";e;sy",GitIndexRemoveBypathFunction,"GitIndexRemoveBypathFunction",NULL);  /* doc: index path -> boolean */
	AddUDF(env,"git-index-remove-directory","b",3,3,";e;sy;l",GitIndexRemoveDirectoryFunction,"GitIndexRemoveDirectoryFunction",NULL);  /* doc: index dir stage -> boolean */
	AddUDF(env,"git-index-remove-all","b",2,2,";e;m",GitIndexRemoveAllFunction,"GitIndexRemoveAllFunction",NULL);  /* doc: index pathspec -> boolean */
	AddUDF(env,"git-index-update-all","b",2,2,";e;m",GitIndexUpdateAllFunction,"GitIndexUpdateAllFunction",NULL);  /* doc: index pathspec -> boolean */
	AddUDF(env,"git-index-find","bl",2,2,";e;sy",GitIndexFindFunction,"GitIndexFindFunction",NULL);  /* doc: index path -> integer */
	AddUDF(env,"git-index-get-byindex","bm",2,2,";e;l",GitIndexGetByindexFunction,"GitIndexGetByindexFunction",NULL);  /* doc: index n -> multifield */
	AddUDF(env,"git-index-get-bypath","bm",2,3,";e;sy;l",GitIndexGetBypathFunction,"GitIndexGetBypathFunction",NULL);  /* doc: index path stage? -> multifield */
	AddUDF(env,"git-index-entry-to-fact","bf",3,3,";e;l;y",GitIndexEntryToFactFunction,"GitIndexEntryToFactFunction",NULL);  /* doc: index n deftemplate -> fact */
	AddUDF(env,"git-index-has-conflicts","b",1,1,";e",GitIndexHasConflictsFunction,"GitIndexHasConflictsFunction",NULL);  /* doc: index -> boolean */
	AddUDF(env,"git-index-conflict-remove","b",2,2,";e;sy",GitIndexConflictRemoveFunction,"GitIndexConflictRemoveFunction",NULL);  /* doc: index path -> boolean */
	AddUDF(env,"git-index-conflict-cleanup","b",1,1,";e",GitIndexConflictCleanupFunction,"GitIndexConflictCleanupFunction",NULL);  /* doc: index -> boolean */
	AddUDF(env,"git-index-free","b",1,1,";e",GitIndexFreeFunction,"GitIndexFreeFunction",NULL);  /* doc: index -> boolean */

	/* doc-group: Configuration */
	AddUDF(env,"git-config-open-default","be",0,0,NULL,GitConfigOpenDefaultFunction,"GitConfigOpenDefaultFunction",NULL);  /* doc: -> handle */
	AddUDF(env,"git-config-new","be",0,0,NULL,GitConfigNewFunction,"GitConfigNewFunction",NULL);  /* doc: -> handle */
	AddUDF(env,"git-config-open-ondisk","be",1,1,";sy",GitConfigOpenOndiskFunction,"GitConfigOpenOndiskFunction",NULL);  /* doc: path -> handle */
	AddUDF(env,"git-config-open-level","be",2,2,";e;lsy",GitConfigOpenLevelFunction,"GitConfigOpenLevelFunction",NULL);  /* doc: config level -> handle */
	AddUDF(env,"git-config-add-file-ondisk","b",3,5,";e;sy;lsy;b;ey",GitConfigAddFileOndiskFunction,"GitConfigAddFileOndiskFunction",NULL);  /* doc: config path level force? repo? -> boolean */
	AddUDF(env,"git-config-snapshot","be",1,1,";e",GitConfigSnapshotFunction,"GitConfigSnapshotFunction",NULL);  /* doc: config -> handle */
	AddUDF(env,"git-config-find-global","bsy",0,0,NULL,GitConfigFindGlobalFunction,"GitConfigFindGlobalFunction",NULL);  /* doc: -> string */
	AddUDF(env,"git-config-find-xdg","bsy",0,0,NULL,GitConfigFindXdgFunction,"GitConfigFindXdgFunction",NULL);  /* doc: -> string */
	AddUDF(env,"git-config-find-system","bsy",0,0,NULL,GitConfigFindSystemFunction,"GitConfigFindSystemFunction",NULL);  /* doc: -> string */
	AddUDF(env,"git-config-find-programdata","bsy",0,0,NULL,GitConfigFindProgramdataFunction,"GitConfigFindProgramdataFunction",NULL);  /* doc: -> string */
	AddUDF(env,"git-config-get-string","bsy",2,2,";e;sy",GitConfigGetStringFunction,"GitConfigGetStringFunction",NULL);  /* doc: config name -> string */
	AddUDF(env,"git-config-get-path","bsy",2,2,";e;sy",GitConfigGetPathFunction,"GitConfigGetPathFunction",NULL);  /* doc: config name -> string */
	AddUDF(env,"git-config-get-int","bly",2,2,";e;sy",GitConfigGetIntFunction,"GitConfigGetIntFunction",NULL);  /* doc: config name -> integer */
	AddUDF(env,"git-config-get-bool","by",2,2,";e;sy",GitConfigGetBoolFunction,"GitConfigGetBoolFunction",NULL);  /* doc: config name -> boolean */
	AddUDF(env,"git-config-get-multivar","bm",2,3,";e;sy;sy",GitConfigGetMultivarFunction,"GitConfigGetMultivarFunction",NULL);  /* doc: config name regexp? -> multifield */
	AddUDF(env,"git-config-entries","bm",1,2,";e;sy",GitConfigEntriesFunction,"GitConfigEntriesFunction",NULL);  /* doc: config regexp? -> multifield */
	AddUDF(env,"git-config-set-string","b",3,3,";e;sy;sy",GitConfigSetStringFunction,"GitConfigSetStringFunction",NULL);  /* doc: config name value -> boolean */
	AddUDF(env,"git-config-set-int","b",3,3,";e;sy;l",GitConfigSetIntFunction,"GitConfigSetIntFunction",NULL);  /* doc: config name value -> boolean */
	AddUDF(env,"git-config-set-bool","b",3,3,";e;sy;b",GitConfigSetBoolFunction,"GitConfigSetBoolFunction",NULL);  /* doc: config name value -> boolean */
	AddUDF(env,"git-config-set-multivar","b",4,4,";e;sy;sy;sy",GitConfigSetMultivarFunction,"GitConfigSetMultivarFunction",NULL);  /* doc: config name regexp value -> boolean */
	AddUDF(env,"git-config-delete-entry","b",2,2,";e;sy",GitConfigDeleteEntryFunction,"GitConfigDeleteEntryFunction",NULL);  /* doc: config name -> boolean */
	AddUDF(env,"git-config-delete-multivar","b",3,3,";e;sy;sy",GitConfigDeleteMultivarFunction,"GitConfigDeleteMultivarFunction",NULL);  /* doc: config name regexp -> boolean */
	AddUDF(env,"git-config-parse-bool","b",1,1,";sy",GitConfigParseBoolFunction,"GitConfigParseBoolFunction",NULL);  /* doc: text -> boolean */
	AddUDF(env,"git-config-parse-int","bl",1,1,";sy",GitConfigParseIntFunction,"GitConfigParseIntFunction",NULL);  /* doc: text -> integer */
	AddUDF(env,"git-config-parse-path","bs",1,1,";sy",GitConfigParsePathFunction,"GitConfigParsePathFunction",NULL);  /* doc: text -> string */
	AddUDF(env,"git-config-free","b",1,1,";e",GitConfigFreeFunction,"GitConfigFreeFunction",NULL);  /* doc: config -> boolean */

	/* doc-group: Remotes */
	AddUDF(env,"git-remote-create","be",3,4,";e;sy;sy;sy",GitRemoteCreateFunction,"GitRemoteCreateFunction",NULL);  /* doc: repo name url fetch-refspec? -> handle */
	AddUDF(env,"git-remote-create-anonymous","be",2,2,";e;sy",GitRemoteCreateAnonymousFunction,"GitRemoteCreateAnonymousFunction",NULL);  /* doc: repo url -> handle */
	AddUDF(env,"git-remote-create-detached","be",1,1,";sy",GitRemoteCreateDetachedFunction,"GitRemoteCreateDetachedFunction",NULL);  /* doc: url -> handle */
	AddUDF(env,"git-remote-lookup","be",2,2,";e;sy",GitRemoteLookupFunction,"GitRemoteLookupFunction",NULL);  /* doc: repo name -> handle */
	AddUDF(env,"git-remote-list","bm",1,1,";e",GitRemoteListFunction,"GitRemoteListFunction",NULL);  /* doc: repo -> multifield */
	AddUDF(env,"git-remote-delete","b",2,2,";e;sy",GitRemoteDeleteFunction,"GitRemoteDeleteFunction",NULL);  /* doc: repo name -> boolean */
	AddUDF(env,"git-remote-rename","bm",3,3,";e;sy;sy",GitRemoteRenameFunction,"GitRemoteRenameFunction",NULL);  /* doc: repo name new-name -> multifield */
	AddUDF(env,"git-remote-set-url","b",3,3,";e;sy;sy",GitRemoteSetUrlFunction,"GitRemoteSetUrlFunction",NULL);  /* doc: repo name url -> boolean */
	AddUDF(env,"git-remote-set-pushurl","b",3,3,";e;sy;sy",GitRemoteSetPushurlFunction,"GitRemoteSetPushurlFunction",NULL);  /* doc: repo name url -> boolean */
	AddUDF(env,"git-remote-add-fetch","b",3,3,";e;sy;sy",GitRemoteAddFetchFunction,"GitRemoteAddFetchFunction",NULL);  /* doc: repo name refspec -> boolean */
	AddUDF(env,"git-remote-add-push","b",3,3,";e;sy;sy",GitRemoteAddPushFunction,"GitRemoteAddPushFunction",NULL);  /* doc: repo name refspec -> boolean */
	AddUDF(env,"git-remote-is-valid-name","b",1,1,";sy",GitRemoteIsValidNameFunction,"GitRemoteIsValidNameFunction",NULL);  /* doc: name -> boolean */
	AddUDF(env,"git-remote-name","bsy",1,1,";e",GitRemoteNameFunction,"GitRemoteNameFunction",NULL);  /* doc: remote -> string */
	AddUDF(env,"git-remote-url","bsy",1,1,";e",GitRemoteUrlFunction,"GitRemoteUrlFunction",NULL);  /* doc: remote -> string */
	AddUDF(env,"git-remote-pushurl","bsy",1,1,";e",GitRemotePushurlFunction,"GitRemotePushurlFunction",NULL);  /* doc: remote -> string */
	AddUDF(env,"git-remote-refspec-count","bl",1,1,";e",GitRemoteRefspecCountFunction,"GitRemoteRefspecCountFunction",NULL);  /* doc: remote -> integer */
	AddUDF(env,"git-remote-get-refspec","be",2,2,";e;l",GitRemoteGetRefspecFunction,"GitRemoteGetRefspecFunction",NULL);  /* doc: remote n -> handle */
	AddUDF(env,"git-remote-get-fetch-refspecs","bm",1,1,";e",GitRemoteGetFetchRefspecsFunction,"GitRemoteGetFetchRefspecsFunction",NULL);  /* doc: remote -> multifield */
	AddUDF(env,"git-remote-get-push-refspecs","bm",1,1,";e",GitRemoteGetPushRefspecsFunction,"GitRemoteGetPushRefspecsFunction",NULL);  /* doc: remote -> multifield */
	AddUDF(env,"git-remote-autotag","by",1,1,";e",GitRemoteAutotagFunction,"GitRemoteAutotagFunction",NULL);  /* doc: remote -> symbol */
	AddUDF(env,"git-remote-prune-refs","b",1,1,";e",GitRemotePruneRefsFunction,"GitRemotePruneRefsFunction",NULL);  /* doc: remote -> boolean */
	AddUDF(env,"git-remote-connect","b",1,2,";e;lsy",GitRemoteConnectFunction,"GitRemoteConnectFunction",NULL);  /* doc: remote direction? -> boolean */
	AddUDF(env,"git-remote-connected","b",1,1,";e",GitRemoteConnectedFunction,"GitRemoteConnectedFunction",NULL);  /* doc: remote -> boolean */
	AddUDF(env,"git-remote-disconnect","b",1,1,";e",GitRemoteDisconnectFunction,"GitRemoteDisconnectFunction",NULL);  /* doc: remote -> boolean */
	AddUDF(env,"git-remote-stop","b",1,1,";e",GitRemoteStopFunction,"GitRemoteStopFunction",NULL);  /* doc: remote -> boolean */
	AddUDF(env,"git-remote-ls","bm",1,1,";e",GitRemoteLsFunction,"GitRemoteLsFunction",NULL);  /* doc: remote -> multifield */
	AddUDF(env,"git-remote-fetch","b",1,3,";e;m;sy",GitRemoteFetchFunction,"GitRemoteFetchFunction",NULL);  /* doc: remote refspecs? reflog-message? -> boolean */
	AddUDF(env,"git-remote-push","b",1,2,";e;m",GitRemotePushFunction,"GitRemotePushFunction",NULL);  /* doc: remote refspecs? -> boolean */
	AddUDF(env,"git-remote-prune","b",1,1,";e",GitRemotePruneFunction,"GitRemotePruneFunction",NULL);  /* doc: remote -> boolean */
	AddUDF(env,"git-remote-update-tips","b",1,2,";e;sy",GitRemoteUpdateTipsFunction,"GitRemoteUpdateTipsFunction",NULL);  /* doc: remote reflog-message? -> boolean */
	AddUDF(env,"git-remote-dup","be",1,1,";e",GitRemoteDupFunction,"GitRemoteDupFunction",NULL);  /* doc: remote -> handle */
	AddUDF(env,"git-remote-free","b",1,1,";e",GitRemoteFreeFunction,"GitRemoteFreeFunction",NULL);  /* doc: remote -> boolean */
	AddUDF(env,"git-refspec-parse","be",2,2,";sy;b",GitRefspecParseFunction,"GitRefspecParseFunction",NULL);  /* doc: text is-fetch -> handle */
	AddUDF(env,"git-refspec-src","bsy",1,1,";e",GitRefspecSrcFunction,"GitRefspecSrcFunction",NULL);  /* doc: refspec -> string */
	AddUDF(env,"git-refspec-dst","bsy",1,1,";e",GitRefspecDstFunction,"GitRefspecDstFunction",NULL);  /* doc: refspec -> string */
	AddUDF(env,"git-refspec-string","bsy",1,1,";e",GitRefspecStringFunction,"GitRefspecStringFunction",NULL);  /* doc: refspec -> string */
	AddUDF(env,"git-refspec-force","b",1,1,";e",GitRefspecForceFunction,"GitRefspecForceFunction",NULL);  /* doc: refspec -> boolean */
	AddUDF(env,"git-refspec-direction","by",1,1,";e",GitRefspecDirectionFunction,"GitRefspecDirectionFunction",NULL);  /* doc: refspec -> symbol */
	AddUDF(env,"git-refspec-src-matches","b",2,2,";e;sy",GitRefspecSrcMatchesFunction,"GitRefspecSrcMatchesFunction",NULL);  /* doc: refspec refname -> boolean */
	AddUDF(env,"git-refspec-dst-matches","b",2,2,";e;sy",GitRefspecDstMatchesFunction,"GitRefspecDstMatchesFunction",NULL);  /* doc: refspec refname -> boolean */
	AddUDF(env,"git-refspec-transform","bs",2,2,";e;sy",GitRefspecTransformFunction,"GitRefspecTransformFunction",NULL);  /* doc: refspec refname -> string */
	AddUDF(env,"git-refspec-rtransform","bs",2,2,";e;sy",GitRefspecRtransformFunction,"GitRefspecRtransformFunction",NULL);  /* doc: refspec refname -> string */
	AddUDF(env,"git-refspec-free","b",1,1,";e",GitRefspecFreeFunction,"GitRefspecFreeFunction",NULL);  /* doc: refspec -> boolean */

	/* doc-group: Credentials */
	AddUDF(env,"git-credential-hook","by",0,1,";y",GitCredentialHookFunction,"GitCredentialHookFunction",NULL);  /* doc: name? -> symbol */
	AddUDF(env,"git-credential-prompt","by",0,1,";y",GitCredentialPromptFunction,"GitCredentialPromptFunction",NULL);  /* doc: mode? -> symbol */
	AddUDF(env,"git-credential-ask","bm",1,3,";sy;sy;lmsy",GitCredentialAskFunction,"GitCredentialAskFunction",NULL);  /* doc: url username? kinds? -> multifield */

	/* doc-group: Status */
	AddUDF(env,"git-status-list-new","be",1,4,";e;lmsy;lsy;m",GitStatusListNewFunction,"GitStatusListNewFunction",NULL);  /* doc: repo flags? show? pathspec? -> handle */
	AddUDF(env,"git-status-list-entrycount","bl",1,1,";e",GitStatusListEntrycountFunction,"GitStatusListEntrycountFunction",NULL);  /* doc: list -> integer */
	AddUDF(env,"git-status-byindex","bm",2,2,";e;l",GitStatusByindexFunction,"GitStatusByindexFunction",NULL);  /* doc: list n -> multifield */
	AddUDF(env,"git-status-entry-to-fact","bf",3,3,";e;l;y",GitStatusEntryToFactFunction,"GitStatusEntryToFactFunction",NULL);  /* doc: list n deftemplate -> fact */
	AddUDF(env,"git-status-list-free","b",1,1,";e",GitStatusListFreeFunction,"GitStatusListFreeFunction",NULL);  /* doc: list -> boolean */
	AddUDF(env,"git-status-file","bm",2,2,";e;sy",GitStatusFileFunction,"GitStatusFileFunction",NULL);  /* doc: repo path -> multifield */
	AddUDF(env,"git-status-should-ignore","b",2,2,";e;sy",GitStatusShouldIgnoreFunction,"GitStatusShouldIgnoreFunction",NULL);  /* doc: repo path -> boolean */
	AddUDF(env,"git-ignore-add-rule","b",2,2,";e;sy",GitIgnoreAddRuleFunction,"GitIgnoreAddRuleFunction",NULL);  /* doc: repo rules -> boolean */
	AddUDF(env,"git-ignore-clear-internal-rules","b",1,1,";e",GitIgnoreClearInternalRulesFunction,"GitIgnoreClearInternalRulesFunction",NULL);  /* doc: repo -> boolean */
	AddUDF(env,"git-ignore-path-is-ignored","b",2,2,";e;sy",GitIgnorePathIsIgnoredFunction,"GitIgnorePathIsIgnoredFunction",NULL);  /* doc: repo path -> boolean */

	/* doc-group: Diffs and patches */
	AddUDF(env,"git-diff-tree-to-tree","be",3,7,";e;ey;ey;lmsy;m;l;l",GitDiffTreeToTreeFunction,"GitDiffTreeToTreeFunction",NULL);  /* doc: repo old-tree new-tree flags? pathspec? context-lines? interhunk-lines? -> handle */
	AddUDF(env,"git-diff-tree-to-index","be",3,7,";e;ey;ey;lmsy;m;l;l",GitDiffTreeToIndexFunction,"GitDiffTreeToIndexFunction",NULL);  /* doc: repo old-tree index flags? pathspec? context-lines? interhunk-lines? -> handle */
	AddUDF(env,"git-diff-index-to-workdir","be",2,6,";e;ey;lmsy;m;l;l",GitDiffIndexToWorkdirFunction,"GitDiffIndexToWorkdirFunction",NULL);  /* doc: repo index flags? pathspec? context-lines? interhunk-lines? -> handle */
	AddUDF(env,"git-diff-tree-to-workdir","be",2,6,";e;ey;lmsy;m;l;l",GitDiffTreeToWorkdirFunction,"GitDiffTreeToWorkdirFunction",NULL);  /* doc: repo old-tree flags? pathspec? context-lines? interhunk-lines? -> handle */
	AddUDF(env,"git-diff-tree-to-workdir-with-index","be",2,6,";e;ey;lmsy;m;l;l",GitDiffTreeToWorkdirWithIndexFunction,"GitDiffTreeToWorkdirWithIndexFunction",NULL);  /* doc: repo old-tree flags? pathspec? context-lines? interhunk-lines? -> handle */
	AddUDF(env,"git-diff-index-to-index","be",3,7,";e;e;e;lmsy;m;l;l",GitDiffIndexToIndexFunction,"GitDiffIndexToIndexFunction",NULL);  /* doc: repo old-index new-index flags? pathspec? context-lines? interhunk-lines? -> handle */
	AddUDF(env,"git-diff-from-buffer","be",1,1,";sy",GitDiffFromBufferFunction,"GitDiffFromBufferFunction",NULL);  /* doc: patch-text -> handle */
	AddUDF(env,"git-diff-num-deltas","bl",1,1,";e",GitDiffNumDeltasFunction,"GitDiffNumDeltasFunction",NULL);  /* doc: diff -> integer */
	AddUDF(env,"git-diff-num-deltas-of-type","bl",2,2,";e;lsy",GitDiffNumDeltasOfTypeFunction,"GitDiffNumDeltasOfTypeFunction",NULL);  /* doc: diff type -> integer */
	AddUDF(env,"git-diff-get-delta","bm",2,2,";e;l",GitDiffGetDeltaFunction,"GitDiffGetDeltaFunction",NULL);  /* doc: diff n -> multifield */
	AddUDF(env,"git-diff-delta-to-fact","bf",3,3,";e;l;y",GitDiffDeltaToFactFunction,"GitDiffDeltaToFactFunction",NULL);  /* doc: diff n deftemplate -> fact */
	AddUDF(env,"git-diff-is-sorted-icase","b",1,1,";e",GitDiffIsSortedIcaseFunction,"GitDiffIsSortedIcaseFunction",NULL);  /* doc: diff -> boolean */
	AddUDF(env,"git-diff-merge","b",2,2,";e;e",GitDiffMergeFunction,"GitDiffMergeFunction",NULL);  /* doc: onto from -> boolean */
	AddUDF(env,"git-diff-find-similar","b",1,2,";e;lmsy",GitDiffFindSimilarFunction,"GitDiffFindSimilarFunction",NULL);  /* doc: diff flags? -> boolean */
	AddUDF(env,"git-diff-to-buf","bs",1,2,";e;lsy",GitDiffToBufFunction,"GitDiffToBufFunction",NULL);  /* doc: diff format? -> string */
	AddUDF(env,"git-diff-stats","bm",1,1,";e",GitDiffStatsFunction,"GitDiffStatsFunction",NULL);  /* doc: diff -> multifield */
	AddUDF(env,"git-diff-stats-to-buf","bs",1,3,";e;lmsy;l",GitDiffStatsToBufFunction,"GitDiffStatsToBufFunction",NULL);  /* doc: diff format? width? -> string */
	AddUDF(env,"git-diff-patchid","bs",1,1,";e",GitDiffPatchidFunction,"GitDiffPatchidFunction",NULL);  /* doc: diff -> string */
	AddUDF(env,"git-diff-free","b",1,1,";e",GitDiffFreeFunction,"GitDiffFreeFunction",NULL);  /* doc: diff -> boolean */
	AddUDF(env,"git-patch-from-diff","bey",2,2,";e;l",GitPatchFromDiffFunction,"GitPatchFromDiffFunction",NULL);  /* doc: diff n -> handle */
	AddUDF(env,"git-patch-from-blobs","be",4,4,";ey;sy;ey;sy",GitPatchFromBlobsFunction,"GitPatchFromBlobsFunction",NULL);  /* doc: old-blob old-path new-blob new-path -> handle */
	AddUDF(env,"git-patch-from-buffers","be",4,4,";sy;sy;sy;sy",GitPatchFromBuffersFunction,"GitPatchFromBuffersFunction",NULL);  /* doc: old-text old-path new-text new-path -> handle */
	AddUDF(env,"git-patch-get-delta","bm",1,1,";e",GitPatchGetDeltaFunction,"GitPatchGetDeltaFunction",NULL);  /* doc: patch -> multifield */
	AddUDF(env,"git-patch-num-hunks","bl",1,1,";e",GitPatchNumHunksFunction,"GitPatchNumHunksFunction",NULL);  /* doc: patch -> integer */
	AddUDF(env,"git-patch-get-hunk","bm",2,2,";e;l",GitPatchGetHunkFunction,"GitPatchGetHunkFunction",NULL);  /* doc: patch n -> multifield */
	AddUDF(env,"git-patch-num-lines-in-hunk","bl",2,2,";e;l",GitPatchNumLinesInHunkFunction,"GitPatchNumLinesInHunkFunction",NULL);  /* doc: patch n -> integer */
	AddUDF(env,"git-patch-get-line-in-hunk","bm",3,3,";e;l;l",GitPatchGetLineInHunkFunction,"GitPatchGetLineInHunkFunction",NULL);  /* doc: patch hunk line -> multifield */
	AddUDF(env,"git-patch-line-stats","bm",1,1,";e",GitPatchLineStatsFunction,"GitPatchLineStatsFunction",NULL);  /* doc: patch -> multifield */
	AddUDF(env,"git-patch-size","bl",1,1,";e",GitPatchSizeFunction,"GitPatchSizeFunction",NULL);  /* doc: patch -> integer */
	AddUDF(env,"git-patch-to-buf","bs",1,1,";e",GitPatchToBufFunction,"GitPatchToBufFunction",NULL);  /* doc: patch -> string */
	AddUDF(env,"git-patch-free","b",1,1,";e",GitPatchFreeFunction,"GitPatchFreeFunction",NULL);  /* doc: patch -> boolean */
	AddUDF(env,"git-apply","b",2,3,";e;e;lsy",GitApplyFunction,"GitApplyFunction",NULL);  /* doc: repo diff location? -> boolean */
	AddUDF(env,"git-apply-to-tree","be",3,3,";e;e;e",GitApplyToTreeFunction,"GitApplyToTreeFunction",NULL);  /* doc: repo tree diff -> handle */

	/* doc-group: Checkout and reset */
	AddUDF(env,"git-checkout-head","b",1,3,";e;lmsy;m",GitCheckoutHeadFunction,"GitCheckoutHeadFunction",NULL);  /* doc: repo strategy? paths? -> boolean */
	AddUDF(env,"git-checkout-index","b",2,4,";e;ey;lmsy;m",GitCheckoutIndexFunction,"GitCheckoutIndexFunction",NULL);  /* doc: repo index strategy? paths? -> boolean */
	AddUDF(env,"git-checkout-tree","b",2,4,";e;e;lmsy;m",GitCheckoutTreeFunction,"GitCheckoutTreeFunction",NULL);  /* doc: repo treeish strategy? paths? -> boolean */
	AddUDF(env,"git-reset","b",3,3,";e;e;lsy",GitResetFunction,"GitResetFunction",NULL);  /* doc: repo target type -> boolean */
	AddUDF(env,"git-reset-default","b",3,3,";e;ey;m",GitResetDefaultFunction,"GitResetDefaultFunction",NULL);  /* doc: repo target paths -> boolean */

	/* doc-group: Revision parsing */
	AddUDF(env,"git-revparse-single","be",2,2,";e;sy",GitRevparseSingleFunction,"GitRevparseSingleFunction",NULL);  /* doc: repo spec -> handle */
	AddUDF(env,"git-revparse-ext","bm",2,2,";e;sy",GitRevparseExtFunction,"GitRevparseExtFunction",NULL);  /* doc: repo spec -> multifield */
	AddUDF(env,"git-revparse","bm",2,2,";e;sy",GitRevparseFunction,"GitRevparseFunction",NULL);  /* doc: repo spec -> multifield */
	AddUDF(env,"git-describe-commit","bs",1,3,";e;lsy;b",GitDescribeCommitFunction,"GitDescribeCommitFunction",NULL);  /* doc: object strategy? always-long? -> string */
	AddUDF(env,"git-describe-workdir","bs",1,3,";e;lsy;b",GitDescribeWorkdirFunction,"GitDescribeWorkdirFunction",NULL);  /* doc: repo strategy? always-long? -> string */
	AddUDF(env,"git-graph-ahead-behind","bm",3,3,";e;sy;sy",GitGraphAheadBehindFunction,"GitGraphAheadBehindFunction",NULL);  /* doc: repo local upstream -> multifield */
	AddUDF(env,"git-graph-descendant-of","b",3,3,";e;sy;sy",GitGraphDescendantOfFunction,"GitGraphDescendantOfFunction",NULL);  /* doc: repo commit ancestor -> boolean */

	/* doc-group: Merging */
	AddUDF(env,"git-annotated-commit-from-ref","be",2,2,";e;e",GitAnnotatedCommitFromRefFunction,"GitAnnotatedCommitFromRefFunction",NULL);  /* doc: repo ref -> handle */
	AddUDF(env,"git-annotated-commit-lookup","be",2,2,";e;sy",GitAnnotatedCommitLookupFunction,"GitAnnotatedCommitLookupFunction",NULL);  /* doc: repo id -> handle */
	AddUDF(env,"git-annotated-commit-from-revspec","be",2,2,";e;sy",GitAnnotatedCommitFromRevspecFunction,"GitAnnotatedCommitFromRevspecFunction",NULL);  /* doc: repo spec -> handle */
	AddUDF(env,"git-annotated-commit-id","bs",1,1,";e",GitAnnotatedCommitIdFunction,"GitAnnotatedCommitIdFunction",NULL);  /* doc: annotated -> string */
	AddUDF(env,"git-annotated-commit-ref","bsy",1,1,";e",GitAnnotatedCommitRefFunction,"GitAnnotatedCommitRefFunction",NULL);  /* doc: annotated -> string */
	AddUDF(env,"git-annotated-commit-free","b",1,1,";e",GitAnnotatedCommitFreeFunction,"GitAnnotatedCommitFreeFunction",NULL);  /* doc: annotated -> boolean */
	AddUDF(env,"git-merge-analysis","bm",2,2,";e;m",GitMergeAnalysisFunction,"GitMergeAnalysisFunction",NULL);  /* doc: repo heads -> multifield */
	AddUDF(env,"git-merge","b",2,4,";e;m;lmsy;lmsy",GitMergeFunction,"GitMergeFunction",NULL);  /* doc: repo heads merge-flags? checkout-strategy? -> boolean */
	AddUDF(env,"git-merge-base","bsy",3,3,";e;sy;sy",GitMergeBaseFunction,"GitMergeBaseFunction",NULL);  /* doc: repo id-one id-two -> string */
	AddUDF(env,"git-merge-trees","be",4,5,";e;ey;e;e;lmsy",GitMergeTreesFunction,"GitMergeTreesFunction",NULL);  /* doc: repo ancestor ours theirs flags? -> handle */
	AddUDF(env,"git-merge-commits","be",3,4,";e;e;e;lmsy",GitMergeCommitsFunction,"GitMergeCommitsFunction",NULL);  /* doc: repo ours theirs flags? -> handle */
	AddUDF(env,"git-cherrypick","b",2,2,";e;e",GitCherrypickFunction,"GitCherrypickFunction",NULL);  /* doc: repo commit -> boolean */
	AddUDF(env,"git-cherrypick-commit","be",3,4,";e;e;e;l",GitCherrypickCommitFunction,"GitCherrypickCommitFunction",NULL);  /* doc: repo commit our-commit mainline? -> handle */
	AddUDF(env,"git-revert","b",2,2,";e;e",GitRevertFunction,"GitRevertFunction",NULL);  /* doc: repo commit -> boolean */
	AddUDF(env,"git-revert-commit","be",3,4,";e;e;e;l",GitRevertCommitFunction,"GitRevertCommitFunction",NULL);  /* doc: repo commit our-commit mainline? -> handle */

	/* doc-group: Stashes */
	AddUDF(env,"git-stash-save","bs",2,4,";e;ey;sy;lmsy",GitStashSaveFunction,"GitStashSaveFunction",NULL);  /* doc: repo stasher message? flags? -> string */
	AddUDF(env,"git-stash-list","bm",1,1,";e",GitStashListFunction,"GitStashListFunction",NULL);  /* doc: repo -> multifield */
	AddUDF(env,"git-stash-apply","b",1,3,";e;l;lmsy",GitStashApplyFunction,"GitStashApplyFunction",NULL);  /* doc: repo n? flags? -> boolean */
	AddUDF(env,"git-stash-pop","b",1,3,";e;l;lmsy",GitStashPopFunction,"GitStashPopFunction",NULL);  /* doc: repo n? flags? -> boolean */
	AddUDF(env,"git-stash-drop","b",1,2,";e;l",GitStashDropFunction,"GitStashDropFunction",NULL);  /* doc: repo n? -> boolean */

	/* doc-group: Blame */
	AddUDF(env,"git-blame-file","be",2,3,";e;sy;lmsy",GitBlameFileFunction,"GitBlameFileFunction",NULL);  /* doc: repo path flags? -> handle */
	AddUDF(env,"git-blame-hunkcount","bl",1,1,";e",GitBlameHunkcountFunction,"GitBlameHunkcountFunction",NULL);  /* doc: blame -> integer */
	AddUDF(env,"git-blame-get-hunk-byindex","bm",2,2,";e;l",GitBlameGetHunkByindexFunction,"GitBlameGetHunkByindexFunction",NULL);  /* doc: blame n -> multifield */
	AddUDF(env,"git-blame-get-hunk-byline","bm",2,2,";e;l",GitBlameGetHunkBylineFunction,"GitBlameGetHunkBylineFunction",NULL);  /* doc: blame line -> multifield */
	AddUDF(env,"git-blame-free","b",1,1,";e",GitBlameFreeFunction,"GitBlameFreeFunction",NULL);  /* doc: blame -> boolean */

	/* doc-group: Notes */
	AddUDF(env,"git-note-read","bsy",2,3,";e;sy;sy",GitNoteReadFunction,"GitNoteReadFunction",NULL);  /* doc: repo id notes-ref? -> string */
	AddUDF(env,"git-note-create","bs",5,7,";e;sy;sy;ey;ey;sy;b",GitNoteCreateFunction,"GitNoteCreateFunction",NULL);  /* doc: repo id message author committer notes-ref? force? -> string */
	AddUDF(env,"git-note-remove","b",4,5,";e;sy;ey;ey;sy",GitNoteRemoveFunction,"GitNoteRemoveFunction",NULL);  /* doc: repo id author committer notes-ref? -> boolean */
	AddUDF(env,"git-note-default-ref","bs",1,1,";e",GitNoteDefaultRefFunction,"GitNoteDefaultRefFunction",NULL);  /* doc: repo -> string */

	/* doc-group: Worktrees */
	AddUDF(env,"git-worktree-list","bm",1,1,";e",GitWorktreeListFunction,"GitWorktreeListFunction",NULL);  /* doc: repo -> multifield */
	AddUDF(env,"git-worktree-lookup","be",2,2,";e;sy",GitWorktreeLookupFunction,"GitWorktreeLookupFunction",NULL);  /* doc: repo name -> handle */
	AddUDF(env,"git-worktree-open-from-repository","be",1,1,";e",GitWorktreeOpenFromRepositoryFunction,"GitWorktreeOpenFromRepositoryFunction",NULL);  /* doc: repo -> handle */
	AddUDF(env,"git-worktree-add","be",3,5,";e;sy;sy;ey;b",GitWorktreeAddFunction,"GitWorktreeAddFunction",NULL);  /* doc: repo name path ref? lock? -> handle */
	AddUDF(env,"git-worktree-name","bsy",1,1,";e",GitWorktreeNameFunction,"GitWorktreeNameFunction",NULL);  /* doc: worktree -> string */
	AddUDF(env,"git-worktree-path","bsy",1,1,";e",GitWorktreePathFunction,"GitWorktreePathFunction",NULL);  /* doc: worktree -> string */
	AddUDF(env,"git-worktree-validate","b",1,1,";e",GitWorktreeValidateFunction,"GitWorktreeValidateFunction",NULL);  /* doc: worktree -> boolean */
	AddUDF(env,"git-worktree-lock","b",1,2,";e;sy",GitWorktreeLockFunction,"GitWorktreeLockFunction",NULL);  /* doc: worktree reason? -> boolean */
	AddUDF(env,"git-worktree-unlock","b",1,1,";e",GitWorktreeUnlockFunction,"GitWorktreeUnlockFunction",NULL);  /* doc: worktree -> boolean */
	AddUDF(env,"git-worktree-is-locked","bs",1,1,";e",GitWorktreeIsLockedFunction,"GitWorktreeIsLockedFunction",NULL);  /* doc: worktree -> string */
	AddUDF(env,"git-worktree-is-prunable","b",1,2,";e;lmsy",GitWorktreeIsPrunableFunction,"GitWorktreeIsPrunableFunction",NULL);  /* doc: worktree flags? -> boolean */
	AddUDF(env,"git-worktree-prune","b",1,2,";e;lmsy",GitWorktreePruneFunction,"GitWorktreePruneFunction",NULL);  /* doc: worktree flags? -> boolean */
	AddUDF(env,"git-worktree-free","b",1,1,";e",GitWorktreeFreeFunction,"GitWorktreeFreeFunction",NULL);  /* doc: worktree -> boolean */

	/* doc-group: Submodules */
	AddUDF(env,"git-submodule-list","bm",1,1,";e",GitSubmoduleListFunction,"GitSubmoduleListFunction",NULL);  /* doc: repo -> multifield */
	AddUDF(env,"git-submodule-lookup","be",2,2,";e;sy",GitSubmoduleLookupFunction,"GitSubmoduleLookupFunction",NULL);  /* doc: repo name -> handle */
	AddUDF(env,"git-submodule-add-setup","be",3,4,";e;sy;sy;b",GitSubmoduleAddSetupFunction,"GitSubmoduleAddSetupFunction",NULL);  /* doc: repo url path use-gitlink? -> handle */
	AddUDF(env,"git-submodule-clone","be",1,1,";e",GitSubmoduleCloneFunction,"GitSubmoduleCloneFunction",NULL);  /* doc: submodule -> handle */
	AddUDF(env,"git-submodule-repo-init","be",1,2,";e;b",GitSubmoduleRepoInitFunction,"GitSubmoduleRepoInitFunction",NULL);  /* doc: submodule use-gitlink? -> handle */
	AddUDF(env,"git-submodule-add-finalize","b",1,1,";e",GitSubmoduleAddFinalizeFunction,"GitSubmoduleAddFinalizeFunction",NULL);  /* doc: submodule -> boolean */
	AddUDF(env,"git-submodule-add-to-index","b",1,2,";e;b",GitSubmoduleAddToIndexFunction,"GitSubmoduleAddToIndexFunction",NULL);  /* doc: submodule write-index? -> boolean */
	AddUDF(env,"git-submodule-name","bsy",1,1,";e",GitSubmoduleNameFunction,"GitSubmoduleNameFunction",NULL);  /* doc: submodule -> string */
	AddUDF(env,"git-submodule-path","bsy",1,1,";e",GitSubmodulePathFunction,"GitSubmodulePathFunction",NULL);  /* doc: submodule -> string */
	AddUDF(env,"git-submodule-url","bsy",1,1,";e",GitSubmoduleUrlFunction,"GitSubmoduleUrlFunction",NULL);  /* doc: submodule -> string */
	AddUDF(env,"git-submodule-branch","bsy",1,1,";e",GitSubmoduleBranchFunction,"GitSubmoduleBranchFunction",NULL);  /* doc: submodule -> string */
	AddUDF(env,"git-submodule-head-id","bsy",1,1,";e",GitSubmoduleHeadIdFunction,"GitSubmoduleHeadIdFunction",NULL);  /* doc: submodule -> string */
	AddUDF(env,"git-submodule-index-id","bsy",1,1,";e",GitSubmoduleIndexIdFunction,"GitSubmoduleIndexIdFunction",NULL);  /* doc: submodule -> string */
	AddUDF(env,"git-submodule-wd-id","bsy",1,1,";e",GitSubmoduleWdIdFunction,"GitSubmoduleWdIdFunction",NULL);  /* doc: submodule -> string */
	AddUDF(env,"git-submodule-ignore","by",1,1,";e",GitSubmoduleIgnoreFunction,"GitSubmoduleIgnoreFunction",NULL);  /* doc: submodule -> symbol */
	AddUDF(env,"git-submodule-status","bm",2,3,";e;sy;lsy",GitSubmoduleStatusFunction,"GitSubmoduleStatusFunction",NULL);  /* doc: repo name ignore? -> multifield */
	AddUDF(env,"git-submodule-set-url","b",3,3,";e;sy;sy",GitSubmoduleSetUrlFunction,"GitSubmoduleSetUrlFunction",NULL);  /* doc: repo name url -> boolean */
	AddUDF(env,"git-submodule-init","b",1,2,";e;b",GitSubmoduleInitFunction,"GitSubmoduleInitFunction",NULL);  /* doc: submodule overwrite? -> boolean */
	AddUDF(env,"git-submodule-update","b",1,2,";e;b",GitSubmoduleUpdateFunction,"GitSubmoduleUpdateFunction",NULL);  /* doc: submodule init? -> boolean */
	AddUDF(env,"git-submodule-sync","b",1,1,";e",GitSubmoduleSyncFunction,"GitSubmoduleSyncFunction",NULL);  /* doc: submodule -> boolean */
	AddUDF(env,"git-submodule-reload","b",1,2,";e;b",GitSubmoduleReloadFunction,"GitSubmoduleReloadFunction",NULL);  /* doc: submodule force? -> boolean */
	AddUDF(env,"git-submodule-open","be",1,1,";e",GitSubmoduleOpenFunction,"GitSubmoduleOpenFunction",NULL);  /* doc: submodule -> handle */
	AddUDF(env,"git-submodule-free","b",1,1,";e",GitSubmoduleFreeFunction,"GitSubmoduleFreeFunction",NULL);  /* doc: submodule -> boolean */

	/* doc-group: The object database */
	AddUDF(env,"git-odb-exists","b",2,2,";e;sy",GitOdbExistsFunction,"GitOdbExistsFunction",NULL);  /* doc: odb id -> boolean */
	AddUDF(env,"git-odb-read","bs",2,2,";e;sy",GitOdbReadFunction,"GitOdbReadFunction",NULL);  /* doc: odb id -> string */
	AddUDF(env,"git-odb-read-header","bm",2,2,";e;sy",GitOdbReadHeaderFunction,"GitOdbReadHeaderFunction",NULL);  /* doc: odb id -> multifield */
	AddUDF(env,"git-odb-write","bs",3,3,";e;sy;lsy",GitOdbWriteFunction,"GitOdbWriteFunction",NULL);  /* doc: odb content type -> string */
	AddUDF(env,"git-odb-hash","bs",2,2,";sy;lsy",GitOdbHashFunction,"GitOdbHashFunction",NULL);  /* doc: content type -> string */
	AddUDF(env,"git-odb-hashfile","bs",2,2,";sy;lsy",GitOdbHashfileFunction,"GitOdbHashfileFunction",NULL);  /* doc: path type -> string */
	AddUDF(env,"git-odb-refresh","b",1,1,";e",GitOdbRefreshFunction,"GitOdbRefreshFunction",NULL);  /* doc: odb -> boolean */
	AddUDF(env,"git-odb-free","b",1,1,";e",GitOdbFreeFunction,"GitOdbFreeFunction",NULL);  /* doc: odb -> boolean */
}
