; ======================================================================
; What changed, and who changed it.
;
; A diff between two commits is a list of deltas, one for each file. Each
; delta can be a fact. Blame gives the commit that last changed each line.
; Together they answer the first questions of a reviewer. What is in this
; change? Who wrote the lines that it changes?
;
;   CLIPSgit -f2 examples/3-diff-and-blame.bat
; ======================================================================

(deftemplate delta
  (slot status)
  (slot old-path)
  (slot new-path))

(deftemplate touched
  (slot path)
  (slot line)
  (slot author))

(defrule added
  (delta (status GIT_DELTA_ADDED) (new-path ?p))
  =>
  (println "added:    " ?p))

(defrule modified
  (delta (status GIT_DELTA_MODIFIED) (new-path ?p))
  =>
  (println "modified: " ?p))

(defrule deleted
  (delta (status GIT_DELTA_DELETED) (old-path ?p))
  =>
  (println "deleted:  " ?p))

; If a different person last wrote the lines of a modified file, ask that
; person to review the file.
(defrule ask-the-author
  (delta (status GIT_DELTA_MODIFIED) (new-path ?p))
  (touched (path ?p) (author ?who))
  (not (asked ?who ?p))
  =>
  (assert (asked ?who ?p))
  (println "ask " ?who " to review " ?p))

; ----------------------------------------------------------------------
; Two commits by two people, and the difference between them.
; ----------------------------------------------------------------------

(defglobal ?*nl* = (format nil "%n"))

(deffunction write-lines (?repo ?name $?lines)
  (open (str-cat (git-repository-workdir ?repo) ?name) f "w")
  (progn$ (?line ?lines) (printout f ?line ?*nl*))
  (close f))

(deffunction commit-all (?repo ?message ?who ?parents)
  (bind ?idx (git-repository-index ?repo))
  (git-index-add-all ?idx (create$ "*"))
  (git-index-write ?idx)
  (bind ?tree (git-tree-lookup ?repo (git-index-write-tree ?idx)))
  (git-index-free ?idx)
  (bind ?sig (git-signature-now ?who (str-cat (lowcase ?who) "@example.com")))
  (bind ?id (git-commit-create ?repo "HEAD" ?sig ?sig ?message ?tree ?parents))
  (git-signature-free ?sig)
  (git-tree-free ?tree)
  ?id)

(deffunction make-history ()
  (system "rm -rf examples/tmp && mkdir -p examples/tmp")
  (bind ?repo (git-repository-init-ext "examples/tmp/review" GIT_REPOSITORY_INIT_MKPATH "main"))

  (write-lines ?repo "parser.c" "int parse(void)" "{" "  return 0;" "}")
  (write-lines ?repo "TODO" "write the parser")
  (bind ?first (git-commit-lookup ?repo (commit-all ?repo "a parser" "Ada" (create$))))

  (write-lines ?repo "parser.c" "int parse(void)" "{" "  return parse_inner();" "}")
  (write-lines ?repo "inner.c" "int parse_inner(void) { return 1; }")
  (system "rm examples/tmp/review/TODO")
  (bind ?second (git-commit-lookup ?repo (commit-all ?repo "the parser, done" "Grace" (create$ ?first))))
  (create$ ?repo ?first ?second))

; Every delta between the trees of the two commits becomes a delta fact.
(deffunction load-diff (?repo ?from ?to)
  (bind ?old (git-commit-tree ?from))
  (bind ?new (git-commit-tree ?to))
  (bind ?diff (git-diff-tree-to-tree ?repo ?old ?new))
  (bind ?i 0)
  (while (git-diff-delta-to-fact ?diff ?i delta)
    (bind ?i (+ ?i 1)))
  (println (git-diff-stats-to-buf ?diff GIT_DIFF_STATS_SHORT))
  (git-diff-free ?diff)
  (git-tree-free ?old)
  (git-tree-free ?new)
  ?i)

; Every blame hunk of a file becomes a touched fact. The fact names the person
; who last wrote the lines of the hunk.
(deffunction load-blame (?repo ?path)
  (bind ?blame (git-blame-file ?repo ?path))
  (loop-for-count (?i 0 (- (git-blame-hunkcount ?blame) 1))
    (bind ?hunk (git-blame-get-hunk-byindex ?blame ?i))
    ; lines, final commit, final start line, author name, ...
    (assert (touched (path ?path) (line (nth$ 3 ?hunk)) (author (nth$ 4 ?hunk)))))
  (git-blame-free ?blame))

(deffunction main ()
  (bind ?history (make-history))
  (bind ?repo (nth$ 1 ?history))
  (bind ?first (nth$ 2 ?history))
  (bind ?second (nth$ 3 ?history))
  (println (load-diff ?repo ?first ?second) " files differ between "
           (git-object-short-id ?first) " and " (git-object-short-id ?second))
  (load-blame ?repo "parser.c")
  (git-commit-free ?first)
  (git-commit-free ?second)
  (run)
  (git-repository-free ?repo)
  (system "rm -rf examples/tmp")
  (println "done"))

(main)
(exit)
