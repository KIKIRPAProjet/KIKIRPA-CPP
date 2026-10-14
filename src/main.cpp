#include "app/PipelineBuilder.h"
#include <iostream>

using namespace stitching;

int main() {
    Tuile a, b;
    Pipeline p = buildPipeline(PipelineSettings{}, a, b);
    std::cout << (p.run() ? "Pipeline OK" : "Pipeline KO") << '\n';
}
