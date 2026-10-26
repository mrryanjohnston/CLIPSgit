#!/bin/sh
# Runs the CLIPSgit test suite.
#
# The suite has two parts, for two different reasons.
#
#   tests/test.bat        One process, with every suite batched into it,
#                         and one assertion counter. This is the main suite.
#
#   examples/*.bat        Every example, run to the end and checked against
#                         the .expected file next to it. The examples are
#                         documentation. This check makes sure that they
#                         continue to work.
#
# If an assertion fails, or if an example does not give its expected output,
# this script ends with a non-zero status.
#
# Usage:
#   ./tests/run.sh                     everything
#   ./tests/run.sh suite               only tests/test.bat
#   ./tests/run.sh examples            only the examples
#   CLIPS=/path/to/clips ./tests/run.sh
#
# The suites and the examples make every repository that they use under
# tests/tmp or examples/tmp. They do not need an installed git, a network, or
# your files.
#
# If CLIPS is not set, this script runs vendor/clips. That is a symlink to the
# CLIPS version that was built last. "make test-clips" runs all of this
# against the three CLIPS versions that CLIPSgit supports, one after the
# other.
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 1

CLIPS=${CLIPS:-./vendor/clips/clips}
what=${1:-all}

# CLIPS can have a prefix, as it does for the valgrind target. So the program
# is the last word of it that looks like a path.
clips_bin=
for word in $CLIPS; do
    case "$word" in -*) ;; *) clips_bin=$word ;; esac
done

if [ ! -x "$clips_bin" ]; then
    echo "no CLIPSgit binary at $clips_bin -- run 'make' first," >&2
    echo "or point at one with CLIPS=/path/to/clips" >&2
    exit 1
fi

# The suites, the examples, and this script write here. The directory is
# removed at the start and at the end. So a run that stopped early cannot
# affect the next run.
rm -rf tests/tmp examples/tmp
mkdir -p tests/tmp

failures=0

# ----------------------------------------------------------------------
# the in-process suite
# ----------------------------------------------------------------------

if [ "$what" = all ] || [ "$what" = suite ]; then
    # stderr is kept and also shown. The messages that the wrappers write
    # there are most of what the suite checks. One of them means that an
    # assertion did not run at all.
    $CLIPS -f2 tests/test.bat 2>"$root/tests/tmp/suite.err"
    status=$?
    cat "$root/tests/tmp/suite.err" >&2

    if [ "$status" -ne 0 ]; then
        failures=$((failures + 1))
        echo
        echo "FAILED: tests/test.bat (exit $status)"
    fi

    # If an error occurs while an (expect ...) evaluates its arguments, the
    # expect stops. The assertion then disappears from the count instead of a
    # failure, and the suite reports a pass that it did not make. Each
    # deliberate rejected call in the suites is bound on a line of its own for
    # this reason. So this is always a defect in the suite.
    if grep -q "evaluating arguments for the deffunction 'expect'" "$root/tests/tmp/suite.err"; then
        failures=$((failures + 1))
        echo
        echo "FAILED: an assertion was aborted by an evaluation error, so it"
        echo "        did not run. Bind the call on a line of its own and"
        echo "        assert on the binding. The error was:"
        grep -B3 "evaluating arguments for the deffunction 'expect'" "$root/tests/tmp/suite.err" | sed 's/^/          /'
    fi
fi

# ----------------------------------------------------------------------
# the prompt
#
# The suite above must not read from stdin: a person can be there. So the
# prompt for a credential is tested in a process of its own. Three lines go
# in on a pipe. The output must show the prompts and the username, and must
# not show the secrets.
# ----------------------------------------------------------------------

if [ "$what" = all ] || [ "$what" = suite ]; then
    out=tests/tmp/prompt.out
    err=tests/tmp/prompt.err
    printf 'alice\nhunter2\ntopsecret\n' | $CLIPS -f2 tests/prompt-test.bat >"$out" 2>"$err"
    status=$?
    prompt_failed=0

    # Under valgrind, stderr has the report of valgrind. Every line of it
    # starts with the process id between "==". Any other line is a message
    # from a wrapper, and the prompt test must write none.
    if [ "$status" -ne 0 ] || grep -v '^==[0-9]*==' "$err" | grep -q .; then
        prompt_failed=1
    fi
    for line in \
        "Username for 'https://github.com': " \
        "Password for 'https://alice@github.com': " \
        'ask: (userpass "alice")' \
        'readline from git-secret gave 9 characters' \
        'at the end, readline gives EOF'; do
        grep -Fq "$line" "$out" || prompt_failed=1
    done
    if grep -Fq 'hunter2' "$out" || grep -Fq 'topsecret' "$out"; then
        prompt_failed=1
    fi

    if [ "$prompt_failed" -ne 0 ]; then
        failures=$((failures + 1))
        echo
        echo "FAILED: tests/prompt-test.bat (exit $status)"
        echo "  what it said:"
        sed 's/^/    /' "$out"
        sed 's/^/    /' "$err"
    else
        printf '.'
    fi
fi

# ----------------------------------------------------------------------
# the examples
#
# Each example runs to the end, and its output must contain what its .expected
# file says. A line in that file must be a substring of some line of the
# output. So the checks cover what the example shows, and not what changes
# between machines: a libgit2 version, a commit id, the order in which the
# agenda fired.
#
# Output on stderr also fails the example. Every function in this library
# reports a rejected call there. An example that starts to do that has stopped
# working, independent of its printed output.
# ----------------------------------------------------------------------

check_example() {
    example=$1
    expected=${example%.bat}.expected
    name=$(basename "$example")
    out=tests/tmp/${name%.bat}.out
    err=tests/tmp/${name%.bat}.err

    if [ ! -f "$expected" ]; then
        echo
        echo "FAILED: $example has no $expected beside it"
        failures=$((failures + 1))
        return
    fi

    $CLIPS -f2 "$example" >"$out" 2>"$err"
    status=$?

    if [ "$status" -ne 0 ]; then
        echo
        echo "FAILED: $example exited $status"
        sed -n '1,20p' "$err"
        failures=$((failures + 1))
        return
    fi

    if [ -s "$err" ]; then
        echo
        echo "FAILED: $example wrote to stderr"
        sed -n '1,20p' "$err"
        failures=$((failures + 1))
        return
    fi

    missing=0
    while IFS= read -r line; do
        case "$line" in ''|'#'*) continue ;; esac
        if ! grep -Fq "$line" "$out"; then
            if [ "$missing" -eq 0 ]; then
                echo
                echo "FAILED: $example did not say what $expected says it says"
            fi
            echo "  missing: $line"
            missing=$((missing + 1))
        fi
    done < "$expected"

    if [ "$missing" -gt 0 ]; then
        echo "  what it said:"
        sed 's/^/    /' "$out"
        failures=$((failures + 1))
        return
    fi

    # An example that leaves its scratch directory behind would affect the
    # next run of itself. That is a defect in the example, not in what it
    # shows.
    if [ -d examples/tmp ]; then
        echo
        echo "FAILED: $example left examples/tmp behind"
        rm -rf examples/tmp
        failures=$((failures + 1))
        return
    fi

    printf '.'
}

if [ "$what" = all ] || [ "$what" = examples ]; then
    mkdir -p tests/tmp
    echo
    printf 'examples '
    for example in examples/*.bat; do
        [ -f "$example" ] || continue
        check_example "$example"
    done
    echo
fi

rm -rf tests/tmp examples/tmp

if [ "$failures" -gt 0 ]; then
    echo "FAILED"
    exit 1
fi

echo "PASSED"
