#include "MarkdownOutlineCore.h"
#include "MarkdownOutline.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
extern "C" void MarkdownOutline_Free(MD_OUTLINE_RESULT* result) {
    if(!result)return;
    for(size_t index=0;index<result->count;++index)free(result->items[index].title);
    free(result->items);*result={};
}
extern "C" int MarkdownOutline_Parse(const char* text,size_t length,MD_OUTLINE_RESULT* result) {
    if(!result)return 1;*result={};
    if(!text && length)return 1;
    if(length>16*1024*1024)return 2;
    if(length && (memchr(text,0,length) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,static_cast<int>(length),nullptr,0)))return 1;
    try {
        const auto headings=np3::MarkdownOutline({text?text:"",length});
        if(headings.empty())return 0;
        result->items=static_cast<MD_OUTLINE_ITEM*>(calloc(headings.size(),sizeof(MD_OUTLINE_ITEM)));
        if(!result->items)return 3;
        result->count=headings.size();
        for(size_t index=0;index<headings.size();++index){
            const auto& heading=headings[index];if(heading.text.size()>4096)throw std::length_error("outline title limit");
            auto& item=result->items[index];item.level=heading.level;item.line=heading.line;item.offset=heading.offset;
            item.title=static_cast<char*>(malloc(heading.text.size()+1));if(!item.title)throw std::bad_alloc();
            memcpy(item.title,heading.text.c_str(),heading.text.size()+1);
        }
        return 0;
    }catch(const std::length_error&){MarkdownOutline_Free(result);return 2;}catch(...){MarkdownOutline_Free(result);return 3;}
}
