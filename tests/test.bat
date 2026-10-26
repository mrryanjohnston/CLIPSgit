; ======================================================================
; CLIPSgit in-process test suite
;
; Every suite that this file batches shares one CLIPS environment, one
; process, and one assertion counter. The run ends with a non-zero exit status
; if an assertion failed.
;
; Two rules apply everywhere:
;
;   1. A call with arguments that are wrong for the wrapper goes through a
;      defglobal, never a literal. CLIPS checks literal arguments against
;      the AddUDF restriction string at parse time. So a literal does not
;      reach the guard of the wrapper, and the guard is what the test
;      checks.
;
;   2. Such a call is bound to a global on a line of its own, and the
;      assertion is on the next line. A rejected argument causes an
;      [ARGACCES2] evaluation error. An error inside an (expect ...) stops
;      the expect itself. The assertion would then disappear from the
;      count instead of a failure. The messages that the wrappers write
;      to stderr are the test, not noise.
;
; Each suite makes every repository that it uses under tests/tmp. The tests do
; not need an installed git, a network, or one of your repositories.
; ======================================================================

(defglobal
  ?*tests-ran*    = 0
  ?*tests-failed* = 0)

(deffunction expect (?msg ?expected ?actual)
  (bind ?*tests-ran* (+ ?*tests-ran* 1))
  (if (eq ?expected ?actual)
   then
     (print ".")
   else
     (bind ?*tests-failed* (+ ?*tests-failed* 1))
     (println crlf "FAILURE: " ?msg
             crlf "  expected=" ?expected
             crlf "  actual=" ?actual)))

; For values that are real but not fixed: an object id, a timestamp, a path
; under tests/tmp. A check of the exact value would make the suite a record of
; this machine, not of the wrapper.
(deffunction expect-true (?msg ?actual)
  (expect ?msg TRUE (if ?actual then TRUE else FALSE)))

; A scratch directory for the repositories. tests/run.sh removes it at the
; start and at the end. Each suite removes the repository that it is about to
; make. So a run by hand also cannot be affected by an earlier run.
(system "mkdir -p tests/tmp")

(batch* tests/constructs.bat)

; ----------------------------------------------------------------------
; The libgit2 that this binary was built against, in the numbers that this
; library uses for versions: 10807 is 1.8.7, 10907 is 1.9.7.
; ----------------------------------------------------------------------

(defglobal ?*libgit2* = (git-built-version))

; CLIPS strings do not read \n as a newline. So a newline is written here, and
; a suite adds it with str-cat where it needs one.
(defglobal ?*nl* = (format nil "%n"))

; CLIPS has no factp. This is the test for a fact address.
(deffunction factp (?x) (eq (type ?x) FACT-ADDRESS))

(deffunction have-1-9 () (>= ?*libgit2* 10900))

; ----------------------------------------------------------------------
; The functions that every suite uses to get a repository and to put commits
; in it.
; ----------------------------------------------------------------------

; A new repository under tests/tmp with an identity configured. Then nil can
; stand for the author and the committer in all the suites below.
(deffunction make-repo (?name)
  (bind ?path (str-cat "tests/tmp/" ?name))
  (system (str-cat "rm -rf " ?path))
  ; The initial branch is named here, not left to libgit2. The libgit2 default
  ; is master unless init.defaultBranch says otherwise. The suites are written
  ; for one name, whatever the configuration of this machine.
  (bind ?repo (git-repository-init-ext ?path (create$ GIT_REPOSITORY_INIT_MKPATH) "main"))
  (if (not ?repo) then (return FALSE))
  (bind ?cfg (git-repository-config ?repo))
  (git-config-set-string ?cfg "user.name" "Test User")
  (git-config-set-string ?cfg "user.email" "test@example.com")
  (git-config-free ?cfg)
  ?repo)

; Writes a file into the working directory.
(deffunction write-file (?repo ?path ?content)
  (bind ?full (str-cat (git-repository-workdir ?repo) ?path))
  (open ?full wf "w")
  (printout wf ?content)
  (close wf)
  ?full)

; Stages everything and commits it, with HEAD as the parent if there is one.
; Returns the id of the new commit.
(deffunction commit-all (?repo ?message)
  (bind ?idx (git-repository-index ?repo))
  (git-index-add-all ?idx (create$ "*"))
  (git-index-write ?idx)
  (bind ?tree-id (git-index-write-tree ?idx))
  (git-index-free ?idx)
  (bind ?tree (git-tree-lookup ?repo ?tree-id))
  (bind ?parents (create$))
  (if (not (git-repository-head-unborn ?repo))
   then
     (bind ?head (git-commit-lookup ?repo (git-reference-name-to-id ?repo "HEAD")))
     (bind ?parents (create$ ?head)))
  (bind ?id (git-commit-create ?repo "HEAD" nil nil ?message ?tree ?parents))
  (git-tree-free ?tree)
  (if (> (length$ ?parents) 0) then (git-commit-free (nth$ 1 ?parents)))
  ?id)

; Writes one file and commits it. Most suites need this form.
(deffunction commit-file (?repo ?path ?content ?message)
  (write-file ?repo ?path ?content)
  (commit-all ?repo ?message))

(println "CLIPSgit test suite: built for libgit2 " ?*libgit2*
         ", running with " (git-libgit2-version))
(println)

(batch* tests/version-test-suite.bat)
(batch* tests/repository-test-suite.bat)
(batch* tests/commit-test-suite.bat)
(batch* tests/tree-blob-test-suite.bat)
(batch* tests/reference-test-suite.bat)
(batch* tests/index-status-test-suite.bat)
(batch* tests/diff-test-suite.bat)
(batch* tests/config-test-suite.bat)
(batch* tests/remote-test-suite.bat)
(batch* tests/credential-test-suite.bat)
(batch* tests/merge-test-suite.bat)
(batch* tests/history-test-suite.bat)
(batch* tests/worktree-submodule-test-suite.bat)
(batch* tests/handle-lifetime-test-suite.bat)
(batch* tests/arg-guard-test-suite.bat)

; ----------------------------------------------------------------------
; cleanup
;
; Each suite frees every handle that it took. So the handle table must be
; empty. A wrapper or a suite that did not free a handle shows here and
; nowhere else.
; ----------------------------------------------------------------------

(println)
(expect "no handle is outstanding once every suite has cleaned up"
        0 (git-handle-count))

(println)
(println "Tests run: " ?*tests-ran* "  Failures: " ?*tests-failed*)
(exit (if (> ?*tests-failed* 0) then 1 else 0))
