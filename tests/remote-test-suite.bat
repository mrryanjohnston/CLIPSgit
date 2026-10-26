; ======================================================================
; Remotes, refspecs, fetch, push, and clone. All tests use repositories under
; tests/tmp, by path. No test uses the network.
; ======================================================================

(println crlf "remote-test-suite")

(deffunction remote-suite ()
  (bind ?origin (make-repo "origin"))
  (bind ?first (commit-file ?origin "a.txt" (str-cat "a" ?*nl*) "first"))
  (bind ?origin-path (git-repository-workdir ?origin))

  (bind ?repo (make-repo "local"))
  (expect "a new repository has no remotes" (create$) (git-remote-list ?repo))
  (bind ?remote (git-remote-create ?repo "origin" ?origin-path))
  (expect "git-remote-create makes one" (create$ "origin") (git-remote-list ?repo))
  (expect "git-remote-name is the name" "origin" (git-remote-name ?remote))
  (expect "git-remote-url is the url" ?origin-path (git-remote-url ?remote))
  (expect "git-remote-pushurl is nil until set" nil (git-remote-pushurl ?remote))
  (expect "git-remote-refspec-count is one" 1 (git-remote-refspec-count ?remote))
  (expect "git-remote-get-fetch-refspecs is the default"
          (create$ "+refs/heads/*:refs/remotes/origin/*") (git-remote-get-fetch-refspecs ?remote))
  (expect "git-remote-get-push-refspecs is empty" (create$) (git-remote-get-push-refspecs ?remote))
  (expect "git-remote-connected is false before connecting" FALSE (git-remote-connected ?remote))
  (expect "git-remote-autotag is the default" GIT_REMOTE_DOWNLOAD_TAGS_AUTO (git-remote-autotag ?remote))
  (expect "git-remote-prune-refs is false" FALSE (git-remote-prune-refs ?remote))

  (bind ?spec (git-remote-get-refspec ?remote 0))
  (expect "git-remote-get-refspec answers a refspec" "refs/heads/*" (git-refspec-src ?spec))
  (expect "git-refspec-dst is the other side" "refs/remotes/origin/*" (git-refspec-dst ?spec))
  (expect "git-refspec-string is the whole thing" "+refs/heads/*:refs/remotes/origin/*" (git-refspec-string ?spec))
  (expect "git-refspec-force is true for a + spec" TRUE (git-refspec-force ?spec))
  (expect "git-refspec-direction is fetch" GIT_DIRECTION_FETCH (git-refspec-direction ?spec))
  (expect "git-refspec-src-matches matches a branch" TRUE (git-refspec-src-matches ?spec "refs/heads/main"))
  (expect "git-refspec-dst-matches matches a tracking branch" TRUE (git-refspec-dst-matches ?spec "refs/remotes/origin/main"))
  (expect "git-refspec-transform maps across" "refs/remotes/origin/main" (git-refspec-transform ?spec "refs/heads/main"))
  (expect "git-refspec-rtransform maps back" "refs/heads/main" (git-refspec-rtransform ?spec "refs/remotes/origin/main"))
  (bind ?r (git-refspec-free ?spec))
  (expect "a refspec borrowed from a remote is not freed on its own" FALSE ?r)
  (bind ?r (git-remote-get-refspec ?remote 5))
  (expect "and one that is not there is refused" FALSE ?r)

  ; Fetch over a path.
  (expect "git-remote-connect connects to a path" TRUE (git-remote-connect ?remote))
  (expect "and git-remote-connected says so" TRUE (git-remote-connected ?remote))
  (bind ?ls (git-remote-ls ?remote))
  (expect-true "git-remote-ls lists what the remote has" (member$ "refs/heads/main" ?ls))
  (expect "git-remote-disconnect disconnects" TRUE (git-remote-disconnect ?remote))
  (expect "git-remote-fetch fetches" TRUE (git-remote-fetch ?remote))
  (expect "and the tracking branch appears" (create$ "origin/main") (git-branch-list ?repo GIT_BRANCH_REMOTE))
  (expect "pointing at the commit" ?first (git-reference-name-to-id ?repo "refs/remotes/origin/main"))
  (expect "git-remote-fetch takes refspecs and a message" TRUE
          (git-remote-fetch ?remote (create$ "+refs/heads/main:refs/remotes/origin/again") "fetched again"))
  (expect "git-remote-update-tips succeeds" TRUE (git-remote-update-tips ?remote))
  (expect "git-remote-prune succeeds" TRUE (git-remote-prune ?remote))
  (expect "git-remote-stop succeeds" TRUE (git-remote-stop ?remote))

  ; A branch from the fetched commit, an upstream, and a push back.
  (bind ?c (git-commit-lookup ?repo ?first))
  (bind ?main (git-branch-create ?repo "main" ?c))
  (git-commit-free ?c)
  (git-repository-set-head ?repo "refs/heads/main")
  (expect "git-branch-set-upstream takes a remote branch" TRUE (git-branch-set-upstream ?main "origin/main"))
  (expect "git-branch-upstream-remote names the remote" "origin" (git-branch-upstream-remote ?repo "refs/heads/main"))
  (expect "git-branch-remote-name names a tracking branch's remote" "origin"
          (git-branch-remote-name ?repo "refs/remotes/origin/main"))
  (git-reference-free ?main)
  (git-checkout-head ?repo (create$ GIT_CHECKOUT_FORCE))
  (bind ?second (commit-file ?repo "b.txt" (str-cat "b" ?*nl*) "second"))
  ; libgit2 pushes over a path only into a bare repository.
  (bind ?bare (git-clone ?origin-path "tests/tmp/origin-bare" TRUE))
  (bind ?bare-path (git-repository-path ?bare))
  (bind ?pushto (git-remote-create ?repo "bare" ?bare-path))
  (expect "git-remote-push pushes a branch" TRUE
          (git-remote-push ?pushto (create$ "refs/heads/main:refs/heads/pushed")))
  (expect "and the bare repository has it" ?second (git-reference-name-to-id ?bare "refs/heads/pushed"))
  (bind ?r (git-remote-push ?remote (create$ "refs/heads/main:refs/heads/pushed")))
  (expect "and refuses to push into a repository with a working directory" FALSE ?r)
  (git-remote-free ?pushto)
  (git-remote-delete ?repo "bare")
  (expect "git-graph-ahead-behind counts" (create$ 1 0) (git-graph-ahead-behind ?repo ?second ?first))
  (expect "git-graph-descendant-of knows" TRUE (git-graph-descendant-of ?repo ?second ?first))
  (expect "and the other way round" FALSE (git-graph-descendant-of ?repo ?first ?second))

  ; Configure remotes by name.
  (expect "git-remote-set-url sets the url" TRUE (git-remote-set-url ?repo "origin" ?origin-path))
  (expect "git-remote-set-pushurl sets the push url" TRUE (git-remote-set-pushurl ?repo "origin" ?origin-path))
  (expect "git-remote-add-fetch adds a refspec" TRUE (git-remote-add-fetch ?repo "origin" "+refs/tags/*:refs/tags/*"))
  (expect "git-remote-add-push adds one too" TRUE (git-remote-add-push ?repo "origin" "refs/heads/main:refs/heads/main"))
  (git-remote-free ?remote)
  (bind ?remote (git-remote-lookup ?repo "origin"))
  (expect "git-remote-lookup sees the push url" ?origin-path (git-remote-pushurl ?remote))
  (expect "and the refspecs" 3 (git-remote-refspec-count ?remote))
  (bind ?dup (git-remote-dup ?remote))
  (expect "git-remote-dup copies it" "origin" (git-remote-name ?dup))
  (git-remote-free ?dup)
  (git-remote-free ?remote)
  ; libgit2 does not correctly rename a remote that has more than one fetch
  ; refspec. So the test renames a remote that still has the default.
  (bind ?plain (git-remote-create ?repo "plain" ?origin-path))
  (git-remote-free ?plain)
  (expect "git-remote-rename renames one" (create$) (git-remote-rename ?repo "plain" "renamed"))
  (expect "and the list agrees" (create$ "origin" "renamed") (git-remote-list ?repo))
  (expect "git-remote-delete removes one" TRUE (git-remote-delete ?repo "renamed"))
  (expect "git-remote-delete removes the other" TRUE (git-remote-delete ?repo "origin"))
  (expect "leaving none" (create$) (git-remote-list ?repo))
  (expect "git-remote-is-valid-name accepts a good name" TRUE (git-remote-is-valid-name "origin"))
  (expect "and refuses a bad one" FALSE (git-remote-is-valid-name "bad name"))
  (bind ?r (git-remote-lookup ?repo "origin"))
  (expect "git-remote-lookup refuses one that is gone" FALSE ?r)

  ; Anonymous, detached, and a fetch refspec on creation.
  (bind ?anon (git-remote-create-anonymous ?repo ?origin-path))
  (expect "git-remote-create-anonymous has no name" nil (git-remote-name ?anon))
  (git-remote-free ?anon)
  (bind ?detached (git-remote-create-detached ?origin-path))
  (expect "git-remote-create-detached has the url" ?origin-path (git-remote-url ?detached))
  (git-remote-free ?detached)
  (bind ?with (git-remote-create ?repo "with" ?origin-path "+refs/heads/main:refs/remotes/with/only"))
  (expect "git-remote-create takes a fetch refspec"
          (create$ "+refs/heads/main:refs/remotes/with/only") (git-remote-get-fetch-refspecs ?with))
  (git-remote-free ?with)

  ; Parse refspecs on their own.
  (bind ?spec (git-refspec-parse "refs/heads/a:refs/heads/b" TRUE))
  (expect "git-refspec-parse reads one" "refs/heads/a" (git-refspec-src ?spec))
  (expect "which is not forced" FALSE (git-refspec-force ?spec))
  (expect "and is freed on its own" TRUE (git-refspec-free ?spec))
  (bind ?r (git-refspec-parse "not a refspec at all" FALSE))
  (expect "and refuses nonsense" FALSE ?r)

  ; Clone.
  (bind ?clone (git-clone ?origin-path "tests/tmp/clone"))
  (expect-true "git-clone clones a path" (external-addressp ?clone))
  (expect "with the commit" ?first (git-reference-name-to-id ?clone "HEAD"))
  (expect "and an origin" (create$ "origin") (git-remote-list ?clone))
  (git-repository-free ?clone)
  (bind ?bare2 (git-clone ?bare-path "tests/tmp/clone-bare" TRUE "pushed"))
  (expect "git-clone takes bare and a branch" TRUE (git-repository-is-bare ?bare2))
  (expect "checked out at that branch" ?second (git-reference-name-to-id ?bare2 "HEAD"))
  (git-repository-free ?bare2)
  (git-repository-free ?bare)
  (bind ?r (git-clone "tests/tmp/nowhere" "tests/tmp/clone-none"))
  (expect "git-clone refuses a source that is not there" FALSE ?r)

  (git-repository-free ?repo)
  (git-repository-free ?origin))

(remote-suite)
