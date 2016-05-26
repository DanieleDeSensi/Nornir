/*
 * node.hpp
 *
 * Created on: 23/03/2015
 *
 * =========================================================================
 *  Copyright (C) 2015-, Daniele De Sensi (d.desensi.software@gmail.com)
 *
 *  This file is part of nornir.
 *
 *  nornir is free software: you can redistribute it and/or
 *  modify it under the terms of the Lesser GNU General Public
 *  License as published by the Free Software Foundation, either
 *  version 3 of the License, or (at your option) any later version.

 *  nornir is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  Lesser GNU General Public License for more details.
 *
 *  You should have received a copy of the Lesser GNU General Public
 *  License along with nornir.
 *  If not, see <http://www.gnu.org/licenses/>.
 *
 * =========================================================================
 */

#ifndef NORNIR_NODE_HPP_
#define NORNIR_NODE_HPP_

#include "./ffincs.hpp"
#include "./parameters.hpp"

#include "external/Mammut/mammut/mammut.hpp"

#undef DEBUG
#undef DEBUGB

#define TERMINATE_APPLICATION do{ terminate(); return (void*) ff::FF_EOS;} while(0)

namespace nornir{

typedef enum{
    NODE_TYPE_EMITTER,
    NODE_TYPE_WORKER,
    NODE_TYPE_COLLECTOR
}NodeType;


/*!
 * \internal
 * \struct NodeSample
 * \brief Represents a sample of values taken from an adaptive node.
 *
 * This struct represents a sample of values taken from an adaptive node.
 */
typedef struct WorkerSample{
    // The percentage ([0, 100]) of time that the node spent on svc().
    double loadPercentage;

    // The number of computed tasks.
    double tasksCount;

    // The average service time (in ticks).
    double latency;

    // The bandwidth of the node.
    double bandwidthTotal;

    WorkerSample():loadPercentage(0), tasksCount(0),
                   latency(0), bandwidthTotal(0){;}

    WorkerSample& operator+=(const WorkerSample& rhs){
        loadPercentage += rhs.loadPercentage;
        tasksCount += rhs.tasksCount;
        latency += rhs.latency;
        bandwidthTotal += rhs.bandwidthTotal;
        return *this;
    }
}NodeSample;

inline WorkerSample operator+(WorkerSample lhs, const WorkerSample& rhs){
    lhs += rhs;
    return lhs;
}

/*!
 * \internal
 * \class ManagementRequestType
 * \brief Possible request types that a manager can make.
 */
typedef enum{
    // Reset the current sample.
    MGMT_REQ_RESET_SAMPLE = 0,
  
    // Get the current sample and reset it.
    MGMT_REQ_GET_AND_RESET_SAMPLE,

    // Freezes the farm.
    MGMT_REQ_FREEZE,

    // Thaws the farm.
    MGMT_REQ_THAW,

    // Switch to blocking/nonblocking
    MGMT_REQ_SWITCH_BLOCKING,

    // ATTENTION: This must always be the last
    MGMT_REQ_NUM
}ManagementRequestType;

/**
 * A management request.
 */
typedef struct{
    ManagementRequestType type;
    void* mark;
    size_t numWorkers;
}ManagementRequest;

/*!private
 * \class AdaptiveNode
 * \brief This class wraps a ff_node to let it reconfigurable.
 *
 * This class wraps a ff_node to let it reconfigurable.
 */
class AdaptiveNode: public ff::ff_node{
private:
    template <typename L, typename G>
    friend class ManagerFarm;
    friend class KnobWorkers;
    friend class KnobMapping;
    friend class TriggerQBlocking;

    volatile bool _started;
    volatile bool* _terminated;
    mammut::task::TasksManager* _tasksManager;
    mammut::task::ThreadHandler* _thread;
    // We push the pointer to a position in the _managementRequests array.
    // In our case is always _managementRequests[i].type == i
    ManagementRequest _managementRequests[MGMT_REQ_NUM];
    WorkerSample _sampleResponse;
    double _ticksPerNs;
    ticks _startTicks;
    ticks _ticksWork;
    ticks _tasksCount;
    NodeType _nodeType;
    ff::ff_thread* _ffThread;
    const Parameters* _p;

