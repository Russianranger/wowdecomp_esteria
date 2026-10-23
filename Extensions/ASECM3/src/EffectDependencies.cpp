#include "ASECM3/EffectDependencies.h"
#include <algorithm>
#include <cctype>
namespace ASECM3 {
std::string EffectDeclarationPath(const std::string& model){return model+".effectbank";}
std::string ResolveEffectDeclaration(const std::string& model,const std::vector<uint8_t>& bytes){
    if(bytes.empty()||bytes.size()>1024)throw Error("M3 effect declaration must contain 1..1024 bytes");
    std::string text(bytes.begin(),bytes.end());if(text.find('\0')!=std::string::npos)throw Error("M3 effect declaration contains a NUL byte");
    const auto newline=text.find('\n');if(newline==std::string::npos)throw Error("M3 effect declaration has no header line");
    auto header=text.substr(0,newline);if(!header.empty()&&header.back()=='\r')header.pop_back();
    if(header!="ASECM3_EFFECT_BANK_V1")throw Error("Unknown M3 effect declaration version");
    auto name=text.substr(newline+1);if(!name.empty()&&name.back()=='\n')name.pop_back();if(!name.empty()&&name.back()=='\r')name.pop_back();
    if(name.empty()||name.size()>255||name.front()==' '||name.back()==' '||name.find_first_of("/\\:\r\n")!=std::string::npos)throw Error("Effect bank must be one adjacent M3A filename");
    for(unsigned char c:name)if(c<32||c==127)throw Error("Effect bank filename contains control characters");
    auto lower=name;for(auto& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(lower.size()<=4||lower.substr(lower.size()-4)!=".m3a")throw Error("Effect bank filename must end in .m3a");
    const auto separator=model.find_last_of("/\\");return (separator==std::string::npos?std::string{}:model.substr(0,separator+1))+name;
}
}
