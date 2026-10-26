; ======================================================================
; Credentials: the hook, the prompt mode, and git-credential-ask. No test
; uses the network. The library stores no secret, so each test that needs
; one puts it in a file under tests/tmp and gives the hook the logical name
; of that file.
;
; The prompt is not tested here. This process may have a terminal on stdin,
; and a prompt would wait for a person. So the first test sets the prompt
; mode to never. tests/prompt-test.bat tests the prompt in a process of its
; own, with a pipe on stdin.
; ======================================================================

(println crlf "credential-test-suite")

(defglobal
  ?*cred-result* = FALSE
  ?*hook-url*    = nil
  ?*hook-user*   = nil
  ?*hook-kinds*  = (create$))

(expect "git-credential-hook is nil at the start" nil (git-credential-hook))
(expect "git-credential-prompt is auto at the start" auto (git-credential-prompt))
(expect "git-credential-prompt sets never" TRUE (git-credential-prompt never))
(expect "and answers the mode" never (git-credential-prompt))

(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "with no hook and no prompt, git-credential-ask refuses" FALSE ?*cred-result*)

; ----------------------------------------------------------------------
; A hook that records what it is asked, and answers a username and a
; password. The password is one line of a file.
; ----------------------------------------------------------------------

(open "tests/tmp/secret.txt" secret-out "w")
(printout secret-out "hunter2" crlf)
(close secret-out)

(deffunction userpass-hook (?url ?user ?kinds)
  (bind ?*hook-url* ?url)
  (bind ?*hook-user* ?user)
  (bind ?*hook-kinds* ?kinds)
  (create$ userpass "alice" secret-in))

(expect "git-credential-hook names a function" TRUE (git-credential-hook userpass-hook))
(expect "and answers the name" userpass-hook (git-credential-hook))

(open "tests/tmp/secret.txt" secret-in "r")
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(close secret-in)
(expect "the hook gives a username and a password" (create$ userpass "alice") ?*cred-result*)
(expect "the hook is given the url" "https://github.com/x/y" ?*hook-url*)
(expect "and nil for a url without a username" nil ?*hook-user*)
(expect "and the kinds the remote accepts" (create$ GIT_CREDENTIAL_USERPASS_PLAINTEXT) ?*hook-kinds*)

(open "tests/tmp/secret.txt" secret-in "r")
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y" "bob"))
(close secret-in)
(expect "the hook is given the username from the url" "bob" ?*hook-user*)

; The file is not open. So there is nothing to read the secret from.
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "a logical name that is not open gives no credential" FALSE ?*cred-result*)

; The remote accepts only an ssh key. It does not accept a username and a
; password.
(bind ?*cred-result* (git-credential-ask "ssh://git@github.com/x/y" "git" GIT_CREDENTIAL_SSH_KEY))
(expect "userpass is refused when the remote does not accept it" FALSE ?*cred-result*)
(expect "the hook sees the kinds for an ssh remote" (create$ GIT_CREDENTIAL_SSH_KEY) ?*hook-kinds*)

; ----------------------------------------------------------------------
; The other kinds
; ----------------------------------------------------------------------

(deffunction username-hook (?url ?user ?kinds) (create$ username "alice"))
(git-credential-hook username-hook)
(bind ?*cred-result* (git-credential-ask "ssh://github.com/x/y" nil GIT_CREDENTIAL_USERNAME))
(expect "a hook can answer only a username" (create$ username "alice") ?*cred-result*)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "but not when the remote wants a password too" FALSE ?*cred-result*)

(deffunction agent-hook (?url ?user ?kinds) (create$ ssh-agent "git"))
(git-credential-hook agent-hook)
(bind ?*cred-result* (git-credential-ask "ssh://git@github.com/x/y" "git" GIT_CREDENTIAL_SSH_KEY))
(expect "a hook can answer the ssh agent" (create$ ssh-key "git") ?*cred-result*)

(deffunction key-hook (?url ?user ?kinds)
  (create$ ssh-key "git" "tests/tmp/id_test.pub" "tests/tmp/id_test"))
(git-credential-hook key-hook)
(bind ?*cred-result* (git-credential-ask "ssh://git@github.com/x/y" "git" GIT_CREDENTIAL_SSH_KEY))
(expect "a hook can answer a key pair" (create$ ssh-key "git") ?*cred-result*)

(deffunction key-passphrase-hook (?url ?user ?kinds)
  (create$ ssh-key "git" "tests/tmp/id_test.pub" "tests/tmp/id_test" secret-in))
(git-credential-hook key-passphrase-hook)
(open "tests/tmp/secret.txt" secret-in "r")
(bind ?*cred-result* (git-credential-ask "ssh://git@github.com/x/y" "git" GIT_CREDENTIAL_SSH_KEY))
(close secret-in)
(expect "a hook can answer a key pair with a passphrase" (create$ ssh-key "git") ?*cred-result*)
(bind ?*cred-result* (git-credential-ask "ssh://git@github.com/x/y" "git" GIT_CREDENTIAL_SSH_KEY))
(expect "and gets no credential when the passphrase cannot be read" FALSE ?*cred-result*)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "a key is refused when the remote does not accept one" FALSE ?*cred-result*)

; ----------------------------------------------------------------------
; A hook that does not give a credential
; ----------------------------------------------------------------------

(deffunction refusing-hook (?url ?user ?kinds) FALSE)
(git-credential-hook refusing-hook)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "a hook that answers FALSE refuses" FALSE ?*cred-result*)

(deffunction prompt-hook (?url ?user ?kinds) (create$ prompt))
(git-credential-hook prompt-hook)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "a hook that asks for the prompt gets none when the mode is never" FALSE ?*cred-result*)

(deffunction bogus-hook (?url ?user ?kinds) (create$ bogus "alice"))
(git-credential-hook bogus-hook)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "a tag the library does not know refuses" FALSE ?*cred-result*)

(deffunction string-hook (?url ?user ?kinds) "alice")
(git-credential-hook string-hook)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "an answer that is not a multifield refuses" FALSE ?*cred-result*)

(deffunction short-hook (?url ?user ?kinds) (create$ userpass))
(git-credential-hook short-hook)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "userpass without a username refuses" FALSE ?*cred-result*)

(deffunction one-arg-hook (?url) (create$ userpass "alice"))
(git-credential-hook one-arg-hook)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "a hook with the wrong number of arguments refuses" FALSE ?*cred-result*)

(git-credential-hook no-such-function)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y"))
(expect "a hook that is not a function refuses" FALSE ?*cred-result*)

; ----------------------------------------------------------------------
; Clearing, and the guards
; ----------------------------------------------------------------------

(expect "git-credential-hook nil clears the hook" TRUE (git-credential-hook nil))
(expect "and the hook is nil again" nil (git-credential-hook))

(bind ?*cred-result* (git-credential-hook ?*a-string*))
(expect "git-credential-hook refuses a string" FALSE ?*cred-result*)
(bind ?*cred-result* (git-credential-prompt sometimes))
(expect "git-credential-prompt refuses a mode it does not have" FALSE ?*cred-result*)
(expect "and the mode did not change" never (git-credential-prompt))
(bind ?*cred-result* (git-credential-ask ?*not-a-handle*))
(expect "git-credential-ask refuses an integer url" FALSE ?*cred-result*)
(bind ?*cred-result* (git-credential-ask "https://github.com/x/y" nil ?*a-float*))
(expect "git-credential-ask refuses a float for the kinds" FALSE ?*cred-result*)
