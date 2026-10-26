# An ordinary user can install without special permissions. This is why the
# default is under $HOME and not /usr/local. Give a privileged location on the
# command line: "sudo make install PREFIX=/usr/local". You must give the
# location. See check-install-prefix.
PREFIX        ?= $(HOME)/.local
BINDIR        := $(PREFIX)/bin
DESTINATION   := CLIPSgit
VERSION       ?= 0.1.0

# The private, versioned location for the real binary. It is never on PATH.
LIBEXECDIR    := $(PREFIX)/libexec/$(DESTINATION)-$(VERSION)

# The system-wide share directory for CLIPSgit data.
DATADIR       := $(PREFIX)/share/$(DESTINATION)

WRAPPER       := $(DESTINATION)-$(VERSION)

# ---------------------------------------------------------------------------
# The CLIPS version to build against.
#
#   6.4.2    the 6.4.2 release tarball from SourceForge (the default)
#   svn-6x   branches/64x of the CLIPS Subversion repository
#   svn-7x   branches/70x
#
# The two branches are pinned to a revision. A build today and a build next
# month give the same result. Set CLIPS_SVN_REV to move a branch:
#
#   make CLIPS_VERSION=svn-7x
#   make CLIPS_VERSION=svn-7x CLIPS_SVN_REV=980
#   make CLIPS_VERSION=svn-7x CLIPS_SVN_REV=HEAD
#
# The makefile fetches and builds each version in its own directory. A change
# to a different version does not rebuild the version that you leave. A change
# to a different libgit2 version under a CLIPS version also does not.
# ---------------------------------------------------------------------------

CLIPS_VERSION  ?= 6.4.2
CLIPS_VERSIONS := 6.4.2 svn-6x svn-7x

ARCHIVE     := clips_core_source_642.tar.gz
ARCHIVE_URL ?= https://sourceforge.net/projects/clipsrules/files/CLIPS/6.4.2/$(ARCHIVE)

CLIPS_SVN_ROOT   ?= https://svn.code.sf.net/p/clipsrules/code
CLIPS_SVN_6X_REV ?= 967
CLIPS_SVN_7X_REV ?= 978

# The digest that SourceForge publishes for clips_core_source_642.tar.gz. It
# applies only to the release. A branch export changes over time and has no
# published digest. So the variable is empty for a branch, and the fetch
# script skips the check.
CLIPS_SHA256_6.4.2 := 608a1eb2fc6e9caff30d63d684095f0bca7108f2294d21ee6f5617427c10455a
CLIPS_SHA256 ?= $(CLIPS_SHA256_$(CLIPS_VERSION))
export CLIPS_SHA256

ifeq ($(CLIPS_VERSION),6.4.2)
  CLIPS_TAG    := 6.4.2
  CLIPS_FETCH  := tarball "$(ARCHIVE_URL)" "$(ARCHIVE)"
  CLIPS_ORIGIN := the 6.4.2 release tarball
else ifeq ($(CLIPS_VERSION),svn-6x)
  CLIPS_SVN_URL ?= $(CLIPS_SVN_ROOT)/branches/64x/core
  CLIPS_SVN_REV ?= $(CLIPS_SVN_6X_REV)
  CLIPS_TAG     := svn-6x-r$(CLIPS_SVN_REV)
  CLIPS_FETCH   := svn "$(CLIPS_SVN_URL)" "$(CLIPS_SVN_REV)"
  CLIPS_ORIGIN  := branches/64x at r$(CLIPS_SVN_REV)
else ifeq ($(CLIPS_VERSION),svn-7x)
  CLIPS_SVN_URL ?= $(CLIPS_SVN_ROOT)/branches/70x/core
  CLIPS_SVN_REV ?= $(CLIPS_SVN_7X_REV)
  CLIPS_TAG     := svn-7x-r$(CLIPS_SVN_REV)
  CLIPS_FETCH   := svn "$(CLIPS_SVN_URL)" "$(CLIPS_SVN_REV)"
  CLIPS_ORIGIN  := branches/70x at r$(CLIPS_SVN_REV)
else
  $(error CLIPS_VERSION is '$(CLIPS_VERSION)': expected one of $(CLIPS_VERSIONS))
endif

