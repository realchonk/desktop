#!/bin/sh
# raftfs test suite — exercises the FUSE mount end-to-end.
# Usage: sh test.sh /path/to/raftfs/binary
# Requires: fusermount3, a writable /tmp.

set -e
B="${1:-./raftfs}"
D="/tmp/raftfs-test-$$"
PASS=0
FAIL=0

cleanup() {
	pkill -x raftfs 2>/dev/null || true
	fusermount3 -u "$D/mnt" 2>/dev/null || true
	rm -rf "$D"
}
trap cleanup EXIT INT TERM

ok() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
ng() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

mkdir -p "$D/mnt"

echo "=== setup: single-node cluster ==="
"$B" format "$D/node" --id test1 --addr 127.0.0.1:18001 --bootstrap "test1=127.0.0.1:18001" >/dev/null 2>&1
"$B" start "$D/node" --mount "$D/mnt" >"$D/log" 2>&1 &
sleep 5

echo "=== basic file ops ==="
echo hello > "$D/mnt/a.txt"
[ "$(cat "$D/mnt/a.txt")" = "hello" ] && ok "write+read small file" || ng "write+read small file"

mkdir "$D/mnt/dir"
echo nested > "$D/mnt/dir/b.txt"
[ "$(cat "$D/mnt/dir/b.txt")" = "nested" ] && ok "nested file" || ng "nested file"

ln -s a.txt "$D/mnt/link"
[ "$(readlink "$D/mnt/link")" = "a.txt" ] && ok "symlink" || ng "symlink"

echo "=== large file ==="
head -c 50000 /dev/urandom > "$D/ref.bin"
cp "$D/ref.bin" "$D/mnt/big.bin"
cmp "$D/ref.bin" "$D/mnt/big.bin" && ok "large file round-trip" || ng "large file round-trip"

echo "=== encryption: lock ==="
mkdir "$D/mnt/secret"
echo mypass > "$D/mnt/secret/.raft-encryptdir"
sleep 1
echo "TOP SECRET" > "$D/mnt/secret/file.txt"
sleep 1
[ "$(cat "$D/mnt/secret/file.txt")" = "TOP SECRET" ] && ok "write+read in encrypted dir" || ng "write+read in encrypted dir"

# Verify blocks are ciphertext.
if grep -rql 'TOP SECRET' "$D/node/blocks/" 2>/dev/null; then
	ng "plaintext leaked to blocks"
else
	ok "blocks are ciphertext"
fi

echo "=== encryption: restart (lock) ==="
pkill -x raftfs 2>/dev/null || true
fusermount3 -u "$D/mnt" 2>/dev/null || true
sleep 1
rm -rf "$D/mnt"; mkdir -p "$D/mnt"
"$B" start "$D/node" --mount "$D/mnt" >"$D/log" 2>&1 &
sleep 5

echo "=== locked: ls shows only unlock ==="
ENTS=$(ls -A "$D/mnt/secret")
[ "$ENTS" = "unlock" ] && ok "locked dir shows only unlock" || ng "locked dir shows: [$ENTS]"

echo "=== locked: file.txt is hidden ==="
if cat "$D/mnt/secret/file.txt" >/dev/null 2>&1; then
	ng "file.txt visible while locked"
else
	ok "file.txt hidden while locked"
fi

echo "=== unlock ==="
echo mypass > "$D/mnt/secret/unlock"
sleep 1
[ "$(cat "$D/mnt/secret/file.txt")" = "TOP SECRET" ] && ok "unlock + read" || ng "unlock + read"

echo "=== wrong passphrase ==="
pkill -x raftfs 2>/dev/null || true
fusermount3 -u "$D/mnt" 2>/dev/null || true
sleep 1
rm -rf "$D/mnt"; mkdir -p "$D/mnt"
"$B" start "$D/node" --mount "$D/mnt" >"$D/log" 2>&1 &
sleep 5
if echo wrongpass > "$D/mnt/secret/unlock" 2>/dev/null; then
	sleep 1
	if cat "$D/mnt/secret/file.txt" >/dev/null 2>&1; then
		ng "wrong passphrase unlocked the dir"
	else
		ok "wrong passphrase rejected"
	fi
else
	ok "wrong passphrase returns error"
fi

echo "=== .raft-encryptdir not in root ==="
if ls -a "$D/mnt" | grep -q '.raft-encryptdir'; then
	ng ".raft-encryptdir visible in root"
else
	ok ".raft-encryptdir hidden in root"
fi

echo "=== .raft-encryptdir in top-level dir ==="
mkdir "$D/mnt/plain"
if ls -a "$D/mnt/plain" | grep -q '.raft-encryptdir'; then
	ok ".raft-encryptdir visible in top-level dir"
else
	ng ".raft-encryptdir hidden in top-level dir"
fi

echo "=== .raft-encryptdir NOT in nested dir ==="
mkdir "$D/mnt/plain/sub"
if ls -a "$D/mnt/plain/sub" | grep -q '.raft-encryptdir'; then
	ng ".raft-encryptdir visible in nested dir"
else
	ok ".raft-encryptdir hidden in nested dir"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
