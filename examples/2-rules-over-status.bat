; ======================================================================
; Rules over the working directory.
;
; git status is a list of paths, with the change to each path. As facts, rules
; can reason about those changes: which changes are safe to commit on their
; own, which need a review, and what to do about each. Here the rules decide,
; and the rule that decides to commit makes the commit.
;
;   CLIPSgit -f2 examples/2-rules-over-status.bat
; ======================================================================

(deftemplate change
  (slot path)
  (multislot status))

(deftemplate decision
  (slot path)
  (slot what))

; A new file under docs is safe to add as it is.
(defrule new-documentation
  (change (path ?p&:(eq (str-index "docs/" ?p) 1)) (status GIT_STATUS_WT_NEW))
  =>
  (assert (decision (path ?p) (what add))))

; A modified source file is also committed.
(defrule modified-source
  (change (path ?p&:(eq (sub-string (- (str-length ?p) 1) (str-length ?p) ?p) ".c"))
          (status GIT_STATUS_WT_MODIFIED))
  =>
  (assert (decision (path ?p) (what add))))

; A deleted file needs a decision from a person.
(defrule deleted-file
  (change (path ?p) (status GIT_STATUS_WT_DELETED))
  =>
  (assert (decision (path ?p) (what ask))))

; Other untracked files are not changed.
(defrule untracked-elsewhere
  (change (path ?p) (status GIT_STATUS_WT_NEW))
  (not (decision (path ?p)))
  =>
  (assert (decision (path ?p) (what leave))))

(defrule report
  (declare (salience -10))
  (decision (path ?p) (what ?w))
  =>
  (println ?w ": " ?p))

; When every decision is made, the files marked add are staged and committed
; together.
(defrule commit-what-was-added
  (declare (salience -20))
  (repo ?repo)
  (exists (decision (what add)))
  =>
  (bind ?idx (git-repository-index ?repo))
  (do-for-all-facts ((?d decision)) (eq ?d:what add)
    (git-index-add-bypath ?idx ?d:path))
  (git-index-write ?idx)
  (bind ?tree (git-tree-lookup ?repo (git-index-write-tree ?idx)))
  (git-index-free ?idx)
  (bind ?head (git-commit-lookup ?repo (git-reference-name-to-id ?repo "HEAD")))
  (bind ?id (git-commit-create ?repo "HEAD" nil nil "changes the rules approved" ?tree (create$ ?head)))
  (git-commit-free ?head)
  (git-tree-free ?tree)
  (println "committed " (sub-string 1 7 ?id) " with the approved changes"))

; ----------------------------------------------------------------------
; A repository with one commit, and then some changes to it.
; ----------------------------------------------------------------------

(defglobal ?*nl* = (format nil "%n"))

(deffunction write-file (?repo ?name ?content)
  (open (str-cat (git-repository-workdir ?repo) ?name) f "w")
  (printout f ?content ?*nl*)
  (close f))

(deffunction make-repository ()
  (system "rm -rf examples/tmp && mkdir -p examples/tmp/status/docs examples/tmp/status/src")
  (bind ?repo (git-repository-init-ext "examples/tmp/status" GIT_REPOSITORY_INIT_MKPATH "main"))
  (bind ?cfg (git-repository-config ?repo))
  (git-config-set-string ?cfg "user.name" "The Rules")
  (git-config-set-string ?cfg "user.email" "rules@example.com")
  (git-config-free ?cfg)

  (write-file ?repo "src/main.c" "int main(void) { return 0; }")
  (write-file ?repo "src/old.c" "/* nothing */")
  (bind ?idx (git-repository-index ?repo))
  (git-index-add-all ?idx (create$ "*"))
  (git-index-write ?idx)
  (bind ?tree (git-tree-lookup ?repo (git-index-write-tree ?idx)))
  (git-index-free ?idx)
  (git-commit-create ?repo "HEAD" nil nil "first" ?tree (create$))
  (git-tree-free ?tree)

  ; Now the changes: a modified source file, a new document, a deleted file,
  ; and an unexpected file at the top level.
  (write-file ?repo "src/main.c" "int main(void) { return 1; }")
  (write-file ?repo "docs/guide.md" "# Guide")
  (system "rm examples/tmp/status/src/old.c")
  (write-file ?repo "scratch.txt" "temporary")
  ?repo)

; Every status entry becomes a change fact. The loop asks the status list for
; entries by position until it returns FALSE.
(deffunction load-status (?repo)
  ; The status includes the contents of untracked directories. So a new file
  ; appears as its own path, not as its directory.
  (bind ?list (git-status-list-new ?repo (create$ GIT_STATUS_OPT_INCLUDE_UNTRACKED
                                                  GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS)))
  (bind ?i 0)
  (while (git-status-entry-to-fact ?list ?i change)
    (bind ?i (+ ?i 1)))
  (git-status-list-free ?list)
  ?i)

(deffunction main ()
  (bind ?repo (make-repository))
  (assert (repo ?repo))
  (println (load-status ?repo) " changes in the working directory")
  (run)
  (bind ?list (git-status-list-new ?repo))
  (println (git-status-list-entrycount ?list) " left uncommitted")
  (git-status-list-free ?list)
  (git-repository-free ?repo)
  (system "rm -rf examples/tmp")
  (println "done"))

(main)
(exit)
