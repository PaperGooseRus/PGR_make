#include "common.hpp"
#include "core/sha256.hpp"
using namespace pgr_test;
int main() { try {
    sandbox t("sha256");
    const auto file=t.path/"digest.txt";
    put(file,"");
    check(pgr_sha256::file(file)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","empty SHA-256");
    put(file,"abc");
    check(pgr_sha256::file(file)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","known vector SHA-256");
    put(file,std::string(100000,'a'));
    check(pgr_sha256::file(file)=="6d1cf22d7cc09b085dfc25ee1a1f3ae0265804c607bc2074ad253bcc82fd81ee","multi-block SHA-256");
    check(pgr_sha256::file(t.path/"missing").empty(),"missing file SHA-256");
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
