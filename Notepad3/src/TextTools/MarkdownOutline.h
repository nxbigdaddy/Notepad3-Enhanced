#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
namespace np3 {
struct MarkdownHeading { unsigned level; size_t line, offset; std::string text; };
// Byte offsets refer to the original buffer; CRLF, LF and CR are supported.
inline std::vector<MarkdownHeading> MarkdownOutline(std::string_view source) {
    if(source.size()>16*1024*1024)throw std::length_error("outline input limit");
    std::vector<MarkdownHeading> result;size_t start=0,line=0,previousOffset=0,previousLine=0;std::string previous;
    char fence=0;size_t fenceLength=0;
    auto add=[&](unsigned level,size_t number,size_t offset,std::string_view text){if(result.size()==4096)throw std::length_error("outline heading limit");result.push_back({level,number,offset,std::string(text)});};
    auto trim=[](std::string_view value){while(!value.empty() && (value.front()==' ' || value.front()=='\t'))value.remove_prefix(1);while(!value.empty() && (value.back()==' ' || value.back()=='\t'))value.remove_suffix(1);return value;};
    while(start<source.size()) {
        size_t end=start;while(end<source.size() && source[end]!='\r' && source[end]!='\n')++end;
        auto raw=source.substr(start,end-start);auto value=raw;size_t indent=0;while(indent<value.size() && value[indent]==' ')++indent;
        if(indent<=3)value.remove_prefix(indent);else value={};
        bool handled=false;
        if(!value.empty() && (value.front()=='`' || value.front()=='~')) {
            const char marker=value.front();size_t count=0;while(count<value.size() && value[count]==marker)++count;
            if(fence){if(marker==fence && count>=fenceLength && trim(value.substr(count)).empty()){fence=0;handled=true;}}
            else if(count>=3 && (marker!='`' || value.substr(count).find('`')==std::string_view::npos)){fence=marker;fenceLength=count;handled=true;}
        }
        if(fence || handled || value.empty()){previous.clear();}
        else {
            size_t hashes=0;while(hashes<value.size() && value[hashes]=='#')++hashes;
            if(hashes && hashes<=6 && (hashes==value.size() || value[hashes]==' ' || value[hashes]=='\t')) {
                auto title=trim(value.substr(hashes));size_t close=title.size();while(close && title[close-1]=='#')--close;
                if(close<title.size() && (close==0 || title[close-1]==' ' || title[close-1]=='\t'))title=trim(title.substr(0,close));
                add(static_cast<unsigned>(hashes),line,start,title);previous.clear();
            } else {
                auto underline=trim(value);bool setext=!underline.empty() && (underline.front()=='=' || underline.front()=='-');
                if(setext)for(char c:underline)if(c!=underline.front()){setext=false;break;}
                if(setext && !previous.empty()){add(underline.front()=='='?1u:2u,previousLine,previousOffset,previous);previous.clear();}
                else {previous=std::string(trim(value));previousOffset=start;previousLine=line;}
            }
        }
        start=end;if(start<source.size() && source[start]=='\r'){++start;if(start<source.size() && source[start]=='\n')++start;}else if(start<source.size())++start;++line;
    }
    return result;
}
}