# The fetched tree is kept apart from the build tree. Then a removal of the
# object files does not cause a new fetch of the sources, and CI has something
# useful to cache.
CLIPS_SRC_DIR := vendor/clips-source/$(CLIPS_TAG)
BUILD_DIR     := vendor/clips-build/$(CLIPS_TAG)
TARGET        := $(BUILD_DIR)/clips

CLIPS_SRC_STAMP := $(CLIPS_SRC_DIR)/.clips-source
BUILD_STAMP     := $(BUILD_DIR)/.clips-source

# vendor/clips points to the version that was built last. Then the paths in
# the README, in the examples, and in tests/run.sh remain valid.
CLIPS_LINK := vendor/clips

# The binary that the tests run: the one that was built last, unless the
# environment or the command line names a different one.
CLIPS     ?=
CLIPS_BIN  = $(if $(strip $(CLIPS)),$(CLIPS),$(TARGET))

UNAME_S       := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  CLIPS_OS := DARWIN
else
  CLIPS_OS := LINUX
endif

# ---------------------------------------------------------------------------
# libgit2.
#
# By default, the makefile downloads the pinned source release. It checks the
# download against the SHA-256 digest that it records. It builds the release
# with CMake as a static library in vendor/, and links it into the binary. So
# the library that this binding calls is the one that its documentation
# describes, and the installed binary has no library to find at run time.
#
# The build is small. Each part that libgit2 can take from the system is off
# or bundled: NTLM, GSSAPI, the system zlib, the system regex. The machine
# must supply only a TLS library for HTTPS: OpenSSL on Linux (libssl-dev), or
# Security.framework on macOS, which CMake finds on its own. LIBGIT2_HTTPS=OFF
# removes that requirement, for a build that uses only local repositories.
#
# SSH is on, through the ssh client of the machine. libgit2 runs "ssh" for an
# ssh:// remote, so the keys, the agent, and the known hosts of the user
# apply. This needs no library at build time, and an ssh client on PATH at
# run time. LIBGIT2_SSH=OFF removes it. LIBGIT2_SSH=libssh2 uses the libssh2
# library instead, if the machine has it.
#
# After the tarball is unpacked, scripts/patch-libgit2.sh applies the patches
# in patches/libgit2/. Each fixes one memory leak in libgit2 that valgrind
# finds when the suite runs. The head of each patch says which.
#
# LIBGIT2_SYSTEM=1 links the libgit2 that the machine has. This is for a
# packager who prefers the system library to a copy of it. That library is
# not patched.
# ---------------------------------------------------------------------------

# The versions that this binding is built and tested against. You can pin any
# of them. A plain "make" builds 1.9.7.
#
#     make LIBGIT2_VERSION=1.8.7
#
# A version that is not in the table below needs its digest:
#
#     make LIBGIT2_VERSION=1.9.8 LIBGIT2_SHA256=<the digest of that tarball>
#
LIBGIT2_VERSION  ?= 1.9.7
LIBGIT2_VERSIONS := 1.8.7 1.9.6 1.9.7

# GitHub generates the tarball for a tag on request. It does not publish a
# signed file. So these digests were calculated from the downloaded tarballs.
# They are a pin. A change under them is what this check must find, and
# scripts/fetch-libgit2.sh reports that if a digest does not match.
LIBGIT2_SHA256_1.8.7 := a548a2209c3e99d6bb685d843396fff3a7d3ffd8340adb5d8851637cfe8cd134
LIBGIT2_SHA256_1.9.6 := a88a42a4ea9bdab7aa8686eead3bf7d9c6dd74529caca16ab22eaa92433d31d9
LIBGIT2_SHA256_1.9.7 := 1a4fbe7589e814777ae76b64734ad80f4ecad22cd33a22682a2aaea4ae5375e7

LIBGIT2_SHA256 ?= $(LIBGIT2_SHA256_$(LIBGIT2_VERSION))

LIBGIT2_MIRROR  ?= https://github.com/libgit2/libgit2/archive/refs/tags
LIBGIT2_TARBALL := libgit2-$(LIBGIT2_VERSION).tar.gz
LIBGIT2_URL     ?= $(LIBGIT2_MIRROR)/v$(LIBGIT2_VERSION).tar.gz
LIBGIT2_ARCHIVE := $(CURDIR)/vendor/$(LIBGIT2_TARBALL)

