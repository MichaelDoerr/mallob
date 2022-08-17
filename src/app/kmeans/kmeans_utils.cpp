#include "kmeans_utils.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

#include "util/assert.hpp"
#include "util/logger.hpp"
namespace KMeansUtils {

typedef std::vector<float> Point;
float eukild(const float* p1, Point& p2) {
    float sum = 0;

    for (int d = 0; d < p2.size(); ++d) {
        sum += (p1[d] - p2[d])*(p1[d] - p2[d]);
    }

    return std::sqrt(sum);
}
// childIndexesOf(1, 12) = [3, 4, 7, 8, 9, 10]
std::vector<int> childIndexesOf(int parentIndex, int jobVolume) {
    std::vector<int> indexList;
    std::vector<int> childBuffer;
    int currentIndex;
    childBuffer.push_back(parentIndex * 2 + 1);
    childBuffer.push_back(parentIndex * 2 + 2);
    while (!childBuffer.empty()) {
        currentIndex = childBuffer[childBuffer.size() - 1];
        childBuffer.pop_back();
        if (currentIndex < jobVolume) {
            indexList.push_back(currentIndex);
            childBuffer.push_back(currentIndex * 2 + 1);
            childBuffer.push_back(currentIndex * 2 + 2);
        }
    }
    return indexList;
}
}  // namespace KMeansUtils