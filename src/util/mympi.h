
#ifndef DOMPASCH_CUCKOO_BALANCER_BOOSTMPI
#define DOMPASCH_CUCKOO_BALANCER_BOOSTMPI

#include <iostream>
#include <memory>
#include <set>
#include <map>
#include <assert.h>
#include <mpi.h>

#include "data/serializable.h"
#include "util/console.h"
#include "util/timer.h"

#define MAX_JOB_MESSAGE_PAYLOAD_PER_NODE 1500*sizeof(int)
#define MAX_ANYTIME_MESSAGE_SIZE 1024
#define MIN_PRIORITY 0


const int MSG_ANYTIME = 1337;
const int MSG_WARMUP = 1;
/*
 * The sender wishes to receive the current volume of job j from the receiver.
 * Data type: 1 int (jobId)
 */
const int MSG_QUERY_VOLUME = 2;
/*
 * The receiver is queried to begin working as the i-th node of job j.
 * Data type: JobRequest
 */
const int MSG_FIND_NODE = 3;
/*
 * The sender asks the receiver to become the sender's parent for some job j
 * of which a corresponding child position was advertised.
 * Data type: JobRequest
 */
const int MSG_OFFER_ADOPTION = 4;
/*
 * The senders confirms that the receiver may become the sender's child
 * with respect to the job and index specified in the signature.
 * Data type: JobSignature
 */
const int MSG_ACCEPT_ADOPTION_OFFER = 5;
/*
 * The sender rejects the receiver to become the sender's child
 * with respect to the job and index specified in the signature.
 * Data type: JobRequest
 */
const int MSG_REJECT_ADOPTION_OFFER = 6;
/*
 * The sender acknowledges that it received the receiver's previous
 * MSG_ACCEPT_ADOPTION_OFFER message.
 * Data type: JobRequest
 */
const int MSG_CONFIRM_ADOPTION = 7;
/*
 * The sender propagates a job's volume update to the receiver.
 * Data type: [jobId, volume]
 */
const int MSG_UPDATE_VOLUME = 8;
/*
 * The sender transfers a full job description to the receiver.
 * Data type: JobDescription
 * Warning: Length may exceed the default maximum message length.
 */
const int MSG_SEND_JOB_DESCRIPTION = 9;
/*
 * The sender informs the receiver that a solution was found for the job
 * of the specified ID.
 * Data type: [jobId, resultCode]
 */
const int MSG_WORKER_FOUND_RESULT = 10;
/*
 * The sender provides the global rank of the client node which initiated
 * a certain job.
 * Data type: [jobId, clientRank]
 */
const int MSG_FORWARD_CLIENT_RANK = 11;
/*
 * A signal to terminate a job is propagated.
 * Data type: [jobId]
 */
const int MSG_TERMINATE = 12;
/*
 * The sender informs the receiver (a client) that a job has been finished,
 * and also provides the size of the upcoming job result message.
 * Data type: [jobId, sizeOfResult]
 */
const int MSG_JOB_DONE = 13;
/*
 * The sender (a client) acknowledges that it received the receiver's MSG_JOB_DONE
 * message and signals that it wishes to receive the full job result.
 * Data type: [jobId, sizeOfResult]
 */
const int MSG_QUERY_JOB_RESULT = 14;
/*
 * The sender provides a job's full result to the receiver (a client).
 * Data type: JobResult
 * Warning: Length may exceed the default maximum message length.
 */
const int MSG_SEND_JOB_RESULT = 15;
/**
 * The sender (a worker node) informs the receiver (the job's root node) that 
 * the sender is defecting to another job.
 * Data type: [jobId, index]
 */
const int MSG_WORKER_DEFECTING = 16;
/* For incremental jobs. Unsupported as of now */
const int MSG_NOTIFY_JOB_REVISION = 17;
/* For incremental jobs. Unsupported as of now */
const int MSG_QUERY_JOB_REVISION_DETAILS = 18;
/* For incremental jobs. Unsupported as of now */
const int MSG_SEND_JOB_REVISION_DETAILS = 19;
/* For incremental jobs. Unsupported as of now */
const int MSG_ACK_JOB_REVISION_DETAILS = 20;
/* For incremental jobs. Unsupported as of now */
const int MSG_SEND_JOB_REVISION_DATA = 21;
/* For incremental jobs. Unsupported as of now */
const int MSG_INCREMENTAL_JOB_FINISHED = 22;
/**
 * The sender informs the receiver that the receiver should interrupt 
 * the specified job it currently computes on (leaving the possibility 
 * to continue computation at some later point). Possibly self message.
 * Data type: [jobId, index]
 */
const int MSG_INTERRUPT = 23;
/**
 * The sender informs the receiver that the receiver should abort, i.e., 
 * terminate the specified job it currently computes on. Possibly self message.
 * Data type: [jobId, index]
 */
const int MSG_ABORT = 24;
/**
 * A message that tells some node (worker or client) to immediately exit the application.
 */
const int MSG_EXIT = 25;
/**
 * A client tells another client that the sender is now out of jobs to introduce to the system.
 * Used to detect early termination.
 */
const int MSG_CLIENT_FINISHED = 26;
/**
 * Some data is being reduced or broadcast via a custom operation.
 */
const int MSG_COLLECTIVES = 27;
/**
 * Some data is being reduced via a custom operation.
 */
const int MSG_ANYTIME_REDUCTION = 28;
/**
 * Some data is being broadcast via a custom operation.
 */
