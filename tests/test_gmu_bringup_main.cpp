#include <cassert>
#include <cstdio>
#include <cstdlib>
extern "C" int ceph_test_bringup(const char *,unsigned);
int main(int argc,char **argv){if(argc!=3){puts("Whole GMU chain skipped: explicit local GMU firmware required");return 0;}
    unsigned mode=(unsigned)std::strtoul(argv[2],nullptr,10);assert(mode<=8);int e=ceph_test_bringup(argv[1],mode);assert((e!=0)==(mode!=0 && mode!=8));return 0;}
