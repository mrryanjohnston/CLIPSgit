# CLIPSgit API Reference

## The library

### git-libgit2-init

```clips
(git-libgit2-init)
```

Initialises libgit2, or counts one more initialisation if it already is.
The environment does this itself when it starts, so calling it is optional;
what it answers is the initialisation count, and each count is one
`git-libgit2-shutdown` owed.

**Returns:** integer, or `FALSE` if the call is refused.

### git-libgit2-shutdown

```clips
(git-libgit2-shutdown)
```

Counts one initialisation down, and answers the count that is left. Before
it does, every handle still open is freed: libgit2 must not be shut down
under a live handle, and after this none of them is any use. The
initialisation the environment took at startup is released only by a
shutdown that is not matched by an init, so a program that calls this once
without calling `git-libgit2-init` has switched the library off.

**Returns:** integer, or `FALSE` if the call is refused.

### git-libgit2-version

```clips
(git-libgit2-version)
```

The version of the libgit2 loaded right now, as a string: `"1.9.7"`.

**Returns:** string. This one takes no arguments and cannot be refused.

### git-libgit2-version-number

```clips
(git-libgit2-version-number)
```

The same version as a number: major times ten thousand, minor times a
hundred, plus the revision. 1.9.7 is `10907`.

**Returns:** integer. This one takes no arguments and cannot be refused.

### git-built-version

```clips
(git-built-version)
```

Not a libgit2 function. The version of the libgit2 the wrappers were
*compiled* against, in the same numbers. That is what decides what is in
this binary -- which feature names `git-libgit2-features` can report, for
one -- and with a static build it is also what is loaded, so the two agree.

**Returns:** integer. This one takes no arguments and cannot be refused.

### git-libgit2-prerelease

```clips
(git-libgit2-prerelease)
```

The prerelease tag of the loaded libgit2 -- `"alpha"`, `"rc1"` -- or `nil`
for a release.

**Returns:** string. This one takes no arguments and cannot be refused.

### git-libgit2-features

```clips
(git-libgit2-features)
```

What the loaded libgit2 was built with, as a multifield of `GIT_FEATURE_*`
symbols. `GIT_FEATURE_HTTPS` means HTTPS transport is compiled in;
`GIT_FEATURE_SSH` is not, in the vendored build. A libgit2 1.9 reports more
of these than a 1.8 does.

**Returns:** multifield. This one takes no arguments and cannot be refused.

### git-handle-count

```clips
(git-handle-count)
```

Not a libgit2 function. How many handles this library has handed out and
not yet had back. A program that frees what it takes ends with zero.

**Returns:** integer. This one takes no arguments and cannot be refused.

### git-handle-kinds

```clips
(git-handle-kinds)
```

Not a libgit2 function. The kind of every open handle, as a multifield of
strings: `"commit"`, `"tree"`, `"repository"`. A borrowed handle names what
it is borrowed from in parentheses, as `"tree entry (tree)"`. For finding
what a program forgot to free.

**Returns:** multifield. This one takes no arguments and cannot be refused.

### git-error-last

```clips
(git-error-last)
```

The message libgit2 left for the last error, or `nil` when there is none.
Every refusal in this library writes the same message to stderr as it
happens, so this is for a program that wants to keep it. libgit2 does not
clear the message on success, so it can be stale after a call that
succeeded: the refusal is what says there was an error, and this is what
it was.

**Returns:** string. This one takes no arguments and cannot be refused.

### git-error-last-class

```clips
(git-error-last-class)
```

Which part of libgit2 the last error came from, as a `GIT_ERROR_*` symbol:
`GIT_ERROR_REFERENCE`, `GIT_ERROR_INDEX`. `GIT_ERROR_NONE` when there is
none.

**Returns:** symbol. This one takes no arguments and cannot be refused.


## Repositories

### git-repository-open

```clips
(git-repository-open ?path)
```

Opens the repository at a path: either a working directory with a `.git`
in it, or a git directory itself. Answers a repository handle, which
`git-repository-free` gives back along with everything that came from it.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-open-ext

```clips
(git-repository-open-ext ?path [?flags] [?ceiling-dirs])
```

The same, with control over the search. The flags are
`GIT_REPOSITORY_OPEN_*` symbols: `GIT_REPOSITORY_OPEN_NO_SEARCH` opens the
path or nothing, where the default walks up the directory tree looking for
a repository; `GIT_REPOSITORY_OPEN_BARE` opens a bare repository quickly
without looking for a working directory; `GIT_REPOSITORY_OPEN_FROM_ENV`
honours `GIT_DIR` and its relatives, and takes `nil` for the path. The
ceiling directories are a colon-separated list the search will not walk
above.

**Arguments:** 1 to 3.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-open-bare

```clips
(git-repository-open-bare ?path)
```

Opens a bare repository at a path, without looking for a working directory
or walking upward.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-open-from-worktree

```clips
(git-repository-open-from-worktree ?worktree)
```

Opens the repository a worktree handle stands for.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-init

```clips
(git-repository-init ?path [?bare])
```

Makes a new repository at a path, creating the directory if it is not
there. `TRUE` for the second argument makes it bare. The initial branch is
whatever `init.defaultBranch` says, or `master`; `git-repository-init-ext`
is the way to choose.

**Arguments:** 1 to 2.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-init-ext

```clips
(git-repository-init-ext ?path [?flags] [?initial-head] [?description] [?origin-url] [?workdir])
```

Makes a new repository with more said about it. The flags are
`GIT_REPOSITORY_INIT_*` symbols and default to `GIT_REPOSITORY_INIT_MKPATH`,
which creates the path; `GIT_REPOSITORY_INIT_BARE` makes it bare and
`GIT_REPOSITORY_INIT_NO_REINIT` refuses a path that is already a
repository. Then, each optional and each taking `nil`: the name of the
initial branch, a description, an origin URL to configure a remote for, and
a working directory to use instead of the path.

**Arguments:** 1 to 6.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-discover

```clips
(git-repository-discover ?start-path [?across-fs] [?ceiling-dirs])
```

Finds the repository a path is in by walking up from it, and answers the
git directory's path. `TRUE` for the second argument lets the walk cross
filesystem boundaries; the third is the colon-separated list of directories
it will not walk above.

**Arguments:** 1 to 3.

**Returns:** string, or `FALSE` if the call is refused.

### git-clone

```clips
(git-clone ?url ?path [?bare] [?branch])
```

Clones a repository from a URL or a path into a new directory, and answers
the new repository. `TRUE` for the third argument makes the clone bare; the
fourth names the branch to check out instead of the remote's HEAD. A source
that needs a credential gets one as described under Credentials.

**Arguments:** 2 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-free

```clips
(git-repository-free ?repo)
```

Gives a repository back, and everything that came from it first: commits,
trees, references, an index, a revwalk, whatever is still open. Every one
of those handles is refused afterwards, and so is this one.

**Returns:** boolean.

### git-repository-path

```clips
(git-repository-path ?repo)
```

The path of the git directory, with a trailing slash.

**Returns:** string, or `FALSE` if the call is refused.

### git-repository-workdir

```clips
(git-repository-workdir ?repo)
```

The path of the working directory, with a trailing slash, or `nil` for a
bare repository.

**Returns:** string, or `FALSE` if the call is refused.

### git-repository-commondir

```clips
(git-repository-commondir ?repo)
```

The path of the directory shared between a repository and its worktrees:
the same as `git-repository-path` for the main repository, the main
repository's path for a worktree.

**Returns:** string, or `FALSE` if the call is refused.

### git-repository-is-bare

```clips
(git-repository-is-bare ?repo)
```

Whether the repository has no working directory.

**Returns:** boolean.

### git-repository-is-empty

```clips
(git-repository-is-empty ?repo)
```

Whether the repository has just been initialised: HEAD is unborn and points
at the default branch, and nothing has been committed.

**Returns:** boolean.

### git-repository-is-shallow

```clips
(git-repository-is-shallow ?repo)
```

Whether the repository was cloned with a limited depth.

**Returns:** boolean.

### git-repository-is-worktree

```clips
(git-repository-is-worktree ?repo)
```

Whether the repository is a linked worktree of another.

**Returns:** boolean.

### git-repository-head

```clips
(git-repository-head ?repo)
```

The reference HEAD points at, resolved: the branch, or a direct reference
when HEAD is detached. A refusal when HEAD is unborn.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-head-detached

```clips
(git-repository-head-detached ?repo)
```

Whether HEAD points at a commit directly rather than at a branch.

**Returns:** boolean.

### git-repository-head-unborn

```clips
(git-repository-head-unborn ?repo)
```

Whether HEAD points at a branch that has no commits yet, as in a new
repository.

**Returns:** boolean.

### git-repository-set-head

```clips
(git-repository-set-head ?repo ?refname)
```

Points HEAD at a reference by name: `refs/heads/main` to be on that branch.
A branch that does not exist yet is fine, and leaves HEAD unborn there; a
reference that is not a branch detaches HEAD at what it points to. Nothing
in the working directory changes: that is `git-checkout-head`.

**Returns:** boolean.

### git-repository-set-head-detached

```clips
(git-repository-set-head-detached ?repo ?commit-id)
```

Points HEAD at a commit directly, by id.

**Returns:** boolean.

### git-repository-detach-head

```clips
(git-repository-detach-head ?repo)
```

Points HEAD at the commit it currently resolves to, so that it no longer
follows the branch.

**Returns:** boolean.

### git-repository-state

```clips
(git-repository-state ?repo)
```

What operation the repository is in the middle of, as a
`GIT_REPOSITORY_STATE_*` symbol: `GIT_REPOSITORY_STATE_MERGE` after
`git-merge`, `GIT_REPOSITORY_STATE_NONE` ordinarily.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-repository-state-cleanup

```clips
(git-repository-state-cleanup ?repo)
```

Removes the files that record an operation in progress -- `MERGE_HEAD`,
`MERGE_MSG`, `CHERRY_PICK_HEAD` -- which is what ends one.

**Returns:** boolean.

### git-repository-message

```clips
(git-repository-message ?repo)
```

The message an operation in progress left, as the commit message it
proposes: `MERGE_MSG` after a merge. `nil` when there is none.

**Returns:** string, or `FALSE` if the call is refused.

### git-repository-message-remove

```clips
(git-repository-message-remove ?repo)
```

Removes that message.

**Returns:** boolean.

### git-repository-config

```clips
(git-repository-config ?repo)
```

