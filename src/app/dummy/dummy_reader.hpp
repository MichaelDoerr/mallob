
#ifndef DOMPASCH_MALLOB_DUMMY_READER_HPP
#define DOMPASCH_MALLOB_DUMMY_READER_HPP

#include <string>

#include "data/job_description.hpp"

namespace DummyReader {
    /*
    Read a revision of a job of the "dummy" application.
    */
    bool read(const std::string& filename, JobDescription& desc);
};

#endif
