
#include <ctype.h>
#include <stdio.h>
#include <iostream>
#include "util/assert.hpp"
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "sat_reader.hpp"
#include "util/sys/terminator.hpp"
#include "app/sat/data/clause_metadata_def.hpp"

bool SatReader::read(JobDescription& desc) {

	FILE* pipe = nullptr;
	int namedpipe = -1;
	if ((_filename.size() > 3 && _filename.substr(_filename.size()-3, 3) == ".xz")
		|| (_filename.size() > 5 && _filename.substr(_filename.size()-5, 5) == ".lzma")) {
		// Decompress, read output
		auto command = "xz -c -d " + _filename;
		pipe = popen(command.c_str(), "r");
		if (pipe == nullptr) return false;
	} else if (_filename.size() > 5 && _filename.substr(_filename.size()-5, 5) == ".pipe") {
		// Named pipe!
		namedpipe = open(_filename.c_str(), O_RDONLY);
	}
	
	const std::string NC_DEFAULT_VAL = "BMMMKKK111";
	desc.setAppConfigurationEntry("__NC", NC_DEFAULT_VAL);
	desc.beginInitialization(desc.getRevision());

	if (pipe == nullptr && namedpipe == -1) {

		if (MALLOB_CLAUSE_METADATA_SIZE == 2 && _params.removeUnitsPreprocessing()) {
			// cadical input.cnf -c 0 -o removed-units.cnf
			std::string newFilename = _params.logDirectory() + "/input_units_removed.cnf";
			remove(newFilename.c_str()); // remove if existing (ignore errors)
			std::string cmd = "cadical " + _filename + " -c 0 -o " + newFilename;
			int systemRetVal = system(cmd.c_str());
			int returnCode = WEXITSTATUS(systemRetVal);
			if (returnCode == 10) {
				LOG(V2_INFO, "external call to CaDiCaL found result SAT\n");
				LOG_OMIT_PREFIX(V0_CRIT, "s SATISFIABLE\n");
				Terminator::broadcastExitSignal();
			} else if (returnCode == 20) {
				LOG(V2_INFO, "external call to CaDiCaL found result UNSAT\n");
				LOG_OMIT_PREFIX(V0_CRIT, "s UNSATISFIABLE\n");
				Terminator::broadcastExitSignal();
			} else assert(returnCode == 0 || log_return_false("Unexpected return code %i\n", returnCode));
			_filename = newFilename;
		}

		// Read file with mmap
		int fd = open(_filename.c_str(), O_RDONLY);
		if (fd == -1) return false;

		off_t size;
    	struct stat s;
		int status = stat(_filename.c_str(), &s);
		if (status == -1) return false;
		size = s.st_size;
		desc.reserveSize(size / sizeof(int));
		void* mmapped = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);

		if (_content_mode == RAW) {
			int* f = (int*) mmapped;
			for (long i = 0; i < size; i++) {
				processInt(f[i], desc);
			}
		} else {
			char* f = (char*) mmapped;
			for (long i = 0; i < size; i++) {
				process(f[i], desc);
			}
			process(EOF, desc);
		}
		munmap(mmapped, size);
		close(fd);

		//if (MALLOB_CLAUSE_METADATA_SIZE == 2) {
		//	remove(_filename.c_str());
		//}

	} else if (namedpipe != -1) {
		// Read formula over named pipe
		int iteration = 0;
		if (_content_mode == RAW) {
			int buffer[1024] = {0};
			while (iteration ^ 511 != 0 || !Terminator::isTerminating()) {
				int numRead = ::read(namedpipe, buffer, sizeof(buffer));
				if (numRead <= 0) break;
				numRead /= sizeof(int);
				for (int i = 0; i < numRead; i++) {
					processInt(buffer[i], desc);
				}
				iteration++;
			}
		} else {
			char buffer[4096] = {'\0'};
			while (iteration ^ 511 != 0 || !Terminator::isTerminating()) {
				int numRead = ::read(namedpipe, buffer, sizeof(buffer));
				if (numRead <= 0) break;
				for (int i = 0; i < numRead; i++) {
					int c = buffer[i];
					process(c, desc);
				}
				iteration++;
			}
			process(EOF, desc);
		}

	} else {
		// Read file over pipe
		if (_content_mode == RAW) {
			int iteration = 0;
			int buffer[1024] = {0};
			while (iteration ^ 511 != 0 || !Terminator::isTerminating()) {
				int numRead = ::read(fileno(pipe), buffer, sizeof(buffer));
				if (numRead <= 0) break;
				for (int i = 0; i < numRead; i++) {
					int c = buffer[i];
					processInt(c, desc);
				}
				iteration++;
			}
		} else {
			char buffer[4096] = {'\0'};
			while (!Terminator::isTerminating() && fgets(buffer, sizeof(buffer), pipe) != nullptr) {
				size_t pos = 0;
				while (buffer[pos] != '\0') {
					int c = buffer[pos++];
					process(c, desc);
				}
			}
			process(EOF, desc);
		}
	}

	std::string numClausesStr = std::to_string(_num_read_clauses);
	assert(numClausesStr.size() < NC_DEFAULT_VAL.size());
	while (numClausesStr.size() < NC_DEFAULT_VAL.size())
		numClausesStr += ".";
	desc.setAppConfigurationEntry("__NC", numClausesStr);

	desc.endInitialization();

	if (pipe != nullptr) pclose(pipe);
	if (namedpipe != -1) close(namedpipe);

	return isValidInput();
}