The repository's configuration, with the global and system files behind it:
what a lookup sees, and what `git-config-set-string` writes to (the
repository's own file). Freed with `git-config-free`, or with the
repository.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-config-snapshot

```clips
(git-repository-config-snapshot ?repo)
```

The same configuration frozen at this moment, which is faster to read many
values from and refuses to be written.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-index

```clips
(git-repository-index ?repo)
```

The repository's index. Freed with `git-index-free`, or with the
repository.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-odb

```clips
(git-repository-odb ?repo)
```

The repository's object database, for reading and writing raw objects.
Freed with `git-odb-free`, or with the repository.

**Returns:** handle, or `FALSE` if the call is refused.

### git-repository-ident

```clips
(git-repository-ident ?repo)
```

The identity the repository has been told to write reflogs with, as a
two-element multifield of name and email, each `nil` when unset.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-repository-set-ident

```clips
(git-repository-set-ident ?repo ?name ?email)
```

Sets that identity. `nil` for either goes back to the configuration.

**Returns:** boolean.

### git-repository-set-workdir

```clips
(git-repository-set-workdir ?repo ?workdir [?update-gitlink])
```

Sets the working directory of a repository. `TRUE` for the third argument
writes a `.git` file into the directory pointing back at the repository.

**Arguments:** 2 to 3.

**Returns:** boolean.

### git-repository-get-namespace

```clips
(git-repository-get-namespace ?repo)
```

The reference namespace in use, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-repository-set-namespace

```clips
(git-repository-set-namespace ?repo ?namespace)
```

Sets the reference namespace, under which every reference name is looked
up: with the namespace `foo`, `refs/heads/main` means
`refs/namespaces/foo/refs/heads/main`. `nil` clears it.

**Returns:** boolean.

### git-repository-item-path

```clips
(git-repository-item-path ?repo ?item)
```

The path of one of the repository's pieces, named by a
`GIT_REPOSITORY_ITEM_*` symbol: `GIT_REPOSITORY_ITEM_INDEX`,
`GIT_REPOSITORY_ITEM_HOOKS`. `nil` for one the repository does not have,
such as the working directory of a bare one.

**Returns:** string, or `FALSE` if the call is refused.

### git-repository-hashfile

```clips
(git-repository-hashfile ?repo ?path [?type] [?as-path])
```

The id a file would have as an object, with the repository's filters --
line endings, `.gitattributes` -- applied as they would be on add. The
path is relative to the working directory; the type defaults to
`GIT_OBJECT_BLOB`; the optional as-path is the path to look filters up by
when it differs.

**Arguments:** 2 to 4.

**Returns:** string, or `FALSE` if the call is refused.


## Objects

### git-object-lookup

```clips
(git-object-lookup ?repo ?id [?type])
```

Looks an object up by id, of any type unless one is named, and answers a
handle of whichever of the four kinds it turns out to be. The functions
for that kind take it, and so do the `git-object-*` ones.

**Arguments:** 2 to 3.

**Returns:** handle, or `FALSE` if the call is refused.

### git-object-lookup-prefix

```clips
(git-object-lookup-prefix ?repo ?prefix [?type])
```

The same by a prefix of the id, at least four characters and unambiguous.

**Arguments:** 2 to 3.

**Returns:** handle, or `FALSE` if the call is refused.

### git-object-id

```clips
(git-object-id ?object)
```

The id of an object of any kind.

**Returns:** string, or `FALSE` if the call is refused.

### git-object-short-id

```clips
(git-object-short-id ?object)
```

The shortest prefix of the id that is unambiguous in this repository, at
least seven characters.

**Returns:** string, or `FALSE` if the call is refused.

### git-object-type

```clips
(git-object-type ?object)
```

Which kind an object is, as a `GIT_OBJECT_*` symbol.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-object-peel

```clips
(git-object-peel ?object ?type)
```

Follows an object to one of the named type: a tag to what it tags, a
commit to its tree. `GIT_OBJECT_ANY` peels a tag as far as it goes.

**Returns:** handle, or `FALSE` if the call is refused.

### git-object-free

```clips
(git-object-free ?object)
```

Gives back an object of any of the four kinds. `git-commit-free` and its
relatives do the same for one kind.

**Returns:** boolean.

### git-object-type2string

```clips
(git-object-type2string ?type)
```

The name git uses for a type: `"commit"` for `GIT_OBJECT_COMMIT`.

**Returns:** string, or `FALSE` if the call is refused.

### git-object-string2type

```clips
(git-object-string2type ?name)
```

The type for a name git uses: `GIT_OBJECT_TREE` for `"tree"`.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-object-rawcontent-is-valid

```clips
(git-object-rawcontent-is-valid ?content ?type)
```

Whether some bytes would parse as an object of a type, without writing
them.

**Returns:** boolean.

### git-oid-is-valid

```clips
(git-oid-is-valid ?text)
```

Not a libgit2 function. Whether a string is forty hex characters.

**Returns:** boolean.

### git-oid-is-zero

```clips
(git-oid-is-zero ?id)
```

Whether an id is the all-zero id, which git uses for "nothing".

**Returns:** boolean.

### git-oid-cmp

```clips
(git-oid-cmp ?id-a ?id-b)
```

Orders two ids: negative, zero or positive.

**Returns:** integer, or `FALSE` if the call is refused.


## Signatures

### git-signature-new

```clips
(git-signature-new ?name ?email ?time ?offset)
```

A signature -- a name, an email, and a time -- for use as an author,
committer, tagger or stasher. The time is seconds since the epoch and the
offset is the timezone in minutes east of UTC. Freed with
`git-signature-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-signature-now

```clips
(git-signature-now ?name ?email)
```

The same with the time being now, in the local timezone.

**Returns:** handle, or `FALSE` if the call is refused.

### git-signature-default

```clips
(git-signature-default ?repo)
```

A signature from the repository's `user.name` and `user.email`. A refusal
when those are not configured. Everywhere a signature is taken, `nil`
means this.

**Returns:** handle, or `FALSE` if the call is refused.

### git-signature-from-buffer

```clips
(git-signature-from-buffer ?text)
```

A signature parsed from the form git writes into a commit:
`"A Name <email> 1700000000 +0100"`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-signature-dup

```clips
(git-signature-dup ?signature)
```

A copy of a signature, which is how a borrowed one -- a commit's author --
is kept past the commit.

**Returns:** handle, or `FALSE` if the call is refused.

### git-signature-name

```clips
(git-signature-name ?signature)
```

The name.

**Returns:** string, or `FALSE` if the call is refused.

### git-signature-email

```clips
(git-signature-email ?signature)
```

The email.

**Returns:** string, or `FALSE` if the call is refused.

### git-signature-time

```clips
(git-signature-time ?signature)
```

The time, in seconds since the epoch.

**Returns:** integer, or `FALSE` if the call is refused.

### git-signature-offset

```clips
(git-signature-offset ?signature)
```

The timezone, in minutes east of UTC.

**Returns:** integer, or `FALSE` if the call is refused.

### git-signature-free

```clips
(git-signature-free ?signature)
```

Gives a signature back. A signature borrowed from a commit or a tag is not
freed on its own and this refuses it; it goes with the object.

**Returns:** boolean.


## Commits

### git-commit-lookup

```clips
(git-commit-lookup ?repo ?id)
```

Looks a commit up by id. A refusal when the id names something else.

**Returns:** handle, or `FALSE` if the call is refused.

### git-commit-lookup-prefix

```clips
(git-commit-lookup-prefix ?repo ?prefix)
```

The same by a prefix of the id, at least four characters and unambiguous.

**Returns:** handle, or `FALSE` if the call is refused.

### git-commit-create

```clips
(git-commit-create ?repo ?update-ref ?author ?committer ?message ?tree ?parents)
```

Writes a commit and answers its id. The reference named second --
`"HEAD"`, `"refs/heads/main"` -- is moved to the new commit, or `nil` moves
nothing. The author and committer are signature handles, or `nil` for the
repository's default identity. The tree is a tree handle and the parents
are a multifield of commit handles, empty for a root commit.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-create-buffer

```clips
(git-commit-create-buffer ?repo ?author ?committer ?message ?tree ?parents)
```

The bytes of the commit `git-commit-create` would write, as a string,
without writing it. The same arguments less the reference to update.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-amend

```clips
(git-commit-amend ?commit ?update-ref ?author ?committer ?message ?tree)
```

Writes a new commit in place of one, taking from the old whatever is given
as `nil`: the author, the committer, the message, the tree. The reference
named second is moved, or `nil` moves nothing. Answers the new id.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-id

```clips
(git-commit-id ?commit)
```

The id.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-message

```clips
(git-commit-message ?commit)
```

The message, with the leading whitespace git strips.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-message-raw

```clips
(git-commit-message-raw ?commit)
```

The message exactly as stored.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-message-encoding

```clips
(git-commit-message-encoding ?commit)
```

The encoding declared in the commit header, or `nil` for UTF-8.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-summary

```clips
(git-commit-summary ?commit)
```

The first paragraph of the message, on one line.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-body

```clips
(git-commit-body ?commit)
```

The message after the summary, or `nil` when there is nothing after it.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-raw-header

```clips
(git-commit-raw-header ?commit)
```

The commit's headers -- tree, parents, author, committer -- as one string.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-header-field

```clips
(git-commit-header-field ?commit ?field)
```

The value of one header by name, or `nil` when the commit has no such
header.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-time

```clips
(git-commit-time ?commit)
```

The committer's time, in seconds since the epoch.

**Returns:** integer, or `FALSE` if the call is refused.

### git-commit-time-offset

```clips
(git-commit-time-offset ?commit)
```

The committer's timezone, in minutes east of UTC.

**Returns:** integer, or `FALSE` if the call is refused.

### git-commit-author

```clips
(git-commit-author ?commit)
```

The author, as a signature handle borrowed from the commit: it goes when
the commit does, and `git-signature-free` refuses it. `git-signature-dup`
makes a copy to keep.

**Returns:** handle, or `FALSE` if the call is refused.

### git-commit-committer

```clips
(git-commit-committer ?commit)
```

The committer, likewise.

**Returns:** handle, or `FALSE` if the call is refused.

### git-commit-tree

```clips
(git-commit-tree ?commit)
```

The tree the commit points at, as a handle to free.

**Returns:** handle, or `FALSE` if the call is refused.

### git-commit-tree-id

```clips
(git-commit-tree-id ?commit)
```

The id of that tree.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-parentcount

```clips
(git-commit-parentcount ?commit)
```

How many parents: zero for a root commit, two for a merge.

**Returns:** integer, or `FALSE` if the call is refused.

### git-commit-parent

```clips
(git-commit-parent ?commit ?n)
```

Parent number n, counted from zero, as a commit handle to free.

**Returns:** handle, or `FALSE` if the call is refused.

### git-commit-parent-id

```clips
(git-commit-parent-id ?commit ?n)
```

The id of parent number n. A refusal when there is no such parent.

**Returns:** string, or `FALSE` if the call is refused.

### git-commit-nth-gen-ancestor

```clips
(git-commit-nth-gen-ancestor ?commit ?n)
```

The commit n generations back along first parents: `HEAD~n`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-commit-to-fact

```clips
(git-commit-to-fact ?commit ?deftemplate)
```

Not a libgit2 function. Asserts the commit as a fact of the named
deftemplate, filling whichever of these slots the template has: `id`,
`tree`, `parents` (a multislot of ids), `summary`, `message`, `body`,
`author-name`, `author-email`, `author-time`, `author-offset`,
`committer-name`, `committer-email`, `committer-time`, `committer-offset`.
A template with fewer of them gets fewer; one whose slot will not take the
value is a refusal. Answers the fact.

**Returns:** fact, or `FALSE` if the call is refused.

### git-commit-to-instance

```clips
(git-commit-to-instance ?commit ?defclass [?instance-name])
```

The same into an instance of the named defclass, with an optional instance
name.

**Arguments:** 2 to 3.

**Returns:** instance, or `FALSE` if the call is refused.

### git-commit-free

```clips
(git-commit-free ?commit)
```

Gives a commit back.

**Returns:** boolean.


## Walking history

### git-revwalk-new

```clips
(git-revwalk-new ?repo)
```

A revision walker for the repository: a cursor over commits, which
`git-revwalk-push` and its relatives tell where to start and
`git-revwalk-next` moves. Freed with `git-revwalk-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-revwalk-push

```clips
(git-revwalk-push ?walk ?id)
```

Starts the walk at a commit, by id. More than one can be pushed.

**Returns:** boolean.

### git-revwalk-push-head

```clips
(git-revwalk-push-head ?walk)
```

Starts the walk at HEAD.

**Returns:** boolean.

### git-revwalk-push-ref

```clips
(git-revwalk-push-ref ?walk ?refname)
```

Starts the walk at what a reference points to, by name.

**Returns:** boolean.

### git-revwalk-push-glob

```clips
(git-revwalk-push-glob ?walk ?glob)
```

Starts the walk at every reference matching a glob: `"refs/heads/*"`.

**Returns:** boolean.

### git-revwalk-push-range

```clips
(git-revwalk-push-range ?walk ?range)
```

Starts the walk over a range: `"a..b"` walks what is reachable from b and
not from a.

**Returns:** boolean.

### git-revwalk-hide

```clips
(git-revwalk-hide ?walk ?id)
```

Hides a commit and everything reachable from it.

**Returns:** boolean.

### git-revwalk-hide-head

```clips
(git-revwalk-hide-head ?walk)
```

Hides HEAD and its ancestors.

**Returns:** boolean.

### git-revwalk-hide-ref

```clips
(git-revwalk-hide-ref ?walk ?refname)
```

Hides what a reference points to, by name.

**Returns:** boolean.

### git-revwalk-hide-glob

```clips
(git-revwalk-hide-glob ?walk ?glob)
```

Hides every reference matching a glob.

**Returns:** boolean.

### git-revwalk-sorting

```clips
(git-revwalk-sorting ?walk ?flags)
```

Sets the order: `GIT_SORT_NONE` for git's own, `GIT_SORT_TOPOLOGICAL` to
show no parent before its children, `GIT_SORT_TIME` by commit time, and
`GIT_SORT_REVERSE` to turn whichever of those around. A multifield combines
them. Set this before pushing.

**Returns:** boolean.

### git-revwalk-simplify-first-parent

```clips
(git-revwalk-simplify-first-parent ?walk)
```

Follows only first parents, so a merge contributes one line of history.

**Returns:** boolean.

### git-revwalk-next

```clips
(git-revwalk-next ?walk)
```

The id of the next commit, or `FALSE` -- without a message -- when the
walk is over.

**Returns:** string, or `FALSE` if the call is refused.

### git-revwalk-reset

```clips
(git-revwalk-reset ?walk)
```

Empties the walk to be set up again.

**Returns:** boolean.

### git-revwalk-free

```clips
(git-revwalk-free ?walk)
```

Gives the walk back.

**Returns:** boolean.


## Trees

### git-tree-lookup

```clips
(git-tree-lookup ?repo ?id)
```

Looks a tree up by id.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tree-lookup-prefix

```clips
(git-tree-lookup-prefix ?repo ?prefix)
```

The same by a prefix of the id.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tree-id

```clips
(git-tree-id ?tree)
```

The id.

**Returns:** string, or `FALSE` if the call is refused.

### git-tree-entrycount

```clips
(git-tree-entrycount ?tree)
```

How many entries the tree has, at this level.

**Returns:** integer, or `FALSE` if the call is refused.

### git-tree-entry-byindex

```clips
(git-tree-entry-byindex ?tree ?n)
```

Entry number n, counted from zero, in the tree's order. The entry is
borrowed from the tree and goes with it; `git-tree-entry-free` refuses it.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tree-entry-byname

```clips
(git-tree-entry-byname ?tree ?name)
```

The entry with a name, at this level. Borrowed, likewise.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tree-entry-byid

```clips
(git-tree-entry-byid ?tree ?id)
```

The entry whose object has an id, at this level. Borrowed, likewise.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tree-entry-bypath

```clips
(git-tree-entry-bypath ?tree ?path)
```

The entry at a path, descending into subtrees: `"dir/file"`. This one is
the caller's, and `git-tree-entry-free` gives it back.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tree-walk

```clips
(git-tree-walk ?tree [?mode])
```

Every path in the tree, recursively, as a multifield of strings, subtrees
with a trailing slash. `GIT_TREEWALK_PRE` (the default) lists a subtree
before what is in it and `GIT_TREEWALK_POST` after.

**Arguments:** 1 to 2.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-tree-free

```clips
(git-tree-free ?tree)
```

Gives a tree back, and the entries borrowed from it.

**Returns:** boolean.

### git-tree-entry-name

```clips
(git-tree-entry-name ?entry)
```

The entry's name.

**Returns:** string, or `FALSE` if the call is refused.

### git-tree-entry-id

```clips
(git-tree-entry-id ?entry)
```

The id of the object the entry points at.

**Returns:** string, or `FALSE` if the call is refused.

### git-tree-entry-type

```clips
(git-tree-entry-type ?entry)
```

What kind of object that is: `GIT_OBJECT_BLOB`, `GIT_OBJECT_TREE`, or
`GIT_OBJECT_COMMIT` for a submodule.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-tree-entry-filemode

```clips
(git-tree-entry-filemode ?entry)
```

The entry's mode as a `GIT_FILEMODE_*` symbol, normalised the way git
does: `GIT_FILEMODE_BLOB`, `GIT_FILEMODE_BLOB_EXECUTABLE`,
`GIT_FILEMODE_TREE`, `GIT_FILEMODE_LINK`, `GIT_FILEMODE_COMMIT`.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-tree-entry-filemode-raw

```clips
(git-tree-entry-filemode-raw ?entry)
```

The mode as stored, which can differ from the normalised one in an old
repository.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-tree-entry-to-object

```clips
(git-tree-entry-to-object ?repo ?entry)
```

The object an entry points at, looked up in the repository.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tree-entry-cmp

```clips
(git-tree-entry-cmp ?entry-a ?entry-b)
```

Orders two entries as git does: negative, zero or positive.

**Returns:** integer, or `FALSE` if the call is refused.

### git-tree-entry-dup

```clips
(git-tree-entry-dup ?entry)
```

A copy of an entry, which is the caller's to free -- how a borrowed one is
kept past its tree.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tree-entry-free

```clips
(git-tree-entry-free ?entry)
```

Gives back an entry from `git-tree-entry-bypath` or `git-tree-entry-dup`.
A borrowed entry is refused.

**Returns:** boolean.

### git-treebuilder-new

```clips
(git-treebuilder-new ?repo [?source])
```

A builder for a tree, starting from the entries of the tree given or from
nothing. Entries are put in with `git-treebuilder-insert`, and
`git-treebuilder-write` writes the result. Freed with
`git-treebuilder-free`.

**Arguments:** 1 to 2.

**Returns:** handle, or `FALSE` if the call is refused.

### git-treebuilder-insert

```clips
(git-treebuilder-insert ?builder ?name ?id ?filemode)
```

Puts an entry in, replacing one of the same name: a name, the id of a blob
or tree already in the repository, and a `GIT_FILEMODE_*` symbol. Answers
the entry, borrowed from the builder.

**Returns:** handle, or `FALSE` if the call is refused.

### git-treebuilder-get

```clips
(git-treebuilder-get ?builder ?name)
```

The entry with a name, borrowed from the builder. A refusal when there is
none.

**Returns:** handle, or `FALSE` if the call is refused.

### git-treebuilder-remove

```clips
(git-treebuilder-remove ?builder ?name)
```

Takes the entry with a name out.

**Returns:** boolean.

### git-treebuilder-clear

```clips
(git-treebuilder-clear ?builder)
```

Takes every entry out.

**Returns:** boolean.

### git-treebuilder-entrycount

```clips
(git-treebuilder-entrycount ?builder)
```

How many entries the builder has.

**Returns:** integer, or `FALSE` if the call is refused.

### git-treebuilder-write

```clips
(git-treebuilder-write ?builder)
```

Writes the tree to the repository and answers its id.

**Returns:** string, or `FALSE` if the call is refused.

### git-treebuilder-free

```clips
(git-treebuilder-free ?builder)
```

Gives the builder back, and the entries borrowed from it.

**Returns:** boolean.


## Blobs

### git-blob-lookup

```clips
(git-blob-lookup ?repo ?id)
```

Looks a blob up by id.

**Returns:** handle, or `FALSE` if the call is refused.

### git-blob-lookup-prefix

```clips
(git-blob-lookup-prefix ?repo ?prefix)
```

The same by a prefix of the id.

**Returns:** handle, or `FALSE` if the call is refused.

### git-blob-id

```clips
(git-blob-id ?blob)
```

The id.

**Returns:** string, or `FALSE` if the call is refused.

### git-blob-rawsize

```clips
(git-blob-rawsize ?blob)
```

The size of the content in bytes.

**Returns:** integer, or `FALSE` if the call is refused.

### git-blob-rawcontent

```clips
(git-blob-rawcontent ?blob)
```

The content as a string. A CLIPS string ends at a NUL byte, so a binary
blob comes back cut at its first one; `git-blob-is-binary` says whether
that is a concern.

**Returns:** string, or `FALSE` if the call is refused.

### git-blob-is-binary

```clips
(git-blob-is-binary ?blob)
```

Whether the content looks binary to git: a NUL in the first eight
kilobytes.

**Returns:** boolean.

### git-blob-filter

```clips
(git-blob-filter ?blob ?as-path)
```

The content with the repository's filters applied as they would be on
checkout to the given path -- line endings, `.gitattributes`.

**Returns:** string, or `FALSE` if the call is refused.

### git-blob-create-from-buffer

```clips
(git-blob-create-from-buffer ?repo ?content)
```

Writes a string to the repository as a blob and answers its id.

**Returns:** string, or `FALSE` if the call is refused.

### git-blob-create-from-workdir

```clips
(git-blob-create-from-workdir ?repo ?relative-path)
```

Writes a file from the working directory, by relative path, applying the
filters an add would.

**Returns:** string, or `FALSE` if the call is refused.

### git-blob-create-from-disk

```clips
(git-blob-create-from-disk ?repo ?path)
```

Writes a file from anywhere, by path, applying no filters.

**Returns:** string, or `FALSE` if the call is refused.

### git-blob-free

```clips
(git-blob-free ?blob)
```

Gives a blob back.

**Returns:** boolean.


## Tags

### git-tag-lookup

```clips
(git-tag-lookup ?repo ?id)
```

Looks an annotated tag up by id -- the tag object, not the commit it tags.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tag-lookup-prefix

```clips
(git-tag-lookup-prefix ?repo ?prefix)
```

The same by a prefix of the id.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tag-create

```clips
(git-tag-create ?repo ?name ?target ?tagger ?message [?force])
```

Writes an annotated tag: a name, the object it tags, a tagger signature or
`nil` for the default, a message, and optionally `TRUE` to replace a tag
of that name. Answers the tag object's id and creates `refs/tags/<name>`.

**Arguments:** 5 to 6.

**Returns:** string, or `FALSE` if the call is refused.

### git-tag-create-lightweight

```clips
(git-tag-create-lightweight ?repo ?name ?target [?force])
```

Creates `refs/tags/<name>` pointing straight at an object, with no tag
object, and answers the object's id.

**Arguments:** 3 to 4.

**Returns:** string, or `FALSE` if the call is refused.

### git-tag-annotation-create

```clips
(git-tag-annotation-create ?repo ?name ?target ?tagger ?message)
```

Writes a tag object like `git-tag-create` but creates no reference for it.

**Returns:** string, or `FALSE` if the call is refused.

### git-tag-delete

```clips
(git-tag-delete ?repo ?name)
```

Removes `refs/tags/<name>`.

**Returns:** boolean.

### git-tag-list

```clips
(git-tag-list ?repo [?pattern])
```

The names of every tag, as a multifield, or of those matching a glob when
one is given.

**Arguments:** 1 to 2.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-tag-id

```clips
(git-tag-id ?tag)
```

The tag object's id.

**Returns:** string, or `FALSE` if the call is refused.

### git-tag-name

```clips
(git-tag-name ?tag)
```

The tag's name.

**Returns:** string, or `FALSE` if the call is refused.

### git-tag-message

```clips
(git-tag-message ?tag)
```

The tag's message.

**Returns:** string, or `FALSE` if the call is refused.

### git-tag-tagger

```clips
(git-tag-tagger ?tag)
```

The tagger, as a signature borrowed from the tag.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tag-target

```clips
(git-tag-target ?tag)
```

The object the tag points at, as a handle of whatever kind it is.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tag-target-id

```clips
(git-tag-target-id ?tag)
```

The id of that object.

**Returns:** string, or `FALSE` if the call is refused.

### git-tag-target-type

```clips
(git-tag-target-type ?tag)
```

The kind of that object, as a `GIT_OBJECT_*` symbol.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-tag-peel

```clips
(git-tag-peel ?tag)
```

The object the tag points at, following tags of tags to the end.

**Returns:** handle, or `FALSE` if the call is refused.

### git-tag-free

```clips
(git-tag-free ?tag)
```

Gives a tag back.

**Returns:** boolean.


## References

### git-reference-lookup

```clips
(git-reference-lookup ?repo ?name)
```

Looks a reference up by its full name: `"refs/heads/main"`, `"HEAD"`.
Freed with `git-reference-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-dwim

```clips
(git-reference-dwim ?repo ?shorthand)
```

Looks a reference up by a short name, trying what git would: `"main"`
finds `refs/heads/main`, `"v1"` finds `refs/tags/v1`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-name-to-id

```clips
(git-reference-name-to-id ?repo ?name)
```

The id a reference resolves to, by full name, following symbolic
references to the end.

**Returns:** string, or `FALSE` if the call is refused.

### git-reference-create

```clips
(git-reference-create ?repo ?name ?id [?force] [?log-message])
```

Creates a direct reference pointing at an id. `TRUE` for the fourth
argument replaces an existing one, which is otherwise a refusal; the fifth
is the reflog message.

**Arguments:** 3 to 5.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-symbolic-create

```clips
(git-reference-symbolic-create ?repo ?name ?target [?force] [?log-message])
```

Creates a symbolic reference pointing at another by name, likewise.

**Arguments:** 3 to 5.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-list

```clips
(git-reference-list ?repo)
```

The full names of every reference, as a multifield.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-reference-list-glob

```clips
(git-reference-list-glob ?repo ?glob)
```

The full names of the references matching a glob: `"refs/tags/*"`.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-reference-remove

```clips
(git-reference-remove ?repo ?name)
```

Deletes a reference by name.

**Returns:** boolean.

### git-reference-has-log

```clips
(git-reference-has-log ?repo ?name)
```

Whether a reference has a reflog.

**Returns:** boolean.

### git-reference-is-valid-name

```clips
(git-reference-is-valid-name ?name)
```

Whether a string is a well-formed full reference name.

**Returns:** boolean.

### git-reference-name

```clips
(git-reference-name ?ref)
```

The full name.

**Returns:** string, or `FALSE` if the call is refused.

### git-reference-shorthand

```clips
(git-reference-shorthand ?ref)
```

The name as git would print it: `main` for `refs/heads/main`.

**Returns:** string, or `FALSE` if the call is refused.

### git-reference-type

```clips
(git-reference-type ?ref)
```

`GIT_REFERENCE_DIRECT` for one pointing at an id, `GIT_REFERENCE_SYMBOLIC`
for one pointing at another reference.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-reference-target

```clips
(git-reference-target ?ref)
```

The id a direct reference points at, or `nil` for a symbolic one.

**Returns:** string, or `FALSE` if the call is refused.

### git-reference-target-peel

```clips
(git-reference-target-peel ?ref)
```

The id of the object a packed tag reference has been peeled to, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-reference-symbolic-target

```clips
(git-reference-symbolic-target ?ref)
```

The name a symbolic reference points at, or `nil` for a direct one.

**Returns:** string, or `FALSE` if the call is refused.

### git-reference-resolve

```clips
(git-reference-resolve ?ref)
```

The direct reference at the end of a chain of symbolic ones.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-peel

```clips
(git-reference-peel ?ref [?type])
```

The object a reference points at, peeled to the named type, or to the end
with `GIT_OBJECT_ANY` (the default).

**Arguments:** 1 to 2.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-set-target

```clips
(git-reference-set-target ?ref ?id [?log-message])
```

Moves a direct reference to an id and answers the moved reference as a new
handle; the old one is unchanged and should be freed. A reference that has
been changed since it was read is refused, which is libgit2 guarding
against a race.

**Arguments:** 2 to 3.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-symbolic-set-target

```clips
(git-reference-symbolic-set-target ?ref ?target [?log-message])
```

Moves a symbolic reference to point at another name, likewise.

**Arguments:** 2 to 3.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-rename

```clips
(git-reference-rename ?ref ?new-name [?force] [?log-message])
```

Renames a reference and answers the renamed one as a new handle. `TRUE`
for the third argument replaces a reference of the new name.

**Arguments:** 2 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-delete

```clips
(git-reference-delete ?ref)
```

Deletes the reference. The handle is still to be freed.

**Returns:** boolean.

### git-reference-is-branch

```clips
(git-reference-is-branch ?ref)
```

Whether the reference is under `refs/heads/`.

**Returns:** boolean.

### git-reference-is-remote

```clips
(git-reference-is-remote ?ref)
```

Whether it is under `refs/remotes/`.

**Returns:** boolean.

### git-reference-is-tag

```clips
(git-reference-is-tag ?ref)
```

Whether it is under `refs/tags/`.

**Returns:** boolean.

### git-reference-is-note

```clips
(git-reference-is-note ?ref)
```

Whether it is under `refs/notes/`.

**Returns:** boolean.

### git-reference-cmp

```clips
(git-reference-cmp ?ref-a ?ref-b)
```

Orders two references: zero when they are the same.

**Returns:** integer, or `FALSE` if the call is refused.

### git-reference-dup

```clips
(git-reference-dup ?ref)
```

A copy of a reference handle.

**Returns:** handle, or `FALSE` if the call is refused.

### git-reference-free

```clips
(git-reference-free ?ref)
```

Gives a reference back.

**Returns:** boolean.

### git-reflog-read

```clips
(git-reflog-read ?repo ?name)
```

The reflog of a reference, by name, as a flat multifield of four elements
per entry, newest first: the old id, the new id, the committer's name, and
the message. A CLIPS multifield cannot hold another, so the entries run on
from one another; `(length$)` divided by four is how many there are. An
empty multifield for a reference with no log.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-reflog-delete

```clips
(git-reflog-delete ?repo ?name)
```

Removes the reflog of a reference.

**Returns:** boolean.


## Branches

### git-branch-create

```clips
(git-branch-create ?repo ?name ?target [?force])
```

Creates a branch pointing at a commit and answers its reference. `TRUE`
for the fourth argument replaces a branch of that name.

**Arguments:** 3 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-branch-create-from-annotated

```clips
(git-branch-create-from-annotated ?repo ?name ?target [?force])
```

The same from an annotated commit, which records where it came from in the
reflog.

**Arguments:** 3 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-branch-lookup

```clips
(git-branch-lookup ?repo ?name [?type])
```

Finds a branch by its short name: `"main"`, or `"origin/main"` with
`GIT_BRANCH_REMOTE`. `GIT_BRANCH_LOCAL` is the default.

**Arguments:** 2 to 3.

**Returns:** handle, or `FALSE` if the call is refused.

### git-branch-list

```clips
(git-branch-list ?repo [?type])
```

The short names of every branch, as a multifield: local ones with
`GIT_BRANCH_LOCAL`, remote-tracking ones with `GIT_BRANCH_REMOTE`, both
with `GIT_BRANCH_ALL` (the default).

**Arguments:** 1 to 2.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-branch-delete

```clips
(git-branch-delete ?ref)
```

Deletes a branch. The branch HEAD is on is refused. The handle is still to
be freed.

**Returns:** boolean.

### git-branch-move

```clips
(git-branch-move ?ref ?new-name [?force])
```

Renames a branch and answers the renamed one as a new handle. `TRUE` for
the third argument replaces a branch of the new name.

**Arguments:** 2 to 3.

**Returns:** handle, or `FALSE` if the call is refused.

### git-branch-name

```clips
(git-branch-name ?ref)
```

The short name of a branch reference.

**Returns:** string, or `FALSE` if the call is refused.

### git-branch-is-head

```clips
(git-branch-is-head ?ref)
```

Whether HEAD is on this branch.

**Returns:** boolean.

### git-branch-is-checked-out

```clips
(git-branch-is-checked-out ?ref)
```

Whether this branch is checked out in this repository or any of its
worktrees.

**Returns:** boolean.

### git-branch-upstream

```clips
(git-branch-upstream ?ref)
```

The branch this one tracks, as a reference, or `nil` when none is
configured.

**Returns:** handle, or `FALSE` if the call is refused.

### git-branch-set-upstream

```clips
(git-branch-set-upstream ?ref ?upstream)
```

Sets the branch this one tracks, by short name: `"origin/main"`, or a
local branch. `nil` unsets it.

**Returns:** boolean.

### git-branch-upstream-name

```clips
(git-branch-upstream-name ?repo ?refname)
```

The full name of the upstream of the branch named, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-branch-upstream-remote

```clips
(git-branch-upstream-remote ?repo ?refname)
```

The name of the remote the upstream of the branch named is on, or `"."`
for a local upstream, or `nil` when there is none.

**Returns:** string, or `FALSE` if the call is refused.

### git-branch-upstream-merge

```clips
(git-branch-upstream-merge ?repo ?refname)
```

The `branch.<name>.merge` value of the branch named, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-branch-remote-name

```clips
(git-branch-remote-name ?repo ?refname)
```

The name of the remote a remote-tracking branch belongs to, by the
branch's full name. A local branch is a refusal.

**Returns:** string, or `FALSE` if the call is refused.

### git-branch-is-valid-name

```clips
(git-branch-is-valid-name ?name)
```

Whether a string is a well-formed branch name.

**Returns:** boolean.


## The index

### git-index-open

```clips
(git-index-open ?path)
```

Opens an index file by path, standing on its own: no repository behind it.
Freed with `git-index-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-index-new

```clips
(git-index-new)
```

An empty index in memory, with no file and no repository.

**Returns:** handle, or `FALSE` if the call is refused.

### git-index-owner

```clips
(git-index-owner ?index)
```

The repository the index belongs to, or `nil` for one that stands alone.

**Returns:** handle, or `FALSE` if the call is refused.

### git-index-path

```clips
(git-index-path ?index)
```

The path of the index file, or `nil` for one in memory.

**Returns:** string, or `FALSE` if the call is refused.

### git-index-read

```clips
(git-index-read ?index)
```

Reads the index file again, discarding changes made in memory.

**Returns:** boolean.

### git-index-write

```clips
(git-index-write ?index)
```

Writes the index to its file.

**Returns:** boolean.

### git-index-read-tree

```clips
(git-index-read-tree ?index ?tree)
```

Replaces the index's contents with a tree's.

**Returns:** boolean.

### git-index-write-tree

```clips
(git-index-write-tree ?index)
```

Writes the index as a tree to its repository and answers the tree's id. The
index must have no conflicts.

**Returns:** string, or `FALSE` if the call is refused.

### git-index-write-tree-to

```clips
(git-index-write-tree-to ?index ?repo)
```

The same, to a repository named explicitly, which is how an index that
stands alone is written.

**Returns:** string, or `FALSE` if the call is refused.

### git-index-entrycount

```clips
(git-index-entrycount ?index)
```

How many entries the index has.

**Returns:** integer, or `FALSE` if the call is refused.

### git-index-clear

```clips
(git-index-clear ?index)
```

Removes every entry.

**Returns:** boolean.

### git-index-version

```clips
(git-index-version ?index)
```

The index file format version, 2 to 4.

**Returns:** integer, or `FALSE` if the call is refused.

### git-index-set-version

```clips
(git-index-set-version ?index ?version)
```

Sets it.

**Returns:** boolean.

### git-index-checksum

```clips
(git-index-checksum ?index)
```

The checksum of the index as last read or written, as an id, or `nil`
before either.

**Returns:** string, or `FALSE` if the call is refused.

### git-index-add-bypath

```clips
(git-index-add-bypath ?index ?path)
```

Stages a file from the working directory, by relative path. A file that
is not there is a refusal; `git-index-remove-bypath` is for that.

**Returns:** boolean.

### git-index-add-all

```clips
(git-index-add-all ?index ?pathspec [?flags])
```

Stages every file matching a pathspec -- a multifield of patterns,
`(create$ "*")` for everything -- adding new files and updating changed
ones. The flags are `GIT_INDEX_ADD_*` symbols: `GIT_INDEX_ADD_FORCE` adds
ignored files too.

**Arguments:** 2 to 3.

**Returns:** boolean.

### git-index-remove

```clips
(git-index-remove ?index ?path ?stage)
```

Removes the entry for a path at a stage: 0 for an ordinary entry, 1 to 3
for the sides of a conflict.

**Returns:** boolean.

### git-index-remove-bypath

```clips
(git-index-remove-bypath ?index ?path)
```

Removes the entry for a path, whatever its stage, and any conflict
recorded for it.

**Returns:** boolean.

### git-index-remove-directory

```clips
(git-index-remove-directory ?index ?dir ?stage)
```

Removes every entry under a directory, at a stage.

**Returns:** boolean.

### git-index-remove-all

```clips
(git-index-remove-all ?index ?pathspec)
```

Removes every entry matching a pathspec.

**Returns:** boolean.

### git-index-update-all

```clips
(git-index-update-all ?index ?pathspec)
```

Updates every entry matching a pathspec to what is in the working
directory, removing entries whose files are gone. New files are not added.

**Returns:** boolean.

### git-index-find

```clips
(git-index-find ?index ?path)
```

The position of the entry for a path, or `FALSE` -- without a message --
when there is none.

**Returns:** integer, or `FALSE` if the call is refused.

### git-index-get-byindex

```clips
(git-index-get-byindex ?index ?n)
```

The entry at a position, as a multifield: the path, the blob id, the mode
as a `GIT_FILEMODE_*` symbol, the file size, and the stage. A refusal past
the end.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-index-get-bypath

```clips
(git-index-get-bypath ?index ?path [?stage])
```

The entry for a path, at stage 0 unless another is given, as the same
multifield.

**Arguments:** 2 to 3.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-index-entry-to-fact

```clips
(git-index-entry-to-fact ?index ?n ?deftemplate)
```

Not a libgit2 function. Asserts the entry at a position as a fact of the
named deftemplate, filling whichever of the slots `path`, `id`, `mode`,
`size` and `stage` the template has, and answers the fact. `FALSE` --
without a message -- past the end, which is what makes it a loop
condition.

**Returns:** fact, or `FALSE` if the call is refused.

### git-index-has-conflicts

```clips
(git-index-has-conflicts ?index)
```

Whether any entry is at a conflict stage.

**Returns:** boolean.

### git-index-conflict-remove

```clips
(git-index-conflict-remove ?index ?path)
```

Removes the conflict entries for a path.

**Returns:** boolean.

### git-index-conflict-cleanup

```clips
(git-index-conflict-cleanup ?index)
```

Removes every conflict entry.

**Returns:** boolean.

### git-index-free

```clips
(git-index-free ?index)
```

Gives the index back.

**Returns:** boolean.


## Configuration

### git-config-open-default

```clips
(git-config-open-default)
```

The configuration a program outside any repository sees: the global,
XDG, system and programdata files, in that order of precedence. Freed
with `git-config-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-config-new

```clips
(git-config-new)
```

An empty configuration, to add files to with `git-config-add-file-ondisk`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-config-open-ondisk

```clips
(git-config-open-ondisk ?path)
```

A configuration with one file in it, by path. The file need not exist yet;
a write creates it.

**Returns:** handle, or `FALSE` if the call is refused.

### git-config-open-level

```clips
(git-config-open-level ?config ?level)
```

One level of a configuration, as a configuration of its own: the
`GIT_CONFIG_LEVEL_*` symbol names it, and `GIT_CONFIG_LEVEL_LOCAL` is the
repository's own file. A level the configuration does not have is a
refusal.

**Returns:** handle, or `FALSE` if the call is refused.

### git-config-add-file-ondisk

```clips
(git-config-add-file-ondisk ?config ?path ?level [?force] [?repo])
```

Adds a file to a configuration at a level. `TRUE` for the fourth argument
replaces a file already at that level; the fifth is the repository the
file belongs to, or `nil`.

**Arguments:** 3 to 5.

**Returns:** boolean.

### git-config-snapshot

```clips
(git-config-snapshot ?config)
```

The configuration frozen at this moment, faster to read many values from
and refusing to be written.

**Returns:** handle, or `FALSE` if the call is refused.

### git-config-find-global

```clips
(git-config-find-global)
```

The path of the user's `.gitconfig`, or `nil` when there is none.

**Returns:** string, or `FALSE` if the call is refused.

### git-config-find-xdg

```clips
(git-config-find-xdg)
```

The path of the XDG configuration file, `~/.config/git/config`, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-config-find-system

```clips
(git-config-find-system)
```

The path of the system configuration file, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-config-find-programdata

```clips
(git-config-find-programdata)
```

The path of the ProgramData configuration file on Windows, or `nil`
elsewhere.

**Returns:** string, or `FALSE` if the call is refused.

### git-config-get-string

```clips
(git-config-get-string ?config ?name)
```

A value as a string, or `nil` when it is not set.

**Returns:** string, or `FALSE` if the call is refused.

### git-config-get-path

```clips
(git-config-get-path ?config ?name)
```

A value as a path, with a leading `~` expanded, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-config-get-int

```clips
(git-config-get-int ?config ?name)
```

A value as an integer, understanding git's `k`, `m` and `g` suffixes, or
`nil` when it is not set. A value that is set and is not a number is a
refusal.

**Returns:** integer, or `FALSE` if the call is refused.

### git-config-get-bool

```clips
(git-config-get-bool ?config ?name)
```

A value as `TRUE` or `FALSE`, understanding `yes`, `on`, `1` and their
opposites, or `nil` when it is not set.

**Returns:** boolean, or `FALSE` if the call is refused.

### git-config-get-multivar

```clips
(git-config-get-multivar ?config ?name [?regexp])
```

Every value of a name that can have several, as a multifield of strings,
narrowed to those matching a regular expression when one is given. Empty
when there are none.

**Arguments:** 2 to 3.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-config-entries

```clips
(git-config-entries ?config [?regexp])
```

Every entry, as a multifield of alternating names and values, narrowed to
the names matching a regular expression when one is given.

**Arguments:** 1 to 2.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-config-set-string

```clips
(git-config-set-string ?config ?name ?value)
```

Sets a value. In a repository's configuration this writes the repository's
own file.

**Returns:** boolean.

### git-config-set-int

```clips
(git-config-set-int ?config ?name ?value)
```

Sets an integer value.

**Returns:** boolean.

### git-config-set-bool

```clips
(git-config-set-bool ?config ?name ?value)
```

Sets a boolean value.

**Returns:** boolean.

### git-config-set-multivar

```clips
(git-config-set-multivar ?config ?name ?regexp ?value)
```

Sets the values of a name matching a regular expression to a value. The
expression `"^$"` matches nothing, which adds a value.

**Returns:** boolean.

### git-config-delete-entry

```clips
(git-config-delete-entry ?config ?name)
```

Removes a value. One that is not set is a refusal.

**Returns:** boolean.

### git-config-delete-multivar

```clips
(git-config-delete-multivar ?config ?name ?regexp)
```

Removes the values of a name matching a regular expression.

**Returns:** boolean.

### git-config-parse-bool

```clips
(git-config-parse-bool ?text)
```

Reads a string the way `git-config-get-bool` would: `"on"` is `TRUE`.

**Returns:** boolean.

### git-config-parse-int

```clips
(git-config-parse-int ?text)
```

Reads a string the way `git-config-get-int` would: `"2k"` is 2048.

**Returns:** integer, or `FALSE` if the call is refused.

### git-config-parse-path

```clips
(git-config-parse-path ?text)
```

Reads a string the way `git-config-get-path` would, expanding `~`.

**Returns:** string, or `FALSE` if the call is refused.

### git-config-free

```clips
(git-config-free ?config)
```

Gives a configuration back.

**Returns:** boolean.


## Remotes

### git-remote-create

```clips
(git-remote-create ?repo ?name ?url [?fetch-refspec])
```

Adds a remote to the repository's configuration with a name and a URL, and
answers it. The fourth argument is a fetch refspec to use instead of the
default `+refs/heads/*:refs/remotes/<name>/*`. Freed with
`git-remote-free`.

**Arguments:** 3 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-remote-create-anonymous

```clips
(git-remote-create-anonymous ?repo ?url)
```

A remote with a URL and no name, which is not written to the configuration.

**Returns:** handle, or `FALSE` if the call is refused.

### git-remote-create-detached

```clips
(git-remote-create-detached ?url)
```

A remote with a URL and no repository, for `git-remote-ls`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-remote-lookup

```clips
(git-remote-lookup ?repo ?name)
```

The remote with a name.

**Returns:** handle, or `FALSE` if the call is refused.

### git-remote-list

```clips
(git-remote-list ?repo)
```

The names of every remote, as a multifield.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-remote-delete

```clips
(git-remote-delete ?repo ?name)
```

Removes a remote and its remote-tracking branches, by name.

**Returns:** boolean.

### git-remote-rename

```clips
(git-remote-rename ?repo ?name ?new-name)
```

Renames a remote, and its remote-tracking branches with it. Answers the
refspecs that could not be renamed, as a multifield -- empty when all of
them were.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-remote-set-url

```clips
(git-remote-set-url ?repo ?name ?url)
```

Sets the URL of the remote named.

**Returns:** boolean.

### git-remote-set-pushurl

```clips
(git-remote-set-pushurl ?repo ?name ?url)
```

Sets the URL the remote named is pushed to, when that differs.

**Returns:** boolean.

### git-remote-add-fetch

```clips
(git-remote-add-fetch ?repo ?name ?refspec)
```

Adds a fetch refspec to the remote named.

**Returns:** boolean.

### git-remote-add-push

```clips
(git-remote-add-push ?repo ?name ?refspec)
```

Adds a push refspec to the remote named.

**Returns:** boolean.

### git-remote-is-valid-name

```clips
(git-remote-is-valid-name ?name)
```

Whether a string is a well-formed remote name.

**Returns:** boolean.

### git-remote-name

```clips
(git-remote-name ?remote)
```

The name, or `nil` for an anonymous remote.

**Returns:** string, or `FALSE` if the call is refused.

### git-remote-url

```clips
(git-remote-url ?remote)
```

The URL.

**Returns:** string, or `FALSE` if the call is refused.

### git-remote-pushurl

```clips
(git-remote-pushurl ?remote)
```

The push URL, or `nil` when it is the same as the URL.

**Returns:** string, or `FALSE` if the call is refused.

### git-remote-refspec-count

```clips
(git-remote-refspec-count ?remote)
```

How many refspecs the remote has, fetch and push together.

**Returns:** integer, or `FALSE` if the call is refused.

### git-remote-get-refspec

```clips
(git-remote-get-refspec ?remote ?n)
```

Refspec number n, borrowed from the remote.

**Returns:** handle, or `FALSE` if the call is refused.

### git-remote-get-fetch-refspecs

```clips
(git-remote-get-fetch-refspecs ?remote)
```

The fetch refspecs, as a multifield of strings.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-remote-get-push-refspecs

```clips
(git-remote-get-push-refspecs ?remote)
```

The push refspecs, likewise.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-remote-autotag

```clips
(git-remote-autotag ?remote)
```

How the remote fetches tags, as a `GIT_REMOTE_DOWNLOAD_TAGS_*` symbol.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-remote-prune-refs

```clips
(git-remote-prune-refs ?remote)
```

Whether the remote is configured to prune remote-tracking branches on
fetch.

**Returns:** boolean.

### git-remote-connect

```clips
(git-remote-connect ?remote [?direction])
```

Connects to the remote, for fetching by default or for pushing with
`GIT_DIRECTION_PUSH`. The functions that need a connection make one
themselves; this is for asking questions of a remote before fetching. A
remote that needs a credential gets one as described under Credentials.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-remote-connected

```clips
(git-remote-connected ?remote)
```

Whether the remote is connected.

**Returns:** boolean.

### git-remote-disconnect

```clips
(git-remote-disconnect ?remote)
```

Closes the connection.

**Returns:** boolean.

### git-remote-stop

```clips
(git-remote-stop ?remote)
```

Asks a fetch or push in progress to stop.

**Returns:** boolean.

### git-remote-ls

```clips
(git-remote-ls ?remote)
```

What the remote advertises, as a multifield of alternating reference names
and ids. Connects if it has to, with a credential if the remote needs one.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-remote-fetch

```clips
(git-remote-fetch ?remote [?refspecs] [?reflog-message])
```

Fetches from the remote, into remote-tracking branches. The optional
refspecs are a multifield of strings, and replace the configured ones for
this fetch; the optional message is written to the reflog. A remote that
needs a credential gets one as described under Credentials.

**Arguments:** 1 to 3.

**Returns:** boolean.

### git-remote-push

```clips
(git-remote-push ?remote [?refspecs])
```

Pushes to the remote. The optional refspecs are a multifield of strings,
`"refs/heads/main:refs/heads/main"`, and replace the configured ones for
this push. Over a path, libgit2 pushes only into a bare repository. A
remote that needs a credential gets one as described under Credentials.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-remote-prune

```clips
(git-remote-prune ?remote)
```

Removes remote-tracking branches whose branches are gone from the remote.

**Returns:** boolean.

### git-remote-update-tips

```clips
(git-remote-update-tips ?remote [?reflog-message])
```

Updates the remote-tracking branches from what the last fetch downloaded,
which `git-remote-fetch` already does; for a program that fetched some
other way.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-remote-dup

```clips
(git-remote-dup ?remote)
```

A copy of a remote handle.

**Returns:** handle, or `FALSE` if the call is refused.

### git-remote-free

```clips
(git-remote-free ?remote)
```

Gives a remote back, and the refspecs borrowed from it.

**Returns:** boolean.

### git-refspec-parse

```clips
(git-refspec-parse ?text ?is-fetch)
```

Parses a refspec on its own -- `"+refs/heads/*:refs/remotes/origin/*"` --
as a fetch refspec with `TRUE` or a push one with `FALSE`. Freed with
`git-refspec-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-refspec-src

```clips
(git-refspec-src ?refspec)
```

The source side.

**Returns:** string, or `FALSE` if the call is refused.

### git-refspec-dst

```clips
(git-refspec-dst ?refspec)
```

The destination side.

**Returns:** string, or `FALSE` if the call is refused.

### git-refspec-string

```clips
(git-refspec-string ?refspec)
```

The whole refspec as written.

**Returns:** string, or `FALSE` if the call is refused.

### git-refspec-force

```clips
(git-refspec-force ?refspec)
```

Whether it starts with `+`.

**Returns:** boolean.

### git-refspec-direction

```clips
(git-refspec-direction ?refspec)
```

`GIT_DIRECTION_FETCH` or `GIT_DIRECTION_PUSH`.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-refspec-src-matches

```clips
(git-refspec-src-matches ?refspec ?refname)
```

Whether a reference name matches the source side.

**Returns:** boolean.

### git-refspec-dst-matches

```clips
(git-refspec-dst-matches ?refspec ?refname)
```

Whether a reference name matches the destination side.

**Returns:** boolean.

### git-refspec-transform

```clips
(git-refspec-transform ?refspec ?refname)
```

A reference name matching the source side, mapped to the destination:
`refs/heads/main` becomes `refs/remotes/origin/main`.

**Returns:** string, or `FALSE` if the call is refused.

### git-refspec-rtransform

```clips
(git-refspec-rtransform ?refspec ?refname)
```

The other way round.

**Returns:** string, or `FALSE` if the call is refused.

### git-refspec-free

```clips
(git-refspec-free ?refspec)
```

Gives back a refspec from `git-refspec-parse`. One borrowed from a remote
is refused.

**Returns:** boolean.


## Credentials

A remote over a path needs no credential, and neither does a public
repository over HTTPS. Any other remote asks for one. libgit2 asks this
library, and this library asks the program. **Nothing is stored.** A secret
is read from an I/O router at the moment libgit2 asks, handed to libgit2,
and set to zero. It is never made into a CLIPS value: CLIPS keeps every
string in its symbol table until garbage collection, and a token there
stays readable. libgit2 sets a password and a passphrase to zero when it
frees the credential, so libgit2 holds the only copy.

Two sources, tried in this order:

1. **The hook.** A CLIPS function named with `git-credential-hook`. It is
   called with the URL, the username from the URL or `nil`, and a
   multifield of the kinds of credential the remote accepts
   (`GIT_CREDENTIAL_USERPASS_PLAINTEXT`, `GIT_CREDENTIAL_SSH_KEY`,
   `GIT_CREDENTIAL_USERNAME`, ...). It answers a multifield whose first
   element says which kind, or `FALSE`:

   | | |
   | --- | --- |
   | `(create$ userpass ?user [?from])` | a username and a password. The password is one line read from the logical name `?from`, or from `git-secret` if `?from` is left out. |
   | `(create$ username ?user)` | a username alone, for a remote that asks for that first. |
   | `(create$ ssh-agent ?user)` | a key that the ssh agent holds. |
   | `(create$ ssh-key ?user ?public-path ?private-path [?from])` | a key pair on disk. The passphrase, if `?from` is given, is one line read from that logical name. |
   | `(create$ prompt)` | the built-in prompt, below. |
   | `FALSE` | no credential. |

   The hook selects the kind and the username, and names where the secret
   is read from. It does not carry the secret. So one hook serves a
   terminal, a file the program opened with `open`, or a pipe on `stdin`.
   A token in a file, read by the library and never by the program:

   ```clips
   (deffunction token-hook (?url ?user ?kinds)
     (create$ userpass "alice" token))
   (git-credential-hook token-hook)

   (open "/run/secrets/github-token" token "r")
   (git-remote-fetch ?origin)
   (close token)
   ```

   A token typed at the terminal, with no echo:

   ```clips
   (deffunction terminal-hook (?url ?user ?kinds)
     (printout t "Username for " ?url ": ")
     (bind ?name (readline))
     (printout t "Token: ")
     (create$ userpass ?name git-secret))
   ```

2. **The prompt.** With no hook, or with a hook that answers `prompt`, the
   library asks the way git does: `Username for 'https://github.com': ` on
   `stdout`, read from `stdin`, then `Password for
   'https://alice@github.com': `, read from `git-secret`.
   `git-credential-prompt` says when. `auto`, the default, prompts only
   when stdin is a terminal and `GIT_TERMINAL_PROMPT` is not `0`. `always`
   prompts even on a pipe, for a script that pipes the token in. `never`
   does not prompt.

With neither, the call is refused with a message that names the remote.
After three credentials that the remote rejects, the call is refused too.

**`git-secret`** is a logical name that this library adds. A read of it is
a read of `stdin` with the echo of the terminal switched off for that
line, so `(readline git-secret)` works wherever a logical name does, and a
batch file or a rerouted stdin still feeds it. A line the program reads
that way is a CLIPS string, with the consequence above. So a credential is
better read by the library: name `git-secret` in the hook's answer instead.

**SSH.** The default build has the ssh transport through the ssh client of
the machine. For an `ssh://` or `git@host:` URL, libgit2 runs `ssh`, so the
keys, the agent, and the known hosts of the user apply, and
`GIT_SSH_COMMAND` is honoured. That transport never asks this library for
a credential. The `ssh-agent` and `ssh-key` answers are for a build against
a libgit2 that uses libssh2 instead. `git-libgit2-features` says whether
`GIT_FEATURE_SSH` is in.

**Checking a hook.** `git-credential-ask` runs the sources as a fetch
would, and answers the kind and the username of the credential they gave.
It never answers the secret.

### git-credential-hook

```clips
(git-credential-hook [?name])
```

Names the CLIPS function that selects a credential when a remote asks for
one, or clears it with `nil`. Without an argument, answers the name, or
`nil`. What the function is called with, and what it answers, is under
Credentials.

**Arguments:** 0 to 1.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-credential-prompt

```clips
(git-credential-prompt [?mode])
```

Sets when the built-in prompt is used: `auto`, only when stdin is a
terminal; `always`, even on a pipe; or `never`. Without an argument,
answers the mode.

**Arguments:** 0 to 1.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-credential-ask

```clips
(git-credential-ask ?url [?username] [?kinds])
```

Not a libgit2 function. Asks the hook and the prompt for a credential, as a
fetch from the URL would, and answers a multifield of the kind --
`userpass`, `username` or `ssh-key` -- and the username. The secret is not
answered. The optional username is the one a URL carries; the optional
kinds, a multifield of `GIT_CREDENTIAL_*` symbols, are what the remote
accepts, `GIT_CREDENTIAL_USERPASS_PLAINTEXT` if left out. For checking a
hook before going to the network.

**Arguments:** 1 to 3.

**Returns:** multifield, or `FALSE` if the call is refused.


## Status

### git-status-list-new

```clips
(git-status-list-new ?repo [?flags] [?show] [?pathspec])
```

The status of the working directory and index against HEAD, as a list to
ask entries of. The flags are `GIT_STATUS_OPT_*` symbols and default to
untracked files included and renames detected;
`GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS` lists the files in an untracked
directory rather than the directory, and `GIT_STATUS_OPT_INCLUDE_IGNORED`
adds ignored files. The third argument is which half to show --
`GIT_STATUS_SHOW_INDEX_ONLY`, `GIT_STATUS_SHOW_WORKDIR_ONLY`, or both (the
default) -- and the fourth a pathspec multifield. Freed with
`git-status-list-free`.

**Arguments:** 1 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-status-list-entrycount

```clips
(git-status-list-entrycount ?list)
```

How many entries the list has.

**Returns:** integer, or `FALSE` if the call is refused.

### git-status-byindex

```clips
(git-status-byindex ?list ?n)
```

Entry number n as a multifield: the path, the old path when the entry is a
rename (or `nil`), and then the status flags as `GIT_STATUS_*` symbols --
last, because there can be several. `(GIT_STATUS_CURRENT)` for a file with
no change, which appears only with `GIT_STATUS_OPT_INCLUDE_UNMODIFIED`.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-status-entry-to-fact

```clips
(git-status-entry-to-fact ?list ?n ?deftemplate)
```

Not a libgit2 function. Asserts entry number n as a fact of the named
deftemplate, filling whichever of the slots `path`, `old-path` and `status`
(a multislot of the flag symbols) the template has, and answers the fact.
`FALSE` -- without a message -- past the end, which is what makes it a
loop condition.

**Returns:** fact, or `FALSE` if the call is refused.

### git-status-list-free

```clips
(git-status-list-free ?list)
```

Gives the list back.

**Returns:** boolean.

### git-status-file

```clips
(git-status-file ?repo ?path)
```

The status flags of one file, by path relative to the working directory,
as a multifield of `GIT_STATUS_*` symbols.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-status-should-ignore

```clips
(git-status-should-ignore ?repo ?path)
```

Whether a path would be ignored, by the ignore rules.

**Returns:** boolean.

### git-ignore-add-rule

```clips
(git-ignore-add-rule ?repo ?rules)
```

Adds ignore rules for this session, as the text of a `.gitignore`: lines,
one pattern each. They are not written anywhere.

**Returns:** boolean.

### git-ignore-clear-internal-rules

```clips
(git-ignore-clear-internal-rules ?repo)
```

Removes the rules `git-ignore-add-rule` added.

**Returns:** boolean.

### git-ignore-path-is-ignored

```clips
(git-ignore-path-is-ignored ?repo ?path)
```

Whether a path is ignored, by the rules in `.gitignore` files, the
repository's `info/exclude`, the global excludes file and
`git-ignore-add-rule`.

**Returns:** boolean.


## Diffs and patches

### git-diff-tree-to-tree

```clips
(git-diff-tree-to-tree ?repo ?old-tree ?new-tree [?flags] [?pathspec] [?context-lines] [?interhunk-lines])
```

The difference between two trees, either of which may be `nil` for the
empty tree, as a diff handle. After the trees, each optional: the diff
flags as `GIT_DIFF_*` symbols (`GIT_DIFF_REVERSE`,
`GIT_DIFF_IGNORE_WHITESPACE`, `GIT_DIFF_INCLUDE_UNTRACKED`), a pathspec
multifield, the lines of context around each hunk (three unless said),
and the lines between hunks that keep them one hunk. Freed with
`git-diff-free`.

**Arguments:** 3 to 7.

**Returns:** handle, or `FALSE` if the call is refused.

### git-diff-tree-to-index

```clips
(git-diff-tree-to-index ?repo ?old-tree ?index [?flags] [?pathspec] [?context-lines] [?interhunk-lines])
```

The difference between a tree and an index: what is staged. `nil` for the
index means the repository's.

**Arguments:** 3 to 7.

**Returns:** handle, or `FALSE` if the call is refused.

### git-diff-index-to-workdir

```clips
(git-diff-index-to-workdir ?repo ?index [?flags] [?pathspec] [?context-lines] [?interhunk-lines])
```

The difference between an index and the working directory: what is not
staged. `nil` for the index means the repository's.

**Arguments:** 2 to 6.

**Returns:** handle, or `FALSE` if the call is refused.

### git-diff-tree-to-workdir

```clips
(git-diff-tree-to-workdir ?repo ?old-tree [?flags] [?pathspec] [?context-lines] [?interhunk-lines])
```

The difference between a tree and the working directory, ignoring the
index, which is not what `git diff <tree>` shows.

**Arguments:** 2 to 6.

**Returns:** handle, or `FALSE` if the call is refused.

### git-diff-tree-to-workdir-with-index

```clips
(git-diff-tree-to-workdir-with-index ?repo ?old-tree [?flags] [?pathspec] [?context-lines] [?interhunk-lines])
```

The difference between a tree and the working directory as `git diff
<tree>` shows it: staged and unstaged changes together.

**Arguments:** 2 to 6.

**Returns:** handle, or `FALSE` if the call is refused.

### git-diff-index-to-index

```clips
(git-diff-index-to-index ?repo ?old-index ?new-index [?flags] [?pathspec] [?context-lines] [?interhunk-lines])
```

The difference between two indexes.

**Arguments:** 3 to 7.

**Returns:** handle, or `FALSE` if the call is refused.

### git-diff-from-buffer

```clips
(git-diff-from-buffer ?patch-text)
```

A diff parsed from patch text, as `git-diff-to-buf` or `git diff` writes
it, for `git-apply`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-diff-num-deltas

```clips
(git-diff-num-deltas ?diff)
```

How many files differ.

**Returns:** integer, or `FALSE` if the call is refused.

### git-diff-num-deltas-of-type

```clips
(git-diff-num-deltas-of-type ?diff ?type)
```

How many differ in one way: `GIT_DELTA_ADDED`, `GIT_DELTA_MODIFIED`,
`GIT_DELTA_DELETED`, `GIT_DELTA_RENAMED`.

**Returns:** integer, or `FALSE` if the call is refused.

### git-diff-get-delta

```clips
(git-diff-get-delta ?diff ?n)
```

Delta number n as a multifield: the status as a `GIT_DELTA_*` symbol, the
old path, the new path, the old blob id, the new blob id, and whether the
file is binary. A path that does not apply -- the old path of an added
file -- is still the path, since git records both.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-diff-delta-to-fact

```clips
(git-diff-delta-to-fact ?diff ?n ?deftemplate)
```

Not a libgit2 function. Asserts delta number n as a fact of the named
deftemplate, filling whichever of the slots `status`, `old-path`,
`new-path`, `old-id`, `new-id` and `binary` the template has, and answers
the fact. `FALSE` -- without a message -- past the end.

**Returns:** fact, or `FALSE` if the call is refused.

### git-diff-is-sorted-icase

```clips
(git-diff-is-sorted-icase ?diff)
```

Whether the deltas are sorted without regard to case.

**Returns:** boolean.

### git-diff-merge

```clips
(git-diff-merge ?onto ?from)
```

Folds the deltas of one diff into another, which then describes both
changes.

**Returns:** boolean.

### git-diff-find-similar

```clips
(git-diff-find-similar ?diff [?flags])
```

Detects renames and copies in a diff, turning a deleted and an added file
into one renamed one. The flags are `GIT_DIFF_FIND_*` symbols;
`GIT_DIFF_FIND_BY_CONFIG` (the default) does what `diff.renames` says,
`GIT_DIFF_FIND_RENAMES` finds renames, `GIT_DIFF_FIND_COPIES` copies too.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-diff-to-buf

```clips
(git-diff-to-buf ?diff [?format])
```

The diff as text, in one of the formats git prints:
`GIT_DIFF_FORMAT_PATCH` (the default), `GIT_DIFF_FORMAT_PATCH_HEADER`,
`GIT_DIFF_FORMAT_RAW`, `GIT_DIFF_FORMAT_NAME_ONLY`,
`GIT_DIFF_FORMAT_NAME_STATUS`.

**Arguments:** 1 to 2.

**Returns:** string, or `FALSE` if the call is refused.

### git-diff-stats

```clips
(git-diff-stats ?diff)
```

Files changed, lines inserted, lines deleted, as a multifield of three
integers.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-diff-stats-to-buf

```clips
(git-diff-stats-to-buf ?diff [?format] [?width])
```

The same as `git diff --stat` prints it. The format is a
`GIT_DIFF_STATS_*` symbol or a multifield of them -- `GIT_DIFF_STATS_FULL`
(the default), `GIT_DIFF_STATS_SHORT`, `GIT_DIFF_STATS_NUMBER` -- and the
width is the number of columns, 80 unless said.

**Arguments:** 1 to 3.

**Returns:** string, or `FALSE` if the call is refused.

### git-diff-patchid

```clips
(git-diff-patchid ?diff)
```

The patch id: a hash of the change that ignores line numbers and
whitespace, the same for a commit and its cherry-pick.

**Returns:** string, or `FALSE` if the call is refused.

### git-diff-free

```clips
(git-diff-free ?diff)
```

Gives a diff back.

**Returns:** boolean.

### git-patch-from-diff

```clips
(git-patch-from-diff ?diff ?n)
```

Delta number n of a diff as a patch, which is the delta with its hunks and
lines. `nil` when the delta has no text to show, as a binary file does not.
Freed with `git-patch-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-patch-from-blobs

```clips
(git-patch-from-blobs ?old-blob ?old-path ?new-blob ?new-path)
```

A patch between two blobs, either of which may be `nil` for nothing, each
with the path to show it under.

**Returns:** handle, or `FALSE` if the call is refused.

### git-patch-from-buffers

```clips
(git-patch-from-buffers ?old-text ?old-path ?new-text ?new-path)
```

A patch between two strings, likewise.

**Returns:** handle, or `FALSE` if the call is refused.

### git-patch-get-delta

```clips
(git-patch-get-delta ?patch)
```

The patch's delta, as `git-diff-get-delta` answers it.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-patch-num-hunks

```clips
(git-patch-num-hunks ?patch)
```

How many hunks.

**Returns:** integer, or `FALSE` if the call is refused.

### git-patch-get-hunk

```clips
(git-patch-get-hunk ?patch ?n)
```

Hunk number n as a multifield: the old start line, the old line count, the
new start line, the new line count, the header (`"@@ -1,3 +1,4 @@"` and
what follows it), and how many lines the hunk has.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-patch-num-lines-in-hunk

```clips
(git-patch-num-lines-in-hunk ?patch ?n)
```

How many lines hunk number n has.

**Returns:** integer, or `FALSE` if the call is refused.

### git-patch-get-line-in-hunk

```clips
(git-patch-get-line-in-hunk ?patch ?hunk ?line)
```

Line number m of hunk number n as a multifield: the origin as a
one-character string -- `" "` for context, `"+"` for added, `"-"` for
deleted -- the line number in the old file or -1, the line number in the
new file or -1, and the content.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-patch-line-stats

```clips
(git-patch-line-stats ?patch)
```

Lines of context, lines added, lines deleted, as a multifield of three
integers.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-patch-size

```clips
(git-patch-size ?patch)
```

The size of the patch as text, in bytes.

**Returns:** integer, or `FALSE` if the call is refused.

### git-patch-to-buf

```clips
(git-patch-to-buf ?patch)
```

The patch as text.

**Returns:** string, or `FALSE` if the call is refused.

### git-patch-free

```clips
(git-patch-free ?patch)
```

Gives a patch back.

**Returns:** boolean.

### git-apply

```clips
(git-apply ?repo ?diff [?location])
```

Applies a diff to the working directory (the default,
`GIT_APPLY_LOCATION_WORKDIR`), to the index (`GIT_APPLY_LOCATION_INDEX`),
or to both. A diff that does not apply cleanly is a refusal that changes
nothing.

**Arguments:** 2 to 3.

**Returns:** boolean.

### git-apply-to-tree

```clips
(git-apply-to-tree ?repo ?tree ?diff)
```

Applies a diff to a tree and answers the result as an index, from which
`git-index-write-tree-to` makes a tree. Nothing on disk changes.

**Returns:** handle, or `FALSE` if the call is refused.


## Checkout and reset

### git-checkout-head

```clips
(git-checkout-head ?repo [?strategy] [?paths])
```

Updates the working directory and the index to match HEAD. The strategy is
`GIT_CHECKOUT_SAFE` (the default) or a multifield of `GIT_CHECKOUT_*`
symbols: `GIT_CHECKOUT_FORCE` overwrites local changes, which SAFE refuses
to; `GIT_CHECKOUT_REMOVE_UNTRACKED` removes untracked files too. The paths
are a pathspec multifield, to check out some files only. A checkout
compares the target with the working directory and leaves alone a file
that already matches, so a stale index entry for such a file stays as it
is; `git-reset` with `GIT_RESET_HARD` makes both match.

**Arguments:** 1 to 3.

**Returns:** boolean.

### git-checkout-index

```clips
(git-checkout-index ?repo ?index [?strategy] [?paths])
```

Updates the working directory to match an index, or the repository's with
`nil`. The same strategy and paths.

**Arguments:** 2 to 4.

**Returns:** boolean.

### git-checkout-tree

```clips
(git-checkout-tree ?repo ?treeish [?strategy] [?paths])
```

Updates the working directory and the index to match a tree, a commit or a
tag. HEAD does not move: that is `git-repository-set-head`, which is how
switching branches is spelled -- set the head, then check it out.

**Arguments:** 2 to 4.

**Returns:** boolean.

### git-reset

```clips
(git-reset ?repo ?target ?type)
```

Moves HEAD's branch to a commit: `GIT_RESET_SOFT` moves only that,
`GIT_RESET_MIXED` resets the index too, and `GIT_RESET_HARD` the working
directory as well.

**Returns:** boolean.

### git-reset-default

```clips
(git-reset-default ?repo ?target ?paths)
```

Resets the index entries for some paths to what a commit has, which is how
a staged change is unstaged. `nil` for the commit means an empty tree,
which removes the entries.

**Returns:** boolean.


## Revision parsing

### git-revparse-single

```clips
(git-revparse-single ?repo ?spec)
```

The object a revision spec names, as a handle of whatever kind it is:
`"HEAD"`, `"main~2"`, `"v1^{tree}"`, `"HEAD:path/to/file"`, or an id or a
prefix of one.

**Returns:** handle, or `FALSE` if the call is refused.

### git-revparse-ext

```clips
(git-revparse-ext ?repo ?spec)
```

The same, and the reference the spec went through, as a two-element
multifield: the object, and the reference or `nil` when there was none.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-revparse

```clips
(git-revparse ?repo ?spec)
```

A spec that may be a range -- `"a..b"`, `"a...b"` -- as a multifield: the
from object, the to object or `nil` for a single spec, and then the
`GIT_REVSPEC_*` flags saying which it was.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-describe-commit

```clips
(git-describe-commit ?object [?strategy] [?always-long])
```

The commit described as `git describe` would: the nearest tag, how many
commits since, and a short id. The strategy is `GIT_DESCRIBE_DEFAULT` (only
annotated tags), `GIT_DESCRIBE_TAGS` (any tag) or `GIT_DESCRIBE_ALL` (any
reference); `TRUE` for the third argument writes the long form even when
the commit is the tag. A commit with no tag behind it is described as its
short id.

**Arguments:** 1 to 3.

**Returns:** string, or `FALSE` if the call is refused.

### git-describe-workdir

```clips
(git-describe-workdir ?repo [?strategy] [?always-long])
```

The same for HEAD, with `-dirty` appended when the working directory has
changes.

**Arguments:** 1 to 3.

**Returns:** string, or `FALSE` if the call is refused.

### git-graph-ahead-behind

```clips
(git-graph-ahead-behind ?repo ?local ?upstream)
```

How many commits one is ahead of and behind another, as a multifield of
two integers: the commits reachable from the first and not the second,
and the reverse.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-graph-descendant-of

```clips
(git-graph-descendant-of ?repo ?commit ?ancestor)
```

Whether one commit is reachable from another.

**Returns:** boolean.


## Merging

### git-annotated-commit-from-ref

```clips
(git-annotated-commit-from-ref ?repo ?ref)
```

A commit with a note of the reference it came from, which is what a merge
takes so that it can write the reference's name in the merge message.
Freed with `git-annotated-commit-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-annotated-commit-lookup

```clips
(git-annotated-commit-lookup ?repo ?id)
```

The same from an id, with no reference behind it.

**Returns:** handle, or `FALSE` if the call is refused.

### git-annotated-commit-from-revspec

```clips
(git-annotated-commit-from-revspec ?repo ?spec)
```

The same from a revision spec, remembering the spec.

**Returns:** handle, or `FALSE` if the call is refused.

### git-annotated-commit-id

```clips
(git-annotated-commit-id ?annotated)
```

The commit's id.

**Returns:** string, or `FALSE` if the call is refused.

### git-annotated-commit-ref

```clips
(git-annotated-commit-ref ?annotated)
```

The reference it came from, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-annotated-commit-free

```clips
(git-annotated-commit-free ?annotated)
```

Gives an annotated commit back.

**Returns:** boolean.

### git-merge-analysis

```clips
(git-merge-analysis ?repo ?heads)
```

What merging some heads into HEAD would do, as a multifield: the
`GIT_MERGE_ANALYSIS_*` flags -- `GIT_MERGE_ANALYSIS_NORMAL`,
`GIT_MERGE_ANALYSIS_UP_TO_DATE`, `GIT_MERGE_ANALYSIS_FASTFORWARD`,
`GIT_MERGE_ANALYSIS_UNBORN` -- and then, last, the configured preference:
`GIT_MERGE_PREFERENCE_NONE`, `GIT_MERGE_PREFERENCE_NO_FASTFORWARD` or
`GIT_MERGE_PREFERENCE_FASTFORWARD_ONLY`. The heads are a multifield of
annotated commits.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-merge

```clips
(git-merge ?repo ?heads [?merge-flags] [?checkout-strategy])
```

Merges some heads into HEAD's working directory and index, and leaves the
repository in `GIT_REPOSITORY_STATE_MERGE` with the message in
`git-repository-message`, for the program to commit -- with both parents
-- and then `git-repository-state-cleanup`. A conflict is not a refusal:
the index records it, `git-index-has-conflicts` says so, and the files
carry conflict markers. `GIT_MERGE_FAIL_ON_CONFLICT` among the merge flags
makes a conflict a refusal that changes nothing. The merge flags are
`GIT_MERGE_*` symbols, `GIT_MERGE_FIND_RENAMES` by default; the checkout
strategy is as for `git-checkout-head`, safe and allowing conflicts by
default.

**Arguments:** 2 to 4.

**Returns:** boolean.

### git-merge-base

```clips
(git-merge-base ?repo ?id-one ?id-two)
```

The nearest common ancestor of two commits, by id, or `nil` when they have
none.

**Returns:** string, or `FALSE` if the call is refused.

### git-merge-trees

```clips
(git-merge-trees ?repo ?ancestor ?ours ?theirs [?flags])
```

Merges two trees against an ancestor -- `nil` for none -- and answers the
result as an index, with conflicts recorded in it. Nothing on disk
changes. The flags are as for `git-merge`.

**Arguments:** 4 to 5.

**Returns:** handle, or `FALSE` if the call is refused.

### git-merge-commits

```clips
(git-merge-commits ?repo ?ours ?theirs [?flags])
```

The same for two commits, finding the ancestor itself.

**Arguments:** 3 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-cherrypick

```clips
(git-cherrypick ?repo ?commit)
```

Applies a commit's change to the working directory and index, and leaves
the repository in `GIT_REPOSITORY_STATE_CHERRYPICK` for the program to
commit and clean up, as `git-merge` does.

**Returns:** boolean.

### git-cherrypick-commit

```clips
(git-cherrypick-commit ?repo ?commit ?our-commit [?mainline])
```

Applies a commit's change on top of another commit and answers the result
as an index, without touching the working directory. The mainline is
which parent of a merge commit to take as its base, from 1, or 0 for a
commit that is not a merge.

**Arguments:** 3 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-revert

```clips
(git-revert ?repo ?commit)
```

Applies the reverse of a commit's change, likewise leaving
`GIT_REPOSITORY_STATE_REVERT`.

**Returns:** boolean.

### git-revert-commit

```clips
(git-revert-commit ?repo ?commit ?our-commit [?mainline])
```

The reverse of a commit's change on top of another commit, as an index.

**Arguments:** 3 to 4.

**Returns:** handle, or `FALSE` if the call is refused.


## Stashes

### git-stash-save

```clips
(git-stash-save ?repo ?stasher [?message] [?flags])
```

Stashes the working directory's changes and answers the stash commit's id.
The stasher is a signature or `nil` for the default; the message is
optional; the flags are `GIT_STASH_*` symbols --
`GIT_STASH_INCLUDE_UNTRACKED`, `GIT_STASH_KEEP_INDEX`. Nothing to stash
is a refusal.

**Arguments:** 2 to 4.

**Returns:** string, or `FALSE` if the call is refused.

### git-stash-list

```clips
(git-stash-list ?repo)
```

Every stash, newest first, as a multifield of alternating messages and
ids.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-stash-apply

```clips
(git-stash-apply ?repo [?n] [?flags])
```

Applies a stash to the working directory: number n, from 0 for the newest,
which is the default. `GIT_STASH_APPLY_REINSTATE_INDEX` among the flags
restores the index as it was too.

**Arguments:** 1 to 3.

**Returns:** boolean.

### git-stash-pop

```clips
(git-stash-pop ?repo [?n] [?flags])
```

Applies a stash and drops it.

**Arguments:** 1 to 3.

**Returns:** boolean.

### git-stash-drop

```clips
(git-stash-drop ?repo [?n])
```

Drops a stash: number n, 0 by default.

**Arguments:** 1 to 2.

**Returns:** boolean.


## Blame

### git-blame-file

```clips
(git-blame-file ?repo ?path [?flags])
```

Which commit last changed each line of a file, by path relative to the
working directory, as a blame handle to ask hunks of. The flags are
`GIT_BLAME_*` symbols: `GIT_BLAME_FIRST_PARENT`,
`GIT_BLAME_IGNORE_WHITESPACE`, `GIT_BLAME_USE_MAILMAP`. Freed with
`git-blame-free`.

**Arguments:** 2 to 3.

**Returns:** handle, or `FALSE` if the call is refused.

### git-blame-hunkcount

```clips
(git-blame-hunkcount ?blame)
```

How many hunks: runs of lines last changed by the same commit.

**Returns:** integer, or `FALSE` if the call is refused.

### git-blame-get-hunk-byindex

```clips
(git-blame-get-hunk-byindex ?blame ?n)
```

Hunk number n as a multifield: the lines in it, the id of the commit that
last changed them, the line the hunk starts at in the file now, that
commit's author name and email, the id of the commit the lines came from
originally, the path they had there, and the line they started at there.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-blame-get-hunk-byline

```clips
(git-blame-get-hunk-byline ?blame ?line)
```

The hunk covering a line, counted from 1, as the same multifield.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-blame-free

```clips
(git-blame-free ?blame)
```

Gives a blame back.

**Returns:** boolean.


## Notes

### git-note-read

```clips
(git-note-read ?repo ?id [?notes-ref])
```

The note attached to an object, by id, or `nil` when there is none. The
notes reference defaults to `refs/notes/commits`.

**Arguments:** 2 to 3.

**Returns:** string, or `FALSE` if the call is refused.

### git-note-create

```clips
(git-note-create ?repo ?id ?message ?author ?committer [?notes-ref] [?force])
```

Attaches a note to an object and answers the note blob's id. The author
and committer are signatures or `nil`; the notes reference is optional;
`TRUE` for the last argument replaces a note already there, which is
otherwise a refusal.

**Arguments:** 5 to 7.

**Returns:** string, or `FALSE` if the call is refused.

### git-note-remove

```clips
(git-note-remove ?repo ?id ?author ?committer [?notes-ref])
```

Removes the note on an object.

**Arguments:** 4 to 5.

**Returns:** boolean.

### git-note-default-ref

```clips
(git-note-default-ref ?repo)
```

The notes reference in use: `refs/notes/commits` unless `core.notesRef`
says otherwise.

**Returns:** string, or `FALSE` if the call is refused.


## Worktrees

### git-worktree-list

```clips
(git-worktree-list ?repo)
```

The names of the repository's linked worktrees, as a multifield.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-worktree-lookup

```clips
(git-worktree-lookup ?repo ?name)
```

A worktree by name. Freed with `git-worktree-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-worktree-open-from-repository

```clips
(git-worktree-open-from-repository ?repo)
```

The worktree a repository opened from one stands for. A repository that
is not a worktree is a refusal.

**Returns:** handle, or `FALSE` if the call is refused.

### git-worktree-add

```clips
(git-worktree-add ?repo ?name ?path [?ref] [?lock])
```

Makes a linked worktree with a name at a path, checked out at a new branch
of that name, or at the reference given fourth. `TRUE` for the fifth
argument locks it.

**Arguments:** 3 to 5.

**Returns:** handle, or `FALSE` if the call is refused.

### git-worktree-name

```clips
(git-worktree-name ?worktree)
```

The name.

**Returns:** string, or `FALSE` if the call is refused.

### git-worktree-path

```clips
(git-worktree-path ?worktree)
```

The path of the worktree's working directory.

**Returns:** string, or `FALSE` if the call is refused.

### git-worktree-validate

```clips
(git-worktree-validate ?worktree)
```

Checks the worktree is intact -- its directories are there and point at
each other -- and refuses when it is not.

**Returns:** boolean.

### git-worktree-lock

```clips
(git-worktree-lock ?worktree [?reason])
```

Locks the worktree so that it is not pruned, with an optional reason.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-worktree-unlock

```clips
(git-worktree-unlock ?worktree)
```

Unlocks it.

**Returns:** boolean.

### git-worktree-is-locked

```clips
(git-worktree-is-locked ?worktree)
```

The reason the worktree is locked, as a string (empty when none was
given), or `FALSE` when it is not.

**Returns:** string, or `FALSE` if the call is refused.

### git-worktree-is-prunable

```clips
(git-worktree-is-prunable ?worktree [?flags])
```

Whether `git-worktree-prune` would prune it, with the same flags. A valid,
unlocked worktree is not prunable unless `GIT_WORKTREE_PRUNE_VALID` is
given.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-worktree-prune

```clips
(git-worktree-prune ?worktree [?flags])
```

Removes the worktree's record from the repository. `GIT_WORKTREE_PRUNE_VALID`
prunes one that is still intact, `GIT_WORKTREE_PRUNE_LOCKED` one that is
locked, and `GIT_WORKTREE_PRUNE_WORKING_TREE` removes the working directory
too.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-worktree-free

```clips
(git-worktree-free ?worktree)
```

Gives a worktree back.

**Returns:** boolean.


## Submodules

### git-submodule-list

```clips
(git-submodule-list ?repo)
```

The names of the repository's submodules, as a multifield.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-submodule-lookup

```clips
(git-submodule-lookup ?repo ?name)
```

A submodule by name or path. Freed with `git-submodule-free`.

**Returns:** handle, or `FALSE` if the call is refused.

### git-submodule-add-setup

```clips
(git-submodule-add-setup ?repo ?url ?path [?use-gitlink])
```

Begins adding a submodule: records it in `.gitmodules` and the
configuration, and makes a place for its repository. What follows is
`git-submodule-clone` to bring it in and `git-submodule-add-finalize` to
stage it. `FALSE` for the fourth argument puts the submodule's git
directory inside its working directory instead of under `.git/modules`.

**Arguments:** 3 to 4.

**Returns:** handle, or `FALSE` if the call is refused.

### git-submodule-clone

```clips
(git-submodule-clone ?submodule)
```

Clones the submodule into its working directory from its URL and answers
the new repository. A URL that needs a credential gets one as described
under Credentials.

**Returns:** handle, or `FALSE` if the call is refused.

### git-submodule-repo-init

```clips
(git-submodule-repo-init ?submodule [?use-gitlink])
```

Makes an empty repository for a submodule in its place, for one that is
recorded but not yet cloned. `git-submodule-init` first, so that the URL
is in the configuration. `FALSE` for the second argument is as for
`git-submodule-add-setup`.

**Arguments:** 1 to 2.

**Returns:** handle, or `FALSE` if the call is refused.

### git-submodule-add-finalize

```clips
(git-submodule-add-finalize ?submodule)
```

Finishes adding a submodule: stages `.gitmodules` and the submodule's
commit.

**Returns:** boolean.

### git-submodule-add-to-index

```clips
(git-submodule-add-to-index ?submodule [?write-index])
```

Stages the submodule's current commit. `TRUE` writes the index too.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-submodule-name

```clips
(git-submodule-name ?submodule)
```

The name.

**Returns:** string, or `FALSE` if the call is refused.

### git-submodule-path

```clips
(git-submodule-path ?submodule)
```

The path within the superproject.

**Returns:** string, or `FALSE` if the call is refused.

### git-submodule-url

```clips
(git-submodule-url ?submodule)
```

The URL.

**Returns:** string, or `FALSE` if the call is refused.

### git-submodule-branch

```clips
(git-submodule-branch ?submodule)
```

The branch the submodule tracks, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-submodule-head-id

```clips
(git-submodule-head-id ?submodule)
```

The commit of the submodule recorded in the superproject's HEAD, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-submodule-index-id

```clips
(git-submodule-index-id ?submodule)
```

The commit recorded in the superproject's index, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-submodule-wd-id

```clips
(git-submodule-wd-id ?submodule)
```

The commit checked out in the submodule's working directory, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### git-submodule-ignore

```clips
(git-submodule-ignore ?submodule)
```

How much the superproject's status ignores about the submodule, as a
`GIT_SUBMODULE_IGNORE_*` symbol.

**Returns:** symbol, or `FALSE` if the call is refused.

### git-submodule-status

```clips
(git-submodule-status ?repo ?name [?ignore])
```

The state of the submodule named, as a multifield of
`GIT_SUBMODULE_STATUS_*` symbols: where it is recorded
(`GIT_SUBMODULE_STATUS_IN_HEAD`, `_IN_INDEX`, `_IN_CONFIG`, `_IN_WD`) and
what differs. The optional last argument is how much to ignore.

**Arguments:** 2 to 3.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-submodule-set-url

```clips
(git-submodule-set-url ?repo ?name ?url)
```

Sets the URL of the submodule named, in `.gitmodules`.

**Returns:** boolean.

### git-submodule-init

```clips
(git-submodule-init ?submodule [?overwrite])
```

Copies the submodule's URL and settings from `.gitmodules` into the
repository's configuration, which is what a clone needs before
`git-submodule-update`. `TRUE` overwrites settings already there.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-submodule-update

```clips
(git-submodule-update ?submodule [?init])
```

Brings the submodule's working directory to the commit the superproject
records, cloning it first if it is not there. `TRUE` initialises it as
`git-submodule-init` would first. A URL that needs a credential gets one as
described under Credentials.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-submodule-sync

```clips
(git-submodule-sync ?submodule)
```

Copies the URL from `.gitmodules` into the configuration and into the
submodule's own remote.

**Returns:** boolean.

### git-submodule-reload

```clips
(git-submodule-reload ?submodule [?force])
```

Reads the submodule's state again from the superproject. `TRUE` reloads
even when nothing seems to have changed.

**Arguments:** 1 to 2.

**Returns:** boolean.

### git-submodule-open

```clips
(git-submodule-open ?submodule)
```

Opens the submodule's repository.

**Returns:** handle, or `FALSE` if the call is refused.

### git-submodule-free

```clips
(git-submodule-free ?submodule)
```

Gives a submodule back.

**Returns:** boolean.


## The object database

### git-odb-exists

```clips
(git-odb-exists ?odb ?id)
```

Whether an object with an id is in the database.

**Returns:** boolean.

### git-odb-read

```clips
(git-odb-read ?odb ?id)
```

The raw content of an object as a string, without its header: a commit's
text, a blob's bytes.

**Returns:** string, or `FALSE` if the call is refused.

### git-odb-read-header

```clips
(git-odb-read-header ?odb ?id)
```

The type and size of an object without reading it, as a multifield of a
`GIT_OBJECT_*` symbol and an integer.

**Returns:** multifield, or `FALSE` if the call is refused.

### git-odb-write

```clips
(git-odb-write ?odb ?content ?type)
```

Writes a string to the database as an object of a type and answers its id.

**Returns:** string, or `FALSE` if the call is refused.

### git-odb-hash

```clips
(git-odb-hash ?content ?type)
```

The id a string would have as an object of a type, without writing it.

**Returns:** string, or `FALSE` if the call is refused.

### git-odb-hashfile

```clips
(git-odb-hashfile ?path ?type)
```

The id a file would have, by path, without writing it and without filters.

**Returns:** string, or `FALSE` if the call is refused.

### git-odb-refresh

```clips
(git-odb-refresh ?odb)
```

Tells the database to look for packs and objects that arrived since it was
opened.

**Returns:** boolean.

### git-odb-free

```clips
(git-odb-free ?odb)
```

Gives the database back.

**Returns:** boolean.


## Not exposed

Deliberately left out, and why:

| | |
| --- | --- |
| `git_odb_backend_*`, `git_refdb_backend_*`, custom transports, filters and merge drivers | They are ways of extending libgit2 with C code, not of using it. |
| `git_mailmap_*`, `git_message_*`, `git_pathspec_*`, `git_packbuilder_*`, `git_indexer_*`, `git_transaction_*`, `git_rebase_*`, `git_email_*` | Not in this first release. The bookkeeping that makes the rest safe extends to them, and they are the natural next additions. |
| `git_libgit2_opts` | A grab-bag of global settings, most of them for embedding. |
| `git_blob_create_from_stream`, `git_odb_open_wstream` | Streams have no representation in CLIPS; `git-blob-create-from-buffer` and `git-odb-write` take the whole content. |
| `git_diff_foreach`, `git_diff_print`, `git_tree_walk` with a callback, `git_stash_foreach`, `git_config_foreach` | Callbacks. Each is here as a function that collects into a multifield or fills facts instead. |
