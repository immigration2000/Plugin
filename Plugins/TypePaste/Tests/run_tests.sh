#!/usr/bin/env bash
# TypePaste pure-logic tests (parser / C++ generator) - runs without Unreal Engine
#
#   ./run_tests.sh              build + golden comparison + fuzzing
#   UPDATE=1 ./run_tests.sh     overwrite expected/ with the current output (only for intended output changes)
#   CXX=clang++ ./run_tests.sh  choose the compiler (default g++)
#   FUZZ_ITERS=20000 ./run_tests.sh
#
# Files that use editor APIs (BlueprintBuilder, CppExporter, Panel, Module) are not built here.
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
SRC=../Source/TypePaste/Private
PURE=("$SRC/TypePasteTypes.cpp" "$SRC/TypePasteParser.cpp" "$SRC/TypePasteCppWriter.cpp")
FLAGS=(-std=c++20 -g -Wall -Wextra -Wshadow -Wno-unused-parameter -I mock -I "$SRC")
if [[ "${SANITIZE:-1}" == "1" && "$(uname -s)" == "Linux" ]]; then
	FLAGS+=(-fsanitize=address,undefined)
fi

mkdir -p build
echo "[build] $CXX"
"$CXX" "${FLAGS[@]}" test_main.cpp "${PURE[@]}" -o build/tptest
"$CXX" "${FLAGS[@]}" fuzz.cpp "${PURE[@]}" -o build/fuzz

fail=0
for input in inputs/*.txt; do
	name="$(basename "$input" .txt)"
	./build/tptest "$input" > "build/$name.out"
	if [[ "${UPDATE:-0}" == "1" ]]; then
		cp "build/$name.out" "expected/$name.out"
		echo "[update] expected/$name.out"
	elif diff -u "expected/$name.out" "build/$name.out" > "build/$name.diff"; then
		echo "[ok] $name"
	else
		echo "[FAIL] $name  (difference: build/$name.diff)"
		fail=1
	fi
done

echo "[fuzz] ${FUZZ_ITERS:-5000} iterations"
./build/fuzz inputs/*.txt

if [[ "$fail" != "0" ]]; then
	echo "Output differs from the golden files. If the change is intended, update them with UPDATE=1."
	exit 1
fi
echo "All tests passed"
