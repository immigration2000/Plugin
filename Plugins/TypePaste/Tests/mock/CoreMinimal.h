// Minimal mock of the Unreal Engine core API subset used by TypePaste's pure logic,
// so the parser/writer can be compiled and tested with g++ outside of Unreal.
// Semantics mirror UE: FString == is case-insensitive, StartsWith/EndsWith/Find/Contains/Replace
// default to IgnoreCase, Equals defaults to CaseSensitive.
#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <cstdint>
#include <utility>
#include <climits>
#include <cstdio>
#include <cstdlib>

typedef wchar_t TCHAR;
typedef int32_t int32;
typedef int64_t int64;
typedef uint8_t uint8;
typedef uint64_t uint64;
typedef uint32_t uint32;
#define TEXT(x) L##x
#define INDEX_NONE (-1)
#define MAX_int32 (INT32_MAX)
#define MIN_int32 (INT32_MIN)

template <typename T> inline T&& MoveTemp(T& V) { return static_cast<T&&>(V); }

namespace ESearchCase { enum Type { CaseSensitive, IgnoreCase }; }
namespace ESearchDir { enum Type { FromStart, FromEnd }; }

struct FMath
{
	template <typename T> static T Min(T A, T B) { return A < B ? A : B; }
	template <typename T> static T Max(T A, T B) { return A > B ? A : B; }
};

template <typename T>
class TArray
{
public:
	std::vector<T> V;
	TArray() = default;
	TArray(std::initializer_list<T> L) : V(L) {}
	int32 Num() const { return (int32)V.size(); }
	bool IsEmpty() const { return V.empty(); }
	bool IsValidIndex(int32 I) const { return I >= 0 && I < Num(); }
	T& operator[](int32 I) { check(I); return V[I]; }
	const T& operator[](int32 I) const { check(I); return V[I]; }
	int32 Add(const T& X) { V.push_back(X); return Num() - 1; }
	int32 Add(T&& X) { V.push_back(std::move(X)); return Num() - 1; }
	int32 AddUnique(const T& X) { for (int32 i = 0; i < Num(); ++i) if (V[i] == X) return i; return Add(X); }
	bool Contains(const T& X) const { for (const T& E : V) if (E == X) return true; return false; }
	T& Last(int32 FromEnd = 0) { check(Num() - 1 - FromEnd); return V[Num() - 1 - FromEnd]; }
	const T& Last(int32 FromEnd = 0) const { check(Num() - 1 - FromEnd); return V[Num() - 1 - FromEnd]; }
	void Reset() { V.clear(); }
	void Empty() { V.clear(); }
	void SetNum(int32 N) { V.resize(N); }
	void Init(const T& X, int32 N) { V.assign(N, X); }
	void Reserve(int32 N) { V.reserve(N); }
	typename std::vector<T>::iterator begin() { return V.begin(); }
	typename std::vector<T>::iterator end() { return V.end(); }
	typename std::vector<T>::const_iterator begin() const { return V.begin(); }
	typename std::vector<T>::const_iterator end() const { return V.end(); }
private:
	void check(int32 I) const { if (I < 0 || I >= Num()) { fprintf(stderr, "TArray index out of range %d/%d\n", I, Num()); abort(); } }
};

// TArray<bool> must not use vector<bool> proxy semantics for operator[] returning T&
template <>
class TArray<bool>
{
public:
	std::vector<char> V;
	int32 Num() const { return (int32)V.size(); }
	void Init(bool X, int32 N) { V.assign(N, X ? 1 : 0); }
	struct Ref { char& C; operator bool() const { return C != 0; } Ref& operator=(bool B) { C = B ? 1 : 0; return *this; } };
	Ref operator[](int32 I) { if (I < 0 || I >= Num()) abort(); return Ref{ V[I] }; }
};

