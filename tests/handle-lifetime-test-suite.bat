; ======================================================================
; What happens to a handle when the object that it points to is freed.
;
; This library records every handle that it gives out. A free of a repository
; first frees every handle that came from it. The libgit2 objects point back
; into their repository and must not remain after it. A handle that is freed,
; by its own free or by the free of its repository, is rejected everywhere.
; ======================================================================

(println crlf "handle-lifetime-test-suite")

(deffunction lifetime-suite ()
  (bind ?repo (make-repo "lifetime"))
  (bind ?id (commit-file ?repo "a" "a" "first"))
  (bind ?start (git-handle-count))

  ; After two lookups of one commit, the caller must make two frees.
  (bind ?c1 (git-commit-lookup ?repo ?id))
  (bind ?c2 (git-commit-lookup ?repo ?id))
  (expect "libgit2 caches objects: two lookups are one pointer" ?c1 ?c2)
  (expect "which is one handle" (+ ?start 1) (git-handle-count))
  (expect "the first free succeeds" TRUE (git-commit-free ?c1))
  (expect-true "and the pointer is still live" (stringp (git-commit-id ?c2)))
  (expect "the second free succeeds" TRUE (git-commit-free ?c2))
  (bind ?r (git-commit-id ?c2))
  (expect "and now it is refused" FALSE ?r)
  (expect "with the table back where it was" ?start (git-handle-count))

  ; A commit that is still open when its repository is freed is freed with it.
  (bind ?c (git-commit-lookup ?repo ?id))
  (bind ?tree (git-commit-tree ?c))
  (bind ?entry (git-tree-entry-byindex ?tree 0))
  (bind ?author (git-commit-author ?c))
  (bind ?walk (git-revwalk-new ?repo))
  (bind ?idx (git-repository-index ?repo))
  (bind ?cfg (git-repository-config ?repo))
  (bind ?kinds (git-handle-kinds))
  (expect "git-handle-kinds lists every one of them" 8 (length$ ?kinds))
  (expect-true "naming a borrowed one after its owner" (member$ "tree entry (tree)" ?kinds))
  (expect-true "and the author likewise" (member$ "signature (commit)" ?kinds))
  (expect "git-repository-free frees everything that came from it" TRUE (git-repository-free ?repo))
  (expect "and nothing is left" 0 (git-handle-count))
  (bind ?r (git-commit-id ?c))
  (expect "the commit is refused" FALSE ?r)
  (bind ?r (git-tree-entrycount ?tree))
  (expect "the tree is refused" FALSE ?r)
  (bind ?r (git-tree-entry-name ?entry))
  (expect "the entry is refused" FALSE ?r)
  (bind ?r (git-signature-name ?author))
  (expect "the author is refused" FALSE ?r)
  (bind ?r (git-revwalk-next ?walk))
  (expect "the revwalk is refused" FALSE ?r)
  (bind ?r (git-index-entrycount ?idx))
  (expect "the index is refused" FALSE ?r)
  (bind ?r (git-config-get-string ?cfg "user.name"))
  (expect "the config is refused" FALSE ?r)
  (bind ?r (git-commit-free ?c))
  (expect "and freeing the commit again is refused too" FALSE ?r)

  ; A signature that is made outside a repository is independent of all
  ; repositories.
  (bind ?repo (make-repo "lifetime"))
  (bind ?sig (git-signature-now "A" "a@b.c"))
  (git-repository-free ?repo)
  (expect "a free-standing signature survives a repository" "A" (git-signature-name ?sig))
  (git-signature-free ?sig)

  ; Handles have types. A tree in the place of a commit is rejected by name.
  (bind ?repo (make-repo "lifetime"))
  (bind ?id (commit-file ?repo "a" "a" "first"))
  (bind ?c (git-commit-lookup ?repo ?id))
  (bind ?tree (git-commit-tree ?c))
  (bind ?r (git-commit-summary ?tree))
  (expect "a tree is refused where a commit belongs" FALSE ?r)
  (bind ?r (git-tree-entrycount ?c))
  (expect "and a commit where a tree belongs" FALSE ?r)
  (bind ?r (git-repository-path ?c))
  (expect "and a commit where a repository belongs" FALSE ?r)
  (expect "but git-object-id takes either" ?id (git-object-id ?c))
  (expect "and git-object-free frees either" TRUE (git-object-free ?tree))
  (git-commit-free ?c)

  ; A shutdown of the library frees every open handle. The library still works
  ; after it, because UserFunctions made an initialization of its own that
  ; this shutdown does not release.
  (bind ?c (git-commit-lookup ?repo ?id))
  (git-libgit2-init)
  (expect-true "git-libgit2-shutdown answers a count" (integerp (git-libgit2-shutdown)))
  (expect "and has freed every handle" 0 (git-handle-count))
  (bind ?r (git-commit-id ?c))
  (expect "so the commit is refused" FALSE ?r)
  (bind ?again (git-repository-open "tests/tmp/lifetime"))
  (expect-true "and the library still works" (external-addressp ?again))
  (git-repository-free ?again))

(lifetime-suite)
