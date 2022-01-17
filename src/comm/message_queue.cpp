
#include "comm/message_queue.hpp"

#include "comm/message_handle.hpp"

#include <list>
#include <cmath>
#include "util/assert.hpp"
#include <unistd.h>

#include "util/hashing.hpp"
#include "util/sys/background_worker.hpp"
#include "util/logger.hpp"
#include "comm/msgtags.h"
#include "util/ringbuffer.hpp"

MessageQueue::MessageQueue(int maxMsgSize) : _max_msg_size(maxMsgSize), 
    _fragmented_queue(1024), _garbage_queue(1024) {
    
    MPI_Comm_rank(MPI_COMM_WORLD, &_my_rank);
    _recv_data = (uint8_t*) malloc(maxMsgSize+20);

    MPI_Irecv(_recv_data, maxMsgSize+20, MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &_recv_request);

    _batch_assembler.run([&]() {runFragmentedMessageAssembler();});
    _gc.run([&]() {runGarbageCollector();});
}

MessageQueue::~MessageQueue() {
    _batch_assembler.stop();
    _gc.stop();
    free(_recv_data);
}

void MessageQueue::registerCallback(int tag, const MsgCallback& cb) {
    if (_callbacks.count(tag)) {
        log(V0_CRIT, "More than one callback for tag %i!\n", tag);
        abort();
    }
    _callbacks[tag] = cb;
}

void MessageQueue::registerSentCallback(std::function<void(int)> callback) {
    _send_done_callback = callback;
}

void MessageQueue::clearCallbacks() {
    _callbacks.clear();
    _send_done_callback = [](int) {};
}

int MessageQueue::send(DataPtr data, int dest, int tag) {

    // Initialize send handle
    {
        SendHandle handle;
        handle.id = _running_send_id++;
        handle.data = data;
        handle.dest = dest;
        handle.tag = tag;

        int msglen = handle.data->size();
        log(V5_DEBG, "MQ SEND n=%i d=[%i] t=%i c=(%i,...,%i,%i,%i)\n", handle.data->size(), dest, tag, 
            msglen>=1*sizeof(int) ? *(int*)(handle.data->data()) : 0, 
            msglen>=3*sizeof(int) ? *(int*)(handle.data->data()+msglen - 3*sizeof(int)) : 0, 
            msglen>=2*sizeof(int) ? *(int*)(handle.data->data()+msglen - 2*sizeof(int)) : 0, 
            msglen>=1*sizeof(int) ? *(int*)(handle.data->data()+msglen - 1*sizeof(int)) : 0);

        if (dest == _my_rank) {
            // Self message
            _self_recv_queue.push_back(std::move(handle));
            return _self_recv_queue.back().id;
        }

        _send_queue.push_back(std::move(handle));
    }

    SendHandle& h = _send_queue.back();
    if (h.data->size() > _max_msg_size+3*sizeof(int)) {
        // Batch data, only send first batch
        log(V5_DEBG, "MQ initialized handle for large msg\n");
        h.sizePerBatch = _max_msg_size;
        h.sentBatches = 0;
        h.totalNumBatches = h.getTotalNumBatches();
        int sendTag = h.prepareForNextBatch();
        log(V5_DEBG, "MQ sending batch %i/%i\n", 0, h.totalNumBatches);
        MPI_Isend(h.tempStorage.data(), h.tempStorage.size(), MPI_BYTE, dest, 
            sendTag, MPI_COMM_WORLD, &h.request);
        log(V4_VVER, "MQ sent batch %i/%i\n", 0, h.totalNumBatches);
    } else {
        // Directly send entire message
        MPI_Isend(h.data->data(), h.data->size(), MPI_BYTE, dest, tag, MPI_COMM_WORLD, &h.request);
    }

    return h.id;
}

void MessageQueue::advance() {
    //log(V5_DEBG, "BEGADV\n");
    _iteration++;
    processReceived();
    processSelfReceived();
    processAssembledReceived();
    processSent();
    //log(V5_DEBG, "ENDADV\n");
}

