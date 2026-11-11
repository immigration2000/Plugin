// Test harness for TypePaste parser + C++ writer (compiled against the mock UE core).
#include "CoreMinimal.h"
#include "TypePasteParser.h"
#include "TypePasteCppWriter.h"
#include <iostream>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static std::string U8(const FString& In)
{
	std::string Out;
	for (wchar_t W : In.S)
	{
		uint32_t C = (uint32_t)W;
		if (C < 0x80) Out.push_back((char)C);
		else if (C < 0x800) { Out.push_back((char)(0xC0 | (C >> 6))); Out.push_back((char)(0x80 | (C & 0x3F))); }
		else if (C < 0x10000) { Out.push_back((char)(0xE0 | (C >> 12))); Out.push_back((char)(0x80 | ((C >> 6) & 0x3F))); Out.push_back((char)(0x80 | (C & 0x3F))); }
		else { Out.push_back((char)(0xF0 | (C >> 18))); Out.push_back((char)(0x80 | ((C >> 12) & 0x3F))); Out.push_back((char)(0x80 | ((C >> 6) & 0x3F))); Out.push_back((char)(0x80 | (C & 0x3F))); }
	}
	return Out;
}

static FString FromU8(const std::string& In)
{
	FString Out;
	size_t i = 0;
	while (i < In.size())
	{
		unsigned char C = In[i];
		uint32_t CP; int N;
		if (C < 0x80) { CP = C; N = 0; }
		else if ((C >> 5) == 6) { CP = C & 0x1F; N = 1; }
		else if ((C >> 4) == 14) { CP = C & 0x0F; N = 2; }
		else { CP = C & 0x07; N = 3; }
		for (int k = 1; k <= N; ++k) CP = (CP << 6) | (In[i + k] & 0x3F);
		Out.AppendChar((wchar_t)CP);
		i += N + 1;
	}
	return Out;
}

static const char* Sev(ETPSeverity S) { return S == ETPSeverity::Error ? "ERROR" : S == ETPSeverity::Warning ? "WARN " : "INFO "; }

class FStubTypeInfo : public ITPExternalTypeInfo
{
public:
	bool FindTypeHeader(const FString& TypeName, FString& OutInclude, FString& OutModule) const override
	{
		if (TypeName == FString(TEXT("FMyEngineThing"))) { OutInclude = TEXT("Engine/MyEngineThing.h"); OutModule = TEXT("Engine"); return true; }
		if (TypeName == FString(TEXT("EPhysicalSurface"))) { OutInclude = TEXT("Chaos/ChaosEngineInterface.h"); OutModule = TEXT("PhysicsCore"); return true; }
		return false;
	}
};

static void Dump(const char* Title, const FString& Src, bool bWrite = true)
{
	std::cout << "\n==================== " << Title << " ====================\n";
	FTPParseResult R = FTypePasteParser::Parse(Src);
	for (const FTPEnumDef& E : R.Enums)
	{
		std::cout << "ENUM " << U8(E.Name) << " scoped=" << E.bScoped << " underlying='" << U8(E.UnderlyingType) << "' macro=" << E.bHadMacro
			<< " spec='" << U8(E.MacroSpecifiers) << "' comment='" << U8(E.Comment) << "'\n";
		for (const FTPEnumEntry& N : E.Entries)
			std::cout << "   - " << U8(N.Name) << " value='" << U8(N.ValueExpr) << "' umeta='" << U8(N.UMetaRaw) << "' display='" << U8(N.DisplayName)
				<< "' hidden=" << N.bHidden << " comment='" << U8(N.Comment) << "'\n";
	}
	for (const FTPStructDef& S : R.Structs)
	{
		std::cout << "STRUCT " << U8(S.Name) << " parent='" << U8(S.ParentName) << "' parentText='" << U8(S.ParentText) << "' macro=" << S.bHadMacro
			<< " spec='" << U8(S.MacroSpecifiers) << "' comment='" << U8(S.Comment) << "'\n";
		for (const FTPStructItem& I : S.Items)
		{
			if (I.Kind == ETPStructItemKind::Raw) { std::cout << "   [RAW] comment='" << U8(I.Comment) << "' text=<<" << U8(I.RawText) << ">>\n"; continue; }
			const FTPStructMember& M = S.Members[I.MemberIndex];
			std::cout << "   - " << U8(M.Name) << " : " << U8(M.Type.ToString()) << " (text='" << U8(M.TypeText) << "') uprop=" << M.bHadUProperty
				<< " spec='" << U8(M.PropertySpecifiers) << "' init='" << U8(M.InitText) << "' default='" << U8(M.DefaultExpr) << "' dim='" << U8(M.ArrayDim)
				<< "' bits=" << M.BitfieldWidth << " comment='" << U8(M.Comment) << "'\n";
		}
	}
	for (const FTPMessage& M : R.Messages) std::cout << "  " << Sev(M.Severity) << " " << U8(M.Text) << "\n";

	if (!bWrite) return;
	FTPCppWriteOptions Opt; Opt.ApiMacro = TEXT("MYGAME_API");
	FStubTypeInfo Info;
	TArray<FTPMessage> Msgs;
	FTPCppFile F = FTypePasteCppWriter::WriteSingleFile(R, FString(), Opt, &Info, Msgs);
	std::cout << "---- single file: " << U8(F.FileName) << " ----\n" << U8(F.Content);
	for (const FTPMessage& M : Msgs) std::cout << "  " << Sev(M.Severity) << " " << U8(M.Text) << "\n";
}

