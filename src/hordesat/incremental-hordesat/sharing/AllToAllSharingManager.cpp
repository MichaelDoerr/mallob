/*
 * AllToAllSharingManager.cpp
 *
 *  Created on: Mar 5, 2015
 *      Author: balyo
 */

#include <assert.h>

#include "AllToAllSharingManager.h"
#include "../utilities/mympi.h"
#include "../utilities/Logger.h"


DefaultSharingManager::DefaultSharingManager(int mpi_size, int mpi_rank,
		vector<PortfolioSolverInterface*>& solvers, ParameterProcessor& params)
	:size(mpi_size),rank(mpi_rank),solvers(solvers),params(params),callback(*this) {
    for (size_t i = 0; i < solvers.size(); i++) {
		solvers[i]->setLearnedClauseCallback(&callback, i);
		if (solvers.size() > 1) {
			solverFilters.push_back(new ClauseFilter());
		}
	}
}

std::vector<int> DefaultSharingManager::prepareSharing() {

    //log(2, "Sharing clauses among %i nodes\n", size);
    static int prodInc = 1;
	static int lastInc = 0;
	if (!params.isSet("fd")) {
		nodeFilter.clear();
	}
	int selectedCount;
	int used = cdb.giveSelection(outBuffer, COMM_BUFFER_SIZE, &selectedCount);
	log(2, "Prepared %i clauses in a buffer of size %i\n", selectedCount, used);
	stats.sharedClauses += selectedCount;
	int usedPercent = (100*used)/COMM_BUFFER_SIZE;
	if (usedPercent < 80) {
		int increaser = lastInc++ % solvers.size();
		solvers[increaser]->increaseClauseProduction();
		log(2, "Node %d production increase for %d. time, core %d will increase.\n", rank, prodInc++, increaser);
	}
	log(1, "Node %d filled %d%% of its learned clause buffer\n", rank, usedPercent);
    std::vector<int> clauseVec(outBuffer, outBuffer + COMM_BUFFER_SIZE);

    return clauseVec;
}

void DefaultSharingManager::digestSharing(const std::vector<int>& result) {

	assert(result.size() % COMM_BUFFER_SIZE == 0);
    size = result.size() / COMM_BUFFER_SIZE;
    
    //std::memcpy(incommingBuffer, result.data(), result.size()*sizeof(int));
    
	if (solvers.size() > 1) {
		// get all the clauses
		cdb.setIncomingBuffer(result.data(), COMM_BUFFER_SIZE, size, -1);
	} else {
		// get all the clauses except for those that this node sent
		cdb.setIncomingBuffer(result.data(), COMM_BUFFER_SIZE, size, rank);
	}
	vector<int> cl;
	int passedFilter = 0;
	int failedFilter = 0;
	long totalLen = 0;
	vector<vector<int> > clausesToAdd;
	while (cdb.getNextIncomingClause(cl)) {
		totalLen += cl.size();
		if (nodeFilter.registerClause(cl)) {
			clausesToAdd.push_back(cl);
			passedFilter++;
		} else {
			failedFilter++;
		}
	}
	for (size_t sid = 0; sid < solvers.size(); sid++) {
		solvers[sid]->addLearnedClauses(clausesToAdd);
	}
	/*
	if (solvers.size() > 1) {
		for (size_t sid = 0; sid < solvers.size(); sid++) {
			for (size_t cid = 0; cid < clausesToAdd.size(); cid++) {
				if (solverFilters[sid]->registerClause(clausesToAdd[cid])) {
					solvers[sid]->addLearnedClause(clausesToAdd[cid]);
				}
			}
			if (!params.isSet("fd")) {
				solverFilters[sid]->clear();
			}
		}
	} else {
		solvers[0]->addLearnedClauses(clausesToAdd);
	}
	*/
	int total = passedFilter + failedFilter;
	stats.filteredClauses += failedFilter;
	stats.importedClauses += passedFilter;
	if (total > 0) {
		log(2, "filter blocked %d%% (%d/%d) of incoming clauses, avg len %.2f\n",
				100*failedFilter/total,
				failedFilter, total, totalLen/(float)total);
	}

}

SharingStatistics DefaultSharingManager::getStatistics() {
	return stats;
}

DefaultSharingManager::~DefaultSharingManager() {
	for (size_t i = 0; i < solverFilters.size(); i++) {
		delete solverFilters[i];
	}
}