void MessageQueue::runFragmentedMessageAssembler() {

    while (_batch_assembler.continueRunning()) {

        usleep(1000); // 1 ms
        if (_fragmented_queue.empty()) continue;

        auto opt = _fragmented_queue.consume();
        if (!opt.has_value()) continue;
        ReceiveFragment& data = opt.value();

        if (data.dataFragments.empty()) continue;

        // Assemble fragments
        MessageHandle h;
        h.source = data.source;
        h.tag = data.tag;
        size_t sumOfSizes = 0;
        for (size_t i = 0; i < data.dataFragments.size(); i++) {
            const auto& frag = data.dataFragments[i];
            assert(frag || log_return_false("No valid fragment %i found!\n", i));
            sumOfSizes += frag->size();
        }
        std::vector<uint8_t> outData(sumOfSizes);
        size_t offset = 0;
        for (const auto& frag : data.dataFragments) {
            memcpy(outData.data()+offset, frag->data(), frag->size());
            offset += frag->size();
        }
        h.setReceive(std::move(outData));
        // Put into finished queue
        {
            auto lock = _fused_mutex.getLock();
            _fused_queue.push_back(std::move(h));
            atomics::incrementRelaxed(_num_fused);
        }
    }
}

void MessageQueue::runGarbageCollector() {

    while (_gc.continueRunning()) {
        usleep(1000*1000); // 1s            
        if (_garbage_queue.empty()) continue;
        auto opt = _garbage_queue.consume();
        if (!opt.has_value()) continue;
        auto& dataPtr = opt.value();
        dataPtr.reset();
    }
}

void MessageQueue::processReceived() {
    // Test receive
    //log(V5_DEBG, "MQ TEST\n");
    int flag = false;
    MPI_Status status;
    MPI_Test(&_recv_request, &flag, &status);
    if (!flag) return;

    // Message finished
    const int source = status.MPI_SOURCE;
    int tag = status.MPI_TAG;
    int msglen;
    MPI_Get_count(&status, MPI_BYTE, &msglen);
    log(V5_DEBG, "MQ RECV n=%i s=[%i] t=%i c=(%i,...,%i,%i,%i)\n", msglen, source, tag, 
            msglen>=1*sizeof(int) ? *(int*)(_recv_data) : 0, 
            msglen>=3*sizeof(int) ? *(int*)(_recv_data+msglen - 3*sizeof(int)) : 0, 
            msglen>=2*sizeof(int) ? *(int*)(_recv_data+msglen - 2*sizeof(int)) : 0, 
            msglen>=1*sizeof(int) ? *(int*)(_recv_data+msglen - 1*sizeof(int)) : 0);

    if (tag >= MSG_OFFSET_BATCHED) {
        // Fragment of a message
        tag -= MSG_OFFSET_BATCHED;
        int id, sentBatch, totalNumBatches;
        // Read meta data from end of message
        memcpy(&id,              _recv_data+msglen - 3*sizeof(int), sizeof(int));
        memcpy(&sentBatch,       _recv_data+msglen - 2*sizeof(int), sizeof(int));
        memcpy(&totalNumBatches, _recv_data+msglen - 1*sizeof(int), sizeof(int));
        msglen -= 3*sizeof(int);
        
        // Store data in fragments structure
        
        //log(V5_DEBG, "MQ STORE (%i,%i) %i/%i\n", source, id, sentBatch, totalNumBatches);
        auto key = std::pair<int, int>(source, id);
        if (!_fragmented_messages.count(key)) {
            auto& fragment = _fragmented_messages[key];
            fragment.source = source;
            fragment.tag = tag;
        }
        auto& fragment = _fragmented_messages[key];

        assert(fragment.source == source);
        assert(fragment.tag == tag);
        assert(sentBatch < totalNumBatches || log_return_false("Invalid batch %i/%i!\n", sentBatch, totalNumBatches));
        if (sentBatch >= fragment.dataFragments.size()) fragment.dataFragments.resize(sentBatch+1);

        //log(V5_DEBG, "MQ STORE alloc\n");
        assert(fragment.dataFragments[sentBatch] == nullptr || log_return_false("Batch %i/%i already present!\n", sentBatch, totalNumBatches));
        fragment.dataFragments[sentBatch].reset(new std::vector<uint8_t>(_recv_data, _recv_data+msglen));
        
        //log(V5_DEBG, "MQ STORE produce\n");
        // All fragments of the message received?
        fragment.receivedFragments++;
        if (fragment.receivedFragments == totalNumBatches) {
            while (!_fragmented_queue.produce(std::move(fragment))) {}
            _fragmented_messages.erase(key);
        }
    } else {
        // Single message
        //log(V5_DEBG, "MQ singlerecv\n");
        MessageHandle h;
        h.setReceive(std::vector<uint8_t>(_recv_data, _recv_data+msglen));
        h.tag = tag;
        h.source = source;
        //log(V5_DEBG, "MQ cb\n");
        _callbacks.at(h.tag)(h);
        //log(V5_DEBG, "MQ dealloc\n");
    }

    // Reset recv handle
    //log(V5_DEBG, "MQ MPI_Irecv\n");
    MPI_Irecv(_recv_data, _max_msg_size+20, MPI_BYTE, MPI_ANY_SOURCE, 
        MPI_ANY_TAG, MPI_COMM_WORLD, &_recv_request);
}