# The tarball unpacks to libgit2-<version>/. The CMake build tree and the
# installed result are next to it.
LIBGIT2_SRC     ?= $(CURDIR)/vendor/libgit2-$(LIBGIT2_VERSION)
LIBGIT2_BUILD   ?= $(CURDIR)/vendor/libgit2-build-$(LIBGIT2_VERSION)
LIBGIT2_PREFIX  ?= $(CURDIR)/vendor/git2-$(LIBGIT2_VERSION)

LIBGIT2_HTTPS   ?= ON
LIBGIT2_PATCHES := $(wildcard patches/libgit2/*.patch)
LIBGIT2_SSH     ?= exec
CMAKE           ?= cmake

# A static build, without the tests or the CLI. Every optional dependency is
# bundled or off. CMAKE_INSTALL_LIBDIR=lib prevents a Fedora-style toolchain
# from putting the result in lib64, where the rules below do not look.
LIBGIT2_CMAKE_OPTS ?= -DCMAKE_BUILD_TYPE=Release \
                      -DBUILD_SHARED_LIBS=OFF \
                      -DBUILD_TESTS=OFF \
                      -DBUILD_CLI=OFF \
                      -DBUILD_EXAMPLES=OFF \
                      -DUSE_SSH=$(LIBGIT2_SSH) \
                      -DUSE_HTTPS=$(LIBGIT2_HTTPS) \
                      -DUSE_BUNDLED_ZLIB=ON \
                      -DREGEX_BACKEND=builtin \
                      -DUSE_HTTP_PARSER=builtin \
                      -DUSE_NTLMCLIENT=OFF \
                      -DUSE_GSSAPI=OFF \
                      -DCMAKE_INSTALL_LIBDIR=lib \
                      -DCMAKE_POSITION_INDEPENDENT_CODE=ON

PKG_CONFIG ?= pkg-config

ifeq ($(LIBGIT2_SYSTEM),1)
  # pkg-config knows the location of the system library headers and the
  # libraries that it needs. A machine without a libgit2.pc gets the usual
  # default.
  LIBGIT2_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libgit2 2>/dev/null)
  LIBGIT2_LDLIBS ?= $(shell $(PKG_CONFIG) --libs libgit2 2>/dev/null || echo -lgit2)
  LIBGIT2_DEP    :=
  LIBGIT2_ORIGIN := the libgit2 this machine has
else
  LIBGIT2_LIB    := $(LIBGIT2_PREFIX)/lib/libgit2.a
  LIBGIT2_PC     := $(LIBGIT2_PREFIX)/lib/pkgconfig
  LIBGIT2_CFLAGS := -I$(LIBGIT2_PREFIX)/include
  # A static libgit2 needs the libraries that it was built against after it on
  # the link line: OpenSSL, librt, the frameworks on macOS. The .pc file that
  # CMake wrote is the one record of those. Its Libs.private line names the
  # libraries that libgit2 calls directly. Its Requires.private line names
  # OpenSSL. The rule asks pkg-config for OpenSSL without --static, because
  # the link takes the shared OpenSSL. With --static, pkg-config also lists
  # the archives that a static OpenSSL needs. On Ubuntu 26.04, one of them,
  # libjitterentropy.a, is not part of libssl-dev, and the link fails.
  # This is a recursive assignment, so make reads it at link time, after the
  # library exists.
  LIBGIT2_LDLIBS  = $(shell export PKG_CONFIG_PATH="$(LIBGIT2_PC)"; \
                            pc="$(LIBGIT2_PC)/libgit2.pc"; \
                            req=$$(sed -n 's/^Requires\.private: *//p' "$$pc" 2>/dev/null); \
                            libs=$$($(PKG_CONFIG) --libs libgit2 2>/dev/null \
                                    && sed -n 's/^Libs\.private: *//p' "$$pc" \
                                    && { [ -z "$$req" ] || $(PKG_CONFIG) --libs $$req 2>/dev/null; }) \
                            && echo $$libs \
                            || echo "-L$(LIBGIT2_PREFIX)/lib -lgit2 -lssl -lcrypto -lpthread")
  LIBGIT2_DEP    := $(LIBGIT2_LIB)
  LIBGIT2_ORIGIN := libgit2 $(LIBGIT2_VERSION), built from source in vendor/
endif

