#include "kmeans_job.hpp"

#include <iostream>
#include <thread>

#include "app/job.hpp"
#include "app/job_tree.hpp"
#include "comm/job_tree_all_reduction.hpp"
#include "comm/mympi.hpp"
#include "kmeans_utils.hpp"
#include "util/assert.hpp"
#include "util/logger.hpp"
#include "util/sys/proc.hpp"
#include "util/sys/process.hpp"
#include "util/sys/thread_pool.hpp"
#include "util/sys/timer.hpp"
KMeansJob::KMeansJob(const Parameters& params, int commSize, int worldRank, int jobId, const int* newPayload)
    : Job(params, commSize, worldRank, jobId, JobDescription::Application::KMEANS) {
    setPayload(newPayload);
}

void KMeansJob::appl_start() {
    countCurrentWorkers = this->getVolume();
    myRank = getJobTree().getIndex();
    LOG(V2_INFO, "                           COMMSIZE: %i myRank: %i \n",
        countCurrentWorkers, myRank);
    LOG(V2_INFO, "                           Children: %i\n",
        this->getJobTree().getNumChildren());
    iAmRoot = (myRank == 0);
    payload = getDescription().getFormulaPayload(0);
    baseMsg = JobMessage(getId(),
                         getRevision(),
                         0,
                         MSG_WARMUP);
    loadTask = ProcessWideThreadPool::get().addTask([&]() {
        loadInstance();
        loaded = true;
        if (iAmRoot) {
            doInitWork();
        }
    });
}
void KMeansJob::initReducer() {
    std::vector<int> neutral;
    neutral.assign(allReduceElementSize, 0);
    auto folder =
        [&](std::list<std::vector<int>>& elems) {
            return aggregate(elems);
        };
    auto rootTransform = [&](std::vector<int> payload) {
        auto data = reduceToclusterCenters(payload);

        clusterCenters = data.first;
        sumMembers = data.second;
        int sum = 0;
        for (auto i : sumMembers) {
            sum += i;
        }
        if (sum == pointsCount) {
            allCollected = true;

            LOG(V2_INFO, "                           AllCollected: Good\n");
        } else {
            LOG(V2_INFO, "                           AllCollected: Error\n");
        }
        auto transformed = clusterCentersToBroadcast(clusterCenters);
        transformed.push_back(this->getVolume());
        if ((1 / 1000 < calculateDifference(
                            [&](Point p1, Point p2) { return KMeansUtils::eukild(p1, p2); }))) {
            return transformed;

        } else {
            return neutral;
        }
    };

    JobTreeAllReduction red(getJobTree(),
                            JobMessage(getId(),
                                       getRevision(),
                                       0,
                                       MSG_ALLREDUCE_CLAUSES),
                            std::move(neutral),
                            folder);
    red.setTransformationOfElementAtRoot(rootTransform);
    reducer = std::move(&red);

    hasReducer = true;
}

