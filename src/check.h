#pragma once
#include <cstdio>
#include <cstdlib>
// Never compile test checks out, including optimized Zig builds.
#define CHECK(...) do { if(!(__VA_ARGS__)) { std::fprintf(stderr,"CHECK failed: %s at %s:%d\n",#__VA_ARGS__,__FILE__,__LINE__);std::abort(); } } while(false)
