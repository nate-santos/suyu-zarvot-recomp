#define NOMINMAX
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
using BlockFn = void (*)(void*);
using Lookup = BlockFn (*)(uint64_t);
using View = int (*)(uint64_t*,uint64_t*,BlockFn**);
extern "C" {
static volatile unsigned stub_identity;
void stub0(void*) { stub_identity=1; } void stub1(void*) { stub_identity=2; }
void stub2(void*) { stub_identity=3; } void stub3(void*) { stub_identity=4; }
static unsigned allocations_on, allocations_fail;
void* lookup_alloc_on(size_t n,size_t bytes) { ++allocations_on; return std::calloc(n,bytes); }
void* lookup_alloc_fail(size_t,size_t) { ++allocations_fail; return nullptr; }
#define DECLARE(n) BlockFn n(uint64_t); void n##_build(void); int n##_view(uint64_t*,uint64_t*,BlockFn**);
DECLARE(baseline_low_on) DECLARE(candidate_low_on) DECLARE(baseline_low_fail) DECLARE(candidate_low_fail)
DECLARE(baseline_high_on) DECLARE(candidate_high_on) DECLARE(baseline_high_fail) DECLARE(candidate_high_fail)
}
struct Leg { const char* name; Lookup lookup; void (*build)(); View view; uint64_t base; bool allocated; };
#define LEG(n,b,a) {#n,n,n##_build,n##_view,b,a}
static const Leg legs[]{
    LEG(baseline_low_on,0x100,true), LEG(candidate_low_on,0x100,true),
    LEG(baseline_low_fail,0x100,false), LEG(candidate_low_fail,0x100,false),
    LEG(baseline_high_on,UINT64_C(0xfffffffffffffc00),true), LEG(candidate_high_on,UINT64_C(0xfffffffffffffc00),true),
    LEG(baseline_high_fail,UINT64_C(0xfffffffffffffc00),false), LEG(candidate_high_fail,UINT64_C(0xfffffffffffffc00),false)
};
static BlockFn Oracle(uint64_t pc,uint64_t base) {
    if (pc < base) return nullptr;
    const uint64_t offset = pc-base;
    if (offset & 3) return nullptr;
    if (offset <= 12) return stub0;
    if (offset >= 32 && offset <= 36) return stub1;
    if (offset >= 256 && offset <= 268) return stub2;
    if (offset >= 1008 && offset <= 1020) return stub3;
    return nullptr;
}
static uint64_t rng=UINT64_C(0x12345ad278c);
static uint64_t Random() { rng^=rng>>12; rng^=rng<<25; rng^=rng>>27; return rng*UINT64_C(0x2545f4914f6cdd1d); }
static bool Check(const Leg& leg,uint64_t pc,bool inject) {
    BlockFn actual=leg.lookup(pc), expected=Oracle(pc,leg.base);
    if (inject && leg.lookup==candidate_low_on && pc==0) actual=stub0;
    if (actual!=expected) {
        std::printf("MISMATCH leg=%s pc=%llx expected=%p actual=%p\n",leg.name,
                    (unsigned long long)pc,reinterpret_cast<void*>(expected),reinterpret_cast<void*>(actual));
        return false;
    }
    return true;
}
static bool Correct(bool inject) {
    const BlockFn stubs[]{stub0,stub1,stub2,stub3};
    for (unsigned i=0;i<4;++i) for (unsigned j=i+1;j<4;++j) if (stubs[i]==stubs[j]) return false;
    uint64_t checks=0;
    for (const auto& leg:legs) {
        leg.build(); leg.build(); // No retry after failed allocation.
        uint64_t lo=0,hi=0; BlockFn* index=nullptr;
        if (leg.view(&lo,&hi,&index)!=int(leg.allocated) ||
            (leg.allocated && (lo!=leg.base || hi!=leg.base+1020 || !index))) return false;
        const uint64_t edges[]{0,1,2,3,UINT64_MAX,UINT64_MAX-1,UINT64_MAX-2,UINT64_MAX-3,
            leg.base-4,leg.base-1,leg.base,leg.base+1,leg.base+12,leg.base+13,
            leg.base+1020,leg.base+1021,leg.base+1023,leg.base+1024};
        for (auto pc:edges) { if (!Check(leg,pc,inject)) return false; ++checks; }
        for (uint64_t offset=0;offset<2048;++offset) {
            if (!Check(leg,leg.base-512+offset,inject)) return false; ++checks;
        }
        for (unsigned i=0;i<200000;++i) {
            const uint64_t pc=(i&1) ? Random() : leg.base+(Random()%2048)-512;
            if (!Check(leg,pc,inject)) return false; ++checks;
        }
    }
    if (allocations_on!=4 || allocations_fail!=4) return false;
    std::printf("CORRECT checks=%llu legs=8 allocations_on=%u allocations_failed=%u\n",
                (unsigned long long)checks,allocations_on,allocations_fail);
    return true;
}
static volatile uint64_t sink;
struct Timed { double seconds; uint64_t checksum; };
static Timed Time(Lookup fn,const std::vector<uint64_t>& pcs,uint64_t repeats) {
    uint64_t sum=0;
    const auto start=std::chrono::steady_clock::now();
    for (uint64_t r=0;r<repeats;++r) for (uint64_t pc:pcs) sum+=reinterpret_cast<uintptr_t>(fn(pc));
    const auto stop=std::chrono::steady_clock::now(); sink=sum;
    return {std::chrono::duration<double>(stop-start).count(),sum};
}
static void Extent(const char* name,Lookup fn) {
#ifdef _WIN32
    DWORD64 base=0;
    const auto entry=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(fn),&base,nullptr);
    if (entry) std::printf("NATIVE_EXTENT function=%s entry_unwind_bytes=%lu\n",name,(unsigned long)(entry->EndAddress-entry->BeginAddress));