void MessageQueue::processSelfReceived() {
    if (_self_recv_queue.empty()) return;
    // copy content of queue due to concurrent modification in callback
    // (up to x elements in order to stay responsive)
    std::vector<SendHandle> copiedQueue;
    while (!_self_recv_queue.empty() && copiedQueue.size() < 4) {
        copiedQueue.push_back(std::move(_self_recv_queue.front()));
        _self_recv_queue.pop_front();
    }
    for (auto& sh : copiedQueue) {
        MessageHandle h;
        h.tag = sh.tag;
        h.source = sh.dest;
        h.setReceive(std::move(*sh.data));
        _callbacks.at(h.tag)(h);
        _send_done_callback(sh.id); // notify completion
    }
}

void MessageQueue::processAssembledReceived() {

    int numFused = _num_fused.load(std::memory_order_relaxed);
    if (numFused > 0 && _fused_mutex.tryLock()) {

        int consumed = 0;
        while (!_fused_queue.empty() && consumed < 4) {

            auto& h = _fused_queue.front();
            log(V5_DEBG, "MQ FUSED t=%i\n", h.tag);
            _callbacks.at(h.tag)(h);
            
            if (h.getRecvData().size() > _max_msg_size) {
                // Concurrent deallocation of large chunk of data
                while (!_garbage_queue.produce(
                    DataPtr(
                        new std::vector<uint8_t>(
                            h.moveRecvData()
                        )
                    )
                )) {}
            }
            _fused_queue.pop_front();
            atomics::decrementRelaxed(_num_fused);

            consumed++;
            if (consumed >= 4) break;
        }

        _fused_mutex.unlock();
    }
}

void MessageQueue::processSent() {

    int numTested = 0;
    auto it = _send_queue.begin();

    // Test each send handle
    while (it != _send_queue.end()) {
        if (numTested == 4) break; // limit number of tests per call

        SendHandle& h = *it;
        assert(h.request != MPI_REQUEST_NULL);
        int flag = false;
        MPI_Test(&h.request, &flag, MPI_STATUS_IGNORE);
        numTested++;

        if (!flag) {
            it++; // go to next handle
            continue;
        }
        
        // Sent!
        //log(V5_DEBG, "MQ SENT n=%i d=[%i] t=%i\n", h.data->size(), h.dest, h.tag);
        bool completed = true;

        // Batched?
        if (h.isBatched()) {
            // Batch of a large message sent
            log(V5_DEBG, "MQ SENT id=%i %i/%i n=%i d=[%i] t=%i c=(%i,...,%i,%i,%i)\n", h.id, h.sentBatches, 
                h.totalNumBatches, h.data->size(), h.dest, h.tag, 
                *(int*)(h.tempStorage.data()), 
                *(int*)(h.tempStorage.data()+h.tempStorage.size()-3*sizeof(int)), 
                *(int*)(h.tempStorage.data()+h.tempStorage.size()-2*sizeof(int)),
                *(int*)(h.tempStorage.data()+h.tempStorage.size()-1*sizeof(int)));
            h.sentBatches++;

            // More batches yet to send?
            if (!h.isFinished()) {
                // Send next batch
                int sendTag = h.prepareForNextBatch();
                MPI_Isend(h.tempStorage.data(), h.tempStorage.size(), MPI_BYTE, h.dest, 
                    sendTag, MPI_COMM_WORLD, &h.request);
                completed = false;
            }
        }

        if (completed) {
            // Notify completion
            _send_done_callback(h.id); 

            if (h.data->size() > _max_msg_size) {
                // Concurrent deallocation of SendHandle's large chunk of data
                while (!_garbage_queue.produce(std::move(h.data))) {}
            }
            
            // Remove handle
            it = _send_queue.erase(it); // go to next handle
        } else {
            it++; // go to next handle
        }
    }
}
