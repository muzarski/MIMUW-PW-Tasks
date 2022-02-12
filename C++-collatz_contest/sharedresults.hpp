#ifndef SHAREDRESULTS_HPP
#define SHAREDRESULTS_HPP

#include <vector>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <utility>

class SharedResults {
public:
    SharedResults() {}

    std::pair<bool, uint64_t> tryRead(const InfInt &in) {
        std::shared_lock<std::shared_mutex> sl(m);
        if (computed.count(in) > 0)
            return {true, computed[in]};
        return {false, 0};
    }

    void assignComputed(const InfInt &in, uint64_t out) {
        std::unique_lock<std::shared_mutex> ul(m);
        computed[in] = out;
    }

private:
    std::shared_mutex m;
    std::map<InfInt, uint64_t> computed;
};

#endif // SHAREDRESULTS_HPP