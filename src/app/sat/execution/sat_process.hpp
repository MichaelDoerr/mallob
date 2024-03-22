
#pragma once

#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <memory>
#include "app/sat/data/clause_metadata.hpp"
#include "util/assert.hpp"

#include "util/sys/timer.hpp"
#include "util/logger.hpp"
#include "util/params.hpp"
#include "util/sys/shared_memory.hpp"
#include "util/sys/process.hpp"
#include "util/sys/proc.hpp"
#include "data/checksum.hpp"
#include "util/sys/terminator.hpp"
#include "app/sat/job/clause_pipe.hpp"

#include "engine.hpp"
#include "../job/sat_shared_memory.hpp"

class SatProcess {

private:
    const Parameters& _params;
    const SatProcessConfig& _config;
    Logger& _log;

    std::string _shmem_id;
    SatSharedMemory* _hsm;

    int _last_imported_revision;
    int _desired_revision;
    Checksum* _checksum;

    std::vector<std::vector<int>> _read_formulae;
    std::vector<std::vector<int>> _read_assumptions;

public:
    SatProcess(const Parameters& params, const SatProcessConfig& config, Logger& log) 
        : _params(params), _config(config), _log(log) {

        // Set up "management" block of shared memory created by the parent
        _shmem_id = _config.getSharedMemId(Proc::getParentPid());
        LOGGER(log, V4_VVER, "Access base shmem: %s\n", _shmem_id.c_str());
        _hsm = (SatSharedMemory*) accessMemory(_shmem_id, sizeof(SatSharedMemory));
        
        _checksum = params.useChecksums() ? new Checksum() : nullptr;

        // Adjust OOM killer score to make this process the first to be killed
        // (always better than touching an MPI process, which would crash everything)
        std::ofstream oomOfs("/proc/self/oom_score_adj");
        oomOfs << "1000";
        oomOfs.close();
    }

