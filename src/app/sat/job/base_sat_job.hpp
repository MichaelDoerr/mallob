
#pragma once

#include "app/job.hpp"
#include "data/checksum.hpp"
#include "app/sat/data/clause_metadata.hpp"

class BaseSatJob : public Job {

public:
    BaseSatJob(const Parameters& params, const JobSetup& setup) : 
        Job(params, setup) {

        // Launched in certified UNSAT mode?
        if (params.certifiedUnsat()) {
            
            // Check that the restrictions of this mode are met
            if (!params.monoFilename.isSet()) {
                LOG(V0_CRIT, "[ERROR] Mallob was launched with certified UNSAT support "
                    "which only supports -mono mode of operation.\n");
                abort();
            }
            if (!params.logDirectory.isSet()) {
                LOG(V0_CRIT, "[ERROR] Mallob was launched with certified UNSAT support "
                    "which requires providing a log directory.\n");
                abort();
            }
            
            ClauseMetadata::enableClauseIds();
        }
    }
    virtual ~BaseSatJob() {}

    // Methods common to all BaseSatJob instances

    virtual bool isInitialized() = 0;
    
    virtual void prepareSharing(int maxSize) = 0;
    virtual bool hasPreparedSharing() = 0;
    virtual std::vector<int> getPreparedClauses(Checksum& checksum) = 0;
    virtual std::pair<int, int> getLastAdmittedClauseShare() = 0;

    virtual void filterSharing(std::vector<int>& clauses) = 0;
    virtual bool hasFilteredSharing() = 0;
    virtual std::vector<int> getLocalFilter() = 0;
    virtual void applyFilter(std::vector<int>& filter) = 0;
    
    virtual void digestSharingWithoutFilter(std::vector<int>& clauses) = 0;
    virtual void returnClauses(std::vector<int>& clauses) = 0;

    // Methods common to all Job instances

    virtual void appl_start() = 0;
    virtual void appl_suspend() = 0;
    virtual void appl_resume() = 0;
    virtual void appl_terminate() = 0;

    virtual int appl_solved() = 0;
    virtual JobResult&& appl_getResult() = 0;
    
    virtual void appl_communicate() = 0;
    virtual void appl_communicate(int source, int mpiTag, JobMessage& msg) = 0;
    
    virtual void appl_dumpStats() = 0;
    virtual bool appl_isDestructible() = 0;
    virtual void appl_memoryPanic() = 0;

    virtual bool checkResourceLimit(float wcSecsPerInstance, float cpuSecsPerInstance) override {
        if (!_done_solving && _params.satSolvingWallclockLimit() > 0) {
            auto age = getAgeSinceActivation();
            if (age > _params.satSolvingWallclockLimit()) {
                LOG(V2_INFO, "#%i SOLVING TIMEOUT: aborting\n", getId());
                return true;
            }
        }
        return Job::checkResourceLimit(wcSecsPerInstance, cpuSecsPerInstance);
    }
    void setSolvingDone() {
        _done_solving = true;
    }

private:
    float _compensation_factor = 1.0f;
    bool _done_solving = false;

    struct DeferredJobMsg {int source; int mpiTag; JobMessage msg;};
    std::list<DeferredJobMsg> _deferred_messages;

public:
    // Helper methods

    float getCompensationFactor() const {
        return _compensation_factor;
    }
    void setSharingCompensationFactor(float compensationFactor) {
        _compensation_factor = compensationFactor;
    }

    size_t getBufferLimit(int numAggregatedNodes, MyMpi::BufferQueryMode mode) {
        if (mode == MyMpi::SELF) return _compensation_factor * _params.clauseBufferBaseSize();
        return _compensation_factor * MyMpi::getBinaryTreeBufferLimit(numAggregatedNodes, 
            _params.clauseBufferBaseSize(), _params.clauseBufferDiscountFactor(), mode);
    }

    void deferMessage(int source, int mpiTag, JobMessage& msg) {
        LOG(V3_VERB, "%s : deferring application msg\n", toStr());
        _deferred_messages.push_front(DeferredJobMsg {source, mpiTag, std::move(msg)});
    }

    bool hasDeferredMessage() const {return !_deferred_messages.empty();}

    DeferredJobMsg getDeferredMessage() {
        LOG(V3_VERB, "%s : fetching deferred application msg\n", toStr());
        auto result = std::move(_deferred_messages.back());
        _deferred_messages.pop_back();
        return result;
    }
};
