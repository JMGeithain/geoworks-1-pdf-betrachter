#!/bin/sh
# Fail if the compiled core copies DS into a pointer (the library is called
# with the caller's DS, so such a pointer would read foreign memory).
# Temporary "push ds / mov ds,x / pop ds" sequences are fine.
if grep -nE ",[[:space:]]*ds[[:space:]]*$" "$1"; then
  echo "checkseg: DS used as a pointer segment - fix the source" >&2; exit 1; fi

# a static table read through DS (table not moved into PDFTABS)
if grep -nE "(^|[^:a-z])_[A-Za-z][A-Za-z0-9_]*\[" "$1" | grep -vE "es:_|ss:_|cs:_"; then
  echo "checkseg: table read through DS - add its type to mkwatcom.py" >&2; exit 1; fi
echo "checkseg: ok"
