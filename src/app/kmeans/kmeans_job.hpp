
#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "app/job.hpp"
#include "util/params.hpp"

class KMeansJob : public Job {
   private:
    typedef std::vector<float> Point;
    std::vector<Point> clusterCenters;   // The centers of cluster 0..n
    std::vector<int> clusterMembership;  // A point KMeansData[i] belongs to cluster ClusterMembership[i]
    std::vector<int> sumMembers;
    int numClusters;
    int dimension;
    int pointsCount;
    std::vector<Point> kMeansData;
    const int* payload;
    std::future<void> calculating;

   public:
    std::vector<Point> getClusterCenters() { return clusterCenters; };      // The centers of cluster 0..n
    std::vector<int> getClusterMembership() { return clusterMembership; };  // A point KMeansData[i] belongs to cluster ClusterMembership[i]
    std::vector<int> getSumMembers() { return sumMembers; };
    int getNumClusters() { return numClusters; };
    int getDimension() { return dimension; };
    int getPointsCount() { return pointsCount; };
    std::vector<Point> getKMeansData() { return kMeansData; };
    const int* getPayload() { return payload; };
    void setPayload(const int* newPayload) { payload = newPayload; };

    KMeansJob(const Parameters& params, int commSize, int worldRank, int jobId, const int* newPayload);
    void appl_start() override;
    void appl_suspend() override ;
    void appl_resume() override ;
    void appl_terminate() override ;
    int appl_solved() override { return -1; }  // atomic bool
    JobResult&& appl_getResult() override { return JobResult(); }
    void appl_communicate() override;
    void appl_communicate(int source, int mpiTag, JobMessage& msg) override;
    void appl_dumpStats() override;
    bool appl_isDestructible() override { return true; }
    void appl_memoryPanic() override;

    void loadInstance();
    void setRandomStartCenters();
    void calcNearestCenter(std::function<float(Point, Point)> metric);
    void calcCurrentClusterCenters();
    std::string dataToString(std::vector<Point> data);
    std::string dataToString(std::vector<int> data);
    void countMembers();
};
