#pragma once
#include <fstream>
#include <string>
#include "VSTPluginLibrary.h"

inline std::string scanJsonString(const std::string& value) {
    std::string output="\"";
    for(unsigned char c:value) {
        if(c=='"'||c=='\\'){output+='\\';output+=char(c);}
        else if(c>=32)output+=char(c);
        else {const char hex[]="0123456789abcdef";output+="\\u00";output+=hex[c>>4];output+=hex[c&15];}
    }
    return output+'"';
}
inline int scanVSTPlugin(const std::wstring& path,const std::wstring& output) {
    std::ofstream file(output,std::ios::trunc);
    if(!file)return 2;
    const auto library=VSTPluginLibrary::getInstance(path);
    const int loaded=library->initialize();
    if(loaded<0) {file<<"{\"status\":\"FAIL\",\"load_error\":"<<loaded<<"}";return 1;}
    file<<"{\"status\":\"PASS\",\"classes\":[";
    for(int i=0;i<library->getVST3ClassCount();++i) {
        const auto uid=library->getVST3ClassUID(i);
        if(i)file<<',';
        file<<"{\"uid\":"<<scanJsonString(std::string(uid.begin(),uid.end()))
            <<",\"name\":"<<scanJsonString(library->getVST3ClassInfo(i).name)<<"}";
    }
    file<<"]}";return file.good()?0:3;
}
