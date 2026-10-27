#!/bin/sh
# Smoke-test orchestration with fake tools; never install packages or open a GUI.
set -eu
root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/project with spaces" "$tmp/tools"
cp "$root/setup.sh" "$root/run.sh" "$tmp/project with spaces/"
export TEST_PROJECT="$tmp/project with spaces" TEST_LOG="$tmp/commands"
export PATH="$tmp/tools:$PATH"

cat > "$tmp/tools/mock" <<'MOCK'
#!/bin/sh
set -eu
name=${0##*/}
printf '%s' "$name" >> "$TEST_LOG"
printf ' <%s>' "$@" >> "$TEST_LOG"
printf '\n' >> "$TEST_LOG"
case "$name" in
    uname) echo "${TEST_OS:-Darwin}" ;;
    xcode-select) exit "${TEST_XCODE_FAIL:-0}" ;;
    brew)
        case "$1" in
            list) [ "$3" != cmake ] ;;
            --prefix) printf '%s\n' "${0%/*}" ;;
            install) : ;;
        esac ;;
    git)
        if [ "$2" = status ]; then
            printf '%s\n' "${TEST_SUBMODULE_STATUS:--abc ui}"
        fi ;;
    cmake)
        if [ "$1" = --build ]; then
            exit "${TEST_BUILD_FAIL:-0}"
        fi
        mkdir -p "$TEST_PROJECT/build"
        touch "$TEST_PROJECT/build/CMakeCache.txt" ;;
    main) [ "$PWD" = "$TEST_PROJECT" ] ;;
esac
MOCK
chmod +x "$tmp/tools/mock"
for tool in uname xcode-select brew git cmake; do
    ln -s mock "$tmp/tools/$tool"
done
cd "$tmp"

if "$TEST_PROJECT/run.sh" > /dev/null 2>&1; then
    echo "run.sh must reject an unconfigured checkout" >&2
    exit 1
fi
TEST_OS=Linux
export TEST_OS
if "$TEST_PROJECT/setup.sh" > /dev/null 2>&1; then exit 1; fi
unset TEST_OS
TEST_XCODE_FAIL=1
export TEST_XCODE_FAIL
if "$TEST_PROJECT/setup.sh" > /dev/null 2>&1; then exit 1; fi
unset TEST_XCODE_FAIL
TEST_SUBMODULE_STATUS='+abc ui'
export TEST_SUBMODULE_STATUS
if "$TEST_PROJECT/setup.sh" > /dev/null 2>&1; then exit 1; fi
! grep -q '^git <submodule> <update>' "$TEST_LOG"
unset TEST_SUBMODULE_STATUS

"$TEST_PROJECT/setup.sh"
grep -q '^brew <install> <cmake>$' "$TEST_LOG"
grep -q '^git <submodule> <update> <--init> <--recursive>$' "$TEST_LOG"
grep -q '<-DUSE_FREERTOS=OFF> <-DPO_UI_CORE_ONLY=OFF> <-DLVGL_PRO_PROJECT_DIR=>' "$TEST_LOG"
: > "$TEST_LOG"
TEST_SUBMODULE_STATUS=' abc ui'
export TEST_SUBMODULE_STATUS
"$TEST_PROJECT/setup.sh"
! grep -q '^git <submodule> <update>' "$TEST_LOG"

mkdir -p "$TEST_PROJECT/bin"
ln -s "$tmp/tools/mock" "$TEST_PROJECT/bin/main"
"$TEST_PROJECT/run.sh" --screen language --screenshot '/tmp/image with spaces.ppm'
grep -q '^cmake <--build> <build> <--parallel> <8>$' "$TEST_LOG"
grep -q '^main <--screen> <language> <--screenshot> </tmp/image with spaces.ppm>$' "$TEST_LOG"
: > "$TEST_LOG"
TEST_BUILD_FAIL=1
export TEST_BUILD_FAIL
if "$TEST_PROJECT/run.sh" > /dev/null 2>&1; then exit 1; fi
! grep -q '^main' "$TEST_LOG"
echo 'build scripts: PASS'
