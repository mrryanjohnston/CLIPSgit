; ======================================================================
; Open, create, and inspect repositories.
; ======================================================================

(println crlf "repository-test-suite")

(deffunction repository-suite ()
  ; git_repository_is_empty needs an unborn HEAD on the default branch name.
  ; make-repo sets its own branch name. So only a plain init is empty here.
  (bind ?plain (git-repository-init "tests/tmp/plain"))
  (expect-true "git-repository-init answers a handle" (external-addressp ?plain))
  (expect "a new repository is empty" TRUE (git-repository-is-empty ?plain))
  (git-repository-free ?plain)
  (bind ?repo (make-repo "repo"))
  (expect "and not bare" FALSE (git-repository-is-bare ?repo))
  (expect "and not shallow" FALSE (git-repository-is-shallow ?repo))
  (expect "and not a worktree" FALSE (git-repository-is-worktree ?repo))
  (expect "its head is unborn" TRUE (git-repository-head-unborn ?repo))
  (expect "and not detached" FALSE (git-repository-head-detached ?repo))
  (expect "its state is none" GIT_REPOSITORY_STATE_NONE (git-repository-state ?repo))
  (expect-true "git-repository-path ends in .git/"
               (eq (sub-string (- (str-length (git-repository-path ?repo)) 4)
                               (str-length (git-repository-path ?repo))
                               (git-repository-path ?repo))
                   ".git/"))
  (expect-true "git-repository-workdir is a directory"
               (> (str-length (git-repository-workdir ?repo)) 0))
  (expect "git-repository-commondir is the git directory here"
          (git-repository-path ?repo) (git-repository-commondir ?repo))
  (expect "git-repository-message is nil when there is none" nil (git-repository-message ?repo))
  (expect "git-repository-get-namespace is nil by default" nil (git-repository-get-namespace ?repo))
  (expect "git-repository-set-namespace takes one" TRUE (git-repository-set-namespace ?repo "test"))
  (expect "and it reads back" "test" (git-repository-get-namespace ?repo))
  (git-repository-set-namespace ?repo nil)
  (expect-true "git-repository-item-path finds the index"
               (stringp (git-repository-item-path ?repo GIT_REPOSITORY_ITEM_INDEX)))
  (expect "git-repository-ident is unset by default" (create$ nil nil) (git-repository-ident ?repo))
  (expect "git-repository-set-ident sets it" TRUE (git-repository-set-ident ?repo "Ident" "ident@example.com"))
  (expect "and it reads back" (create$ "Ident" "ident@example.com") (git-repository-ident ?repo))

  ; A repository with a commit is no longer empty and has a head.
  (bind ?id (commit-file ?repo "a.txt" "a" "first"))
  (expect "after a commit it is not empty" FALSE (git-repository-is-empty ?repo))
  (expect "and its head is born" FALSE (git-repository-head-unborn ?repo))
  (bind ?head (git-repository-head ?repo))
  (expect "git-repository-head is the main branch" "refs/heads/main" (git-reference-name ?head))
  (expect "pointing at the commit" ?id (git-reference-target ?head))
  (git-reference-free ?head)

  ; hash a file in the same way as the repository
  (write-file ?repo "h.txt" (str-cat "hello" ?*nl*))
  (expect "git-repository-hashfile hashes a file as a blob"
          "ce013625030ba8dba906f756967f9e9ca394464a"
          (git-repository-hashfile ?repo "h.txt"))

  ; detach the head and attach it again
  (expect "git-repository-detach-head detaches" TRUE (git-repository-detach-head ?repo))
  (expect "and the head is detached" TRUE (git-repository-head-detached ?repo))
  (expect "git-repository-set-head reattaches" TRUE (git-repository-set-head ?repo "refs/heads/main"))
  (expect "and it is not" FALSE (git-repository-head-detached ?repo))
  (expect "git-repository-set-head-detached takes an id" TRUE (git-repository-set-head-detached ?repo ?id))
  (expect "and detaches" TRUE (git-repository-head-detached ?repo))
  (git-repository-set-head ?repo "refs/heads/main")

  (expect "git-repository-state-cleanup on a clean repository succeeds" TRUE
          (git-repository-state-cleanup ?repo))
  (expect "git-repository-free gives it back" TRUE (git-repository-free ?repo))

  ; Open the new repository.
  (bind ?again (git-repository-open "tests/tmp/repo"))
  (expect-true "git-repository-open opens it again" (external-addressp ?again))
  (expect "and it remembers its commit" ?id (git-reference-name-to-id ?again "HEAD"))
  (git-repository-free ?again)

  ; Discovery from inside the working directory.
  (system "mkdir -p tests/tmp/repo/sub/dir")
  (bind ?found (git-repository-discover "tests/tmp/repo/sub/dir"))
  (expect-true "git-repository-discover walks up to the repository"
               (and (stringp ?found) (> (str-length ?found) 0)))
  (bind ?ext (git-repository-open-ext "tests/tmp/repo/sub/dir"))
  (expect-true "git-repository-open-ext searches upward" (external-addressp ?ext))
  (git-repository-free ?ext)
  (bind ?r (git-repository-open-ext "tests/tmp/repo/sub/dir" (create$ GIT_REPOSITORY_OPEN_NO_SEARCH)))
  (expect "and refuses to with GIT_REPOSITORY_OPEN_NO_SEARCH" FALSE ?r)

  ; A working directory in a different location.
  (bind ?bare (git-repository-init "tests/tmp/detached-git" TRUE))
  (system "mkdir -p tests/tmp/detached-work")
  (expect "git-repository-set-workdir points a repository at a directory" TRUE
          (git-repository-set-workdir ?bare "tests/tmp/detached-work" FALSE))
  (expect "and it is no longer bare" FALSE (git-repository-is-bare ?bare))
  (expect-true "with that working directory"
               (> (str-index "detached-work" (git-repository-workdir ?bare)) 0))
  (git-repository-free ?bare)

  ; A bare repository.
  (bind ?bare (git-repository-init "tests/tmp/bare" TRUE))
  (expect "git-repository-init makes a bare repository" TRUE (git-repository-is-bare ?bare))
  (expect "which has no working directory" nil (git-repository-workdir ?bare))
  (git-repository-free ?bare)
  (bind ?bare (git-repository-open-bare "tests/tmp/bare"))
  (expect-true "git-repository-open-bare opens it" (external-addressp ?bare))
  (git-repository-free ?bare)

  ; The extended init, with a description and an initial branch name.
  (bind ?ext (git-repository-init-ext "tests/tmp/ext" (create$ GIT_REPOSITORY_INIT_MKPATH) "trunk" "a description"))
  (expect-true "git-repository-init-ext makes a repository" (external-addressp ?ext))
  (commit-file ?ext "x" "x" "x")
  (bind ?head (git-repository-head ?ext))
  (expect "with the initial head it was given" "refs/heads/trunk" (git-reference-name ?head))
  (git-reference-free ?head)
  (git-repository-free ?ext)

  ; A repository that does not exist.
  (bind ?r (git-repository-open "tests/tmp/nowhere"))
  (expect "git-repository-open refuses a path that is not a repository" FALSE ?r)
  (expect-true "and git-error-last says why" (stringp (git-error-last))))

(repository-suite)
