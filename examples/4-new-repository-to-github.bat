; ======================================================================
; A new repository, a first commit, and a push to GitHub.
;
; GitHub shows this recipe for a repository that does not exist yet:
;
;   echo "# CLIPSgit" >> README.md
;   git init
;   git add README.md
;   git commit -m "first commit"
;   git branch -M main
;   git remote add origin https://github.com/mrryanjohnston/CLIPSgit.git
;   git push -u origin main
;
; This example does the same seven steps with the library, one function for
; each line. The examples run without a network. So the origin is a bare
; repository under examples/tmp that stands in for GitHub. Put the GitHub URL
; in ?*origin-url* and the program is the same. GitHub then asks for a
; username and a token, and the library asks you, as git does. See
; Credentials in API.md.
;
;   CLIPSgit -f2 examples/4-new-repository-to-github.bat
; ======================================================================

(defglobal ?*nl* = (format nil "%n"))
(defglobal ?*origin-url* = "")

; The stand-in for GitHub. A push over a path goes only into a bare
; repository, which is what GitHub keeps.
(deffunction make-origin ()
  (bind ?bare (git-repository-init "examples/tmp/github/CLIPSgit.git" TRUE))
  (bind ?*origin-url* (git-repository-path ?bare))
  (git-repository-free ?bare))

; echo "# CLIPSgit" >> README.md
(deffunction write-readme (?repo)
  (open (str-cat (git-repository-workdir ?repo) "README.md") f "w")
  (printout f "# CLIPSgit" ?*nl*)
  (close f))

; git add README.md
; git commit -m "first commit"
;
; The signature is given, so that the output is the same on every machine.
; nil in its place takes user.name and user.email from the configuration,
; as git does.
(deffunction first-commit (?repo)
  (bind ?idx (git-repository-index ?repo))
  (git-index-add-bypath ?idx "README.md")
  (git-index-write ?idx)
  (bind ?tree (git-tree-lookup ?repo (git-index-write-tree ?idx)))
  (git-index-free ?idx)
  (bind ?sig (git-signature-now "Ryan Johnston" "ryan@example.com"))
  (bind ?id (git-commit-create ?repo "HEAD" ?sig ?sig "first commit" ?tree (create$)))
  (git-signature-free ?sig)
  (git-tree-free ?tree)
  ?id)

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

(deffunction main ()
  (system "rm -rf examples/tmp && mkdir -p examples/tmp")
  (make-origin)

  ; git init
  ;
  ; git makes the branch that init.defaultBranch names, or master. The example
  ; says master, so that the rename below has something to do.
  (bind ?repo (git-repository-init-ext "examples/tmp/CLIPSgit" GIT_REPOSITORY_INIT_MKPATH "master"))
  (println "init:   " (git-repository-workdir ?repo))

  (write-readme ?repo)
  (first-commit ?repo)
  (println "commit: first commit, on " (head-branch ?repo))

  (rename-branch ?repo "master" "main")
  (println "branch: " (head-branch ?repo))

  ; git remote add origin <url>
  (git-remote-free (git-remote-create ?repo "origin" ?*origin-url*))
  (println "remote: origin is " ?*origin-url*)

  (push-upstream ?repo "origin" "main")
  (println "push:   main tracks " (git-branch-upstream-name ?repo "refs/heads/main"))

  ; What the push left on the other side.
  (bind ?github (git-repository-open ?*origin-url*))
  (bind ?pushed (git-commit-lookup ?github (git-reference-name-to-id ?github "refs/heads/main")))
  (println "origin has main at " (git-object-short-id ?pushed) ": " (git-commit-summary ?pushed))
  (git-commit-free ?pushed)
  (git-repository-free ?github)

  (git-repository-free ?repo)
  (system "rm -rf examples/tmp")
  (println "done"))

(main)
(exit)
