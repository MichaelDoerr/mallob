
#ifndef DOMPASCH_MALLOB_BUFFER_MERGER_HPP
#define DOMPASCH_MALLOB_BUFFER_MERGER_HPP

#include <vector>

#include "buffer_reader.hpp"
#include "app/sat/hordesat/utilities/clause_filter.hpp" 

class BufferMerger {
private:
    int _max_lbd_partitioned_size;
    bool _use_checksum;
    std::vector<BufferReader> _readers;
public:
    BufferMerger(int maxLbdPartitionedSize, bool useChecksum = false);
    void add(BufferReader&& reader);
    std::vector<int> merge(int sizeLimit);
};

#endif 
