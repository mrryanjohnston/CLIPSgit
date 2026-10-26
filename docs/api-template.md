# CLIPSgit API Reference

## The library

<!-- functions -->

## Repositories

<!-- functions -->

## Objects

<!-- functions -->

## Signatures

<!-- functions -->

## Commits

<!-- functions -->

## Walking history

<!-- functions -->

## Trees

<!-- functions -->

## Blobs

<!-- functions -->

## Tags

<!-- functions -->

## References

<!-- functions -->

## Branches

<!-- functions -->

## The index

<!-- functions -->

## Configuration

<!-- functions -->

## Remotes

<!-- functions -->

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

<!-- functions -->

## Status

<!-- functions -->

## Diffs and patches

<!-- functions -->

## Checkout and reset

<!-- functions -->

## Revision parsing

<!-- functions -->

## Merging

<!-- functions -->

## Stashes

<!-- functions -->

## Blame

<!-- functions -->

## Notes

<!-- functions -->

## Worktrees

<!-- functions -->

## Submodules

<!-- functions -->

## The object database

<!-- functions -->

## Not exposed

Deliberately left out, and why:

| | |
| --- | --- |
| `git_odb_backend_*`, `git_refdb_backend_*`, custom transports, filters and merge drivers | They are ways of extending libgit2 with C code, not of using it. |
| `git_mailmap_*`, `git_message_*`, `git_pathspec_*`, `git_packbuilder_*`, `git_indexer_*`, `git_transaction_*`, `git_rebase_*`, `git_email_*` | Not in this first release. The bookkeeping that makes the rest safe extends to them, and they are the natural next additions. |
| `git_libgit2_opts` | A grab-bag of global settings, most of them for embedding. |
| `git_blob_create_from_stream`, `git_odb_open_wstream` | Streams have no representation in CLIPS; `git-blob-create-from-buffer` and `git-odb-write` take the whole content. |
| `git_diff_foreach`, `git_diff_print`, `git_tree_walk` with a callback, `git_stash_foreach`, `git_config_foreach` | Callbacks. Each is here as a function that collects into a multifield or fills facts instead. |