#else
    (void)name; (void)fn;
#endif
}
static bool Benchmark() {
    for (unsigned pair:{0u,4u}) {
        const auto& a=legs[pair]; const auto& b=legs[pair+1]; a.build(); b.build();
        Extent(a.name,a.lookup); Extent(b.name,b.lookup);
        for (unsigned dataset=0;dataset<5;++dataset) {
            std::vector<uint64_t> pcs;
            for (unsigned i=0;i<256;++i) {
                static const uint64_t hits[]{0,4,8,12,32,36,256,260,264,268,1008,1012,1016,1020};
                uint64_t pc=a.base+hits[i%14];
                if (dataset==1) pc=a.base+64+4*(i%32); // Aligned holes.
                if (dataset==2) pc=a.base+(i*4)+1; // Inside range, misaligned.
                if (dataset==3 || (dataset==4 && i%4==0)) pc=(i&1) ? a.base-4 : a.base+1024;
                pcs.push_back(pc);
            }
            uint64_t repeats=256;
            auto warm=Time(a.lookup,pcs,repeats);
            while (warm.seconds<0.01) { repeats*=2; warm=Time(a.lookup,pcs,repeats); }
            repeats=std::max<uint64_t>(1,static_cast<uint64_t>(repeats*0.06/warm.seconds));
            const uint64_t calls=repeats*pcs.size();
            std::array<std::vector<double>,2> ns;
            uint64_t checksum=0;
            for (unsigned i=0;i<8;++i) {
                const char arm="ABBAABBA"[i]; const auto result=Time(arm=='A'?a.lookup:b.lookup,pcs,repeats);
                if (i==0) checksum=result.checksum; else if (checksum!=result.checksum) return false;
                ns[arm=='A'?0:1].push_back(result.seconds*1e9/calls);
                std::printf("TIMING range=%s dataset=%u sequence=%u arm=%c calls=%llu seconds=%.9f checksum=%llx\n",
                    pair==0?"low":"high",dataset,i,arm,(unsigned long long)calls,result.seconds,(unsigned long long)result.checksum);
            }
            for (auto& values:ns) std::sort(values.begin(),values.end());
            const double old=(ns[0][1]+ns[0][2])/2, now=(ns[1][1]+ns[1][2])/2;
            std::printf("MEDIAN range=%s dataset=%u baseline_ns=%.6f candidate_ns=%.6f ratio=%.6f\n",pair==0?"low":"high",dataset,old,now,now/old);
        }
    }
    return true;
}
int main(int argc,char** argv) {
    bool benchmark=false, inject=false;
    for (int i=1;i<argc;++i) {
        if (!std::strcmp(argv[i],"--bench-only")) benchmark=true;
        else if (!std::strcmp(argv[i],"--inject-mismatch")) inject=true;
        else return 2;
    }
    if (benchmark && inject) return 2;
    return (benchmark ? Benchmark() : Correct(inject)) ? 0 : 1;
}