static std::string ReadFile(const char* Path)
{
	std::ifstream In(Path, std::ios::binary);
	std::stringstream SS; SS << In.rdbuf();
	return SS.str();
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	// Windows 텍스트 모드는 \n 을 \r\n 으로 바꿔 골든 비교가 깨지므로 바이트 그대로 출력
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	for (int i = 1; i < argc; ++i)
	{
		Dump(argv[i], FromU8(ReadFile(argv[i])));
	}

	// Per-type mode on the first input
	if (argc > 1)
	{
		FTPParseResult R = FTypePasteParser::Parse(FromU8(ReadFile(argv[1])));
		FTPCppWriteOptions Opt; Opt.ApiMacro = TEXT("MYGAME_API");
		TArray<FTPMessage> Msgs;
		TArray<FTPCppFile> Files = FTypePasteCppWriter::WritePerType(R, Opt, nullptr, Msgs);
		std::cout << "\n==================== PER-TYPE (" << argv[1] << ") ====================\n";
		for (const FTPCppFile& F : Files) std::cout << "---- " << U8(F.FileName) << " ----\n" << U8(F.Content);
		for (const FTPMessage& M : Msgs) std::cout << "  " << Sev(M.Severity) << " " << U8(M.Text) << "\n";
	}

	// Default value conversion
	std::cout << "\n==================== DEFAULT VALUES ====================\n";
	struct { ETPPrimitiveKind K; const wchar_t* E; } Cases[] = {
		{ ETPPrimitiveKind::Real, L"10.f" }, { ETPPrimitiveKind::Real, L"-.5f" }, { ETPPrimitiveKind::Real, L"3." }, { ETPPrimitiveKind::Real, L"1.5e-3f" },
		{ ETPPrimitiveKind::Real, L"100" }, { ETPPrimitiveKind::Real, L"UE_PI" }, { ETPPrimitiveKind::Int, L"0x1F" }, { ETPPrimitiveKind::Int, L"-42" },
		{ ETPPrimitiveKind::Int, L"1'000'000" }, { ETPPrimitiveKind::Int64, L"5000000000LL" }, { ETPPrimitiveKind::Byte, L"300" }, { ETPPrimitiveKind::Byte, L"255u" },
		{ ETPPrimitiveKind::Bool, L"true" }, { ETPPrimitiveKind::Bool, L"1" }, { ETPPrimitiveKind::String, L"TEXT(\"Hello \\\"World\\\"\")" },
		{ ETPPrimitiveKind::Name, L"NAME_None" }, { ETPPrimitiveKind::Name, L"FName(TEXT(\"Socket_R\"))" },
		{ ETPPrimitiveKind::Text, L"NSLOCTEXT(\"Ns\", \"Key\", \"검\")" }, { ETPPrimitiveKind::Text, L"FText::GetEmpty()" }, { ETPPrimitiveKind::String, L"TEXT(\"\")" },
	};
	for (auto& C : Cases)
	{
		FString Out;
		bool bOk = TypePasteUtils::ConvertDefaultValue(C.K, FString(C.E), Out);
		std::cout << "  " << U8(FString(C.E)) << " -> " << (bOk ? U8(Out) : std::string("(skip)")) << "\n";
	}

	std::cout << "\n==================== NAMING ====================\n";
	const wchar_t* Names[] = { L"EWeaponType", L"FWeaponData", L"WeaponType", L"Foo" };
	for (const wchar_t* N : Names)
	{
		std::cout << "  " << U8(FString(N)) << " : keep=" << U8(TypePasteUtils::MakeAssetName(N, N[0] == L'E', ETPAssetNaming::KeepCppName))
			<< " ES=" << U8(TypePasteUtils::MakeAssetName(N, N[0] == L'E', ETPAssetNaming::PrefixE_S_))
			<< " EF=" << U8(TypePasteUtils::MakeAssetName(N, N[0] == L'E', ETPAssetNaming::PrefixE_F_))
			<< " none=" << U8(TypePasteUtils::MakeAssetName(N, N[0] == L'E', ETPAssetNaming::NoPrefix)) << "\n";
	}
	return 0;
}