class FString
{
public:
	std::wstring S;
	FString() = default;
	FString(const TCHAR* In) : S(In ? In : L"") {}
	FString(const std::wstring& In) : S(In) {}

	int32 Len() const { return (int32)S.size(); }
	bool IsEmpty() const { return S.empty(); }
	const TCHAR* operator*() const { return S.c_str(); }
	TCHAR& operator[](int32 I) { if (I < 0 || I >= Len()) abort(); return S[I]; }
	const TCHAR& operator[](int32 I) const { if (I < 0 || I >= Len()) abort(); return S[I]; }

	void Reserve(int32 N) { S.reserve(N); }
	void Reset() { S.clear(); }
	void AppendChar(TCHAR C) { S.push_back(C); }
	FString& operator+=(const FString& O) { S += O.S; return *this; }
	FString& operator+=(const TCHAR* O) { S += O; return *this; }

	FString Mid(int32 Start, int32 Count = MAX_int32) const
	{
		if (Start < 0) { Count += Start; Start = 0; }
		if (Start > Len()) Start = Len();
		int64 End = (int64)Start + (Count < 0 ? 0 : Count);
		if (End > Len()) End = Len();
		return FString(S.substr(Start, (size_t)(End - Start)));
	}
	FString Left(int32 Count) const { return Mid(0, Count); }
	FString LeftChop(int32 Count) const { return Mid(0, Len() - Count); }
	FString Right(int32 Count) const { return Mid(Len() - Count < 0 ? 0 : Len() - Count); }
	FString RightChop(int32 Count) const { return Mid(Count); }
	void LeftChopInline(int32 Count) { *this = LeftChop(Count); }
	void RightChopInline(int32 Count) { *this = RightChop(Count); }

	static bool CharEqI(TCHAR A, TCHAR B) { return towlower(A) == towlower(B); }
	static bool EqualRange(const std::wstring& A, size_t Pos, const std::wstring& B, ESearchCase::Type C)
	{
		if (Pos + B.size() > A.size()) return false;
		for (size_t i = 0; i < B.size(); ++i)
		{
			if (C == ESearchCase::CaseSensitive ? A[Pos + i] != B[i] : !CharEqI(A[Pos + i], B[i])) return false;
		}
		return true;
	}

	bool StartsWith(const FString& P, ESearchCase::Type C = ESearchCase::IgnoreCase) const { return !P.IsEmpty() && EqualRange(S, 0, P.S, C); }
	bool StartsWith(const TCHAR* P, ESearchCase::Type C = ESearchCase::IgnoreCase) const { return StartsWith(FString(P), C); }
	bool EndsWith(const FString& P, ESearchCase::Type C = ESearchCase::IgnoreCase) const { return !P.IsEmpty() && P.Len() <= Len() && EqualRange(S, S.size() - P.S.size(), P.S, C); }
	bool EndsWith(const TCHAR* P, ESearchCase::Type C = ESearchCase::IgnoreCase) const { return EndsWith(FString(P), C); }

	bool Equals(const FString& O, ESearchCase::Type C = ESearchCase::CaseSensitive) const { return Len() == O.Len() && EqualRange(S, 0, O.S, C); }

	int32 Find(const FString& Sub, ESearchCase::Type C = ESearchCase::IgnoreCase, ESearchDir::Type Dir = ESearchDir::FromStart, int32 StartPos = INDEX_NONE) const
	{
		if (Sub.IsEmpty()) return INDEX_NONE;
		if (Dir == ESearchDir::FromStart)
		{
			for (int32 i = (StartPos == INDEX_NONE ? 0 : StartPos); i + Sub.Len() <= Len(); ++i) if (EqualRange(S, i, Sub.S, C)) return i;
		}
		else
		{
			for (int32 i = (StartPos == INDEX_NONE ? Len() - Sub.Len() : StartPos - Sub.Len()); i >= 0; --i) if (EqualRange(S, i, Sub.S, C)) return i;
		}
		return INDEX_NONE;
	}
	int32 Find(const TCHAR* Sub, ESearchCase::Type C = ESearchCase::IgnoreCase, ESearchDir::Type Dir = ESearchDir::FromStart, int32 StartPos = INDEX_NONE) const { return Find(FString(Sub), C, Dir, StartPos); }
	bool Contains(const FString& Sub, ESearchCase::Type C = ESearchCase::IgnoreCase) const { return Find(Sub, C) != INDEX_NONE; }

