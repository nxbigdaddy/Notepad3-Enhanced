#pragma once
#include <algorithm>
#include <string_view>
#include <vector>
#include <unordered_map>
namespace np3 {
struct AiDiffRange { size_t beforeStart, beforeEnd, afterStart, afterEnd; };
struct AiDiffResult { std::vector<AiDiffRange> ranges; bool precise = true; };
inline AiDiffResult AiDifference(std::wstring_view before, std::wstring_view after) {
    AiDiffResult result;
    auto low=[](wchar_t c) { return c>=0xdc00 && c<=0xdfff; };
    auto add=[&](size_t bs,size_t be,size_t as,size_t ae) {
        while(bs<be && as<ae && before[bs]==after[as]) { ++bs; ++as; }
        if (bs<be && bs && low(before[bs])) { --bs; --as; }
        while(be>bs && ae>as && before[be-1]==after[ae-1]) { --be; --ae; }
        if (be<before.size() && be>bs && low(before[be])) { ++be; ++ae; }
        if(bs!=be || as!=ae) result.ranges.push_back({bs,be,as,ae});
    };
    auto lines=[](std::wstring_view text) {
        std::vector<size_t> starts{0};
        for(size_t i=0;i<text.size();++i) if(text[i]==L'\r' || text[i]==L'\n') {
            if(text[i]==L'\r' && i+1<text.size() && text[i+1]==L'\n') ++i;
            starts.push_back(i+1);
        }
        if(starts.back()!=text.size()) starts.push_back(text.size());
        return starts;
    };
    if(before==after) return result;
    auto a=lines(before), b=lines(after);
    size_t prefix=0, endA=a.size()-1, endB=b.size()-1;
    auto equalLine=[&](size_t i,size_t j) { return before.substr(a[i],a[i+1]-a[i])==after.substr(b[j],b[j+1]-b[j]); };
    while(prefix<endA && prefix<endB && equalLine(prefix,prefix)) ++prefix;
    while(endA>prefix && endB>prefix && equalLine(endA-1,endB-1)) { --endA; --endB; }
    a=std::vector<size_t>(a.begin()+prefix,a.begin()+endA+1);
    b=std::vector<size_t>(b.begin()+prefix,b.begin()+endB+1);
    size_t n=a.size()-1,m=b.size()-1;
    constexpr size_t CellLimit=1024*1024;
    if(m+1>CellLimit/(n+1)) { result.precise=false; add(a.front(),a.back(),b.front(),b.back()); return result; }
    std::vector<unsigned> table((n+1)*(m+1));
    std::unordered_map<std::wstring_view,size_t> identities;
    std::vector<size_t> aIds(n),bIds(m);
    auto identify=[&](std::wstring_view line) { auto added=identities.emplace(line,identities.size()); return added.first->second; };
    for(size_t i=0;i<n;++i) aIds[i]=identify(before.substr(a[i],a[i+1]-a[i]));
    for(size_t j=0;j<m;++j) bIds[j]=identify(after.substr(b[j],b[j+1]-b[j]));
    auto same=[&](size_t i,size_t j) { return aIds[i]==bIds[j]; };
    for(size_t i=n;i-->0;) for(size_t j=m;j-->0;)
        table[i*(m+1)+j]=same(i,j)?1+table[(i+1)*(m+1)+j+1]:(std::max)(table[(i+1)*(m+1)+j],table[i*(m+1)+j+1]);
    size_t i=0,j=0,beginA=0,beginB=0;
    while(i<n && j<m) {
        if(same(i,j)) { add(a[beginA],a[i],b[beginB],b[j]); ++i; ++j; beginA=i;beginB=j; }
        else if(table[(i+1)*(m+1)+j]>=table[i*(m+1)+j+1]) ++i;
        else ++j;
    }
    add(a[beginA],a.back(),b[beginB],b.back()); return result;
}
}