# The release flags of the CLIPS makefile. A CFLAGS on the command line
# replaces them.
CLIPS_CFLAGS   ?= -std=c99 -O3 -fno-strict-aliasing
DEBUG_CFLAGS   ?= -std=c99 -O0 -g

CFLAGS  = $(CLIPS_CFLAGS) $(LIBGIT2_CFLAGS)
LDLIBS  = -lm $(LIBGIT2_LDLIBS)

# ---------------------------------------------------------------------------
# The configuration of the last build.
#
# The libgit2 version that the objects were compiled against depends on
# CFLAGS. The version that they are linked against depends on LDLIBS. Make
# does not track CFLAGS or LDLIBS. Without this file, a change of libgit2 version in the same
# tree would link the old object files and make a binary for the wrong
# version, with no warning. The file is in the build tree, one for each CLIPS
# version, because it describes the objects there.
# ---------------------------------------------------------------------------

BUILD_FLAGS := $(BUILD_DIR)/.build-flags

VALGRIND       ?= valgrind
# A block that is lost is an error. A block that is still reachable at exit
# is not: CLIPS and libgit2 keep their tables until the process ends.
VALGRIND_FLAGS ?= --leak-check=full --errors-for-leak-kinds=definite \
                  --num-callers=12 --error-exitcode=1

COVDIR     := $(CURDIR)/coverage
COV_CC     ?= gcc
GCOV       ?= gcov
COV_CFLAGS := -std=c99 -O0 -g --coverage

.PHONY: all clips clips-source debug deps libgit2 install install-bin \
        uninstall clean distclean test test-suite test-examples test-valgrind \
        coverage test-versions test-clips docs docs-check help \
        check-install-prefix check-cmake \
        print-libgit2-version print-libgit2-versions print-libgit2-prefix \
        print-clips print-clips-versions print-clips-target \
        print-cflags print-ldlibs FORCE

all: clips

clips: $(TARGET)
	@:

deps: $(LIBGIT2_DEP) $(CLIPS_SRC_STAMP)

libgit2: $(LIBGIT2_DEP)

$(BUILD_FLAGS): FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(CFLAGS) $(LDLIBS)' | cmp -s - $@ 2>/dev/null || \
	    printf '%s\n' '$(CFLAGS) $(LDLIBS)' > $@

FORCE:

# ---------------------------------------------------------------------------
# Fetch and build CLIPS.
#
# scripts/fetch-clips.sh puts a clean tree in $(CLIPS_SRC_DIR). The build
# happens in a copy of that tree, because the CLIPS makefile finds
# userfunctions.c only next to the CLIPS sources. The copy is made one time.
# So a change to userfunctions.c recompiles one file, not all of them.
# ---------------------------------------------------------------------------

$(CLIPS_SRC_STAMP): | scripts/fetch-clips.sh
	./scripts/fetch-clips.sh $(CLIPS_FETCH) "$(CLIPS_SRC_DIR)"

$(BUILD_STAMP): $(CLIPS_SRC_STAMP)
	mkdir -p "$(BUILD_DIR)"
	cp -R "$(CLIPS_SRC_DIR)/." "$(BUILD_DIR)/"
	touch "$@"

# vendor/clips is a symlink to the version that was built last. An older
# checkout unpacked the sources there directly. A real directory in that
# location comes from that checkout, and the rule removes it.
define point_clips_link
	@[ ! -e "$(CLIPS_LINK)" ] || [ -L "$(CLIPS_LINK)" ] || rm -rf "$(CLIPS_LINK)"
	@ln -sfn "clips-build/$(CLIPS_TAG)" "$(CLIPS_LINK)"
	@echo "$(CLIPS_LINK) -> clips-build/$(CLIPS_TAG)  ($(CLIPS_ORIGIN))"
endef

$(TARGET): userfunctions.c $(LIBGIT2_DEP) $(BUILD_FLAGS) $(BUILD_STAMP)
	cp userfunctions.c $(BUILD_DIR)/
	$(MAKE) -C $(BUILD_DIR) LDLIBS="$(LDLIBS)" CFLAGS="$(CFLAGS)"
	$(point_clips_link)