void KMeansJob::sendStartNotification() {
    auto tree = getJobTree();
    baseMsg.payload = clusterCentersToBroadcast(clusterCenters);
    baseMsg.payload.push_back(getVolume());
    baseMsg.tag = MSG_JOB_TREE_BROADCAST;
    MyMpi::isend(myRank, MSG_JOB_TREE_BROADCAST, baseMsg);
    initSend = false;
}
void KMeansJob::doInitWork() {
    initMsgTask = ProcessWideThreadPool::get().addTask([&]() {
        setRandomStartCenters();
        initSend = true;
    });

    /* Result
    internal_result.result = RESULT_SAT;
    internal_result.id = getId();
    internal_result.revision = getRevision();
    std::vector<int> transformSolution;

    internal_result.encodedType = JobResult::EncodedType::FLOAT;
    auto solution = clusterCentersToSolution();
    internal_result.setSolutionToSerialize((int*)(solution.data()), solution.size());
    finished = true;
    */
}
void KMeansJob::appl_suspend() {}
void KMeansJob::appl_resume() {}
JobResult&& KMeansJob::appl_getResult() {
    return std::move(internal_result);
}
void KMeansJob::appl_terminate() {
    if (loaded && loadTask.valid()) loadTask.get();
    if (initSend && initMsgTask.valid()) initMsgTask.get();
}
void KMeansJob::appl_communicate() {
    if (!loaded) return;
    if (initSend) sendStartNotification();
    if (calculatingFinished) {
        calculatingFinished = false;
        if (calculatingTask.valid()) calculatingTask.get();
        
        LOG(V2_INFO, "                           shouldResult: \n%s\n",
                dataToString(localClusterCenters).c_str());
        auto result = std::move(clusterCentersToReduce(localSumMembers, localClusterCenters));
        LOG(V2_INFO, "                           corruptResult: \n%s\n",
                dataToString(reduceToclusterCenters(result).first).c_str());
        auto producer = [&]() {           
            
            return std::move(result);
        };
            LOG(V2_INFO, "                           Problem?\n");
        (reducer)->produce(producer);
    };
    if (hasReducer) (reducer)->advance();
}
void KMeansJob::appl_communicate(int source, int mpiTag, JobMessage& msg) {
    if (!loaded) return;
    if (mpiTag == MSG_JOB_TREE_BROADCAST) {
        if (myRank < countCurrentWorkers) {
            advanceCollective(msg, MSG_JOB_TREE_BROADCAST);
            // continue broadcasting
            LOG(V2_INFO, "                           clusterCenters1: \n%s\n",
                dataToString(clusterCenters).c_str());
            clusterCenters = broadcastToClusterCenters(msg.payload, true);

            LOG(V2_INFO, "                           clusterCenters2: \n%s\n",
                dataToString(clusterCenters).c_str());
            calculatingTask = ProcessWideThreadPool::get().addTask([&]() {
                calcNearestCenter(metric);
                calcCurrentClusterCenters();
                initReducer();
                calculatingFinished = true;
            });
        }
    }
    if (hasReducer) {
        if ((reducer)->hasResult()) {
            clusterCenters = broadcastToClusterCenters((reducer)->extractResult(), true);
        }
        (reducer)->receive(source, mpiTag, msg);
    }
}
void KMeansJob::advanceCollective(JobMessage& msg, int broadcastTag) {
    if (msg.tag == broadcastTag) {
        // Broadcast to children
        if (getJobTree().hasLeftChild())
            MyMpi::isend(getJobTree().getLeftChildNodeRank(), MSG_JOB_TREE_BROADCAST, msg);
        if (getJobTree().hasRightChild())
            MyMpi::isend(getJobTree().getRightChildNodeRank(), MSG_JOB_TREE_BROADCAST, msg);
    }
}
void KMeansJob::appl_dumpStats() {}
void KMeansJob::appl_memoryPanic() {}
void KMeansJob::loadInstance() {
    countClusters = payload[0];
    dimension = payload[1];
    pointsCount = payload[2];
    kMeansData.reserve(pointsCount);
    payload += 3;  // pointer start at first datapoint instead of metadata
    for (int point = 0; point < pointsCount; ++point) {
        Point p;
        p.reserve(dimension);
        for (int entry = 0; entry < dimension; ++entry) {
            p.push_back(*((float*)(payload + entry)));
        }
        kMeansData.push_back(p);
        payload = payload + dimension;
    }
    allReduceElementSize = (dimension + 1) * countClusters;
    loaded = true;
}

void KMeansJob::setRandomStartCenters() {
    clusterCenters.clear();
    clusterCenters.resize(countClusters);
    for (int i = 0; i < countClusters; ++i) {
        clusterCenters[i] = kMeansData[static_cast<int>((static_cast<float>(i) / static_cast<float>(countClusters)) * (pointsCount - 1))];
    }
}

