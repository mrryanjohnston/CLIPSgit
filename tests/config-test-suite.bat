; ======================================================================
; Configuration.
; ======================================================================

(println crlf "config-test-suite")

(deffunction config-suite ()
  (bind ?repo (make-repo "config"))
  (bind ?cfg (git-repository-config ?repo))

  (expect "a string set by make-repo reads back" "Test User" (git-config-get-string ?cfg "user.name"))
  (expect "a value that is not set is nil" nil (git-config-get-string ?cfg "clipsgit.nothing"))
  (expect "git-config-set-int sets an integer" TRUE (git-config-set-int ?cfg "clipsgit.count" 42))
  (expect "and git-config-get-int reads it" 42 (git-config-get-int ?cfg "clipsgit.count"))
  (expect "git-config-get-int of an unset value is nil" nil (git-config-get-int ?cfg "clipsgit.nothing"))
  (expect "git-config-set-bool sets a boolean" TRUE (git-config-set-bool ?cfg "clipsgit.flag" TRUE))
  (expect "and git-config-get-bool reads it" TRUE (git-config-get-bool ?cfg "clipsgit.flag"))
  (expect "git-config-get-bool of an unset value is nil" nil (git-config-get-bool ?cfg "clipsgit.nothing"))
  (expect "git-config-get-bool reads yes as TRUE" TRUE
          (progn (git-config-set-string ?cfg "clipsgit.yes" "yes") (git-config-get-bool ?cfg "clipsgit.yes")))
  (bind ?r (git-config-get-int ?cfg "user.name"))
  (expect "git-config-get-int refuses a value that is not a number" FALSE ?r)
  (expect "git-config-set-string sets a path" TRUE (git-config-set-string ?cfg "clipsgit.path" "~/x"))
  (expect-true "git-config-get-path expands it" (neq (git-config-get-path ?cfg "clipsgit.path") "~/x"))
  (expect "git-config-get-path of an unset value is nil" nil (git-config-get-path ?cfg "clipsgit.nothing"))

  ; Multivars.
  (expect "git-config-set-multivar adds a value" TRUE (git-config-set-multivar ?cfg "clipsgit.multi" "^$" "one"))
  (expect "and another" TRUE (git-config-set-multivar ?cfg "clipsgit.multi" "^$" "two"))
  (expect "git-config-get-multivar answers both" (create$ "one" "two") (git-config-get-multivar ?cfg "clipsgit.multi"))
  (expect "and filters by regexp" (create$ "two") (git-config-get-multivar ?cfg "clipsgit.multi" "^t"))
  (expect "git-config-get-multivar of nothing is empty" (create$) (git-config-get-multivar ?cfg "clipsgit.nothing"))
  (expect "git-config-delete-multivar removes by regexp" TRUE (git-config-delete-multivar ?cfg "clipsgit.multi" "^o"))
  (expect "leaving one" (create$ "two") (git-config-get-multivar ?cfg "clipsgit.multi"))
  (expect "git-config-delete-entry removes a value" TRUE (git-config-delete-entry ?cfg "clipsgit.count"))
  (expect "and it is gone" nil (git-config-get-int ?cfg "clipsgit.count"))
  (bind ?r (git-config-delete-entry ?cfg "clipsgit.count"))
  (expect "and a second delete is refused" FALSE ?r)

  ; Every entry.
  (bind ?entries (git-config-entries ?cfg "^clipsgit\\."))
  (expect-true "git-config-entries answers name-value pairs" (and (evenp (length$ ?entries)) (>= (length$ ?entries) 6)))
  (expect-true "and all of them" (>= (length$ (git-config-entries ?cfg)) (length$ ?entries)))

  ; Snapshots and levels.
  (bind ?snap (git-config-snapshot ?cfg))
  (expect "git-config-snapshot reads what was there" "Test User" (git-config-get-string ?snap "user.name"))
  (bind ?r (git-config-set-string ?snap "user.name" "Nobody"))
  (expect "and refuses to be written" FALSE ?r)
  (git-config-free ?snap)
  (bind ?snap (git-repository-config-snapshot ?repo))
  (expect "git-repository-config-snapshot is the same thing" "Test User" (git-config-get-string ?snap "user.name"))
  (git-config-free ?snap)
  (bind ?local (git-config-open-level ?cfg GIT_CONFIG_LEVEL_LOCAL))
  (expect "git-config-open-level opens one level" "Test User" (git-config-get-string ?local "user.name"))
  (git-config-free ?local)
  (bind ?r (git-config-open-level ?cfg GIT_CONFIG_LEVEL_APP))
  (expect "and refuses a level that is not there" FALSE ?r)
  (git-config-free ?cfg)

  ; A separate config file.
  (open "tests/tmp/own.gitconfig" f "w")
  (printout f "[section]" crlf "  key = value" crlf)
  (close f)
  (bind ?own (git-config-open-ondisk "tests/tmp/own.gitconfig"))
  (expect "git-config-open-ondisk reads a file" "value" (git-config-get-string ?own "section.key"))
  (git-config-free ?own)
  (bind ?fresh (git-config-new))
  (expect "git-config-new has nothing in it" nil (git-config-get-string ?fresh "section.key"))
  (expect "git-config-add-file-ondisk adds a file" TRUE
          (git-config-add-file-ondisk ?fresh "tests/tmp/own.gitconfig" GIT_CONFIG_LEVEL_APP))
  (expect "and the value is there" "value" (git-config-get-string ?fresh "section.key"))
  (git-config-free ?fresh)
  (bind ?r (git-config-open-ondisk "tests/tmp/nowhere.gitconfig"))
  (expect-true "git-config-open-ondisk of a missing file opens or refuses, but does not crash"
               (or (eq ?r FALSE) (external-addressp ?r)))
  (if ?r then (git-config-free ?r))

  ; The default and the search paths.
  (bind ?default (git-config-open-default))
  (expect-true "git-config-open-default opens something" (external-addressp ?default))
  (git-config-free ?default)
  (expect-true "git-config-find-global is a path or nil"
               (or (eq (git-config-find-global) nil) (stringp (git-config-find-global))))
  (expect-true "git-config-find-xdg likewise"
               (or (eq (git-config-find-xdg) nil) (stringp (git-config-find-xdg))))
  (expect-true "git-config-find-system likewise"
               (or (eq (git-config-find-system) nil) (stringp (git-config-find-system))))
  (expect-true "git-config-find-programdata likewise"
               (or (eq (git-config-find-programdata) nil) (stringp (git-config-find-programdata))))

  ; Parse.
  (expect "git-config-parse-bool reads on" TRUE (git-config-parse-bool "on"))
  (expect "and off" FALSE (git-config-parse-bool "off"))
  (bind ?r (git-config-parse-bool "maybe"))
  (expect "and refuses maybe" FALSE ?r)
  (expect "git-config-parse-int reads a suffix" 2048 (git-config-parse-int "2k"))
  (bind ?r (git-config-parse-int "many"))
  (expect "and refuses words" FALSE ?r)
  (expect-true "git-config-parse-path expands a tilde" (neq (git-config-parse-path "~/x") "~/x"))

  (git-repository-free ?repo))

(config-suite)
