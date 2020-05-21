// test:

#include <iostream>
#include <future>
#include "stack.hpp"
#include "lru_cache.hpp"

static constexpr int loops = 80000;

int main(int argc, const char * argv[]) {
    {
        auto doubler = [](int x){return x*x;};
        
        frd::cache cached_doubler(doubler);
        {
            auto pol = std::launch::async;
            auto f = std::async(pol, [&](){ for(int i=0;i<loops;i++) {
                cached_doubler(i%100 + i);
            }});
            auto g = std::async(pol, [&](){ for(int i=0;i<loops;i++) {
                cached_doubler(i%109 + i);
            }});
            auto h = std::async(pol, [&](){ for(int i=0;i<loops;i++) {
                cached_doubler((i*i)%104 + i);
            }});
        }
        std::cout << "nodes: " << leaks << "\n";
    }
    std::cout << "leaks: " << leaks << "\n";
    return 0;
}
