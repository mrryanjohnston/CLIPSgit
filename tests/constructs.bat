; ------------------------------------------------------------
; The templates and classes that the suites map git objects to.
;
; git-commit-to-fact and git-commit-to-instance fill the slots of a shape that
; have one of these fixed names: id, tree, parents, summary, message, body,
; author-name, author-email, author-time, author-offset, committer-name,
; committer-email, committer-time, committer-offset. The other slots are not
; changed. So a shape with only some of the names is correct.
; ------------------------------------------------------------

; every part of a commit
(deftemplate commit
  (slot id) (slot tree) (multislot parents)
  (slot summary) (slot message) (slot body)
  (slot author-name) (slot author-email) (slot author-time) (slot author-offset)
  (slot committer-name) (slot committer-email) (slot committer-time) (slot committer-offset))

; only some parts, and an extra slot that is not changed
(deftemplate commit-brief (slot id) (slot summary) (slot tag (default none)))

(defclass COMMIT (is-a USER) (role concrete)
  (slot id) (slot summary) (slot author-name) (multislot parents))

; a slot with the correct name but the wrong type, which causes a rejected
; call
(deftemplate commit-wrong-type (slot id (type INTEGER)))

; the slots that git-index-entry-to-fact fills
(deftemplate index-entry (slot path) (slot id) (slot mode) (slot size) (slot stage))

; the slots that git-status-entry-to-fact fills
(deftemplate status-entry (slot path) (multislot status) (slot old-path (default nil)))

; the slots that git-diff-delta-to-fact fills
(deftemplate delta (slot status) (slot old-path) (slot new-path) (slot old-id) (slot new-id) (slot binary))