debug: userfunctions.c $(LIBGIT2_DEP) $(BUILD_FLAGS) $(BUILD_STAMP)
	cp userfunctions.c $(BUILD_DIR)/
	$(MAKE) -C $(BUILD_DIR) debug LDLIBS="$(LDLIBS)" CFLAGS="$(DEBUG_CFLAGS) $(LIBGIT2_CFLAGS)"
	$(point_clips_link)

# Fetch the selected CLIPS without a build, to fill a cache or to look at a
# branch.
clips-source: $(CLIPS_SRC_STAMP)
	@cat "$(CLIPS_SRC_STAMP)"

# ---------------------------------------------------------------------------
# The vendored libgit2.
# ---------------------------------------------------------------------------

check-cmake:
	@command -v $(CMAKE) >/dev/null 2>&1 || { \
	    echo "makefile: cannot build libgit2 $(LIBGIT2_VERSION) from source: $(CMAKE) not found." >&2; \
	    echo >&2; \
	    echo "  libgit2 builds with CMake." >&2; \
	    echo "  Debian/Ubuntu:  sudo apt-get install cmake libssl-dev" >&2; \
	    echo "  Fedora/RHEL:    sudo dnf install cmake openssl-devel" >&2; \
	    echo "  macOS:          brew install cmake" >&2; \
	    echo >&2; \
	    echo "  Or build against the libgit2 already on this machine:" >&2; \
	    echo "      make LIBGIT2_SYSTEM=1" >&2; \
	    exit 1; \
	}

$(LIBGIT2_ARCHIVE): | scripts/fetch-libgit2.sh
	@[ -n "$(LIBGIT2_SHA256)" ] || { \
	    echo "makefile: no digest is pinned for libgit2 $(LIBGIT2_VERSION)." >&2; \
	    echo >&2; \
	    echo "  The versions with one are:" >&2; \
	    echo "      $(LIBGIT2_VERSIONS)" >&2; \
	    echo >&2; \
	    echo "  For any other, download it once and pass its digest:" >&2; \
	    echo "      make LIBGIT2_VERSION=$(LIBGIT2_VERSION) LIBGIT2_SHA256=<digest>" >&2; \
	    echo "  The tarball is at $(LIBGIT2_URL)" >&2; \
	    exit 1; \
	}
	./scripts/fetch-libgit2.sh "$(LIBGIT2_URL)" "$(LIBGIT2_SHA256)" "$@"

$(LIBGIT2_SRC)/CMakeLists.txt: $(LIBGIT2_ARCHIVE) $(LIBGIT2_PATCHES) | scripts/patch-libgit2.sh
	mkdir -p "$(dir $(LIBGIT2_SRC))"
	tar -xzf "$(LIBGIT2_ARCHIVE)" -C "$(dir $(LIBGIT2_SRC))"
	./scripts/patch-libgit2.sh "$(LIBGIT2_SRC)"
	touch "$@"

# The rules use a vendor/git2-<version> that is already populated as it is. It
# can come from an earlier build here, from a cache, or from a copy of an
# installation. For it, the rules fetch no tarball, configure nothing, and
# compile nothing. This is why there are two rules, not one. Make builds a
# missing prerequisite even if the target that needs it is current. So the
# source tree must not be a prerequisite at all, not only an order-only one.
ifeq ($(wildcard $(LIBGIT2_LIB)),)

$(LIBGIT2_LIB): $(LIBGIT2_SRC)/CMakeLists.txt
	@$(MAKE) --no-print-directory check-cmake
	$(CMAKE) -S "$(LIBGIT2_SRC)" -B "$(LIBGIT2_BUILD)" \
	    -DCMAKE_INSTALL_PREFIX="$(LIBGIT2_PREFIX)" $(LIBGIT2_CMAKE_OPTS)
	$(CMAKE) --build "$(LIBGIT2_BUILD)" --parallel
	$(CMAKE) --install "$(LIBGIT2_BUILD)"

else

$(LIBGIT2_LIB): ;

endif

print-libgit2-version:
	@echo '$(LIBGIT2_VERSION)'

print-libgit2-versions:
	@echo '$(LIBGIT2_VERSIONS)'

print-libgit2-prefix:
	@echo '$(LIBGIT2_PREFIX)'

print-clips-versions:
	@echo '$(CLIPS_VERSIONS)'

print-clips-target:
	@echo '$(TARGET)'

