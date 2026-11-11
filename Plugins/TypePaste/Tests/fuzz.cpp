#include "CoreMinimal.h"
#include "TypePasteParser.h"
#include "TypePasteCppWriter.h"
#include <random>
#include <fstream>
#include <sstream>
#include <iostream>
static FString FromU8(const std::string& In){FString O;size_t i=0;while(i<In.size()){unsigned char C=In[i];uint32_t CP;int N;if(C<0x80){CP=C;N=0;}else if((C>>5)==6){CP=C&0x1F;N=1;}else if((C>>4)==14){CP=C&0x0F;N=2;}else{CP=C&7;N=3;}for(int k=1;k<=N&&i+k<In.size();++k)CP=(CP<<6)|(In[i+k]&0x3F);O.AppendChar((wchar_t)CP);i+=N+1;}return O;}
int main(int argc,char**argv){
  std::vector<FString> Seeds;
  for(int i=1;i<argc;++i){std::ifstream f(argv[i]);std::stringstream ss;ss<<f.rdbuf();Seeds.push_back(FromU8(ss.str()));}
  const wchar_t* Pieces[]={L"UENUM(",L"USTRUCT(",L")",L"(",L"{",L"}",L";",L",",L"enum",L"class",L"struct",L"FFoo",L"EBar",L"=",L"<",L">",L"TArray",L"int32",L"UPROPERTY(",L"UMETA(",L"\"",L"'",L"//",L"/*",L"*/",L"\n",L" ",L":",L"::",L"operator",L"GENERATED_BODY()",L"public:",L"[",L"]",L"*",L"&",L"#",L"`",L"10.f",L"한글",L"x"};
  std::mt19937 rng(1234);
  int Iter=0;
  const char* EnvIters = std::getenv("FUZZ_ITERS"); const int Iters = EnvIters ? std::atoi(EnvIters) : 5000;
  for(int it=0;it<Iters;++it){
    FString S;
    if(!Seeds.empty() && (it%3)){
      const FString& Seed=Seeds[rng()%Seeds.size()];
      int a=rng()%(Seed.Len()+1), b=rng()%(Seed.Len()+1); if(a>b) std::swap(a,b);
      S = Seed.Mid(0,a) + Seed.Mid(b);           // cut a chunk
      int ins = rng()%(S.Len()+1);
      S = S.Mid(0,ins) + FString(Pieces[rng()%(sizeof(Pieces)/sizeof(*Pieces))]) + S.Mid(ins);
    } else {
      int n=rng()%60; for(int k=0;k<n;++k) S += Pieces[rng()%(sizeof(Pieces)/sizeof(*Pieces))];
    }
    FTPParseResult R = FTypePasteParser::Parse(S);
    FTPCppWriteOptions O; TArray<FTPMessage> M;
    FTypePasteCppWriter::WriteSingleFile(R, FString(), O, nullptr, M);
    FTypePasteCppWriter::WritePerType(R, O, nullptr, M);
    ++Iter;
  }
  std::cout<<"fuzz iterations ok: "<<Iter<<"\n";
}
