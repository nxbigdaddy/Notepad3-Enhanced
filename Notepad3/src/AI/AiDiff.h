#pragma once
#include <algorithm>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <utility>
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
    size_t remaining=CellLimit;
    std::unordered_map<std::wstring_view,size_t> identities;
    std::vector<size_t> aIds(n),bIds(m);
    auto identify=[&](std::wstring_view line) { auto added=identities.emplace(line,identities.size()); return added.first->second; };
    for(size_t i=0;i<n;++i) aIds[i]=identify(before.substr(a[i],a[i+1]-a[i]));
    for(size_t j=0;j<m;++j) bIds[j]=identify(after.substr(b[j],b[j+1]-b[j]));
    std::function<void(size_t,size_t,size_t,size_t,unsigned)> align;
    align=[&](size_t ab,size_t ae,size_t bb,size_t be,unsigned depth) {
        while(ab<ae && bb<be && aIds[ab]==bIds[bb]) { ++ab; ++bb; }
        while(ae>ab && be>bb && aIds[ae-1]==bIds[be-1]) { --ae; --be; }
        size_t an=ae-ab,bn=be-bb;
        if(!an || !bn) { add(a[ab],a[ae],b[bb],b[be]); return; }
        if(depth<32 && bn+1>remaining/(an+1)) {
            // Patience anchors: unique common lines ordered by a longest increasing subsequence.
            struct Count { size_t aCount=0,bCount=0,bIndex=0; };
            std::vector<std::pair<size_t,size_t>> candidates;
            {
                std::unordered_map<size_t,Count> counts;
                for(size_t i=ab;i<ae;++i) ++counts[aIds[i]].aCount;
                for(size_t j=bb;j<be;++j) { auto& count=counts[bIds[j]]; ++count.bCount; count.bIndex=j; }
                for(size_t i=ab;i<ae;++i) { const auto& count=counts[aIds[i]]; if(count.aCount==1 && count.bCount==1) candidates.emplace_back(i,count.bIndex); }
            }
            if(candidates.empty() && an==bn) {
                for(size_t offset=0;offset<an;++offset) if(aIds[ab+offset]==bIds[bb+offset]) candidates.emplace_back(ab+offset,bb+offset);
            }
            if(!candidates.empty()) {
                const size_t none=static_cast<size_t>(-1);
                std::vector<size_t> tails,previous(candidates.size(),none);
                for(size_t k=0;k<candidates.size();++k) {
                    auto position=std::lower_bound(tails.begin(),tails.end(),candidates[k].second,[&](size_t index,size_t value) { return candidates[index].second<value; });
                    size_t slot=static_cast<size_t>(position-tails.begin());
                    if(slot) previous[k]=tails[slot-1];
                    if(position==tails.end()) tails.push_back(k); else *position=k;
                }
                std::vector<size_t> anchors;
                for(size_t k=tails.back();k!=none;k=previous[k]) anchors.push_back(k);
                std::reverse(anchors.begin(),anchors.end());
                for(size_t k:anchors) { auto anchor=candidates[k]; align(ab,anchor.first,bb,anchor.second,depth+1); ab=anchor.first+1;bb=anchor.second+1; }
                align(ab,ae,bb,be,depth+1); return;
            }
        }
        if(bn+1>remaining/(an+1)) { result.precise=false; add(a[ab],a[ae],b[bb],b[be]); return; }
        size_t cells=(an+1)*(bn+1); remaining-=cells;
        std::vector<unsigned> table(cells);
        auto same=[&](size_t i,size_t j) { return aIds[ab+i]==bIds[bb+j]; };
        for(size_t i=an;i-->0;) for(size_t j=bn;j-->0;)
            table[i*(bn+1)+j]=same(i,j)?1+table[(i+1)*(bn+1)+j+1]:(std::max)(table[(i+1)*(bn+1)+j],table[i*(bn+1)+j+1]);
        size_t i=0,j=0,beginA=0,beginB=0;
        while(i<an && j<bn) {
            if(same(i,j)) { add(a[ab+beginA],a[ab+i],b[bb+beginB],b[bb+j]); ++i; ++j; beginA=i;beginB=j; }
            else if(table[(i+1)*(bn+1)+j]>=table[i*(bn+1)+j+1]) ++i;
            else ++j;
        }
        add(a[ab+beginA],a[ae],b[bb+beginB],b[be]);
    };
    align(0,n,0,m,0); return result;
}
}