# The flags that this configuration compiles and links with. A different
# build that must use the same libgit2 can read them: "make -s print-ldlibs".
print-cflags:
	@echo '$(CFLAGS)'

print-ldlibs:
	@echo '$(LDLIBS)'

print-clips:
	@echo 'CLIPS_VERSION $(CLIPS_VERSION)'
	@echo 'origin        $(CLIPS_ORIGIN)'
	@echo 'source        $(CLIPS_SRC_DIR)'
	@echo 'build         $(BUILD_DIR)'
	@echo 'binary        $(TARGET)'
	@echo 'libgit2       $(LIBGIT2_ORIGIN)'
	@[ -f "$(CLIPS_SRC_STAMP)" ] && printf 'fetched       ' && cat "$(CLIPS_SRC_STAMP)" || true

install: check-install-prefix clips install-bin
	@echo
	@echo "installed $(DESTINATION) $(VERSION) in $(PREFIX)"
	@echo "  $(BINDIR)/$(DESTINATION)"
	@case ":$$PATH:" in \
	    *":$(BINDIR):"*) ;; \
	    *) echo; echo "note: $(BINDIR) is not on your PATH." ;; \
	esac

# ---------------------------------------------------------------------------
# sudo resets HOME to the home directory of root. So a "sudo make install"
# without a PREFIX would install into /root/.local with no warning. Each step
# would report success, and the files would not be where the user looks for
# them. The purpose of sudo here is to name the location. So a command that
# does not name it is an error.
# ---------------------------------------------------------------------------

PREFIX_NAMED := $(if $(filter command line environment,$(origin PREFIX)),yes,no)

check-install-prefix:
	@if [ "$$(id -u)" -eq 0 ] && [ "$(PREFIX_NAMED)" = no ]; then \
	    echo "makefile: running as root with no PREFIX named." >&2; \
	    echo >&2; \
	    echo "  sudo resets HOME, so this would install into $(PREFIX)," >&2; \
	    echo "  which is almost certainly not what you meant. Name the" >&2; \
	    echo "  location instead:" >&2; \
	    echo >&2; \
	    echo "      sudo make install PREFIX=/usr/local" >&2; \
	    echo >&2; \
	    echo "  Or install as yourself, which needs no sudo at all:" >&2; \
	    echo >&2; \
	    echo "      make install" >&2; \
	    exit 1; \
	fi

install-bin:
	install -d "$(LIBEXECDIR)" "$(BINDIR)"
	install -m755 "$(TARGET)" "$(LIBEXECDIR)/clips"
	sed -e 's|@REAL@|$(LIBEXECDIR)/clips|g' \
	    -e 's|@VERSION@|$(VERSION)|g' \
	    CLIPSgit.in > "$(BINDIR)/$(WRAPPER)"
	chmod 755 "$(BINDIR)/$(WRAPPER)"
	ln -sfn "$(WRAPPER)" "$(BINDIR)/$(DESTINATION)"

uninstall:
	rm -f "$(BINDIR)/$(WRAPPER)" "$(BINDIR)/$(DESTINATION)"
	rm -rf "$(LIBEXECDIR)"
	rmdir "$(DATADIR)" 2>/dev/null || true

# clean keeps the trees under vendor/clips-source. They are slow to fetch
# again, and a build writes nothing there. clean also keeps the vendored
# libgit2. Use distclean to remove it.
clean:
	-rm -rf vendor/clips-build "$(CLIPS_LINK)"
	-rm -rf "$(COVDIR)" tests/tmp examples/tmp

distclean:
	rm -rf vendor
	rm -f "$(ARCHIVE)"

# ---------------------------------------------------------------------------
# Tests. tests/run.sh runs the in-process suite and then the examples. It
# checks each example against the .expected file next to it. So an example
# that stops working fails the build. It does not remain in the repository as
# a bad guide. The suite makes every repository that it uses under tests/tmp.
# The tests do not need an installed git or a network.
# ---------------------------------------------------------------------------

test: all
	CLIPS="$(CLIPS_BIN)" ./tests/run.sh

test-suite: all
	CLIPS="$(CLIPS_BIN)" ./tests/run.sh suite

test-examples: all
	CLIPS="$(CLIPS_BIN)" ./tests/run.sh examples