void KMeansJob::calcNearestCenter(std::function<float(Point, Point)> metric) {
    struct centerDistance {
        int cluster;
        float distance;
    } currentNearestCenter;
    clusterMembership.assign(pointsCount, 0);
    float distanceToCluster;
    // while own or child slices todo
    for (int pointID = 0; pointID < pointsCount; ++pointID) {
        currentNearestCenter.cluster = -1;
        currentNearestCenter.distance = std::numeric_limits<float>::infinity();
        for (int clusterID = 0; clusterID < countClusters; ++clusterID) {
            distanceToCluster = metric(kMeansData[pointID], clusterCenters[clusterID]);
            if (distanceToCluster < currentNearestCenter.distance) {
                currentNearestCenter.cluster = clusterID;
                currentNearestCenter.distance = distanceToCluster;
            }
        }
        clusterMembership[pointID] = currentNearestCenter.cluster;
    }
}

void KMeansJob::calcCurrentClusterCenters() {
    oldClusterCenters = clusterCenters;

    countMembers();

    LOG(V2_INFO, "                           sumMembers: %s\n",
        dataToString(localSumMembers).c_str());
    localClusterCenters.clear();
    localClusterCenters.resize(countClusters);
    for (int cluster = 0; cluster < countClusters; ++cluster) {
        localClusterCenters[cluster].assign(dimension, 0);
    }
    for (int pointID = 0; pointID < pointsCount; ++pointID) {
        for (int d = 0; d < dimension; ++d) {
            localClusterCenters[clusterMembership[pointID]][d] +=
                kMeansData[pointID][d] / static_cast<float>(localSumMembers[clusterMembership[pointID]]);
        }
    }
    ++iterationsDone;
}

std::string KMeansJob::dataToString(std::vector<Point> data) {
    std::stringstream result;

    for (auto point : data) {
        for (auto entry : point) {
            result << entry << " ";
        }
        result << "\n";
    }
    return result.str();
}
std::string KMeansJob::dataToString(std::vector<int> data) {
    std::stringstream result;

    for (auto entry : data) {
        result << entry << " ";
    }
    result << "\n";
    return result.str();
}

void KMeansJob::countMembers() {
    std::stringstream result;
    localSumMembers.assign(countClusters, 0);
    for (int clusterID : clusterMembership) {
        localSumMembers[clusterID] += 1;
    }
}

float KMeansJob::calculateDifference(std::function<float(Point, Point)> metric) {
    if (iterationsDone == 0) {
        return std::numeric_limits<float>::infinity();
    }
    float sumOldvec = 0.0;
    float sumDifference = 0.0;
    Point v0(dimension, 0);
    for (int k = 0; k < countClusters; ++k) {
        sumOldvec += metric(v0, clusterCenters[k]);

        sumDifference += metric(clusterCenters[k], oldClusterCenters[k]);
    }
    return sumDifference / sumOldvec;
}

