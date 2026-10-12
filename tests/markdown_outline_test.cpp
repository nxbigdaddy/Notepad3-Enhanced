#include "../Notepad3/src/TextTools/MarkdownOutline.h"
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
 bool limited=false;try{np3::MarkdownOutline(std::string(16*1024*1024+1,'x'));}catch(const std::length_error&){limited=true;}check(limited);
 std::string many;for(int i=0;i<4097;++i)many+="# heading\n";limited=false;try{np3::MarkdownOutline(many);}catch(const std::length_error&){limited=true;}check(limited);
 std::printf("PASS %d Markdown outline checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s after %d checks\n",e.what(),checks);return 1;}}
