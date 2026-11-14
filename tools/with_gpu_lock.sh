#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 zrecore-org contributors
# Run a command while holding the development machine's shared GPU test lock, so that GPU runs of this repository never overlap another project's GPU work on the same
# machine (fair ticket queue in $LOCK.queue, lock directory $LOCK with pid/lstart/tree/start/beat files, a heartbeat every minute, stale owners taken over).
# The lock path and the protocol are the ones the other project uses; see docs/PROVENANCE.md. It does NOT build or install anything.
#   tools/with_gpu_lock.sh <command> [args...]        (PGSTROM_TEST_LOCK overrides the lock path, for testing)
LOCK=${PGSTROM_TEST_LOCK:-/tmp/pgstrom_test.lock}
QUEUE=$LOCK.queue
TREE="exactmesh $*"
lstart_of() { ps -o lstart= -p "$1" 2>/dev/null | sed 's/^ *//'; }
alive() { [ -n "$1" ] || return 1; kill -0 "$1" 2>/dev/null || return 1; [ -z "$2" ] || [ "$(lstart_of "$1")" = "$2" ]; }
mkdir -p "$QUEUE" 2>/dev/null
seq=$(ls "$QUEUE" 2>/dev/null | tail -1 | sed 's/^0*//'); seq=$(( ${seq:-0} + 1 ))
while :; do
	TICKET=$QUEUE/$(printf %010d "$seq")
	mkdir "$TICKET" 2>/dev/null && break
	seq=$((seq + 1))
done
echo $$ > "$TICKET/pid"; lstart_of $$ > "$TICKET/lstart"; echo "$TREE" > "$TICKET/tree"
trap 'rm -rf "$TICKET"' EXIT INT TERM
waited=0
while :; do
	ahead=0
	for t in $(ls "$QUEUE" 2>/dev/null); do
		[ "$QUEUE/$t" = "$TICKET" ] && break
		tp=$(cat "$QUEUE/$t/pid" 2>/dev/null)
		if [ -n "$tp" ] && ! alive "$tp" "$(cat "$QUEUE/$t/lstart" 2>/dev/null)"; then rm -rf "$QUEUE/$t"; continue; fi
		ahead=$((ahead + 1))
	done
	[ $ahead -eq 0 ] && break
	[ $waited -eq 0 ] && echo "with_gpu_lock: $ahead run(s) ahead in the queue"
	waited=1; sleep 5
done
waited=0
while ! mkdir "$LOCK" 2>/dev/null; do
	owner=$(cat "$LOCK/pid" 2>/dev/null)
	if [ -n "$owner" ] && ! alive "$owner" "$(cat "$LOCK/lstart" 2>/dev/null)"; then rm -rf "$LOCK"; continue; fi
	[ $waited -eq 0 ] && echo "with_gpu_lock: waiting for the lock held by pid ${owner:-?} ($(cat "$LOCK/tree" 2>/dev/null))"
	waited=1; sleep 5
done
echo $$ > "$LOCK/pid"; lstart_of $$ > "$LOCK/lstart"; echo "$TREE" > "$LOCK/tree"; date +%s > "$LOCK/start"; touch "$LOCK/beat"
rm -rf "$TICKET"
( me=$$; while kill -0 "$me" 2>/dev/null; do touch "$LOCK/beat" 2>/dev/null; sleep 60; done ) >/dev/null 2>&1 &
HB=$!
trap 'kill $HB 2>/dev/null; rm -rf "$LOCK"' EXIT INT TERM
echo "with_gpu_lock: got the lock; running: $*"
"$@"
