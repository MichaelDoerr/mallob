
#include "job_transfer.hpp"

#include <cstring>
#include <sstream>

#include "data/job_description.hpp"

size_t JobRequest::getTransferSize() {
    return 8*sizeof(int)+sizeof(float)+sizeof(JobDescription::Application);
}

std::vector<uint8_t> JobRequest::serialize() const {
    int size = getTransferSize();
    std::vector<uint8_t> packed(size);
    int i = 0, n;
    n = sizeof(int); memcpy(packed.data()+i, &jobId, n); i += n;
    n = sizeof(JobDescription::Application); memcpy(packed.data()+i, &application, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &rootRank, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &requestingNodeRank, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &requestedNodeIndex, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &currentRevision, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &lastKnownRevision, n); i += n;
    n = sizeof(float); memcpy(packed.data()+i, &timeOfBirth, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &numHops, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &balancingEpoch, n); i += n;
    return packed;
}

JobRequest& JobRequest::deserialize(const std::vector<uint8_t> &packed) {
    int i = 0, n;
    n = sizeof(int); memcpy(&jobId, packed.data()+i, n); i += n;
    n = sizeof(JobDescription::Application); memcpy(&application, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&rootRank, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&requestingNodeRank, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&requestedNodeIndex, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&currentRevision, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&lastKnownRevision, packed.data()+i, n); i += n;
    n = sizeof(float); memcpy(&timeOfBirth, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&numHops, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&balancingEpoch, packed.data()+i, n); i += n;
    return *this;
}

std::string JobRequest::toStr() const {
    std::ostringstream out;
    out.precision(3);
    out << std::fixed << timeOfBirth;
    auto birthStr = out.str();
    return "r.#" + std::to_string(jobId) + ":" + std::to_string(requestedNodeIndex) 
            + " rev. " + std::to_string(currentRevision) + " <- [" 
            + std::to_string(requestingNodeRank) + "] born=" + birthStr 
            + " hops=" + std::to_string(numHops)
            + " epoch=" + std::to_string(balancingEpoch);
}

bool JobRequest::operator==(const JobRequest& other) const {
    return jobId == other.jobId 
        && requestedNodeIndex == other.requestedNodeIndex 
        && balancingEpoch == other.balancingEpoch
        && currentRevision == other.currentRevision
        && numHops == other.numHops;
}
bool JobRequest::operator!=(const JobRequest& other) const {
    return !(*this == other);
}
bool JobRequest::operator<(const JobRequest& other) const {
    if (balancingEpoch != other.balancingEpoch) return balancingEpoch < other.balancingEpoch;
    if (jobId != other.jobId) return jobId < other.jobId;
    if (requestedNodeIndex != other.requestedNodeIndex) return requestedNodeIndex < other.requestedNodeIndex;
    if (currentRevision != other.currentRevision) return currentRevision < other.currentRevision;
    return false;
}
    
std::vector<uint8_t> OneshotJobRequestRejection::serialize() const {
    std::vector<uint8_t> packed = request.serialize();
    size_t sizeBefore = packed.size();
    packed.resize(packed.size()+sizeof(bool));
    memcpy(packed.data()+sizeBefore, &isChildStillDormant, sizeof(bool));
    return packed;
}

OneshotJobRequestRejection& OneshotJobRequestRejection::deserialize(const std::vector<uint8_t> &packed) {
    request.deserialize(packed);
    memcpy(&isChildStillDormant, packed.data()+packed.size()-sizeof(bool), sizeof(bool));
    return *this;
}

std::vector<uint8_t> WorkRequest::serialize() const {
    std::vector<uint8_t> packed(3*sizeof(int));
    int i = 0, n;
    n = sizeof(int); memcpy(packed.data()+i, &requestingRank, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &numHops, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &balancingEpoch, n); i += n;
    return packed;
}

WorkRequest& WorkRequest::deserialize(const std::vector<uint8_t> &packed) {
    int i = 0, n;
    n = sizeof(int); memcpy(&requestingRank, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&numHops, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&balancingEpoch, packed.data()+i, n); i += n;
    return *this;
}

