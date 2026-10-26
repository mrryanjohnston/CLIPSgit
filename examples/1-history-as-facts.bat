; ======================================================================
; History as facts.
;
; A git binding for CLIPS is not for a printed log. git does that better. The
; purpose is that a commit can become a fact, and rules match facts. This
; example builds a small history, puts it into working memory, and lets the
; rule engine make the conclusions.
;
;   CLIPSgit -f2 examples/1-history-as-facts.bat
; ======================================================================

(deftemplate commit
  (slot id)
  (slot summary)
  (slot author-name)
  (slot author-email)
  (multislot parents))

(defrule root-commit
  (commit (summary ?s) (parents))
  =>
  (println "the history starts with: " ?s))

(defrule merge-commit
  (commit (summary ?s) (parents ?a ?b $?))
  =>
  (println "a merge: " ?s))

(defrule no-email-domain
  (commit (summary ?s) (author-email ?e&:(not (str-index "@" ?e))))
  =>
  (println "no domain in the author's email: " ?s " (" ?e ")"))

(defrule prolific-author
  (commit (author-name ?who))
  (not (counted ?who))
  =>
  (bind ?n 0)
  (do-for-all-facts ((?c commit)) (eq ?c:author-name ?who) (bind ?n (+ ?n 1)))
  (assert (counted ?who))
  (if (>= ?n 2) then (println ?who " made " ?n " commits")))

; ----------------------------------------------------------------------
; Build a history. A file is written, staged, and committed three times by two
; people. The last commit merges a branch.
; ----------------------------------------------------------------------

(defglobal ?*nl* = (format nil "%n"))

(deffunction write-file (?repo ?name ?content)
  (open (str-cat (git-repository-workdir ?repo) ?name) f "w")
  (printout f ?content ?*nl*)
  (close f))

(deffunction commit-all (?repo ?message ?who ?email ?parents)
  (bind ?idx (git-repository-index ?repo))
  (git-index-add-all ?idx (create$ "*"))
  (git-index-write ?idx)
  (bind ?tree (git-tree-lookup ?repo (git-index-write-tree ?idx)))
  (git-index-free ?idx)
  (bind ?sig (git-signature-now ?who ?email))
  (bind ?id (git-commit-create ?repo "HEAD" ?sig ?sig ?message ?tree ?parents))
  (git-signature-free ?sig)
  (git-tree-free ?tree)
  ?id)

(deffunction head-commit (?repo)
  (git-commit-lookup ?repo (git-reference-name-to-id ?repo "HEAD")))

(deffunction build-history ()
  (system "rm -rf examples/tmp && mkdir -p examples/tmp")
  (bind ?repo (git-repository-init-ext "examples/tmp/history" GIT_REPOSITORY_INIT_MKPATH "main"))

  (write-file ?repo "notes.txt" "first draft")
  (commit-all ?repo "start the notes" "Ada" "ada@example.com" (create$))

  (bind ?first (head-commit ?repo))
  (write-file ?repo "notes.txt" "second draft")
  (commit-all ?repo "revise the notes" "Ada" "ada@example.com" (create$ ?first))

  ; A branch from the first commit, with a commit of its own.
  (bind ?branch (git-branch-create ?repo "typo" ?first))
  (git-reference-free ?branch)
  (git-repository-set-head ?repo "refs/heads/typo")
  (git-checkout-head ?repo GIT_CHECKOUT_FORCE)
  (write-file ?repo "README" "read me")
  (commit-all ?repo "add a readme" "Grace" "grace" (create$ ?first))
  (bind ?side (head-commit ?repo))
  (git-commit-free ?first)

  ; Go back to main and merge the branch.
  (git-repository-set-head ?repo "refs/heads/main")
  (git-checkout-head ?repo GIT_CHECKOUT_FORCE)
  (bind ?main (head-commit ?repo))
  (bind ?theirs (git-annotated-commit-lookup ?repo (git-commit-id ?side)))
  (git-merge ?repo (create$ ?theirs))
  (git-annotated-commit-free ?theirs)
  (commit-all ?repo "merge the readme" "Grace" "grace@example.com" (create$ ?main ?side))
  (git-repository-state-cleanup ?repo)
  (git-commit-free ?main)
  (git-commit-free ?side)
  ?repo)

; ----------------------------------------------------------------------
; Put the history into working memory. Every commit that HEAD can reach
; becomes a commit fact. The slot names are fixed (id, summary, author-name,
; and the others), and the deftemplate uses the slots that it needs.
; ----------------------------------------------------------------------

(deffunction load-history (?repo)
  (bind ?walk (git-revwalk-new ?repo))
  (git-revwalk-sorting ?walk (create$ GIT_SORT_TOPOLOGICAL GIT_SORT_REVERSE))
  (git-revwalk-push-head ?walk)
  (bind ?n 0)
  (while (bind ?id (git-revwalk-next ?walk))
    (bind ?c (git-commit-lookup ?repo ?id))
    (git-commit-to-fact ?c commit)
    (git-commit-free ?c)
    (bind ?n (+ ?n 1)))
  (git-revwalk-free ?walk)
  ?n)

(deffunction main ()
  (bind ?repo (build-history))
  (println "loaded " (load-history ?repo) " commits")
  (git-repository-free ?repo)
  (run)
  (system "rm -rf examples/tmp")
  (println "done"))

(main)
(exit)