std::vector<float> KMeansJob::clusterCentersToSolution() {
    std::vector<float> result;
    result.push_back((float)countClusters);
    result.push_back((float)dimension);

    for (auto point : clusterCenters) {
        for (auto entry : point) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<int> KMeansJob::clusterCentersToBroadcast(std::vector<Point> reduceClusterCenters) {
    std::vector<int> result;
    for (auto point : reduceClusterCenters) {
        auto centerData = point.data();
        for (int entry = 0; entry < dimension; ++entry) {
            result.push_back(*((int*)(centerData + entry)));
        }
        LOG(V2_INFO, "Push in: %f\n", *(centerData));
    }
    LOG(V2_INFO, "                           clusterCenters3: \n%s\n",
        dataToString(reduceClusterCenters).c_str());
    LOG(V2_INFO, "                           reduce in clusterCentersToBroadcast: \n%s\n",
        dataToString(result).c_str());
    return std::move(result);
}

std::vector<KMeansJob::Point> KMeansJob::broadcastToClusterCenters(std::vector<int> reduce, bool withNumWorkers) {
    std::vector<Point> localClusterCentersResult;
    const int elementsCount = allReduceElementSize - countClusters;
    int* reduceData = reduce.data();

    if (!withNumWorkers) reduceData += countClusters;
    
    localClusterCentersResult.clear();
    localClusterCentersResult.resize(countClusters);
    for (int i = 0; i < countClusters; ++i) {
        localClusterCentersResult[i].assign(dimension, 0);
    }
    for (int i = 0; i < elementsCount; ++i) {
        localClusterCentersResult[i / dimension][i % dimension] = *((float*)(reduceData + i));
    }
    if (withNumWorkers) {
        countCurrentWorkers = *(reduceData + elementsCount);
    }

    return localClusterCentersResult;
}

std::vector<int> KMeansJob::clusterCentersToReduce(std::vector<int> reduceSumMembers, std::vector<Point> reduceClusterCenters) {
    std::vector<int> result;
    std::vector<int> tempCenters;

    tempCenters = clusterCentersToBroadcast(reduceClusterCenters);

    for (auto entry : reduceSumMembers) {
        result.push_back(entry);
    }
    result.insert(result.end(), tempCenters.begin(), tempCenters.end());

    LOG(V2_INFO, "                           HERE!\n");
    return std::move(result);
}

std::pair<std::vector<std::vector<float>>, std::vector<int>>
KMeansJob::reduceToclusterCenters(std::vector<int> reduce) {
    // auto [centers, counts] = reduceToclusterCenters(); //call example
    std::vector<int> localSumMembersResult;
    std::vector<Point> localClusterCentersResult;
    const int elementsCount = allReduceElementSize - countClusters;

    int* reduceData = reduce.data();

    localSumMembersResult.assign(countClusters, 0);
    for (int i = 0; i < countClusters; ++i) {
        // LOG(V2_INFO, "i: %d\n", i);
        localSumMembersResult[i] = reduceData[i];
    }

    localClusterCentersResult = broadcastToClusterCenters(reduce);

    return std::pair(std::move(localClusterCentersResult), std::move(localSumMembersResult));
}

std::vector<int> KMeansJob::aggregate(std::list<std::vector<int>> messages) {
    std::vector<std::vector<KMeansJob::Point>> centers;
    std::vector<std::vector<int>> counts;
    std::vector<int> tempSumMembers;
    std::vector<Point> tempClusterCenters;
    const int countMessages = messages.size();
    centers.resize(countMessages);
    counts.resize(countMessages);
    LOG(V2_INFO, "countMessages : \n%d\n", countMessages);
    for (int i = 0; i < countMessages; ++i) {
        auto data = reduceToclusterCenters(messages.front());
        messages.pop_front();
        centers[i] = data.first;
        counts[i] = data.second;

        LOG(V2_INFO, "centers[%d] : \n%s\n", i, dataToString(centers[i]).c_str());
        LOG(V2_INFO, "counts[%d] : \n%s\n", i, dataToString(counts[i]).c_str());
    }
    tempSumMembers.assign(countClusters, 0);
    for (int i = 0; i < countMessages; ++i) {
        for (int j = 0; j < countClusters; ++j) {
            tempSumMembers[j] = tempSumMembers[j] + counts[i][j];

            // LOG(V2_INFO, "tempSumMembers[%d] + counts[%d][%d] : \n%d\n",j,i,j, tempSumMembers[j] + counts[i][j]);
            // LOG(V2_INFO, "tempSumMembers[%d] : \n%d\n",j, tempSumMembers[j]);
            // LOG(V2_INFO, "KRITcounts[%d] : \n%s\n",i, dataToString(tempSumMembers).c_str());
        }
    }
    LOG(V2_INFO, "tempSumMembers : \n%s\n", dataToString(tempSumMembers).c_str());
    tempClusterCenters.resize(countClusters);
    for (int i = 0; i < countClusters; ++i) {
        tempClusterCenters[i].assign(dimension, 0);
    }
    for (int i = 0; i < countMessages; ++i) {
        for (int j = 0; j < countClusters; ++j) {
            for (int k = 0; k < dimension; ++k) {
                tempClusterCenters[j][k] += centers[i][j][k] *
                                            (static_cast<float>(counts[i][j]) /
                                             static_cast<float>(tempSumMembers[j]));
            }
        }
    }
    if (iAmRoot) {
        int sum = 0;
        for (auto i : tempSumMembers) {
            sum += i;
        }
        sum == pointsCount;
        allCollected = true;
    }

    return clusterCentersToReduce(tempSumMembers, tempClusterCenters);  // localSumMembers, tempClusterCenters
}
