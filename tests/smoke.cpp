#include <array>
#include <cassert>
#include <numeric>

static_assert(__cplusplus >= 202002L);

int main()
{
    constexpr std::array samples{0, 1, 4, 9};
    const int sum = std::accumulate(samples.begin(), samples.end(), 0);
    assert(sum == 14);
    return 0;
}