    void run() {
        SatEngine engine(_params, _config, _log);
        try {
            mainProgram(engine);
            // Everything has been safely cleaned up, so we can send the terminate response
            // which allows the parent process to clean up all the shared memory.
            _hsm->didTerminate = true;
        } catch (const std::exception& ex) {
            LOG(V0_CRIT, "[ERROR] uncaught \"%s\"\n", ex.what());
            Process::doExit(1);
        } catch (...) {
            LOG(V0_CRIT, "[ERROR] uncaught exception\n");
            Process::doExit(1);
        }
    }

private:
    void mainProgram(SatEngine& engine) {

        // Set up pipe communication for clause sharing
        BiDirectionalPipe pipe(BiDirectionalPipe::ACCESS,
            TmpDir::get()+_shmem_id+".fromsub.pipe",
            TmpDir::get()+_shmem_id+".tosub.pipe");
        pipe.open();
        LOGGER(_log, V4_VVER, "Pipes set up\n");

        // Wait until everything is prepared for the solver to begin
        while (!_hsm->doBegin) doSleep();
        
        // Terminate directly?
        if (checkTerminate(engine, false)) return;

        // Import first revision
        _desired_revision = _config.firstrev;
        readFormulaAndAssumptionsFromSharedMem(engine, 0);
        _last_imported_revision = 0;
        // Import subsequent revisions
        importRevisions(engine);
        if (checkTerminate(engine, false)) return;
        
        // Start solver threads
        engine.solve();
        
        std::vector<int> solutionVec;
        std::string solutionShmemId = "";
        char* solutionShmem;
        int solutionShmemSize = 0;
        int lastSolvedRevision = -1;

        int exitStatus = 0;

        std::vector<int> incomingClauses;

        // Main loop
        while (true) {

            doSleep();
            Timer::cacheElapsedSeconds();

            // Terminate
            if (_hsm->doTerminate || Terminator::isTerminating(/*fromMainThread=*/false)) {
                LOGGER(_log, V4_VVER, "DO terminate\n");
                engine.dumpStats(/*final=*/true);
                break;
            }

            // Read new revisions as necessary
            importRevisions(engine);

            // Dump stats
            if (_hsm->doDumpStats && !_hsm->didDumpStats) {
                LOGGER(_log, V5_DEBG, "DO dump stats\n");
                
                engine.dumpStats(/*final=*/false);

                // For this management thread
                double cpuShare; float sysShare;
                bool success = Proc::getThreadCpuRatio(Proc::getTid(), cpuShare, sysShare);
                if (success) {
                    LOGGER(_log, V3_VERB, "child_main cpuratio=%.3f sys=%.3f\n", cpuShare, sysShare);
                }

                // For each solver thread
                std::vector<long> threadTids = engine.getSolverTids();
                for (size_t i = 0; i < threadTids.size(); i++) {
                    if (threadTids[i] < 0) continue;
                    
                    success = Proc::getThreadCpuRatio(threadTids[i], cpuShare, sysShare);
                    if (success) {
                        LOGGER(_log, V3_VERB, "td.%ld cpuratio=%.3f sys=%.3f\n", threadTids[i], cpuShare, sysShare);
                    }
                }

                auto rtInfo = Proc::getRuntimeInfo(Proc::getPid(), Proc::SubprocessMode::FLAT);
                LOGGER(_log, V3_VERB, "child_mem=%.3fGB\n", 0.001*0.001*rtInfo.residentSetSize);

                _hsm->didDumpStats = true;
            }
            if (!_hsm->doDumpStats) _hsm->didDumpStats = false;

            // Check if clauses should be exported
            if (_hsm->doExport && !_hsm->didExport && engine.isReadyToPrepareSharing()) {
                LOGGER(_log, V5_DEBG, "DO export clauses\n");
                // Collect local clauses, put into shared memory
                _hsm->exportChecksum = Checksum();
                _hsm->successfulSolverId = -1;
                auto clauses = engine.prepareSharing(_hsm->exportLiteralLimit, _hsm->successfulSolverId, _hsm->numCollectedLits);
                if (!clauses.empty()) {
                    _hsm->didExport = true;
                    pipe.writeData(clauses);
                }
            }
            if (!_hsm->doExport) _hsm->didExport = false;

            // Check if clauses should be filtered
            if (_hsm->doFilterImport && !_hsm->didFilterImport) {
                LOGGER(_log, V5_DEBG, "DO filter clauses\n");
                int winningSolverId = _hsm->winningSolverId;
                incomingClauses = pipe.readData();
                auto filter = engine.filterSharing(incomingClauses);
                _hsm->didFilterImport = true;
                pipe.writeData(filter);
                if (winningSolverId >= 0) {
                    LOGGER(_log, V4_VVER, "winning solver ID: %i", winningSolverId);
                    engine.setWinningSolverId(winningSolverId);
                }
            }
            if (!_hsm->doFilterImport) _hsm->didFilterImport = false;

            // Check if clauses should be digested (must not be "from the future")
            if ((_hsm->doDigestImportWithFilter || _hsm->doDigestImportWithoutFilter) 
                    && !_hsm->didDigestImport && _hsm->importBufferRevision <= _last_imported_revision) {
                LOGGER(_log, V5_DEBG, "DO import clauses\n");
                // Write imported clauses from shared memory into vector
                engine.setClauseBufferRevision(_hsm->importBufferRevision);
                if (_hsm->doDigestImportWithFilter) {
                    auto filter = pipe.readData();
                    engine.digestSharingWithFilter(incomingClauses, filter);
                } else {
                    engine.digestSharingWithoutFilter(incomingClauses);
                }
                engine.addSharingEpoch(_hsm->importEpoch);
                engine.syncDeterministicSolvingAndCheckForLocalWinner();
                _hsm->lastAdmittedStats = engine.getLastAdmittedClauseShare();
                _hsm->didDigestImport = true;
            }
            if (!_hsm->doDigestImportWithFilter && !_hsm->doDigestImportWithoutFilter) 
                _hsm->didDigestImport = false;

            // Re-insert returned clauses into the local clause database to be exported later
            if (_hsm->doReturnClauses && !_hsm->didReturnClauses) {
                LOGGER(_log, V5_DEBG, "DO return clauses\n");
                auto clauses = pipe.readData();
                engine.returnClauses(clauses);
                _hsm->didReturnClauses = true;
            }
            if (!_hsm->doReturnClauses) _hsm->didReturnClauses = false;

            if (_hsm->doDigestHistoricClauses && !_hsm->didDigestHistoricClauses) {
                LOGGER(_log, V5_DEBG, "DO digest historic clauses\n");
                engine.setClauseBufferRevision(_hsm->importBufferRevision);
                auto clauses = pipe.readData();
                engine.digestHistoricClauses(_hsm->historicEpochBegin, _hsm->historicEpochEnd, 
                    clauses);
                _hsm->didDigestHistoricClauses = true;
            }
            if (!_hsm->doDigestHistoricClauses) _hsm->didDigestHistoricClauses = false;

            // Check initialization state
            if (!_hsm->isInitialized && engine.isFullyInitialized()) {
                LOGGER(_log, V5_DEBG, "DO set initialized\n");
                _hsm->isInitialized = true;
            }
            
            // Terminate "improperly" in order to be restarted automatically
            if (_hsm->doCrash) {
                LOGGER(_log, V3_VERB, "Restarting this subprocess\n");
                exitStatus = SIGUSR2;
                break;
            }

            // Reduce active thread count (to reduce memory usage)
            if (_hsm->doReduceThreadCount && !_hsm->didReduceThreadCount) {
                LOGGER(_log, V3_VERB, "Reducing thread count\n");
                engine.reduceActiveThreadCount();
                _hsm->didReduceThreadCount = true;
            }
            if (!_hsm->doReduceThreadCount) _hsm->didReduceThreadCount = false;

            // Do not check solved state if the current 
            // revision has already been solved
            if (lastSolvedRevision == _last_imported_revision) continue;

            // Check solved state
            int resultCode = engine.solveLoop();
            if (resultCode >= 0 && !_hsm->hasSolution) {
                // Solution found!
                auto& result = engine.getResult();
                result.id = _config.jobid;
                if (_hsm->doTerminate || result.revision < _desired_revision) {
                    // Result obsolete
                    continue;
                }
                assert(result.revision == _last_imported_revision);

                solutionVec = result.extractSolution();
                _hsm->solutionRevision = result.revision;
                _hsm->winningInstance = result.winningInstanceId;
                _hsm->globalStartOfSuccessEpoch = result.globalStartOfSuccessEpoch;
                LOGGER(_log, V5_DEBG, "DO write solution (winning instance: %i)\n", _hsm->winningInstance);
                _hsm->result = SatResult(result.result);
                size_t* solutionSize = (size_t*) SharedMemory::create(_shmem_id + ".solutionsize." + std::to_string(_hsm->solutionRevision), sizeof(size_t));
                *solutionSize = solutionVec.size();
                // Write solution
                if (*solutionSize > 0) {
                    solutionShmemId = _shmem_id + ".solution." + std::to_string(_hsm->solutionRevision);
                    solutionShmemSize =  *solutionSize*sizeof(int);
                    solutionShmem = (char*) SharedMemory::create(solutionShmemId, solutionShmemSize);
                    memcpy(solutionShmem, solutionVec.data(), solutionShmemSize);
                }
                lastSolvedRevision = result.revision;
                LOGGER(_log, V5_DEBG, "DONE write solution\n");
                _hsm->hasSolution = true;
            }
        }

        Terminator::setTerminating();
        checkTerminate(engine, true, exitStatus); // exits
        abort(); // should be unreachable

        // Shared memory will be cleaned up by the parent process.
    }