bool WorkRequestComparator::operator()(const WorkRequest& lhs, const WorkRequest& rhs) const {
    if (lhs.balancingEpoch != rhs.balancingEpoch) return lhs.balancingEpoch > rhs.balancingEpoch;
    if (lhs.numHops != rhs.numHops) return lhs.numHops < rhs.numHops;
    return std::hash<int>()(lhs.requestingRank) < std::hash<int>()(rhs.requestingRank);
}

int JobSignature::getTransferSize() const {
    return transferSize;
}

std::vector<uint8_t> JobSignature::serialize() const {
    int size = (3*sizeof(int) + sizeof(size_t));
    std::vector<uint8_t> packed(size);

    int i = 0, n;
    n = sizeof(int);    memcpy(packed.data()+i, &jobId, n); i += n;
    n = sizeof(int);    memcpy(packed.data()+i, &rootRank, n); i += n;
    n = sizeof(int);    memcpy(packed.data()+i, &firstIncludedRevision, n); i += n;
    n = sizeof(size_t); memcpy(packed.data()+i, &transferSize, n); i += n;
    return packed;
}

JobSignature& JobSignature::deserialize(const std::vector<uint8_t>& packed) {
    int i = 0, n;
    n = sizeof(int);    memcpy(&jobId, packed.data()+i, n); i += n;
    n = sizeof(int);    memcpy(&rootRank, packed.data()+i, n); i += n;
    n = sizeof(int);    memcpy(&firstIncludedRevision, packed.data()+i, n); i += n;
    n = sizeof(size_t); memcpy(&transferSize, packed.data()+i, n); i += n;
    return *this;
}

std::vector<uint8_t> JobMessage::serialize() const {
    int size = 4*sizeof(int) + payload.size()*sizeof(int) + sizeof(Checksum);
    std::vector<uint8_t> packed(size);

    int i = 0, n;
    n = sizeof(int); memcpy(packed.data()+i, &jobId, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &revision, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &tag, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &epoch, n); i += n;
    n = sizeof(Checksum); memcpy(packed.data()+i, &checksum, n); i += n;
    n = payload.size()*sizeof(int); memcpy(packed.data()+i, payload.data(), n); i += n;
    return packed;
}

JobMessage& JobMessage::deserialize(const std::vector<uint8_t>& packed) {
    int i = 0, n;
    n = sizeof(int); memcpy(&jobId, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&revision, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&tag, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&epoch, packed.data()+i, n); i += n;
    n = sizeof(Checksum); memcpy(&checksum, packed.data()+i, n); i += n;
    n = packed.size()-i; payload.resize(n/sizeof(int)); 
    memcpy(payload.data(), packed.data()+i, n); i += n;
    return *this;
}

std::vector<uint8_t> IntPair::serialize() const {
    int size = (2*sizeof(int));
    std::vector<uint8_t> packed(size);
    int i = 0, n;
    n = sizeof(int); memcpy(packed.data()+i, &first, n); i += n;
    n = sizeof(int); memcpy(packed.data()+i, &second, n); i += n;
    return packed;
}

IntPair& IntPair::deserialize(const std::vector<uint8_t>& packed) {
    int i = 0, n;
    n = sizeof(int); memcpy(&first, packed.data()+i, n); i += n;
    n = sizeof(int); memcpy(&second, packed.data()+i, n); i += n;
    return *this;
}

std::vector<uint8_t> IntVec::serialize() const {
    int size = (data.size()*sizeof(int));
    std::vector<uint8_t> packed(size);
    memcpy(packed.data(), data.data(), size);
    return packed;
}

IntVec& IntVec::deserialize(const std::vector<uint8_t>& packed) {
    data.resize(packed.size() / sizeof(int));
    memcpy(data.data(), packed.data(), packed.size());
    return *this;
}

int& IntVec::operator[](const int pos) {
    return data[pos];   
}
