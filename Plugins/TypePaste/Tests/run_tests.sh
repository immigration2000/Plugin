#!/usr/bin/env bash
# TypePaste 순수 로직(파서 / C++ 생성기) 테스트 — 언리얼 엔진 없이 실행
#
#   ./run_tests.sh              빌드 + 골든 비교 + 퍼징
#   UPDATE=1 ./run_tests.sh     현재 출력으로 expected/ 갱신 (의도한 출력 변경일 때만)
#   CXX=clang++ ./run_tests.sh  컴파일러 지정 (기본 g++)
#   FUZZ_ITERS=20000 ./run_tests.sh
#
# 에디터 API 를 쓰는 파일(BlueprintBuilder, CppExporter, Panel, Module)은 여기서 빌드하지 않는다.
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
		echo "[FAIL] $name  (차이: build/$name.diff)"
		fail=1
	fi
done

echo "[fuzz] ${FUZZ_ITERS:-5000} iterations"
./build/fuzz inputs/*.txt

if [[ "$fail" != "0" ]]; then
	echo "골든 출력과 다릅니다. 의도한 변경이면 UPDATE=1 로 갱신하세요."
	exit 1
fi
echo "모든 테스트 통과"
