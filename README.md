# CLIPSgit

CLIPSgit is a git library for CLIPS. It gives CLIPS programs access to
[libgit2](https://libgit2.org).

```clips
(deftemplate commit (slot id) (slot summary) (slot author-name) (multislot parents))

(defrule merge-commit
  (commit (summary ?s) (parents ?a ?b $?))
  =>
  (println "a merge: " ?s))

(bind ?repo (git-repository-open "."))
(bind ?walk (git-revwalk-new ?repo))
(git-revwalk-push-head ?walk)

(while (bind ?id (git-revwalk-next ?walk))
  (bind ?c (git-commit-lookup ?repo ?id))
  (git-commit-to-fact ?c commit)
  (git-commit-free ?c))

(git-revwalk-free ?walk)
(git-repository-free ?repo)
(run)
```

[API.md](API.md) lists every function. The [examples](examples/) directory
has complete example programs.

CLIPSgit supports **libgit2 1.8** and **1.9**. The build links libgit2
statically. The binary does not need an installed libgit2.

## Requirements

You need these tools:

- `curl` or `wget`
- `tar`
- `cmake`
- a C compiler
- a TLS library for the HTTPS transport. On Debian and Ubuntu, install
  `libssl-dev`. On Fedora, install `openssl-devel`. On macOS, no extra
  package is necessary.
- an ssh client on PATH, at run time, for a remote over ssh. The build does
  not need it.

A build against a CLIPS development branch also needs `svn`. The default
build does not.

## Build

```
make
```

To fetch and build libgit2 without CLIPS:

```
make libgit2
```

Other `make` variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `LIBGIT2_VERSION` | `1.9.7` | The libgit2 release to download and build |
| `LIBGIT2_SHA256` | the digest of that release | The makefile checks the digest before it unpacks the tarball |
| `LIBGIT2_SYSTEM` | not set | Set `1` to link the libgit2 that is installed on this machine |
| `LIBGIT2_HTTPS` | `ON` | Set `OFF` to build without a TLS library and without HTTPS |
| `LIBGIT2_CMAKE_OPTS` | see the makefile | The CMake options for the vendored build |
| `CLIPS_VERSION` | `6.4.2` | The CLIPS to build against: `6.4.2`, `svn-6x`, or `svn-7x` |
| `CLIPS_SVN_REV` | the pinned revision | The revision of a branch to build, or `HEAD` |
| `PREFIX` | `$(HOME)/.local` | The install root for `make install` |
| `VERSION` | `0.1.0` | The version of this library, used in the installed file names |

`make help` lists the targets.

### Other CLIPS versions

CLIPSgit builds against the CLIPS 6.4.2 release and against the two
development branches:

```
make                          # 6.4.2, the release tarball (the default)
make CLIPS_VERSION=svn-6x     # branches/64x, the 6.4 maintenance branch
make CLIPS_VERSION=svn-7x     # branches/70x, which adds deftable and goals
make CLIPS_VERSION=svn-7x CLIPS_SVN_REV=980
make CLIPS_VERSION=svn-7x CLIPS_SVN_REV=HEAD
```

### Other libgit2 versions

```
make LIBGIT2_VERSION=1.8.7
```

The makefile has a digest for these versions. The tests run against these
versions:

| | | |
| --- | --- | --- |
| 1.8.7 | 1.9.6 | 1.9.7 |

To build a version that is not in the table, give its digest:

```
sha256sum libgit2-1.9.8.tar.gz
make LIBGIT2_VERSION=1.9.8 LIBGIT2_SHA256=<that digest>
```

## Test

```
make test
```

`tests/run.sh` runs the in-process suite and then the examples. It checks
each example against the `.expected` file next to it.

| | |
| --- | --- |
| `make test` | the complete suite and the examples |
| `make test-suite` | only the in-process suite |
| `make test-examples` | only the examples |
| `make test-valgrind` | the suite under valgrind |
| `make test-versions` | the suite against every supported libgit2 |
| `make test-clips` | the suite against all three CLIPS versions |
| `make coverage` | line coverage of `userfunctions.c` |
| `make docs-check` | fails if `API.md` is not current |

## Install

```
make install
```

This installs into `$HOME/.local` and needs no special permissions.

For a system-wide location, give the prefix:

```
sudo make install PREFIX=/usr/local
```

`make uninstall` removes the installed files.

## License

MIT. See [LICENSE](LICENSE).