    // Queue used by the manager to notify that a request is present.
    ff::SWSR_Ptr_Buffer _managementQ;

    // Queue used by the node to notify that a response is present
    // on _sampleResponse.
    ff::SWSR_Ptr_Buffer _responseQ;

    /**
     * Operations that need to take place before the node is already running.
     * @param p The adaptivity parameters.
     * @param nodeType The type of the node.
     * @param terminated A pointer to the termination flag.
     * @param ffThread The lb or gt thread.
     */
    void initPreRun(const Parameters* p, NodeType nodeType,
                    volatile bool* terminated, ff::ff_thread* ffThread = NULL);


    /**
     * Operations that need to take place after the node is already running.
     */
    void initPostRun();

    /**
     * Cleans then node.
     */
    void clean();

    /**
     * Moves this node on a specific virtual core.
     * @param vc The virtual core where this nodes must be moved.
     */
    void move(mammut::topology::VirtualCore* vc);

    /**
     * Moves this node on a specific set of virtual cores.
     * @param vc The virtual cores where this nodes must be moved.
     */
    void move(const std::vector<const mammut::topology::VirtualCore*>& virtualCores);

    /**
     * The result of askForSample call.
     * @param sample The statistics computed since the last
     *               time 'askForSample' has been called.
     * @param avgLatency The average latency.
     */
    void getSampleResponse(WorkerSample& sample, double avgLatency);

    /**
     * Asks the node to reset the current sample.
     **/
    void resetSample();

    /**
     * Asks the node for a sample of the statistics computed since the last
     * time this method has been called.
     * The result can be retrieved with getSampleResponse call.
     */
    void askForSample();

    /**
     * Sets the queues to blocking.
     * ATTENTION: Can only be called on emitter.
     */
    void setQBlocking();

    /**
     * Sets the queues to nonblocking.
     * ATTENTION: Can only be called on emitter.
     */
    void setQNonblocking();

    /**
     * Tells the node to freeze the farm.
     * ATTENTION: Can only be called on emitter.
     */
    void freezeAll(void* mark);

    /**
     * Thaws the farm.
     * ATTENTION: Can only be called on emitter.
     */
    void thawAll(size_t numWorkers);

    /**
     * Called on the node before starting it.
     * It must be called when the node is running.
     */
    void prepareToFreeze();

    /**
     * Called on the node before starting them.
     * It must be called when the node is frozen.
     */
    void prepareToRun();

    /**
     * Resets the current sample.
     */
    void reset();

    /**
     * Stores a sample.
     */
    void storeSample();

    /**
     * The callback that will be executed by the ff_node before
     * reading a task from the queue.
     * @param p Is the lb_t or gt_t in case of emitter or collector.
     */
    void callbackIn(void *p) CX11_KEYWORD(final);

    /**
     * The callback that will be executed by the ff_node after
     * pushing a task to a queue.
     * @param p Is the lb_t or gt_t in case of emitter or collector.
     */
    void callbackOut(void *p) CX11_KEYWORD(final);

    void svc_end() CX11_KEYWORD(final);
    void eosnotify(ssize_t id) CX11_KEYWORD(final);

public:
    /**
     * Builds an adaptive node.
     */
    AdaptiveNode();

    /**
     * Destroyes this adaptive node.
     */
    ~AdaptiveNode();

    /**
     * To be called inside svc() before sending EOS.
     */
    void terminate();

    /**
     * This method can be implemented by the nodes to be aware of a change
     * in the number of workers.
     * When the farm is stopped and before running it again with the new
     * number of workers, this method is called. It is called on the emitter
     * (if present), on the collector (if present) and on all the workers of
     * the new configuration.
     * In this way, if needed action may be taken to prepare for the new
     * configuration (e.g. shared state modification, etc..).
     * @param oldNumWorkers The old number of workers.
     * @param newNumWorkers The new number of workers.
     */
    virtual void notifyRethreading(size_t oldNumWorkers,
                                   size_t newNumWorkers);
};

}

#endif /* NORNIR_NODE_HPP_ */