const int MSG_ANYTIME_BROADCAST = 29;
/**
 * Tag for the job-internal, application-specific communication inside a job.
 * The payload should contain another job-internal message tag.
 */
const int MSG_JOB_COMMUNICATION = 30;
/**
 * The sender notifies the receiver that the job result the receiver just sent
 * is obsolete and will not be needed. It does not need to be preserved.
 */
const int MSG_RESULT_OBSOLETE = 31;

const int MSG_FIND_NODE_ONESHOT = 32;
const int MSG_ONESHOT_DECLINED = 33;

struct MsgTag {
    int id;
    bool anytime;
};

struct MessageHandle {
    int id;
    int tag;
    int source;
    std::shared_ptr<std::vector<uint8_t>> sendData;
    std::shared_ptr<std::vector<uint8_t>> recvData;
    bool selfMessage = false;
    bool finished = false;
    float creationTime = 0;
    MPI_Request request;
    MPI_Status status;

    MessageHandle(int id) : id(id) {
        status.MPI_SOURCE = -1; 
        status.MPI_TAG = -1; 
        sendData = std::make_shared<std::vector<uint8_t>>();
        recvData = std::make_shared<std::vector<uint8_t>>();
        creationTime = Timer::elapsedSeconds();
        //Console::log(Console::VVVVERB, "Msg ID=%i created", id);
    }
    MessageHandle(int id, int recvSize) : id(id) {
        status.MPI_SOURCE = -1; 
        status.MPI_TAG = -1; 
        sendData = std::make_shared<std::vector<uint8_t>>();
        recvData = std::make_shared<std::vector<uint8_t>>(recvSize);
        creationTime = Timer::elapsedSeconds();
        //Console::log(Console::VVVVERB, "Msg ID=%i created", id);
    }
    MessageHandle(int id, const std::shared_ptr<std::vector<uint8_t>>& data) : id(id), sendData(data) {
        status.MPI_SOURCE = -1; 
        status.MPI_TAG = -1; 
        recvData = std::make_shared<std::vector<uint8_t>>();
        creationTime = Timer::elapsedSeconds();
        //Console::log(Console::VVVVERB, "Msg ID=%i created", id);
    }
    MessageHandle(int id, const std::shared_ptr<std::vector<uint8_t>>& sendData, const std::shared_ptr<std::vector<uint8_t>>& recvData) : 
        id(id), sendData(sendData), recvData(recvData) {
        status.MPI_SOURCE = -1; 
        status.MPI_TAG = -1;
        creationTime = Timer::elapsedSeconds();
        //Console::log(Console::VVVVERB, "Msg ID=%i created", id);
    }

    ~MessageHandle() {
        sendData = NULL;
        recvData = NULL;
        //Console::log(Console::VVVVERB, "Msg ID=%i deleted", id);
    }

    bool testSent();
    bool testReceived();
    bool shouldCancel(float elapsedTime);
    void cancel();
};

/**
 * A std::shared_ptr around a MessageHandle instance which captures all relevant information
 * on a specific MPI message.
 */
typedef std::shared_ptr<MessageHandle> MessageHandlePtr;

class MyMpi {

private:
    static std::set<MessageHandlePtr> _handles;
    static std::set<MessageHandlePtr> _sent_handles;
    static std::map<int, MsgTag> _tags;

public:
    static int _max_msg_length;
    static bool _monitor_off;

    static void init(int argc, char *argv[]);
    static void beginListening();

    static MessageHandlePtr isend(MPI_Comm communicator, int recvRank, int tag, const Serializable& object);
    static MessageHandlePtr isend(MPI_Comm communicator, int recvRank, int tag, const std::shared_ptr<std::vector<uint8_t>>& object);
    static MessageHandlePtr irecv(MPI_Comm communicator);
    static MessageHandlePtr irecv(MPI_Comm communicator, int tag);
    static MessageHandlePtr irecv(MPI_Comm communicator, int source, int tag);
    static MessageHandlePtr irecv(MPI_Comm communicator, int source, int tag, int size);
    /*
    static MessageHandlePtr  send(MPI_Comm communicator, int recvRank, int tag, const Serializable& object);
    static MessageHandlePtr  send(MPI_Comm communicator, int recvRank, int tag, const std::shared_ptr<std::vector<uint8_t>>& object);
    static MessageHandlePtr  recv(MPI_Comm communicator, int tag, int size);
    static MessageHandlePtr  recv(MPI_Comm communicator, int tag);
    */
    static MPI_Request    ireduce(MPI_Comm communicator, float* contribution, float* result, int rootRank);
    static MPI_Request iallreduce(MPI_Comm communicator, float* contribution, float* result);
    static MPI_Request iallreduce(MPI_Comm communicator, float* contribution, float* result, int numFloats);
    static bool test(MPI_Request& request, MPI_Status& status);

    static std::vector<MessageHandlePtr> poll();
    static int getNumActiveHandles() {
        return _handles.size();
    }
    static bool hasOpenSentHandles();
    static void testSentHandles();
    static bool isAnytimeTag(int tag);

    static int size(MPI_Comm comm);
    static int rank(MPI_Comm comm);
    static int random_other_node(MPI_Comm comm, const std::set<int>& excludedNodes);
    
    static int nextHandleId();

    // defined in mpi_monitor.*
    static std::string currentCall(double* callStart);


private:
    static void resetListenerIfNecessary(int tag);

};

#endif