    bool checkTerminate(SatEngine& engine, bool force, int exitStatus = 0) {
        bool terminate = _hsm->doTerminate || Terminator::isTerminating(/*fromMainThread=*/true);
        if (terminate && force) {
            // clean up all resources which MUST be cleaned up (e.g., child processes)
            engine.cleanUp(true);
            _log.flush();
            _hsm->didTerminate = true;
            // terminate yourself
            Process::doExit(exitStatus);
        }
        return terminate;
    }

    void readFormulaAndAssumptionsFromSharedMem(SatEngine& engine, int revision) {

        float time = Timer::elapsedSeconds();

        size_t fSize, aSize;
        if (revision == 0) {
            fSize = _hsm->fSize;
            aSize = _hsm->aSize;
        } else {
            size_t* fSizePtr = (size_t*) accessMemory(_shmem_id + ".fsize." + std::to_string(revision), sizeof(size_t));
            size_t* aSizePtr = (size_t*) accessMemory(_shmem_id + ".asize." + std::to_string(revision), sizeof(size_t));
            fSize = *fSizePtr;
            aSize = *aSizePtr;
        }

        const int* fPtr = (const int*) accessMemory(_shmem_id + ".formulae." + std::to_string(revision),
            sizeof(int) * fSize, SharedMemory::READONLY);
        const int* aPtr = (const int*) accessMemory(_shmem_id + ".assumptions." + std::to_string(revision),
            sizeof(int) * aSize, SharedMemory::READONLY);

        if (_params.copyFormulaeFromSharedMem()) {
            // Copy formula and assumptions to your own local memory
            _read_formulae.emplace_back(fPtr, fPtr+fSize);
            _read_assumptions.emplace_back(aPtr, aPtr+aSize);

            // Reference the according positions in local memory when forwarding the data
            engine.appendRevision(revision, fSize, _read_formulae.back().data(),
                aSize, _read_assumptions.back().data(), revision == _desired_revision);
            updateChecksum(_read_formulae.back().data(), fSize);
        } else {
            // Let the solvers read from shared memory directly
            engine.appendRevision(revision, fSize, fPtr, aSize, aPtr, revision == _desired_revision);
            updateChecksum(fPtr, fSize);
        }

        if (revision > 0) {
            // Access checksum from outside
            Checksum* chk = (Checksum*) accessMemory(_shmem_id + ".checksum." + std::to_string(revision), sizeof(Checksum));
            if (chk->count() > 0) {
                // Check checksum
                if (_checksum->get() != chk->get()) {
                    LOGGER(_log, V0_CRIT, "[ERROR] Checksum fail at rev. %i. Incoming count: %ld ; local count: %ld\n", revision, chk->count(), _checksum->count());
                    abort();
                }
            }
        }

        time = Timer::elapsedSeconds() - time;
        LOGGER(_log, V3_VERB, "Read formula rev. %i (size:%lu,%lu) from shared memory in %.4fs\n", revision, fSize, aSize, time);
    }