	FString Replace(const TCHAR* From, const TCHAR* To, ESearchCase::Type C = ESearchCase::IgnoreCase) const
	{
		FString F(From), T(To), Out;
		if (F.IsEmpty()) return *this;
		size_t i = 0;
		while (i < S.size())
		{
			if (EqualRange(S, i, F.S, C)) { Out.S += T.S; i += F.S.size(); }
			else { Out.S.push_back(S[i]); ++i; }
		}
		return Out;
	}

	static bool IsWs(TCHAR C) { return C == L' ' || C == L'\t' || C == L'\r' || C == L'\n' || C == L'\f' || C == L'\v'; }
	FString TrimStart() const { size_t i = 0; while (i < S.size() && IsWs(S[i])) ++i; return FString(S.substr(i)); }
	FString TrimEnd() const { size_t n = S.size(); while (n > 0 && IsWs(S[n - 1])) --n; return FString(S.substr(0, n)); }
	FString TrimStartAndEnd() const { return TrimStart().TrimEnd(); }

	int32 ParseIntoArrayLines(TArray<FString>& Out, bool bCullEmpty = true) const
	{
		Out.Reset();
		std::wstring Cur;
		for (size_t i = 0; i < S.size(); ++i)
		{
			TCHAR C = S[i];
			if (C == L'\r' || C == L'\n')
			{
				if (C == L'\r' && i + 1 < S.size() && S[i + 1] == L'\n') ++i;
				if (!(bCullEmpty && Cur.empty())) Out.Add(FString(Cur));
				Cur.clear();
				continue;
			}
			Cur.push_back(C);
		}
		if (!(bCullEmpty && Cur.empty())) Out.Add(FString(Cur));
		return Out.Num();
	}

	static FString FromInt(int32 N) { return FString(std::to_wstring(N)); }
	static FString Join(const TArray<FString>& Arr, const TCHAR* Sep)
	{
		FString Out;
		for (int32 i = 0; i < Arr.Num(); ++i) { if (i) Out.S += Sep; Out.S += Arr[i].S; }
		return Out;
	}
};

inline bool operator==(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::IgnoreCase); }
inline bool operator!=(const FString& A, const FString& B) { return !(A == B); }
inline FString operator+(const FString& A, const FString& B) { FString R = A; R += B; return R; }
inline FString operator+(const FString& A, const TCHAR* B) { FString R = A; R += B; return R; }
inline FString operator+(const TCHAR* A, const FString& B) { FString R(A); R += B; return R; }

struct FCString
{
	static int32 Atoi(const TCHAR* S) { return (int32)wcstol(S, nullptr, 10); }
};

template <typename K, typename V>
struct TPair { K Key; V Value; };

template <typename K, typename V>
class TMap
{
public:
	std::vector<TPair<K, V>> P;
	void Add(const K& Key, const V& Value) { for (auto& E : P) if (E.Key == Key) { E.Value = Value; return; } P.push_back({ Key, Value }); }
	V* Find(const K& Key) { for (auto& E : P) if (E.Key == Key) return &E.Value; return nullptr; }
	const V* Find(const K& Key) const { for (auto& E : P) if (E.Key == Key) return &E.Value; return nullptr; }
	V FindRef(const K& Key) const { const V* X = Find(Key); return X ? *X : V(); }
	bool Contains(const K& Key) const { return Find(Key) != nullptr; }
	int32 Num() const { return (int32)P.size(); }
};
