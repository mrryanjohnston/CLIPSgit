; ======================================================================
; The prompt for a credential, and the logical name git-secret.
;
; tests/run.sh runs this file in a process of its own, with these three
; lines on a pipe to stdin:
;
;     alice
;     hunter2
;     topsecret
;
; The prompt mode is always, so the prompt is used with no terminal. The
; run.sh checks that the output shows the two prompts and the username, and
; does not show either secret.
; ======================================================================

(git-credential-prompt always)

(deffunction ask ()
  (bind ?r (git-credential-ask "https://github.com/x/y"))
  (println "ask: " ?r))
(ask)

(deffunction read-secret ()
  (bind ?s (readline git-secret))
  (println "readline from git-secret gave " (str-length ?s) " characters"))
(read-secret)

; Nothing is left on stdin. The end of the file is EOF, as for readline
; on stdin.
(deffunction read-nothing ()
  (println "at the end, readline gives " (readline git-secret)))
(read-nothing)
(exit)