test-valgrind: all
	@command -v $(VALGRIND) >/dev/null 2>&1 || { \
	    echo "$(VALGRIND) not found: install valgrind, or point at it with VALGRIND=/path/to/valgrind" >&2; \
	    exit 1; \
	}
	CLIPS="$(VALGRIND) $(VALGRIND_FLAGS) $(CLIPS_BIN)" ./tests/run.sh suite

# The complete suite against every supported libgit2 release, one after the
# other. Each release is fetched and built in its own directory, or reused if
# it is already in vendor/. Then the suite runs against the binary that is
# linked to it.
test-versions:
	./scripts/test-versions.sh

# The complete suite against every CLIPS that this binds to, one after the
# other. The interpreter is the variable here, so one libgit2 serves all of
# them.
#
# CLIPS= clears an inherited value. Each version must be tested against its
# own binary, or the test has no meaning.
test-clips:
	@for v in $(CLIPS_VERSIONS); do \
	    echo; \
	    echo "=== CLIPS_VERSION=$$v ==="; \
	    $(MAKE) --no-print-directory CLIPS= CLIPS_VERSION="$$v" test || exit 1; \
	done

# ---------------------------------------------------------------------------
# API.md.
#
# The descriptions are written by hand, in docs/api-descriptions.md. The
# generator makes the structure: which functions exist, in which order, and
# with which arguments. The generator stops if a registered function has no
# description, or if a description names a function that is not registered.
# This keeps the two in agreement.
# ---------------------------------------------------------------------------

DOCS_GEN   := docs/gen-api-docs.sh
DOCS_INPUT := $(DOCS_GEN) docs/api-template.md docs/api-descriptions.md \
              userfunctions.c makefile

docs: API.md

API.md: $(DOCS_INPUT)
	$(DOCS_GEN) "$@"

docs-check: $(DOCS_INPUT)
	@$(DOCS_GEN) "$(CURDIR)/API.md.check" >/dev/null
	@if cmp -s API.md API.md.check; then \
	    rm -f API.md.check; echo "API.md is up to date"; \
	else \
	    diff -u API.md API.md.check | head -40; rm -f API.md.check; \
	    echo "API.md is stale: run 'make docs'" >&2; exit 1; \
	fi

help:
	@printf 'CLIPSgit targets:\n\n'
	@printf '  %-16s %s\n' \
	    all             'build the binary against one CLIPS (the default)' \
	    debug           'the same build with debugging symbols' \
	    libgit2         'fetch, verify and build the pinned libgit2 only' \
	    clips-source    'fetch the selected CLIPS without building it' \
	    test            'the whole suite and the examples: tests/run.sh' \
	    test-suite      'only the in-process suite, tests/test.bat' \
	    test-examples   'only the examples, checked against examples/*.expected' \
	    test-valgrind   'the in-process suite under valgrind' \
	    test-versions   'run the whole suite against every supported libgit2' \
	    test-clips      'run the whole suite against all three CLIPS versions' \
	    coverage        'line coverage of userfunctions.c' \
	    docs            'regenerate API.md' \
	    docs-check      'fail if API.md is not what docs would write' \
	    install         'install the binary and its wrapper under PREFIX' \
	    uninstall       'remove what install put there' \
	    clean           'remove the build trees, coverage and test scratch' \
	    distclean       'also remove the fetched CLIPS and libgit2 sources'
	@printf '\nlibgit2 is pinned, downloaded, checked against a SHA-256 this\n'
	@printf 'makefile records for it, and built statically with CMake.\n'
	@printf 'This build uses %s.\n' '$(LIBGIT2_VERSION)'
	@printf '\nSupported: %s\n' '$(LIBGIT2_VERSIONS)'
	@printf '\n  LIBGIT2_SYSTEM=1      link the libgit2 this machine has\n'
	@printf '  LIBGIT2_VERSION=      pin a different release (also set LIBGIT2_SHA256)\n'
	@printf '  LIBGIT2_HTTPS=OFF     build without a TLS library\n'
	@printf '  LIBGIT2_SSH=OFF       build without the ssh transport (default: exec,\n'
	@printf '                        which runs the ssh client of the machine)\n'
	@printf '  LIBGIT2_CMAKE_OPTS=   the CMake options for the vendored build\n'
	@printf '  LIBGIT2_PREFIX=       use a libgit2 already built and installed\n'
	@printf '\nCLIPS is built from one of three sources, selected with\n'
	@printf 'CLIPS_VERSION. This build uses %s: %s.\n' \
	    '$(CLIPS_VERSION)' '$(CLIPS_ORIGIN)'
	@printf '\n  CLIPS_VERSION=6.4.2   the release tarball from SourceForge\n'
	@printf '  CLIPS_VERSION=svn-6x  branches/64x, pinned at r%s\n' '$(CLIPS_SVN_6X_REV)'
	@printf '  CLIPS_VERSION=svn-7x  branches/70x, pinned at r%s\n' '$(CLIPS_SVN_7X_REV)'
	@printf '  CLIPS_SVN_REV=        build a branch at another revision,\n'
	@printf '                        or at HEAD (needs svn installed)\n'
	@printf '  CLIPS_SVN_URL=        take the branch from somewhere else\n'
	@printf '\nEach version is fetched and built under its own directory, and\n'
	@printf 'vendor/clips points at the one built last. "make print-clips"\n'
	@printf 'says which that is. "make -s print-cflags" and "make -s print-ldlibs"\n'
	@printf 'print the flags this configuration compiles and links with.\n'
	@printf '\ninstall goes to %s and needs no privileges.\n' '$(PREFIX)'
	@printf 'For a system-wide location, name it:\n'
	@printf '\n      sudo make install PREFIX=/usr/local\n'
	@printf '\nOther variables: PREFIX, VERSION, CLIPS (which binary the tests\n'
	@printf 'run), VALGRIND, COV_CC, GCOV.\n'

