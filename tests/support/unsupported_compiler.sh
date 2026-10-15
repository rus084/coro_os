#!/usr/bin/env sh
# Deterministic negative fixture for script orchestration, independent of the
# user's host compiler gaining genuine noalloc support.
echo 'intentional unsupported compiler fixture' >&2
exit 1
