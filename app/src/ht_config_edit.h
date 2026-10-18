#pragma once
#include "ht_library_paths.h"
#include <map>
namespace ht {
class ConfigEdit {
 public:
  explicit ConfigEdit(std::filesystem::path path):path_(std::move(path)){}
  void Set(const std::string& key,const std::string& value){values_[key]=value;}
  void Commit(){
    std::ifstream in(path_,std::ios::binary);std::string original((std::istreambuf_iterator<char>(in)),{}),text=original;in.close();
    for(const auto& [key,value]:values_){
      const auto assignment=key+" = "+value;size_t start=0;bool found=false;
      while(start<text.size()){
        const auto end=text.find('\n',start);auto line=text.substr(start,end==std::string::npos?std::string::npos:end-start);
        const auto first=line.find_first_not_of(" \t\r");if(first!=std::string::npos&&line[first]=='[')break;
        const auto eq=line.find('=');auto name=line.substr(0,eq);const auto last=name.find_last_not_of(" \t\r");if(last!=std::string::npos)name.resize(last+1);
        const auto left=name.find_first_not_of(" \t");if(left!=std::string::npos)name.erase(0,left);
        if(name==key){text.replace(start,line.size(),assignment);found=true;break;}
        if(end==std::string::npos)break;start=end+1;
      }
      if(!found)text=assignment+"\n"+text;
    }
    if(text==original)return;
    auto temp=path_;temp+=".new";std::filesystem::create_directories(path_.parent_path());
    {std::ofstream out(temp,std::ios::binary);out<<text;out.close();if(!out)throw std::runtime_error("Could not save game settings.");}
#if defined(_WIN32)
    if(!MoveFileExW(temp.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not replace game settings.");
#else
    std::filesystem::rename(temp,path_);
#endif
  }
 private:
  std::filesystem::path path_;
  std::map<std::string,std::string> values_;
};
}