# ---------------------------------------------------------------------------
# Line coverage of the wrappers, and the list of wrappers that no test
# entered.
# ---------------------------------------------------------------------------
coverage: clips
	mkdir -p "$(COVDIR)"
	cp userfunctions.c "$(COVDIR)/"
	$(COV_CC) -c -D$(CLIPS_OS) $(COV_CFLAGS) $(LIBGIT2_CFLAGS) -I"$(BUILD_DIR)" \
	    -o "$(COVDIR)/userfunctions.o" "$(COVDIR)/userfunctions.c"
	cp "$(BUILD_DIR)/libclips.a" "$(COVDIR)/libclips.a"
	ar d "$(COVDIR)/libclips.a" userfunctions.o
	ar r "$(COVDIR)/libclips.a" "$(COVDIR)/userfunctions.o"
	$(COV_CC) -o "$(COVDIR)/clips" "$(BUILD_DIR)/main.o" \
	    -L"$(COVDIR)" -lclips --coverage $(LDLIBS)
	rm -f "$(COVDIR)/userfunctions.gcda"
	: > "$(COVDIR)/never-entered.txt"
	CLIPS="$(COVDIR)/clips" ./tests/run.sh || true
	cd "$(COVDIR)" && $(GCOV) -b -f userfunctions.c > by-function.txt
	@echo
	@sed -n "/^File .*userfunctions\.c/,/^Creating/p" "$(COVDIR)/by-function.txt" | grep -v '^Creating'
	@grep -v '^[[:space:]]*//' userfunctions.c | grep 'AddUDF(' \
	    | grep -oE '"Git[A-Za-z0-9_]*Function"' | tr -d '"' | sort -u \
	    > "$(COVDIR)/registered-udfs.txt"
	@awk -v out="$(COVDIR)/never-entered.txt" \
	     'NR==FNR { udf[$$0]=1; next } \
	      /^Function / { fn=substr($$2,2,length($$2)-2); want=(fn in udf); next } \
	      /^Lines executed:/ && want { \
	        want=0; p=$$2; sub(/^executed:/,"",p); sub(/%$$/,"",p); \
	        n=$$4+0; fns++; tot+=n; cov+=p*n/100; \
	        if (p+0==0) { zero++; print fn > out } } \
	      END { if (tot) printf "\nUDF handlers only: %d registered, %d lines, %.1f%% executed (%d never entered)\n", fns, tot, 100*cov/tot, zero }' \
	    "$(COVDIR)/registered-udfs.txt" "$(COVDIR)/by-function.txt"
	@echo
	@echo "per-function detail: $(COVDIR)/by-function.txt"
	@echo "annotated source:    $(COVDIR)/userfunctions.c.gcov"
	@echo "never entered:       $(COVDIR)/never-entered.txt"