    void* accessMemory(const std::string& shmemId, size_t size, SharedMemory::AccessMode accessMode = SharedMemory::ARBITRARY) {
        void* ptr = SharedMemory::access(shmemId, size, accessMode);
        if (ptr == nullptr) {
            LOGGER(_log, V0_CRIT, "[ERROR] Could not access shmem %s\n", shmemId.c_str());  
            abort();
        }
        return ptr;
    }

    void updateChecksum(const int* ptr, size_t size) {
        if (_checksum == nullptr) return;
        for (size_t i = 0; i < size; i++) _checksum->combine(ptr[i]);
    }

    void importRevisions(SatEngine& engine) {
        while ((_hsm->doStartNextRevision && !_hsm->didStartNextRevision) 
                || _last_imported_revision < _desired_revision) {
            if (checkTerminate(engine, false)) return;
            if (_hsm->doStartNextRevision && !_hsm->didStartNextRevision) {
                _desired_revision = _hsm->desiredRevision;
                _last_imported_revision++;
                readFormulaAndAssumptionsFromSharedMem(engine, _last_imported_revision);
                _hsm->didStartNextRevision = true;
                _hsm->hasSolution = false;
            } else doSleep();
            if (!_hsm->doStartNextRevision) _hsm->didStartNextRevision = false;
        }
        if (!_hsm->doStartNextRevision) _hsm->didStartNextRevision = false;
    }

    void doSleep() {
        // Wait until something happens
        // (can be interrupted by Fork::wakeUp(hsm->childPid))
        usleep(1000 /*1 millisecond*/);
    }
};
