#include <vultra/api/compute_bridge.hpp>

extern "C" int testComputeC(const VultraComputeApi* api);

int main()
{
    return testComputeC(&vultra::computeApi());
}
