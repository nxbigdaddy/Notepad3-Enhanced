#include "../Notepad3/src/TextTools/MarkdownOutline.h"
#include "../Notepad3/src/TextTools/MarkdownOutlineCore.h"
#include <cstring>
#include <cstdio>
#include <stdexcept>
int checks=0;void check(bool okay){++checks;if(!okay)throw std::runtime_error("outline assertion");}
int main(){try{
 auto headings=np3::MarkdownOutline("# 中文 #\r\ntext\r\nTitle\r\n=====\r\n## next\n");
 check(headings.size()==3);check(headings[0].text=="中文" && headings[0].level==1);check(headings[1].line==2 && headings[1].text=="Title");check(headings[2].offset==std::string("# 中文 #\r\ntext\r\nTitle\r\n=====\r\n").size());
 check(np3::MarkdownOutline("```lang\n# hidden\n```\n# shown").size()==1);
 check(np3::MarkdownOutline("~~~~\n# hidden\n~~~\n## hidden\n~~~~\n# shown").size()==1);
 check(np3::MarkdownOutline("    # indented\n#nospace\n####### too deep").empty());
 check(np3::MarkdownOutline("   ### title###\r#\r")[0].text=="title###");
 check(np3::MarkdownOutline("text\n\n---\n").empty());
 check(np3::MarkdownOutline("text\n---\n")[0].level==2);
 auto bom=np3::MarkdownOutline("\xef\xbb\xbf# BOM\n## next");check(bom.size()==2 && bom[0].offset==0 && bom[0].text=="BOM");check(bom[1].offset==9);
 check(np3::MarkdownOutline("<!--\n# hidden\n-->\n# visible").size()==1);
 check(np3::MarkdownOutline("<!-- # hidden -->\n# visible").size()==1);
 check(np3::MarkdownOutline("\tindented\n---\n").empty());
 check(np3::MarkdownOutline("> quote\n---\n").empty());
 check(np3::MarkdownOutline("- item\n---\n1. item\n===\n").empty());
 check(np3::MarkdownOutline("```\n<!--\n```\n# visible").size()==1);
 bool limited=false;try{np3::MarkdownOutline(std::string(16*1024*1024+1,'x'));}catch(const std::length_error&){limited=true;}check(limited);
 std::string many;for(int i=0;i<4097;++i)many+="# heading\n";limited=false;try{np3::MarkdownOutline(many);}catch(const std::length_error&){limited=true;}check(limited);
 MD_OUTLINE_RESULT parsed{};const char* fixture="# 中文\n## second";
 check(MarkdownOutline_Parse(fixture,strlen(fixture),&parsed)==0 && parsed.count==2);check(parsed.items[1].line==1 && !strcmp(parsed.items[0].title,"中文"));MarkdownOutline_Free(&parsed);check(!parsed.items && !parsed.count);MarkdownOutline_Free(&parsed);
 check(MarkdownOutline_Parse("\xff",1,&parsed)==1 && !parsed.items);check(MarkdownOutline_Parse("x\0x",3,&parsed)==1);check(MarkdownOutline_Parse(nullptr,0,&parsed)==0 && !parsed.count);
 check(MarkdownOutline_Parse(nullptr,1,&parsed)==1);check(MarkdownOutline_Parse("x",16*1024*1024+1,&parsed)==2);
 const auto longHeading="# "+std::string(4097,'x');check(MarkdownOutline_Parse(longHeading.data(),longHeading.size(),&parsed)==2 && !parsed.count && !parsed.items);
 std::printf("PASS %d Markdown outline checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s after %d checks\n",e.what(),checks);return 1;}}
