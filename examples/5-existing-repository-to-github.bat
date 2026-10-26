; ======================================================================
; An existing repository, pushed to GitHub.
;
; GitHub shows this recipe for a repository that already has commits:
;
;   git remote add origin https://github.com/mrryanjohnston/CLIPSgit.git
;   git branch -M main
;   git push -u origin main
;
; This example first makes a repository with some history on master, the
; kind of repository that the recipe is for. Then it does the three steps
; with the library. The examples run without a network. So the origin is a
; bare repository under examples/tmp that stands in for GitHub. Put the
; GitHub URL in ?*origin-url* and the program is the same. GitHub then asks
; for a username and a token, and the library asks you, as git does. See
; Credentials in API.md.
;
;   CLIPSgit -f2 examples/5-existing-repository-to-github.bat
; ======================================================================

(defglobal ?*nl* = (format nil "%n"))
(defglobal ?*origin-url* = "")

; The stand-in for GitHub. A push over a path goes only into a bare
; repository, which is what GitHub keeps.
(deffunction make-origin ()
  (bind ?bare (git-repository-init "examples/tmp/github/CLIPSgit.git" TRUE))
  (bind ?*origin-url* (git-repository-path ?bare))
  (git-repository-free ?bare))

; ----------------------------------------------------------------------
; The existing repository: three commits on master, by one person, and no
; remote. In practice this is the repository that you already have.
; ----------------------------------------------------------------------

(deffunction write-file (?repo ?name ?content)
  (open (str-cat (git-repository-workdir ?repo) ?name) f "w")
  (printout f ?content ?*nl*)
  (close f))

(deffunction commit-all (?repo ?message)
  (bind ?idx (git-repository-index ?repo))
  (git-index-add-all ?idx (create$ "*"))
  (git-index-write ?idx)
  (bind ?tree (git-tree-lookup ?repo (git-index-write-tree ?idx)))
  (git-index-free ?idx)
  (bind ?parents (create$))
  (if (not (git-repository-head-unborn ?repo))
    then (bind ?parents (create$ (git-commit-lookup ?repo (git-reference-name-to-id ?repo "HEAD")))))
  (bind ?sig (git-signature-now "Ryan Johnston" "ryan@example.com"))
  (bind ?id (git-commit-create ?repo "HEAD" ?sig ?sig ?message ?tree ?parents))
  (git-signature-free ?sig)
  (git-tree-free ?tree)
  (progn$ (?p ?parents) (git-commit-free ?p))
  ?id)

(deffunction existing-repository ()
  (bind ?repo (git-repository-init-ext "examples/tmp/CLIPSgit" GIT_REPOSITORY_INIT_MKPATH "master"))
  (write-file ?repo "README.md" "# CLIPSgit")
  (commit-all ?repo "first commit")
  (write-file ?repo "userfunctions.c" "/* the bindings */")
  (commit-all ?repo "bind libgit2")
  (write-file ?repo "makefile" "all: clips")
  (commit-all ?repo "build it")
  ?repo)

; ----------------------------------------------------------------------
; The three steps.
; ----------------------------------------------------------------------

; git branch -M main
;
; The rename moves HEAD with the branch. TRUE replaces a branch that already
; has the new name, which is what -M adds to -m.
(deffunction rename-branch (?repo ?from ?to)
  (bind ?old (git-branch-lookup ?repo ?from))
  (bind ?new (git-branch-move ?old ?to TRUE))
  (git-reference-free ?old)
  (git-reference-free ?new))

; git push -u origin main
;
; The push updates origin/main in this repository too. Then -u is one more
; call: main tracks origin/main from now on.
(deffunction push-upstream (?repo ?remote ?branch)
  (bind ?origin (git-remote-lookup ?repo ?remote))
  (bind ?spec (str-cat "refs/heads/" ?branch ":refs/heads/" ?branch))
  (git-remote-push ?origin (create$ ?spec))
  (git-remote-free ?origin)
  (bind ?ref (git-branch-lookup ?repo ?branch))
  (git-branch-set-upstream ?ref (str-cat ?remote "/" ?branch))
  (git-reference-free ?ref))

; The short name of the branch HEAD is on.
(deffunction head-branch (?repo)
  (bind ?head (git-repository-head ?repo))
  (bind ?name (git-branch-name ?head))
  (git-reference-free ?head)
  ?name)

; How many commits a branch of a repository has.
(deffunction count-commits (?repo ?refname)
  (bind ?walk (git-revwalk-new ?repo))
  (git-revwalk-push-ref ?walk ?refname)
  (bind ?n 0)
  (while (git-revwalk-next ?walk) (bind ?n (+ ?n 1)))
  (git-revwalk-free ?walk)
  ?n)

(deffunction main ()
  (system "rm -rf examples/tmp && mkdir -p examples/tmp")
  (make-origin)
  (bind ?repo (existing-repository))
  (println "before: " (count-commits ?repo "HEAD") " commits on " (head-branch ?repo)
           ", remotes " (git-remote-list ?repo))

  ; git remote add origin <url>
  (git-remote-free (git-remote-create ?repo "origin" ?*origin-url*))
  (println "remote: origin is " ?*origin-url*)

  (rename-branch ?repo "master" "main")
  (println "branch: " (head-branch ?repo))

  (push-upstream ?repo "origin" "main")
  (println "push:   main tracks " (git-branch-upstream-name ?repo "refs/heads/main"))

  ; What the push left on the other side: the whole history.
  (bind ?github (git-repository-open ?*origin-url*))
  (println "origin has " (count-commits ?github "refs/heads/main") " commits on main")
  (git-repository-free ?github)

  (git-repository-free ?repo)
  (system "rm -rf examples/tmp")
  (println "done"))

(main)
(exit)
