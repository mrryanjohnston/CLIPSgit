; ======================================================================
; What the wrappers do with arguments that CLIPS did not check.
;
; CLIPS checks a literal argument against the AddUDF restriction string when
; it reads the call. So a literal of the wrong type does not reach the
; wrapper. CLIPS does not check a value that comes through a variable. Every
; wrapper checks it again, and this suite tests that check. This is why the
; suite uses globals. They are the only way to get a bad value past the parser
; and into the guard.
;
; Each of these calls writes a line to stderr and then returns FALSE. That
; output is the test, not noise.
; ======================================================================

(println crlf "arg-guard-test-suite")

(defglobal
  ?*not-a-handle*  = 42
  ?*a-symbol*      = some-symbol
  ?*a-string*      = "some string"
  ?*a-float*       = 1.5
  ?*a-multifield*  = (create$ 1 2 3)
  ?*guard-result*  = FALSE)

; ----------------------------------------------------------------------
; A value that is not a handle
; ----------------------------------------------------------------------

(bind ?*guard-result* (git-repository-free ?*not-a-handle*))
(expect "git-repository-free refuses an integer" FALSE ?*guard-result*)

(bind ?*guard-result* (git-commit-lookup ?*a-string* "0123456789abcdef0123456789abcdef01234567"))
(expect "git-commit-lookup refuses a string repository" FALSE ?*guard-result*)

(bind ?*guard-result* (git-tree-entrycount ?*a-symbol*))
(expect "git-tree-entrycount refuses a symbol tree" FALSE ?*guard-result*)

(bind ?*guard-result* (git-revwalk-next ?*a-multifield*))
(expect "git-revwalk-next refuses a multifield revwalk" FALSE ?*guard-result*)

(bind ?*guard-result* (git-signature-name ?*a-float*))
(expect "git-signature-name refuses a float signature" FALSE ?*guard-result*)

; ----------------------------------------------------------------------
; A handle that is already freed.
;
; A free makes the handle that it received empty. So the second call sees a
; NULL pointer, not a freed pointer. This is the difference between a message
; and a crash.
; ----------------------------------------------------------------------

(deffunction double-free ()
  (bind ?repo (make-repo "guard"))
  (git-repository-free ?repo)
  (git-repository-free ?repo))

(bind ?*guard-result* (double-free))
(expect "a second git-repository-free is refused, not repeated" FALSE ?*guard-result*)

(deffunction freed-handle-used ()
  (bind ?repo (make-repo "guard"))
  (git-repository-free ?repo)
  (git-repository-path ?repo))

(bind ?*guard-result* (freed-handle-used))
(expect "a freed repository handle is refused everywhere" FALSE ?*guard-result*)

; ----------------------------------------------------------------------
; Arguments of the correct type with a wrong value
; ----------------------------------------------------------------------

(defglobal ?*guard-repo* = (make-repo "guard"))
(defglobal ?*guard-id* = (commit-file ?*guard-repo* "a" "a" "first"))
(defglobal ?*guard-commit* = (git-commit-lookup ?*guard-repo* ?*guard-id*))

(bind ?*guard-result* (git-commit-lookup ?*guard-repo* "not-an-id"))
(expect "git-commit-lookup refuses text that is not an id" FALSE ?*guard-result*)

(bind ?*guard-result* (git-commit-lookup ?*guard-repo* "0123456789abcdef0123456789abcdef01234567"))
(expect "and an id that names nothing" FALSE ?*guard-result*)

(bind ?*guard-result* (git-object-lookup ?*guard-repo* ?*guard-id* GIT_OBJECT_NOT_REAL))
(expect "git-object-lookup refuses a type name it does not know" FALSE ?*guard-result*)

(bind ?*guard-result* (git-revwalk-sorting (git-revwalk-new ?*guard-repo*) (create$ GIT_SORT_NOT_REAL)))
(expect "git-revwalk-sorting refuses a flag name it does not know" FALSE ?*guard-result*)

(bind ?*guard-result* (git-status-list-new ?*guard-repo* -1))
(expect "git-status-list-new refuses negative flags" FALSE ?*guard-result*)

(bind ?*guard-result* (git-commit-parent ?*guard-commit* -1))
(expect "git-commit-parent refuses a negative number" FALSE ?*guard-result*)

(bind ?*guard-result* (git-commit-create ?*guard-repo* "HEAD" nil nil "m" ?*guard-commit* (create$)))
(expect "git-commit-create refuses a commit where the tree belongs" FALSE ?*guard-result*)

(defglobal ?*guard-tree* = (git-commit-tree ?*guard-commit*))

(bind ?*guard-result* (git-commit-create ?*guard-repo* "HEAD" nil nil "m" ?*guard-tree* (create$ ?*guard-tree*)))
(expect "and a tree among the parents" FALSE ?*guard-result*)

(bind ?*guard-result* (git-commit-create ?*guard-repo* "HEAD" nil nil "m" ?*guard-tree* (create$ 1)))
(expect "and an integer among the parents" FALSE ?*guard-result*)

(bind ?*guard-result* (git-commit-create ?*guard-repo* "HEAD" ?*a-symbol* nil "m" ?*guard-tree* (create$)))
(expect "and a symbol that is not nil for the author" FALSE ?*guard-result*)

(bind ?*guard-result* (git-index-add-all (git-repository-index ?*guard-repo*) (create$ 1 2)))
(expect "git-index-add-all refuses a pathspec of integers" FALSE ?*guard-result*)

(bind ?*guard-result* (git-repository-init ?*guard-repo*))
(expect "git-repository-init refuses a handle where a path belongs" FALSE ?*guard-result*)

(bind ?*guard-result* (git-repository-init "tests/tmp/guard-bare" ?*a-symbol*))
(expect "git-repository-init refuses a symbol where TRUE or FALSE belongs" FALSE ?*guard-result*)

(bind ?*guard-result* (git-tree-entry-byindex ?*guard-tree* ?*a-float*))
(expect "git-tree-entry-byindex refuses a float index" FALSE ?*guard-result*)

(bind ?*guard-result* (git-diff-tree-to-tree ?*guard-repo* ?*guard-tree* ?*guard-tree* GIT_DIFF_NORMAL (create$) -1))
(expect "git-diff-tree-to-tree refuses negative context lines" FALSE ?*guard-result*)

(bind ?*guard-result* (git-commit-to-fact ?*guard-commit* ?*a-string*))
(expect "git-commit-to-fact refuses a string for the template name" FALSE ?*guard-result*)

(bind ?*guard-result* (git-commit-to-instance ?*guard-commit* NO-SUCH-CLASS))
(expect "git-commit-to-instance refuses a class that is not there" FALSE ?*guard-result*)

(bind ?*guard-result* (git-merge-analysis ?*guard-repo* (create$ ?*guard-commit*)))
(expect "git-merge-analysis refuses a commit where an annotated commit belongs" FALSE ?*guard-result*)

(bind ?*guard-result* (git-reference-create ?*guard-repo* "refs/heads/x" "short"))
(expect "git-reference-create refuses a short id" FALSE ?*guard-result*)

(bind ?*guard-result* (git-config-open-level (git-repository-config ?*guard-repo*) GIT_CONFIG_LEVEL_NOT_REAL))
(expect "git-config-open-level refuses a level it does not know" FALSE ?*guard-result*)

(git-repository-free ?*guard-repo*)
