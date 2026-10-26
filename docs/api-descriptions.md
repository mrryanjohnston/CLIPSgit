# Descriptions

One `### <function>` block per registered function. `docs/gen-api-docs.sh`
folds these into API.md and fails if any function is missing one, or if any
block here names a function that is not registered.

### git-libgit2-init
Initialises libgit2, or counts one more initialisation if it already is.
The environment does this itself when it starts, so calling it is optional;
what it answers is the initialisation count, and each count is one
`git-libgit2-shutdown` owed.

### git-libgit2-shutdown
Counts one initialisation down, and answers the count that is left. Before
it does, every handle still open is freed: libgit2 must not be shut down
under a live handle, and after this none of them is any use. The
initialisation the environment took at startup is released only by a
shutdown that is not matched by an init, so a program that calls this once
without calling `git-libgit2-init` has switched the library off.

### git-libgit2-version
The version of the libgit2 loaded right now, as a string: `"1.9.7"`.

### git-libgit2-version-number
The same version as a number: major times ten thousand, minor times a
hundred, plus the revision. 1.9.7 is `10907`.

### git-built-version
Not a libgit2 function. The version of the libgit2 the wrappers were
*compiled* against, in the same numbers. That is what decides what is in
this binary -- which feature names `git-libgit2-features` can report, for
one -- and with a static build it is also what is loaded, so the two agree.

### git-libgit2-prerelease
The prerelease tag of the loaded libgit2 -- `"alpha"`, `"rc1"` -- or `nil`
for a release.

### git-libgit2-features
What the loaded libgit2 was built with, as a multifield of `GIT_FEATURE_*`
symbols. `GIT_FEATURE_HTTPS` means HTTPS transport is compiled in;
`GIT_FEATURE_SSH` is not, in the vendored build. A libgit2 1.9 reports more
of these than a 1.8 does.

### git-handle-count
Not a libgit2 function. How many handles this library has handed out and
not yet had back. A program that frees what it takes ends with zero.

### git-handle-kinds
Not a libgit2 function. The kind of every open handle, as a multifield of
strings: `"commit"`, `"tree"`, `"repository"`. A borrowed handle names what
it is borrowed from in parentheses, as `"tree entry (tree)"`. For finding
what a program forgot to free.

### git-error-last
The message libgit2 left for the last error, or `nil` when there is none.
Every refusal in this library writes the same message to stderr as it
happens, so this is for a program that wants to keep it. libgit2 does not
clear the message on success, so it can be stale after a call that
succeeded: the refusal is what says there was an error, and this is what
it was.

### git-error-last-class
Which part of libgit2 the last error came from, as a `GIT_ERROR_*` symbol:
`GIT_ERROR_REFERENCE`, `GIT_ERROR_INDEX`. `GIT_ERROR_NONE` when there is
none.

### git-repository-open
Opens the repository at a path: either a working directory with a `.git`
in it, or a git directory itself. Answers a repository handle, which
`git-repository-free` gives back along with everything that came from it.

### git-repository-open-ext
The same, with control over the search. The flags are
`GIT_REPOSITORY_OPEN_*` symbols: `GIT_REPOSITORY_OPEN_NO_SEARCH` opens the
path or nothing, where the default walks up the directory tree looking for
a repository; `GIT_REPOSITORY_OPEN_BARE` opens a bare repository quickly
without looking for a working directory; `GIT_REPOSITORY_OPEN_FROM_ENV`
honours `GIT_DIR` and its relatives, and takes `nil` for the path. The
ceiling directories are a colon-separated list the search will not walk
above.

### git-repository-open-bare
Opens a bare repository at a path, without looking for a working directory
or walking upward.

### git-repository-open-from-worktree
Opens the repository a worktree handle stands for.

### git-repository-init
Makes a new repository at a path, creating the directory if it is not
there. `TRUE` for the second argument makes it bare. The initial branch is
whatever `init.defaultBranch` says, or `master`; `git-repository-init-ext`
is the way to choose.

### git-repository-init-ext
Makes a new repository with more said about it. The flags are
`GIT_REPOSITORY_INIT_*` symbols and default to `GIT_REPOSITORY_INIT_MKPATH`,
which creates the path; `GIT_REPOSITORY_INIT_BARE` makes it bare and
`GIT_REPOSITORY_INIT_NO_REINIT` refuses a path that is already a
repository. Then, each optional and each taking `nil`: the name of the
initial branch, a description, an origin URL to configure a remote for, and
a working directory to use instead of the path.

### git-repository-discover
Finds the repository a path is in by walking up from it, and answers the
git directory's path. `TRUE` for the second argument lets the walk cross
filesystem boundaries; the third is the colon-separated list of directories
it will not walk above.

### git-clone
Clones a repository from a URL or a path into a new directory, and answers
the new repository. `TRUE` for the third argument makes the clone bare; the
fourth names the branch to check out instead of the remote's HEAD. A source
that needs a credential gets one as described under Credentials.

### git-repository-free
Gives a repository back, and everything that came from it first: commits,
trees, references, an index, a revwalk, whatever is still open. Every one
of those handles is refused afterwards, and so is this one.

### git-repository-path
The path of the git directory, with a trailing slash.

### git-repository-workdir
The path of the working directory, with a trailing slash, or `nil` for a
bare repository.

### git-repository-commondir
The path of the directory shared between a repository and its worktrees:
the same as `git-repository-path` for the main repository, the main
repository's path for a worktree.

### git-repository-is-bare
Whether the repository has no working directory.

### git-repository-is-empty
Whether the repository has just been initialised: HEAD is unborn and points
at the default branch, and nothing has been committed.

### git-repository-is-shallow
Whether the repository was cloned with a limited depth.

### git-repository-is-worktree
Whether the repository is a linked worktree of another.

### git-repository-head
The reference HEAD points at, resolved: the branch, or a direct reference
when HEAD is detached. A refusal when HEAD is unborn.

### git-repository-head-detached
Whether HEAD points at a commit directly rather than at a branch.

### git-repository-head-unborn
Whether HEAD points at a branch that has no commits yet, as in a new
repository.

### git-repository-set-head
Points HEAD at a reference by name: `refs/heads/main` to be on that branch.
A branch that does not exist yet is fine, and leaves HEAD unborn there; a
reference that is not a branch detaches HEAD at what it points to. Nothing
in the working directory changes: that is `git-checkout-head`.

### git-repository-set-head-detached
Points HEAD at a commit directly, by id.

### git-repository-detach-head
Points HEAD at the commit it currently resolves to, so that it no longer
follows the branch.

### git-repository-state
What operation the repository is in the middle of, as a
`GIT_REPOSITORY_STATE_*` symbol: `GIT_REPOSITORY_STATE_MERGE` after
`git-merge`, `GIT_REPOSITORY_STATE_NONE` ordinarily.

### git-repository-state-cleanup
Removes the files that record an operation in progress -- `MERGE_HEAD`,
`MERGE_MSG`, `CHERRY_PICK_HEAD` -- which is what ends one.

