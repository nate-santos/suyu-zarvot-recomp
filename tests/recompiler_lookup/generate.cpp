// Extract the production-generated index/lookup code; substitute synthetic
// segment metadata only. Neither generated blocks nor game data are executed.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include "core/recompiler/arm64_to_c.h"

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path out(argv[1]);
    std::filesystem::create_directories(out);
    const uint32_t words[]{0xd503201f, 0xd65f03c0}; // NOP; RET, synthetic only.
    suyu::recomp::EmitProject("lookup", reinterpret_cast<const uint8_t*>(words),
                             sizeof(words), 0x1000, (out / "emitted").string(), true);
    std::ifstream source(out / "emitted/src/recompiled_lookup.c", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(source), {}};
    const auto start = text.find("/* Direct block index.");
    if (start == std::string::npos) throw std::runtime_error("missing generated lookup/index");
    const std::string body = text.substr(start);
#ifdef LOOKUP_CANDIDATE
    const std::string tag = "candidate";
    if (body.find("if(_idx){") == std::string::npos) throw std::runtime_error("candidate lacks indexed miss path");
#else
    const std::string tag = "baseline";
    if (body.find("if(_idx &&") == std::string::npos) throw std::runtime_error("baseline must precede early NULL change");
#endif
    for (const std::string range : {"low", "high"}) {
        for (const std::string allocation : {"on", "fail"}) {
            const std::string name = tag + "_" + range + "_" + allocation;
            std::ofstream target(out / (name + ".c"), std::ios::binary);
            target << "#include <stdint.h>\n#include <stddef.h>\n"
                      "typedef void (*BlockFn)(void*);\n"
                      "struct _recomp_ent{uint64_t lo,hi; BlockFn fn;};\n"
                      "void stub0(void*); void stub1(void*); void stub2(void*); void stub3(void*);\n"
                      "void* lookup_alloc_on(size_t,size_t); void* lookup_alloc_fail(size_t,size_t);\n"
                   << "#define calloc lookup_alloc_" << allocation << "\n"
                   << "#define recomp_lookup " << name << "\n"
                   << "#define recomp_build_index " << name << "_build\n"
                   << "#define _recomp_index_view " << name << "_view\n"
                   << "#define BASE " << (range == "low" ? "0x100ULL" : "0xfffffffffffffc00ULL") << "\n"
                      "static const struct _recomp_ent seg0[]={{BASE,BASE+12,stub0},{BASE+32,BASE+36,stub1}};\n"
                      "static const struct _recomp_ent seg1[]={{BASE+256,BASE+268,stub2},{BASE+1008,BASE+1020,stub3}};\n"
                      "static const unsigned n0=2,n1=2;\n"
                      "static const struct _recomp_ent* const _segs[]={seg0,seg1};\n"
                      "static const unsigned* const _segn[]={&n0,&n1};\n"
                      "static const uint64_t _seg_hi[]={BASE+36,BASE+1020};\n"
                      "#define _NSEG 2\n"
                   << body;
            if (!target) throw std::runtime_error("write failed");
        }
    }
}
