; ======================================================================
; The library itself: versions, features, and errors.
; ======================================================================

(println crlf "version-test-suite")

(expect-true "git-libgit2-version is a dotted version string"
             (> (str-length (git-libgit2-version)) 4))
(expect-true "git-libgit2-version-number is at least 1.8"
             (>= (git-libgit2-version-number) 10800))
(expect-true "git-built-version is at least 1.8"
             (>= (git-built-version) 10800))
(expect "the loaded library is the one the binary was built against"
        (git-built-version) (git-libgit2-version-number))
(expect-true "git-libgit2-prerelease is nil or a string"
             (or (eq (git-libgit2-prerelease) nil) (stringp (git-libgit2-prerelease))))

(defglobal ?*features* = (git-libgit2-features))
(expect-true "git-libgit2-features is a multifield" (multifieldp ?*features*))
(expect-true "and threads are compiled in"
             (member$ GIT_FEATURE_THREADS ?*features*))

; A libgit2 1.9 reports more about its build.
(if (have-1-9)
 then
   (expect-true "libgit2 1.9 reports its SHA-1 provider"
                (member$ GIT_FEATURE_SHA1 ?*features*)))

; Initialization counts nest. After one more init, the caller must make one
; more shutdown.
(defglobal ?*n* = (git-libgit2-init))
(expect-true "git-libgit2-init answers the initialisation count" (> ?*n* 1))
(expect "and git-libgit2-shutdown answers one less" (- ?*n* 1) (git-libgit2-shutdown))

(expect-true "git-error-last-class is a symbol" (symbolp (git-error-last-class)))
(expect "no handles are outstanding at the start" 0 (git-handle-count))
(expect "and git-handle-kinds has nothing to list" (create$) (git-handle-kinds))

; object ids as text
(expect "git-oid-is-valid takes forty hex characters" TRUE
        (git-oid-is-valid "0123456789abcdef0123456789abcdef01234567"))
(expect "and refuses fewer" FALSE (git-oid-is-valid "0123456"))
(expect "and refuses non-hex" FALSE
        (git-oid-is-valid "xyz3456789abcdef0123456789abcdef01234567"))
(expect "git-oid-is-zero knows the null id" TRUE
        (git-oid-is-zero "0000000000000000000000000000000000000000"))
(expect "and a real one" FALSE
        (git-oid-is-zero "0123456789abcdef0123456789abcdef01234567"))
(expect "git-oid-cmp orders ids" -1
        (git-oid-cmp "0000000000000000000000000000000000000000"
                     "0123456789abcdef0123456789abcdef01234567"))
(expect "git-object-type2string names a type" "commit" (git-object-type2string GIT_OBJECT_COMMIT))
(expect "git-object-string2type reads one" GIT_OBJECT_TREE (git-object-string2type "tree"))
(expect "git-object-rawcontent-is-valid checks a blob" TRUE
        (git-object-rawcontent-is-valid "anything" GIT_OBJECT_BLOB))
(expect "and a commit that is not one" FALSE
        (git-object-rawcontent-is-valid "not a commit" GIT_OBJECT_COMMIT))
(expect "git-odb-hash hashes content without a repository"
        "ce013625030ba8dba906f756967f9e9ca394464a"
        (git-odb-hash (str-cat "hello" ?*nl*) GIT_OBJECT_BLOB))