### git-repository-message
The message an operation in progress left, as the commit message it
proposes: `MERGE_MSG` after a merge. `nil` when there is none.

### git-repository-message-remove
Removes that message.

### git-repository-config
The repository's configuration, with the global and system files behind it:
what a lookup sees, and what `git-config-set-string` writes to (the
repository's own file). Freed with `git-config-free`, or with the
repository.

### git-repository-config-snapshot
The same configuration frozen at this moment, which is faster to read many
values from and refuses to be written.

### git-repository-index
The repository's index. Freed with `git-index-free`, or with the
repository.

### git-repository-odb
The repository's object database, for reading and writing raw objects.
Freed with `git-odb-free`, or with the repository.

### git-repository-ident
The identity the repository has been told to write reflogs with, as a
two-element multifield of name and email, each `nil` when unset.

### git-repository-set-ident
Sets that identity. `nil` for either goes back to the configuration.

### git-repository-set-workdir
Sets the working directory of a repository. `TRUE` for the third argument
writes a `.git` file into the directory pointing back at the repository.

### git-repository-get-namespace
The reference namespace in use, or `nil`.

### git-repository-set-namespace
Sets the reference namespace, under which every reference name is looked
up: with the namespace `foo`, `refs/heads/main` means
`refs/namespaces/foo/refs/heads/main`. `nil` clears it.

### git-repository-item-path
The path of one of the repository's pieces, named by a
`GIT_REPOSITORY_ITEM_*` symbol: `GIT_REPOSITORY_ITEM_INDEX`,
`GIT_REPOSITORY_ITEM_HOOKS`. `nil` for one the repository does not have,
such as the working directory of a bare one.

### git-repository-hashfile
The id a file would have as an object, with the repository's filters --
line endings, `.gitattributes` -- applied as they would be on add. The
path is relative to the working directory; the type defaults to
`GIT_OBJECT_BLOB`; the optional as-path is the path to look filters up by
when it differs.

### git-object-lookup
Looks an object up by id, of any type unless one is named, and answers a
handle of whichever of the four kinds it turns out to be. The functions
for that kind take it, and so do the `git-object-*` ones.

### git-object-lookup-prefix
The same by a prefix of the id, at least four characters and unambiguous.

### git-object-id
The id of an object of any kind.

### git-object-short-id
The shortest prefix of the id that is unambiguous in this repository, at
least seven characters.

### git-object-type
Which kind an object is, as a `GIT_OBJECT_*` symbol.

### git-object-peel
Follows an object to one of the named type: a tag to what it tags, a
commit to its tree. `GIT_OBJECT_ANY` peels a tag as far as it goes.

### git-object-free
Gives back an object of any of the four kinds. `git-commit-free` and its
relatives do the same for one kind.

### git-object-type2string
The name git uses for a type: `"commit"` for `GIT_OBJECT_COMMIT`.

### git-object-string2type
The type for a name git uses: `GIT_OBJECT_TREE` for `"tree"`.

### git-object-rawcontent-is-valid
Whether some bytes would parse as an object of a type, without writing
them.

### git-oid-is-valid
Not a libgit2 function. Whether a string is forty hex characters.

### git-oid-is-zero
Whether an id is the all-zero id, which git uses for "nothing".

### git-oid-cmp
Orders two ids: negative, zero or positive.

### git-signature-new
A signature -- a name, an email, and a time -- for use as an author,
committer, tagger or stasher. The time is seconds since the epoch and the
offset is the timezone in minutes east of UTC. Freed with
`git-signature-free`.

### git-signature-now
The same with the time being now, in the local timezone.

### git-signature-default
A signature from the repository's `user.name` and `user.email`. A refusal
when those are not configured. Everywhere a signature is taken, `nil`
means this.

### git-signature-from-buffer
A signature parsed from the form git writes into a commit:
`"A Name <email> 1700000000 +0100"`.

### git-signature-dup
A copy of a signature, which is how a borrowed one -- a commit's author --
is kept past the commit.

### git-signature-name
The name.

### git-signature-email
The email.

### git-signature-time
The time, in seconds since the epoch.

### git-signature-offset
The timezone, in minutes east of UTC.

### git-signature-free
Gives a signature back. A signature borrowed from a commit or a tag is not
freed on its own and this refuses it; it goes with the object.

### git-commit-lookup
Looks a commit up by id. A refusal when the id names something else.

### git-commit-lookup-prefix
The same by a prefix of the id, at least four characters and unambiguous.

### git-commit-create
Writes a commit and answers its id. The reference named second --
`"HEAD"`, `"refs/heads/main"` -- is moved to the new commit, or `nil` moves
nothing. The author and committer are signature handles, or `nil` for the
repository's default identity. The tree is a tree handle and the parents
are a multifield of commit handles, empty for a root commit.

### git-commit-create-buffer
The bytes of the commit `git-commit-create` would write, as a string,
without writing it. The same arguments less the reference to update.

### git-commit-amend
Writes a new commit in place of one, taking from the old whatever is given
as `nil`: the author, the committer, the message, the tree. The reference
named second is moved, or `nil` moves nothing. Answers the new id.

### git-commit-id
The id.

### git-commit-message
The message, with the leading whitespace git strips.

### git-commit-message-raw
The message exactly as stored.

### git-commit-message-encoding
The encoding declared in the commit header, or `nil` for UTF-8.

### git-commit-summary
The first paragraph of the message, on one line.

### git-commit-body
The message after the summary, or `nil` when there is nothing after it.

### git-commit-raw-header
The commit's headers -- tree, parents, author, committer -- as one string.

### git-commit-header-field
The value of one header by name, or `nil` when the commit has no such
header.

### git-commit-time
The committer's time, in seconds since the epoch.

### git-commit-time-offset
The committer's timezone, in minutes east of UTC.

### git-commit-author
The author, as a signature handle borrowed from the commit: it goes when
the commit does, and `git-signature-free` refuses it. `git-signature-dup`
makes a copy to keep.

### git-commit-committer
The committer, likewise.

### git-commit-tree
The tree the commit points at, as a handle to free.

### git-commit-tree-id
The id of that tree.

### git-commit-parentcount
How many parents: zero for a root commit, two for a merge.

### git-commit-parent
Parent number n, counted from zero, as a commit handle to free.

### git-commit-parent-id
The id of parent number n. A refusal when there is no such parent.

### git-commit-nth-gen-ancestor
The commit n generations back along first parents: `HEAD~n`.

### git-commit-to-fact
Not a libgit2 function. Asserts the commit as a fact of the named
deftemplate, filling whichever of these slots the template has: `id`,
`tree`, `parents` (a multislot of ids), `summary`, `message`, `body`,
`author-name`, `author-email`, `author-time`, `author-offset`,
`committer-name`, `committer-email`, `committer-time`, `committer-offset`.
A template with fewer of them gets fewer; one whose slot will not take the
value is a refusal. Answers the fact.

### git-commit-to-instance
The same into an instance of the named defclass, with an optional instance
name.

### git-commit-free
Gives a commit back.

### git-revwalk-new
A revision walker for the repository: a cursor over commits, which
`git-revwalk-push` and its relatives tell where to start and
`git-revwalk-next` moves. Freed with `git-revwalk-free`.

### git-revwalk-push
Starts the walk at a commit, by id. More than one can be pushed.

### git-revwalk-push-head
Starts the walk at HEAD.

### git-revwalk-push-ref
Starts the walk at what a reference points to, by name.

### git-revwalk-push-glob
Starts the walk at every reference matching a glob: `"refs/heads/*"`.

### git-revwalk-push-range
Starts the walk over a range: `"a..b"` walks what is reachable from b and
not from a.

### git-revwalk-hide
Hides a commit and everything reachable from it.

### git-revwalk-hide-head
Hides HEAD and its ancestors.

### git-revwalk-hide-ref
Hides what a reference points to, by name.

### git-revwalk-hide-glob
Hides every reference matching a glob.

### git-revwalk-sorting
Sets the order: `GIT_SORT_NONE` for git's own, `GIT_SORT_TOPOLOGICAL` to
show no parent before its children, `GIT_SORT_TIME` by commit time, and
`GIT_SORT_REVERSE` to turn whichever of those around. A multifield combines
them. Set this before pushing.

### git-revwalk-simplify-first-parent
Follows only first parents, so a merge contributes one line of history.

### git-revwalk-next
The id of the next commit, or `FALSE` -- without a message -- when the
walk is over.

### git-revwalk-reset
Empties the walk to be set up again.

### git-revwalk-free
Gives the walk back.

### git-tree-lookup
Looks a tree up by id.

### git-tree-lookup-prefix
The same by a prefix of the id.

### git-tree-id
The id.

### git-tree-entrycount
How many entries the tree has, at this level.

### git-tree-entry-byindex
Entry number n, counted from zero, in the tree's order. The entry is
borrowed from the tree and goes with it; `git-tree-entry-free` refuses it.

### git-tree-entry-byname
The entry with a name, at this level. Borrowed, likewise.

### git-tree-entry-byid
The entry whose object has an id, at this level. Borrowed, likewise.

### git-tree-entry-bypath
The entry at a path, descending into subtrees: `"dir/file"`. This one is
the caller's, and `git-tree-entry-free` gives it back.

### git-tree-walk
Every path in the tree, recursively, as a multifield of strings, subtrees
with a trailing slash. `GIT_TREEWALK_PRE` (the default) lists a subtree
before what is in it and `GIT_TREEWALK_POST` after.

### git-tree-free
Gives a tree back, and the entries borrowed from it.

### git-tree-entry-name
The entry's name.

### git-tree-entry-id
The id of the object the entry points at.

### git-tree-entry-type
What kind of object that is: `GIT_OBJECT_BLOB`, `GIT_OBJECT_TREE`, or
`GIT_OBJECT_COMMIT` for a submodule.

### git-tree-entry-filemode
The entry's mode as a `GIT_FILEMODE_*` symbol, normalised the way git
does: `GIT_FILEMODE_BLOB`, `GIT_FILEMODE_BLOB_EXECUTABLE`,
`GIT_FILEMODE_TREE`, `GIT_FILEMODE_LINK`, `GIT_FILEMODE_COMMIT`.

### git-tree-entry-filemode-raw
The mode as stored, which can differ from the normalised one in an old
repository.

### git-tree-entry-to-object
The object an entry points at, looked up in the repository.

### git-tree-entry-cmp
Orders two entries as git does: negative, zero or positive.

### git-tree-entry-dup
A copy of an entry, which is the caller's to free -- how a borrowed one is
kept past its tree.

### git-tree-entry-free
Gives back an entry from `git-tree-entry-bypath` or `git-tree-entry-dup`.
A borrowed entry is refused.

### git-treebuilder-new
A builder for a tree, starting from the entries of the tree given or from
nothing. Entries are put in with `git-treebuilder-insert`, and
`git-treebuilder-write` writes the result. Freed with
`git-treebuilder-free`.

### git-treebuilder-insert
Puts an entry in, replacing one of the same name: a name, the id of a blob
or tree already in the repository, and a `GIT_FILEMODE_*` symbol. Answers
the entry, borrowed from the builder.

### git-treebuilder-get
The entry with a name, borrowed from the builder. A refusal when there is
none.

### git-treebuilder-remove
Takes the entry with a name out.

### git-treebuilder-clear
Takes every entry out.

### git-treebuilder-entrycount
How many entries the builder has.

### git-treebuilder-write
Writes the tree to the repository and answers its id.

### git-treebuilder-free
Gives the builder back, and the entries borrowed from it.

### git-blob-lookup
Looks a blob up by id.

### git-blob-lookup-prefix
The same by a prefix of the id.

### git-blob-id
The id.

### git-blob-rawsize
The size of the content in bytes.

### git-blob-rawcontent
The content as a string. A CLIPS string ends at a NUL byte, so a binary
blob comes back cut at its first one; `git-blob-is-binary` says whether
that is a concern.

### git-blob-is-binary
Whether the content looks binary to git: a NUL in the first eight
kilobytes.

### git-blob-filter
The content with the repository's filters applied as they would be on
checkout to the given path -- line endings, `.gitattributes`.

### git-blob-create-from-buffer
Writes a string to the repository as a blob and answers its id.

### git-blob-create-from-workdir
Writes a file from the working directory, by relative path, applying the
filters an add would.

### git-blob-create-from-disk
Writes a file from anywhere, by path, applying no filters.

### git-blob-free
Gives a blob back.

### git-tag-lookup
Looks an annotated tag up by id -- the tag object, not the commit it tags.

### git-tag-lookup-prefix
The same by a prefix of the id.

### git-tag-create
Writes an annotated tag: a name, the object it tags, a tagger signature or
`nil` for the default, a message, and optionally `TRUE` to replace a tag
of that name. Answers the tag object's id and creates `refs/tags/<name>`.

### git-tag-create-lightweight
Creates `refs/tags/<name>` pointing straight at an object, with no tag
object, and answers the object's id.

### git-tag-annotation-create
Writes a tag object like `git-tag-create` but creates no reference for it.

### git-tag-delete
Removes `refs/tags/<name>`.

### git-tag-list
The names of every tag, as a multifield, or of those matching a glob when
one is given.

### git-tag-id
The tag object's id.

### git-tag-name
The tag's name.

### git-tag-message
The tag's message.

### git-tag-tagger
The tagger, as a signature borrowed from the tag.

### git-tag-target
The object the tag points at, as a handle of whatever kind it is.

### git-tag-target-id
The id of that object.

### git-tag-target-type
The kind of that object, as a `GIT_OBJECT_*` symbol.

### git-tag-peel
The object the tag points at, following tags of tags to the end.

### git-tag-free
Gives a tag back.

### git-reference-lookup
Looks a reference up by its full name: `"refs/heads/main"`, `"HEAD"`.
Freed with `git-reference-free`.

### git-reference-dwim
Looks a reference up by a short name, trying what git would: `"main"`
finds `refs/heads/main`, `"v1"` finds `refs/tags/v1`.

### git-reference-name-to-id
The id a reference resolves to, by full name, following symbolic
references to the end.

### git-reference-create
Creates a direct reference pointing at an id. `TRUE` for the fourth
argument replaces an existing one, which is otherwise a refusal; the fifth
is the reflog message.

### git-reference-symbolic-create
Creates a symbolic reference pointing at another by name, likewise.

### git-reference-list
The full names of every reference, as a multifield.

### git-reference-list-glob
The full names of the references matching a glob: `"refs/tags/*"`.

### git-reference-remove
Deletes a reference by name.

### git-reference-has-log
Whether a reference has a reflog.

### git-reference-is-valid-name
Whether a string is a well-formed full reference name.

### git-reference-name
The full name.

### git-reference-shorthand
The name as git would print it: `main` for `refs/heads/main`.

### git-reference-type
`GIT_REFERENCE_DIRECT` for one pointing at an id, `GIT_REFERENCE_SYMBOLIC`
for one pointing at another reference.

### git-reference-target
The id a direct reference points at, or `nil` for a symbolic one.

### git-reference-target-peel
The id of the object a packed tag reference has been peeled to, or `nil`.

### git-reference-symbolic-target
The name a symbolic reference points at, or `nil` for a direct one.

### git-reference-resolve
The direct reference at the end of a chain of symbolic ones.

### git-reference-peel
The object a reference points at, peeled to the named type, or to the end
with `GIT_OBJECT_ANY` (the default).

### git-reference-set-target
Moves a direct reference to an id and answers the moved reference as a new
handle; the old one is unchanged and should be freed. A reference that has
been changed since it was read is refused, which is libgit2 guarding
against a race.

### git-reference-symbolic-set-target
Moves a symbolic reference to point at another name, likewise.

### git-reference-rename
Renames a reference and answers the renamed one as a new handle. `TRUE`
for the third argument replaces a reference of the new name.

### git-reference-delete
Deletes the reference. The handle is still to be freed.

### git-reference-is-branch
Whether the reference is under `refs/heads/`.

### git-reference-is-remote
Whether it is under `refs/remotes/`.

### git-reference-is-tag
Whether it is under `refs/tags/`.

### git-reference-is-note
Whether it is under `refs/notes/`.

### git-reference-cmp
Orders two references: zero when they are the same.

### git-reference-dup
A copy of a reference handle.

### git-reference-free
Gives a reference back.

### git-reflog-read
The reflog of a reference, by name, as a flat multifield of four elements
per entry, newest first: the old id, the new id, the committer's name, and
the message. A CLIPS multifield cannot hold another, so the entries run on
from one another; `(length$)` divided by four is how many there are. An
empty multifield for a reference with no log.

### git-reflog-delete
Removes the reflog of a reference.

### git-branch-create
Creates a branch pointing at a commit and answers its reference. `TRUE`
for the fourth argument replaces a branch of that name.

### git-branch-create-from-annotated
The same from an annotated commit, which records where it came from in the
reflog.

### git-branch-lookup
Finds a branch by its short name: `"main"`, or `"origin/main"` with
`GIT_BRANCH_REMOTE`. `GIT_BRANCH_LOCAL` is the default.

### git-branch-list
The short names of every branch, as a multifield: local ones with
`GIT_BRANCH_LOCAL`, remote-tracking ones with `GIT_BRANCH_REMOTE`, both
with `GIT_BRANCH_ALL` (the default).

### git-branch-delete
Deletes a branch. The branch HEAD is on is refused. The handle is still to
be freed.

### git-branch-move
Renames a branch and answers the renamed one as a new handle. `TRUE` for
the third argument replaces a branch of the new name.

### git-branch-name
The short name of a branch reference.

### git-branch-is-head
Whether HEAD is on this branch.

### git-branch-is-checked-out
Whether this branch is checked out in this repository or any of its
worktrees.

### git-branch-upstream
The branch this one tracks, as a reference, or `nil` when none is
configured.

### git-branch-set-upstream
Sets the branch this one tracks, by short name: `"origin/main"`, or a
local branch. `nil` unsets it.

### git-branch-upstream-name
The full name of the upstream of the branch named, or `nil`.

### git-branch-upstream-remote
The name of the remote the upstream of the branch named is on, or `"."`
for a local upstream, or `nil` when there is none.

### git-branch-upstream-merge
The `branch.<name>.merge` value of the branch named, or `nil`.

### git-branch-remote-name
The name of the remote a remote-tracking branch belongs to, by the
branch's full name. A local branch is a refusal.

### git-branch-is-valid-name
Whether a string is a well-formed branch name.

### git-index-open
Opens an index file by path, standing on its own: no repository behind it.
Freed with `git-index-free`.

### git-index-new
An empty index in memory, with no file and no repository.

### git-index-owner
The repository the index belongs to, or `nil` for one that stands alone.

### git-index-path
The path of the index file, or `nil` for one in memory.

### git-index-read
Reads the index file again, discarding changes made in memory.

### git-index-write
Writes the index to its file.

### git-index-read-tree
Replaces the index's contents with a tree's.

### git-index-write-tree
Writes the index as a tree to its repository and answers the tree's id. The
index must have no conflicts.

### git-index-write-tree-to
The same, to a repository named explicitly, which is how an index that
stands alone is written.

### git-index-entrycount
How many entries the index has.

### git-index-clear
Removes every entry.

### git-index-version
The index file format version, 2 to 4.

### git-index-set-version
Sets it.

### git-index-checksum
The checksum of the index as last read or written, as an id, or `nil`
before either.

### git-index-add-bypath
Stages a file from the working directory, by relative path. A file that
is not there is a refusal; `git-index-remove-bypath` is for that.

### git-index-add-all
Stages every file matching a pathspec -- a multifield of patterns,
`(create$ "*")` for everything -- adding new files and updating changed
ones. The flags are `GIT_INDEX_ADD_*` symbols: `GIT_INDEX_ADD_FORCE` adds
ignored files too.

### git-index-remove
Removes the entry for a path at a stage: 0 for an ordinary entry, 1 to 3
for the sides of a conflict.

### git-index-remove-bypath
Removes the entry for a path, whatever its stage, and any conflict
recorded for it.

### git-index-remove-directory
Removes every entry under a directory, at a stage.

### git-index-remove-all
Removes every entry matching a pathspec.

### git-index-update-all
Updates every entry matching a pathspec to what is in the working
directory, removing entries whose files are gone. New files are not added.

### git-index-find
The position of the entry for a path, or `FALSE` -- without a message --
when there is none.

### git-index-get-byindex
The entry at a position, as a multifield: the path, the blob id, the mode
as a `GIT_FILEMODE_*` symbol, the file size, and the stage. A refusal past
the end.

### git-index-get-bypath
The entry for a path, at stage 0 unless another is given, as the same
multifield.

### git-index-entry-to-fact
Not a libgit2 function. Asserts the entry at a position as a fact of the
named deftemplate, filling whichever of the slots `path`, `id`, `mode`,
`size` and `stage` the template has, and answers the fact. `FALSE` --
without a message -- past the end, which is what makes it a loop
condition.

### git-index-has-conflicts
Whether any entry is at a conflict stage.

### git-index-conflict-remove
Removes the conflict entries for a path.

### git-index-conflict-cleanup
Removes every conflict entry.

### git-index-free
Gives the index back.

### git-config-open-default
The configuration a program outside any repository sees: the global,
XDG, system and programdata files, in that order of precedence. Freed
with `git-config-free`.

### git-config-new
An empty configuration, to add files to with `git-config-add-file-ondisk`.

### git-config-open-ondisk
A configuration with one file in it, by path. The file need not exist yet;
a write creates it.

### git-config-open-level
One level of a configuration, as a configuration of its own: the
`GIT_CONFIG_LEVEL_*` symbol names it, and `GIT_CONFIG_LEVEL_LOCAL` is the
repository's own file. A level the configuration does not have is a
refusal.

### git-config-add-file-ondisk
Adds a file to a configuration at a level. `TRUE` for the fourth argument
replaces a file already at that level; the fifth is the repository the
file belongs to, or `nil`.

### git-config-snapshot
The configuration frozen at this moment, faster to read many values from
and refusing to be written.

### git-config-find-global
The path of the user's `.gitconfig`, or `nil` when there is none.

### git-config-find-xdg
The path of the XDG configuration file, `~/.config/git/config`, or `nil`.

### git-config-find-system
The path of the system configuration file, or `nil`.

### git-config-find-programdata
The path of the ProgramData configuration file on Windows, or `nil`
elsewhere.

### git-config-get-string
A value as a string, or `nil` when it is not set.

### git-config-get-path
A value as a path, with a leading `~` expanded, or `nil`.

### git-config-get-int
A value as an integer, understanding git's `k`, `m` and `g` suffixes, or
`nil` when it is not set. A value that is set and is not a number is a
refusal.

### git-config-get-bool
A value as `TRUE` or `FALSE`, understanding `yes`, `on`, `1` and their
opposites, or `nil` when it is not set.

### git-config-get-multivar
Every value of a name that can have several, as a multifield of strings,
narrowed to those matching a regular expression when one is given. Empty
when there are none.

### git-config-entries
Every entry, as a multifield of alternating names and values, narrowed to
the names matching a regular expression when one is given.

### git-config-set-string
Sets a value. In a repository's configuration this writes the repository's
own file.

### git-config-set-int
Sets an integer value.

### git-config-set-bool
Sets a boolean value.

### git-config-set-multivar
Sets the values of a name matching a regular expression to a value. The
expression `"^$"` matches nothing, which adds a value.

### git-config-delete-entry
Removes a value. One that is not set is a refusal.

### git-config-delete-multivar
Removes the values of a name matching a regular expression.

### git-config-parse-bool
Reads a string the way `git-config-get-bool` would: `"on"` is `TRUE`.

### git-config-parse-int
Reads a string the way `git-config-get-int` would: `"2k"` is 2048.

### git-config-parse-path
Reads a string the way `git-config-get-path` would, expanding `~`.

### git-config-free
Gives a configuration back.

### git-remote-create
Adds a remote to the repository's configuration with a name and a URL, and
answers it. The fourth argument is a fetch refspec to use instead of the
default `+refs/heads/*:refs/remotes/<name>/*`. Freed with
`git-remote-free`.

### git-remote-create-anonymous
A remote with a URL and no name, which is not written to the configuration.

### git-remote-create-detached
A remote with a URL and no repository, for `git-remote-ls`.

### git-remote-lookup
The remote with a name.

### git-remote-list
The names of every remote, as a multifield.

### git-remote-delete
Removes a remote and its remote-tracking branches, by name.

### git-remote-rename
Renames a remote, and its remote-tracking branches with it. Answers the
refspecs that could not be renamed, as a multifield -- empty when all of
them were.

### git-remote-set-url
Sets the URL of the remote named.

### git-remote-set-pushurl
Sets the URL the remote named is pushed to, when that differs.

### git-remote-add-fetch
Adds a fetch refspec to the remote named.

### git-remote-add-push
Adds a push refspec to the remote named.

### git-remote-is-valid-name
Whether a string is a well-formed remote name.

### git-remote-name
The name, or `nil` for an anonymous remote.

### git-remote-url
The URL.

### git-remote-pushurl
The push URL, or `nil` when it is the same as the URL.

### git-remote-refspec-count
How many refspecs the remote has, fetch and push together.

### git-remote-get-refspec
Refspec number n, borrowed from the remote.

### git-remote-get-fetch-refspecs
The fetch refspecs, as a multifield of strings.

### git-remote-get-push-refspecs
The push refspecs, likewise.

### git-remote-autotag
How the remote fetches tags, as a `GIT_REMOTE_DOWNLOAD_TAGS_*` symbol.

### git-remote-prune-refs
Whether the remote is configured to prune remote-tracking branches on
fetch.

### git-remote-connect
Connects to the remote, for fetching by default or for pushing with
`GIT_DIRECTION_PUSH`. The functions that need a connection make one
themselves; this is for asking questions of a remote before fetching. A
remote that needs a credential gets one as described under Credentials.

### git-remote-connected
Whether the remote is connected.

### git-remote-disconnect
Closes the connection.

### git-remote-stop
Asks a fetch or push in progress to stop.

### git-remote-ls
What the remote advertises, as a multifield of alternating reference names
and ids. Connects if it has to, with a credential if the remote needs one.

### git-remote-fetch
Fetches from the remote, into remote-tracking branches. The optional
refspecs are a multifield of strings, and replace the configured ones for
this fetch; the optional message is written to the reflog. A remote that
needs a credential gets one as described under Credentials.

### git-remote-push
Pushes to the remote. The optional refspecs are a multifield of strings,
`"refs/heads/main:refs/heads/main"`, and replace the configured ones for
this push. Over a path, libgit2 pushes only into a bare repository. A
remote that needs a credential gets one as described under Credentials.

### git-remote-prune
Removes remote-tracking branches whose branches are gone from the remote.

### git-remote-update-tips
Updates the remote-tracking branches from what the last fetch downloaded,
which `git-remote-fetch` already does; for a program that fetched some
other way.

### git-remote-dup
A copy of a remote handle.

### git-remote-free
Gives a remote back, and the refspecs borrowed from it.

### git-refspec-parse
Parses a refspec on its own -- `"+refs/heads/*:refs/remotes/origin/*"` --
as a fetch refspec with `TRUE` or a push one with `FALSE`. Freed with
`git-refspec-free`.

### git-refspec-src
The source side.

### git-refspec-dst
The destination side.

### git-refspec-string
The whole refspec as written.

### git-refspec-force
Whether it starts with `+`.

### git-refspec-direction
`GIT_DIRECTION_FETCH` or `GIT_DIRECTION_PUSH`.

### git-refspec-src-matches
Whether a reference name matches the source side.

### git-refspec-dst-matches
Whether a reference name matches the destination side.

### git-refspec-transform
A reference name matching the source side, mapped to the destination:
`refs/heads/main` becomes `refs/remotes/origin/main`.

### git-refspec-rtransform
The other way round.

### git-refspec-free
Gives back a refspec from `git-refspec-parse`. One borrowed from a remote
is refused.

### git-status-list-new
The status of the working directory and index against HEAD, as a list to
ask entries of. The flags are `GIT_STATUS_OPT_*` symbols and default to
untracked files included and renames detected;
`GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS` lists the files in an untracked
directory rather than the directory, and `GIT_STATUS_OPT_INCLUDE_IGNORED`
adds ignored files. The third argument is which half to show --
`GIT_STATUS_SHOW_INDEX_ONLY`, `GIT_STATUS_SHOW_WORKDIR_ONLY`, or both (the
default) -- and the fourth a pathspec multifield. Freed with
`git-status-list-free`.

### git-status-list-entrycount
How many entries the list has.

### git-status-byindex
Entry number n as a multifield: the path, the old path when the entry is a
rename (or `nil`), and then the status flags as `GIT_STATUS_*` symbols --
last, because there can be several. `(GIT_STATUS_CURRENT)` for a file with
no change, which appears only with `GIT_STATUS_OPT_INCLUDE_UNMODIFIED`.

### git-status-entry-to-fact
Not a libgit2 function. Asserts entry number n as a fact of the named
deftemplate, filling whichever of the slots `path`, `old-path` and `status`
(a multislot of the flag symbols) the template has, and answers the fact.
`FALSE` -- without a message -- past the end, which is what makes it a
loop condition.

### git-status-list-free
Gives the list back.

### git-status-file
The status flags of one file, by path relative to the working directory,
as a multifield of `GIT_STATUS_*` symbols.

### git-status-should-ignore
Whether a path would be ignored, by the ignore rules.

### git-ignore-add-rule
Adds ignore rules for this session, as the text of a `.gitignore`: lines,
one pattern each. They are not written anywhere.

### git-ignore-clear-internal-rules
Removes the rules `git-ignore-add-rule` added.

### git-ignore-path-is-ignored
Whether a path is ignored, by the rules in `.gitignore` files, the
repository's `info/exclude`, the global excludes file and
`git-ignore-add-rule`.

### git-diff-tree-to-tree
The difference between two trees, either of which may be `nil` for the
empty tree, as a diff handle. After the trees, each optional: the diff
flags as `GIT_DIFF_*` symbols (`GIT_DIFF_REVERSE`,
`GIT_DIFF_IGNORE_WHITESPACE`, `GIT_DIFF_INCLUDE_UNTRACKED`), a pathspec
multifield, the lines of context around each hunk (three unless said),
and the lines between hunks that keep them one hunk. Freed with
`git-diff-free`.

### git-diff-tree-to-index
The difference between a tree and an index: what is staged. `nil` for the
index means the repository's.

### git-diff-index-to-workdir
The difference between an index and the working directory: what is not
staged. `nil` for the index means the repository's.

### git-diff-tree-to-workdir
The difference between a tree and the working directory, ignoring the
index, which is not what `git diff <tree>` shows.

### git-diff-tree-to-workdir-with-index
The difference between a tree and the working directory as `git diff
<tree>` shows it: staged and unstaged changes together.

### git-diff-index-to-index
The difference between two indexes.

### git-diff-from-buffer
A diff parsed from patch text, as `git-diff-to-buf` or `git diff` writes
it, for `git-apply`.

### git-diff-num-deltas
How many files differ.

### git-diff-num-deltas-of-type
How many differ in one way: `GIT_DELTA_ADDED`, `GIT_DELTA_MODIFIED`,
`GIT_DELTA_DELETED`, `GIT_DELTA_RENAMED`.

### git-diff-get-delta
Delta number n as a multifield: the status as a `GIT_DELTA_*` symbol, the
old path, the new path, the old blob id, the new blob id, and whether the
file is binary. A path that does not apply -- the old path of an added
file -- is still the path, since git records both.

### git-diff-delta-to-fact
Not a libgit2 function. Asserts delta number n as a fact of the named
deftemplate, filling whichever of the slots `status`, `old-path`,
`new-path`, `old-id`, `new-id` and `binary` the template has, and answers
the fact. `FALSE` -- without a message -- past the end.

### git-diff-is-sorted-icase
Whether the deltas are sorted without regard to case.

### git-diff-merge
Folds the deltas of one diff into another, which then describes both
changes.

### git-diff-find-similar
Detects renames and copies in a diff, turning a deleted and an added file
into one renamed one. The flags are `GIT_DIFF_FIND_*` symbols;
`GIT_DIFF_FIND_BY_CONFIG` (the default) does what `diff.renames` says,
`GIT_DIFF_FIND_RENAMES` finds renames, `GIT_DIFF_FIND_COPIES` copies too.

### git-diff-to-buf
The diff as text, in one of the formats git prints:
`GIT_DIFF_FORMAT_PATCH` (the default), `GIT_DIFF_FORMAT_PATCH_HEADER`,
`GIT_DIFF_FORMAT_RAW`, `GIT_DIFF_FORMAT_NAME_ONLY`,
`GIT_DIFF_FORMAT_NAME_STATUS`.

### git-diff-stats
Files changed, lines inserted, lines deleted, as a multifield of three
integers.

### git-diff-stats-to-buf
The same as `git diff --stat` prints it. The format is a
`GIT_DIFF_STATS_*` symbol or a multifield of them -- `GIT_DIFF_STATS_FULL`
(the default), `GIT_DIFF_STATS_SHORT`, `GIT_DIFF_STATS_NUMBER` -- and the
width is the number of columns, 80 unless said.

### git-diff-patchid
The patch id: a hash of the change that ignores line numbers and
whitespace, the same for a commit and its cherry-pick.

### git-diff-free
Gives a diff back.

### git-patch-from-diff
Delta number n of a diff as a patch, which is the delta with its hunks and
lines. `nil` when the delta has no text to show, as a binary file does not.
Freed with `git-patch-free`.

### git-patch-from-blobs
A patch between two blobs, either of which may be `nil` for nothing, each
with the path to show it under.

### git-patch-from-buffers
A patch between two strings, likewise.

### git-patch-get-delta
The patch's delta, as `git-diff-get-delta` answers it.

### git-patch-num-hunks
How many hunks.

### git-patch-get-hunk
Hunk number n as a multifield: the old start line, the old line count, the
new start line, the new line count, the header (`"@@ -1,3 +1,4 @@"` and
what follows it), and how many lines the hunk has.

### git-patch-num-lines-in-hunk
How many lines hunk number n has.

### git-patch-get-line-in-hunk
Line number m of hunk number n as a multifield: the origin as a
one-character string -- `" "` for context, `"+"` for added, `"-"` for
deleted -- the line number in the old file or -1, the line number in the
new file or -1, and the content.

### git-patch-line-stats
Lines of context, lines added, lines deleted, as a multifield of three
integers.

### git-patch-size
The size of the patch as text, in bytes.

### git-patch-to-buf
The patch as text.

### git-patch-free
Gives a patch back.

### git-apply
Applies a diff to the working directory (the default,
`GIT_APPLY_LOCATION_WORKDIR`), to the index (`GIT_APPLY_LOCATION_INDEX`),
or to both. A diff that does not apply cleanly is a refusal that changes
nothing.

### git-apply-to-tree
Applies a diff to a tree and answers the result as an index, from which
`git-index-write-tree-to` makes a tree. Nothing on disk changes.

### git-checkout-head
Updates the working directory and the index to match HEAD. The strategy is
`GIT_CHECKOUT_SAFE` (the default) or a multifield of `GIT_CHECKOUT_*`
symbols: `GIT_CHECKOUT_FORCE` overwrites local changes, which SAFE refuses
to; `GIT_CHECKOUT_REMOVE_UNTRACKED` removes untracked files too. The paths
are a pathspec multifield, to check out some files only. A checkout
compares the target with the working directory and leaves alone a file
that already matches, so a stale index entry for such a file stays as it
is; `git-reset` with `GIT_RESET_HARD` makes both match.

### git-checkout-index
Updates the working directory to match an index, or the repository's with
`nil`. The same strategy and paths.

### git-checkout-tree
Updates the working directory and the index to match a tree, a commit or a
tag. HEAD does not move: that is `git-repository-set-head`, which is how
switching branches is spelled -- set the head, then check it out.

### git-reset
Moves HEAD's branch to a commit: `GIT_RESET_SOFT` moves only that,
`GIT_RESET_MIXED` resets the index too, and `GIT_RESET_HARD` the working
directory as well.

### git-reset-default
Resets the index entries for some paths to what a commit has, which is how
a staged change is unstaged. `nil` for the commit means an empty tree,
which removes the entries.

### git-revparse-single
The object a revision spec names, as a handle of whatever kind it is:
`"HEAD"`, `"main~2"`, `"v1^{tree}"`, `"HEAD:path/to/file"`, or an id or a
prefix of one.

### git-revparse-ext
The same, and the reference the spec went through, as a two-element
multifield: the object, and the reference or `nil` when there was none.

### git-revparse
A spec that may be a range -- `"a..b"`, `"a...b"` -- as a multifield: the
from object, the to object or `nil` for a single spec, and then the
`GIT_REVSPEC_*` flags saying which it was.

### git-describe-commit
The commit described as `git describe` would: the nearest tag, how many
commits since, and a short id. The strategy is `GIT_DESCRIBE_DEFAULT` (only
annotated tags), `GIT_DESCRIBE_TAGS` (any tag) or `GIT_DESCRIBE_ALL` (any
reference); `TRUE` for the third argument writes the long form even when
the commit is the tag. A commit with no tag behind it is described as its
short id.

### git-describe-workdir
The same for HEAD, with `-dirty` appended when the working directory has
changes.

### git-graph-ahead-behind
How many commits one is ahead of and behind another, as a multifield of
two integers: the commits reachable from the first and not the second,
and the reverse.

### git-graph-descendant-of
Whether one commit is reachable from another.

### git-annotated-commit-from-ref
A commit with a note of the reference it came from, which is what a merge
takes so that it can write the reference's name in the merge message.
Freed with `git-annotated-commit-free`.

### git-annotated-commit-lookup
The same from an id, with no reference behind it.

### git-annotated-commit-from-revspec
The same from a revision spec, remembering the spec.

### git-annotated-commit-id
The commit's id.

### git-annotated-commit-ref
The reference it came from, or `nil`.

### git-annotated-commit-free
Gives an annotated commit back.

### git-merge-analysis
What merging some heads into HEAD would do, as a multifield: the
`GIT_MERGE_ANALYSIS_*` flags -- `GIT_MERGE_ANALYSIS_NORMAL`,
`GIT_MERGE_ANALYSIS_UP_TO_DATE`, `GIT_MERGE_ANALYSIS_FASTFORWARD`,
`GIT_MERGE_ANALYSIS_UNBORN` -- and then, last, the configured preference:
`GIT_MERGE_PREFERENCE_NONE`, `GIT_MERGE_PREFERENCE_NO_FASTFORWARD` or
`GIT_MERGE_PREFERENCE_FASTFORWARD_ONLY`. The heads are a multifield of
annotated commits.

### git-merge
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

### git-merge-base
The nearest common ancestor of two commits, by id, or `nil` when they have
none.

### git-merge-trees
Merges two trees against an ancestor -- `nil` for none -- and answers the
result as an index, with conflicts recorded in it. Nothing on disk
changes. The flags are as for `git-merge`.

### git-merge-commits
The same for two commits, finding the ancestor itself.

### git-cherrypick
Applies a commit's change to the working directory and index, and leaves
the repository in `GIT_REPOSITORY_STATE_CHERRYPICK` for the program to
commit and clean up, as `git-merge` does.

### git-cherrypick-commit
Applies a commit's change on top of another commit and answers the result
as an index, without touching the working directory. The mainline is
which parent of a merge commit to take as its base, from 1, or 0 for a
commit that is not a merge.

### git-revert
Applies the reverse of a commit's change, likewise leaving
`GIT_REPOSITORY_STATE_REVERT`.

### git-revert-commit
The reverse of a commit's change on top of another commit, as an index.

### git-stash-save
Stashes the working directory's changes and answers the stash commit's id.
The stasher is a signature or `nil` for the default; the message is
optional; the flags are `GIT_STASH_*` symbols --
`GIT_STASH_INCLUDE_UNTRACKED`, `GIT_STASH_KEEP_INDEX`. Nothing to stash
is a refusal.

### git-stash-list
Every stash, newest first, as a multifield of alternating messages and
ids.

### git-stash-apply
Applies a stash to the working directory: number n, from 0 for the newest,
which is the default. `GIT_STASH_APPLY_REINSTATE_INDEX` among the flags
restores the index as it was too.

### git-stash-pop
Applies a stash and drops it.

### git-stash-drop
Drops a stash: number n, 0 by default.

### git-blame-file
Which commit last changed each line of a file, by path relative to the
working directory, as a blame handle to ask hunks of. The flags are
`GIT_BLAME_*` symbols: `GIT_BLAME_FIRST_PARENT`,
`GIT_BLAME_IGNORE_WHITESPACE`, `GIT_BLAME_USE_MAILMAP`. Freed with
`git-blame-free`.

### git-blame-hunkcount
How many hunks: runs of lines last changed by the same commit.

### git-blame-get-hunk-byindex
Hunk number n as a multifield: the lines in it, the id of the commit that
last changed them, the line the hunk starts at in the file now, that
commit's author name and email, the id of the commit the lines came from
originally, the path they had there, and the line they started at there.

### git-blame-get-hunk-byline
The hunk covering a line, counted from 1, as the same multifield.

### git-blame-free
Gives a blame back.

### git-note-read
The note attached to an object, by id, or `nil` when there is none. The
notes reference defaults to `refs/notes/commits`.

### git-note-create
Attaches a note to an object and answers the note blob's id. The author
and committer are signatures or `nil`; the notes reference is optional;
`TRUE` for the last argument replaces a note already there, which is
otherwise a refusal.

### git-note-remove
Removes the note on an object.

### git-note-default-ref
The notes reference in use: `refs/notes/commits` unless `core.notesRef`
says otherwise.

### git-worktree-list
The names of the repository's linked worktrees, as a multifield.

### git-worktree-lookup
A worktree by name. Freed with `git-worktree-free`.

### git-worktree-open-from-repository
The worktree a repository opened from one stands for. A repository that
is not a worktree is a refusal.

### git-worktree-add
Makes a linked worktree with a name at a path, checked out at a new branch
of that name, or at the reference given fourth. `TRUE` for the fifth
argument locks it.

### git-worktree-name
The name.

### git-worktree-path
The path of the worktree's working directory.

### git-worktree-validate
Checks the worktree is intact -- its directories are there and point at
each other -- and refuses when it is not.

### git-worktree-lock
Locks the worktree so that it is not pruned, with an optional reason.

### git-worktree-unlock
Unlocks it.

### git-worktree-is-locked
The reason the worktree is locked, as a string (empty when none was
given), or `FALSE` when it is not.

### git-worktree-is-prunable
Whether `git-worktree-prune` would prune it, with the same flags. A valid,
unlocked worktree is not prunable unless `GIT_WORKTREE_PRUNE_VALID` is
given.

### git-worktree-prune
Removes the worktree's record from the repository. `GIT_WORKTREE_PRUNE_VALID`
prunes one that is still intact, `GIT_WORKTREE_PRUNE_LOCKED` one that is
locked, and `GIT_WORKTREE_PRUNE_WORKING_TREE` removes the working directory
too.

### git-worktree-free
Gives a worktree back.

### git-submodule-list
The names of the repository's submodules, as a multifield.

### git-submodule-lookup
A submodule by name or path. Freed with `git-submodule-free`.

### git-submodule-add-setup
Begins adding a submodule: records it in `.gitmodules` and the
configuration, and makes a place for its repository. What follows is
`git-submodule-clone` to bring it in and `git-submodule-add-finalize` to
stage it. `FALSE` for the fourth argument puts the submodule's git
directory inside its working directory instead of under `.git/modules`.

### git-submodule-clone
Clones the submodule into its working directory from its URL and answers
the new repository. A URL that needs a credential gets one as described
under Credentials.

### git-submodule-repo-init
Makes an empty repository for a submodule in its place, for one that is
recorded but not yet cloned. `git-submodule-init` first, so that the URL
is in the configuration. `FALSE` for the second argument is as for
`git-submodule-add-setup`.

### git-submodule-add-finalize
Finishes adding a submodule: stages `.gitmodules` and the submodule's
commit.

### git-submodule-add-to-index
Stages the submodule's current commit. `TRUE` writes the index too.

### git-submodule-name
The name.

### git-submodule-path
The path within the superproject.

### git-submodule-url
The URL.

### git-submodule-branch
The branch the submodule tracks, or `nil`.

### git-submodule-head-id
The commit of the submodule recorded in the superproject's HEAD, or `nil`.

### git-submodule-index-id
The commit recorded in the superproject's index, or `nil`.

### git-submodule-wd-id
The commit checked out in the submodule's working directory, or `nil`.

### git-submodule-ignore
How much the superproject's status ignores about the submodule, as a
`GIT_SUBMODULE_IGNORE_*` symbol.

### git-submodule-status
The state of the submodule named, as a multifield of
`GIT_SUBMODULE_STATUS_*` symbols: where it is recorded
(`GIT_SUBMODULE_STATUS_IN_HEAD`, `_IN_INDEX`, `_IN_CONFIG`, `_IN_WD`) and
what differs. The optional last argument is how much to ignore.

### git-submodule-set-url
Sets the URL of the submodule named, in `.gitmodules`.

### git-submodule-init
Copies the submodule's URL and settings from `.gitmodules` into the
repository's configuration, which is what a clone needs before
`git-submodule-update`. `TRUE` overwrites settings already there.

### git-submodule-update
Brings the submodule's working directory to the commit the superproject
records, cloning it first if it is not there. `TRUE` initialises it as
`git-submodule-init` would first. A URL that needs a credential gets one as
described under Credentials.

### git-credential-hook
Names the CLIPS function that selects a credential when a remote asks for
one, or clears it with `nil`. Without an argument, answers the name, or
`nil`. What the function is called with, and what it answers, is under
Credentials.

### git-credential-prompt
Sets when the built-in prompt is used: `auto`, only when stdin is a
terminal; `always`, even on a pipe; or `never`. Without an argument,
answers the mode.

### git-credential-ask
Not a libgit2 function. Asks the hook and the prompt for a credential, as a
fetch from the URL would, and answers a multifield of the kind --
`userpass`, `username` or `ssh-key` -- and the username. The secret is not
answered. The optional username is the one a URL carries; the optional
kinds, a multifield of `GIT_CREDENTIAL_*` symbols, are what the remote
accepts, `GIT_CREDENTIAL_USERPASS_PLAINTEXT` if left out. For checking a
hook before going to the network.

### git-submodule-sync
Copies the URL from `.gitmodules` into the configuration and into the
submodule's own remote.

### git-submodule-reload
Reads the submodule's state again from the superproject. `TRUE` reloads
even when nothing seems to have changed.

### git-submodule-open
Opens the submodule's repository.

### git-submodule-free
Gives a submodule back.

### git-odb-exists
Whether an object with an id is in the database.

### git-odb-read
The raw content of an object as a string, without its header: a commit's
text, a blob's bytes.

### git-odb-read-header
The type and size of an object without reading it, as a multifield of a
`GIT_OBJECT_*` symbol and an integer.

### git-odb-write
Writes a string to the database as an object of a type and answers its id.

### git-odb-hash
The id a string would have as an object of a type, without writing it.

### git-odb-hashfile
The id a file would have, by path, without writing it and without filters.

### git-odb-refresh
Tells the database to look for packs and objects that arrived since it was
opened.

### git-odb-free
Gives the database back.
