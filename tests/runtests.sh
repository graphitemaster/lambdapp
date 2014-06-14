#!/usr/bin/bash

VERBOSE="${VERBOSE:-0}"
LAMBDAPP="../lambdapp"
CC="${CC:-cc}"

err() {
  local mesg="$1"; shift
  printf "*** ${mesg}\n" "$@" >&2
}

log() {
  local mesg="$1"; shift
  printf "${mesg}\n" "$@"
}

msg() {
  log "$@"
  local mesg="$1"; shift
  printf "==> ${mesg}\n" "$@" >&2
}

die() {
  err "$@"
  exit 1
}

logexec() {
  ((VERBOSE)) && msg "%s" "$*"
  "$@"
}

# setup
[[ -x ${LAMBDAPP} ]] || die 'failed to find lambdapp at: %s' "$LAMBDAPP"
exec >test.log || die 'failed to open ./test.log'

[[ -d pp  ]] || mkdir pp \
  || die 'failed to create directory for lambdapp output: pp/'
[[ -d obj ]] || mkdir obj \
  || die 'failed to create directory for test binaries: obj/'
[[ -d expected ]] || mkdir expected \
  || die 'failed to create directory to store expected output: expected/'

# functionality
preprocess() {
  local src="$1"; shift
  local dst="$1"; shift
  local flags=("$@")
  ((VERBOSE)) && msg '%s' "${LAMBDAPP} ${flags[*]} $src"
  ${LAMBDAPP} "${flags[@]}" "$src" > "$dst"
}

compile() {
  local src="$1"; shift
  local dst="$1"; shift
  local flags=("$@")
  logexec ${CC} "${flags[@]}" -o "$dst" "$src"
}

trytest() {
  local obj="$1"
  local expect="$2"

  local output="${expect}.got"
  local diffout="${expect}.diff"

  log 'running %s' "$obj"
  $obj > "$output"
  if diff -up "$expect" "$output" > "$diffout"; then
    rm "$diffout"
  else
    false
  fi
}

create_expect() {
  local src="$1"
  local dst="$2"
  sed -ne '/^\/\* OUTPUT:$/,$p' "$src" | sed -e 1d -e '$d' > "$dst"
}

runtest() {
  local file="$i"
  local csrc="pp/${file%.l.c}.p.c"
  local obj="obj/${file%.l.c}.x"
  local expect="expected/${file%.l.c}.txt"

  msg 'TEST: %s' "$file"

  if ! create_expect "$file" "$expect"; then
    let testppfail++
    msg 'FAIL: %s is illformatted' "$file"
    return
  fi
  if ! preprocess "$file" "$csrc"; then
    let testppfail++
    msg 'FAIL: %s failed to process' "$file"
    return
  fi
  if ! compile    "$csrc" "$obj"; then
    let testccfail++
    msg 'FAIL: %s failed to compile' "$file"
    return
  fi
  if ! trytest "$obj" "$expect"; then
    let textxxfail++
    msg 'FAIL: %s produced the wrong output' "$file"
    return
  fi
  let testsuccess++
}

summary() {
  if (( testcount == testsuccess )); then
    msg 'All tests succeeded'
    return
  fi
  msg 'Tests: %s' "$testcount"
  msg 'Succeeded: %s' "$testsuccess"
  ((testppfail)) && msg 'Failed to preprocess: %s' "$testppfail"
  ((testccfail)) && msg 'Failed to build:      %s' "$testccfail"
  ((testxxfail)) && msg 'Failed to execute:    %s' "$testxxfail"
}

tests=(*.l.c)
testcount="${#tests[@]}"
testppfail=0
testccfail=0
testxxfail=0
testsuccess=0
for i in *.l.c; do
  runtest "$i"
done
summary

if (( testsuccess == testcount )); then
  rm -r pp obj expected
fi
