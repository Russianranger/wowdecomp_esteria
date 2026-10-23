#include "ASECM3/Semantics.h"
#include <iostream>
#include <sstream>
static std::string Quote(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='\\'||c=='\"')out+='\\';if(c<32)out+=' ';else out+=c;}return out+'\"';}
// Tab-separated source identity / index / flags / original name on stdin.
int main(int argc,char** argv){
    const bool world=argc==2&&std::string(argv[1])=="--world";
    std::vector<ASECM3::ActionMapping> mappings;
    std::string line;
    while(std::getline(std::cin,line))try{
        std::istringstream row(line);std::string model,index,flags,name;
        if(!std::getline(row,model,'\t')||!std::getline(row,index,'\t')||!std::getline(row,flags,'\t')||!std::getline(row,name))throw ASECM3::Error("Expected model, index, flags, name");
        const auto result=ASECM3::ResolveAction(name,model,std::stoul(index),std::stoul(flags));
        if(world){if(std::stoul(index)!=mappings.size())throw ASECM3::Error("World inspection requires contiguous source indices");mappings.push_back(result);continue;}
        std::cout<<"{\"ok\":true,\"action\":"<<result.action<<",\"excluded\":"<<(result.excluded?"true":"false")<<",\"global\":"<<(result.global?"true":"false")<<",\"weight\":"<<result.weight<<",\"confidence\":"<<Quote(result.confidence)<<",\"reason\":"<<Quote(result.reason)<<"}\n";
    }catch(const std::exception& e){std::cout<<"{\"ok\":false,\"error\":"<<Quote(e.what())<<"}\n";}
    if(world)for(const auto action:{0u,4u,5u,69u,143u}){
        const auto clip=ASECM3::SelectWorldAction(mappings,action);
        std::cout<<"{\"action\":"<<action<<",\"clip\":"<<clip<<",\"fallback\":"<<(clip>=0&&mappings[clip].action!=int32_t(action)?"true":"false")<<"}\n";
    }
}
