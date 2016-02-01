/*
 * manager.cpp
 *
 * Created on: 23/03/2015
 *
 * =========================================================================
 *  Copyright (C) 2015-, Daniele De Sensi (d.desensi.software@gmail.com)
 *
 *  This file is part of AdaptiveFastFlow.
 *
 *  AdaptiveFastFlow is free software: you can redistribute it and/or
 *  modify it under the terms of the Lesser GNU General Public
 *  License as published by the Free Software Foundation, either
 *  version 3 of the License, or (at your option) any later version.

 *  AdaptiveFastFlow is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  Lesser GNU General Public License for more details.
 *
 *  You should have received a copy of the Lesser GNU General Public
 *  License along with AdaptiveFastFlow.
 *  If not, see <http://www.gnu.org/licenses/>.
 *
 * =========================================================================
 */

#include "./manager.hpp"
#include "parameters.hpp"
#include "predictors.hpp"
#include "./node.hpp"
#include "utils.hpp"

#include <ff/farm.hpp>
#include <mammut/module.hpp>
#include <mammut/utils.hpp>
#include <mammut/mammut.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

#undef DEBUG
#undef DEBUGB

#ifdef DEBUG_MANAGER
#define DEBUG(x) do { cerr << "[Manager] " << x << endl; } while (0)
#define DEBUGB(x) do {x;} while(0)
#else
#define DEBUG(x)
#define DEBUGB(x)
#endif

namespace adpff{

class Parameters;

using namespace std;
using namespace ff;
using namespace mammut::cpufreq;
using namespace mammut::energy;
using namespace mammut::task;
using namespace mammut::topology;
using namespace mammut::utils;

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::setDomainToHighestFrequency(const Domain* domain){
    if(!domain->setGovernor(GOVERNOR_PERFORMANCE)){
        if(!domain->setGovernor(GOVERNOR_USERSPACE) ||
           !domain->setHighestFrequencyUserspace()){
            throw runtime_error("AdaptivityManagerFarm: Fatal error while "
                                "setting highest frequency for sensitive "
                                "emitter/collector. Try to run it without "
                                "sensitivity parameters.");
        }
    }
}

template <typename lb_t, typename gt_t>
double ManagerFarm<lb_t, gt_t>::getPrimaryValue(const MonitoredSample& sample) const{
    switch(_p.contractType){
        case CONTRACT_PERF_UTILIZATION:{
            return sample.utilization;
        }break;
        case CONTRACT_PERF_BANDWIDTH:
        case CONTRACT_PERF_COMPLETION_TIME:{
            return sample.bandwidth;
        }break;
        case CONTRACT_POWER_BUDGET:{
            return sample.watts;
        }break;
        default:{
            return 0;
        }break;
    }
}

template <typename lb_t, typename gt_t>
double ManagerFarm<lb_t, gt_t>::getSecondaryValue(const MonitoredSample& sample) const{
    switch(_p.contractType){
        case CONTRACT_PERF_UTILIZATION:
        case CONTRACT_PERF_BANDWIDTH:
        case CONTRACT_PERF_COMPLETION_TIME:{
            return sample.watts;
        }break;
        case CONTRACT_POWER_BUDGET:{
            return sample.bandwidth;
        }break;
        default:{
            return 0;
        }break;
    }
}

template <typename lb_t, typename gt_t>
double ManagerFarm<lb_t, gt_t>::getPrimaryValue() const{
    return getPrimaryValue(_samples->average());
}

template <typename lb_t, typename gt_t>
double ManagerFarm<lb_t, gt_t>::getSecondaryValue() const{
    return getSecondaryValue(_samples->average());
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::changeKnobs(){
    if(_p.contractType == CONTRACT_NONE || !_configuration.knobsChangeNeeded()){
        return;
    }

    KnobsValues values = _calibrator->getNextKnobsValues(getPrimaryValue(),
                                                         getSecondaryValue(),
                                                         _remainingTasks);
    if(!_configuration.equal(values)){
        _configuration.setValues(values);

        std::vector<AdaptiveNode*> newWorkers = dynamic_cast<const KnobWorkers*>(_configuration.getKnob(KNOB_TYPE_WORKERS))->getActiveWorkers();        
        WorkerSample ws;

        if(_activeWorkers.size() != newWorkers.size()){
            /** 
             * Since I stopped the workers after I asked for a sample, there
             * may still be tasks that have been processed but I did not count.
             * For this reason, I get them.
             * I do not need to ask since the node put it in the Q when it 
             * terminated.
             */
            DEBUG("Getting spurious..");
            getWorkersSamples(ws);
            DEBUG("Spurious got.");
        }

        _activeWorkers = newWorkers;

        /****************** Clean state ******************/
        _samples->reset();
        if(_counter){
            _counter->reset();
        }
        _totalTasks = 0;
        DEBUG("Resetting sample.");
        _lastStoredSampleMs = getMillisecondsTime();
        askForWorkersSamples();
        getWorkersSamples(ws);
        //resetSample();
    }
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::observe(){
    if(_p.observer){
        const KnobMapping* kMapping = dynamic_cast<const KnobMapping*>(_configuration.getKnob(KNOB_TYPE_MAPPING));
        MonitoredSample ms = _samples->average();
        _p.observer->observe(_lastStoredSampleMs,
                             _configuration.getRealValue(KNOB_TYPE_WORKERS),
                             _configuration.getRealValue(KNOB_TYPE_FREQUENCY),
                             kMapping->getEmitterVirtualCore(),
                             kMapping->getWorkersVirtualCore(),
                             kMapping->getCollectorVirtualCore(),
                             _samples->getLastSample().bandwidth,
                             ms.bandwidth,
                             _samples->coefficientVariation().bandwidth,
                             ms.latency,
                             ms.utilization,
                             _samples->getLastSample().watts,
                             ms.watts);
    }
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::askForWorkersSamples(){
    for(size_t i = 0; i < _activeWorkers.size(); i++){
        _activeWorkers.at(i)->askForSample();
    }
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::getWorkersSamples(WorkerSample& sample){
    AdaptiveNode* w;
    uint numActiveWorkers = _activeWorkers.size();
    sample = WorkerSample();

    for(size_t i = 0; i < numActiveWorkers; i++){
        WorkerSample tmp;
        w = _activeWorkers.at(i);
        w->getSampleResponse(tmp, _p.strategyPolling,
                             _samples->average().latency);
        sample += tmp;
    }
    sample.loadPercentage /= numActiveWorkers;
    sample.latency /= numActiveWorkers;
    updateTasksCount(sample);
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::resetSample(){
    for(size_t i = 0; i < _activeWorkers.size(); i++){
        _activeWorkers.at(i)->resetSample();
    }
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::updateTasksCount(WorkerSample& sample){
    _totalTasks += sample.tasksCount;
    if(_p.contractType == CONTRACT_PERF_COMPLETION_TIME){
        if(_remainingTasks > sample.tasksCount){
            _remainingTasks -= sample.tasksCount;
        }else{
            _remainingTasks = 0;
        }
    }
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::storeNewSample(){
    MonitoredSample sample;
    WorkerSample ws;
    Joules joules = 0.0;

    askForWorkersSamples();
    getWorkersSamples(ws);

    if(_counter){
        switch(_counter->getType()){
            case COUNTER_CPUS:{
                joules = ((CounterCpus*) _counter)->getJoulesCoresAll();
            }break;
            default:{
                joules = _counter->getJoules();
            }break;
        }
    }

    double now = getMillisecondsTime();
    double durationSecs = (now - _lastStoredSampleMs) / 1000.0;
    _lastStoredSampleMs = now;

    sample.watts = joules / durationSecs;
    sample.utilization = ws.loadPercentage;
    // ATTENTION: Bandwidth is not the number of task since the
    //            last observation but the number of expected
    //            tasks that will be processed in 1 second.
    //            For this reason, if we sum all the bandwidths in
    //            the result observation file, we may have an higher
    //            number than the number of tasks.
    sample.bandwidth = ws.bandwidthTotal;
    sample.latency = ws.latency;

    if(_counter){
        _counter->reset();
    }
    _samples->add(sample);

    DEBUGB(samplesFile << *_samples << "\n");
}

template <typename lb_t, typename gt_t>
bool ManagerFarm<lb_t, gt_t>::persist() const{
    bool r = false;
    switch(_p.strategyPersistence){
        case STRATEGY_PERSISTENCE_SAMPLES:{
            r = _samples->size() < _p.persistenceValue;
        }break;
        case STRATEGY_PERSISTENCE_TASKS:{
            r = _totalTasks < _p.persistenceValue;
        }break;
        case STRATEGY_PERSISTENCE_VARIATION:{
            const MonitoredSample& variation = _samples->coefficientVariation();
            r = getPrimaryValue(variation) < _p.persistenceValue &&
                getSecondaryValue(variation) < _p.persistenceValue;
        }break;
    }
    return r;
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::initPredictors(){
    if(_p.contractType == CONTRACT_NONE){
        _calibrator = new CalibratorDummy(_p, _configuration, _samples);
    }else{
        switch(_p.strategyPrediction){
            case STRATEGY_PREDICTION_SIMPLE:{
                _calibrator = new CalibratorDummy(_p, _configuration, _samples);
            }break;
            case STRATEGY_PREDICTION_REGRESSION_LINEAR:{
                switch(_p.strategyCalibration){
                    case STRATEGY_CALIBRATION_RANDOM:{
                        _calibrator = new CalibratorRandom(_p, _configuration, _samples);
                    }
                    case STRATEGY_CALIBRATION_HALTON:
                    case STRATEGY_CALIBRATION_HALTON_REVERSE:
                    case STRATEGY_CALIBRATION_NIEDERREITER:
                    case STRATEGY_CALIBRATION_SOBOL:{
                        _calibrator = new CalibratorLowDiscrepancy(_p, _configuration, _samples);
                    }break;
                }
            }break;
            case STRATEGY_PREDICTION_LIMARTINEZ:{
                _calibrator = new CalibratorLiMartinez(_p, _configuration, _samples);
            }break;
    }
    }
}

static Parameters& validate(Parameters& p){
    ParametersValidation apv = p.validate();
    if(apv != VALIDATION_OK){
        throw runtime_error("Invalid adaptivity parameters: " + std::to_string(apv));
    }
    return p;
}

static std::vector<AdaptiveNode*> convertWorkers(svector<ff_node*> w){
    std::vector<AdaptiveNode*> r;
    for(size_t i = 0; i < w.size(); i++){
        r.push_back(dynamic_cast<AdaptiveNode*>(w[i]));
    }
    return r;
}

template <typename lb_t, typename gt_t>
ManagerFarm<lb_t, gt_t>::ManagerFarm(ff_farm<lb_t, gt_t>* farm, Parameters parameters):
        _farm(farm),
        _terminated(false),
        _p(validate(parameters)),
        _startTimeMs(0),
        _cpufreq(_p.mammut.getInstanceCpuFreq()),
        _counter(_p.mammut.getInstanceEnergy()->getCounter()),
        _task(_p.mammut.getInstanceTask()),
        _topology(_p.mammut.getInstanceTopology()),
        _emitter(dynamic_cast<AdaptiveNode*>(_farm->getEmitter())),
        _collector(dynamic_cast<AdaptiveNode*>(_farm->getCollector())),
        _activeWorkers(convertWorkers(_farm->getWorkers())),
        _samples(initSamples()),
        _configuration(_p, _emitter, _collector, _farm->getgt(), _activeWorkers,
                       _samples, &_terminated),
        _totalTasks(0),
        _remainingTasks(0),
        _deadline(0),
        _lastStoredSampleMs(0),
        _calibrator(NULL){
    DEBUGB(samplesFile.open("samples.csv"));
}

template <typename lb_t, typename gt_t>
ManagerFarm<lb_t, gt_t>::~ManagerFarm(){
    delete _samples;
    if(_calibrator){
        delete _calibrator;
    }
    DEBUGB(samplesFile.close());
}

template <typename lb_t, typename gt_t>
Smoother<MonitoredSample>* ManagerFarm<lb_t, gt_t>::initSamples() const{
    switch(_p.strategySmoothing){
        case STRATEGY_SMOOTHING_MOVING_AVERAGE:{
            return new MovingAverageSimple<MonitoredSample>(_p.smoothingFactor);
        }break;
        case STRATEGY_SMOOTHING_EXPONENTIAL:{
            return new MovingAverageExponential<MonitoredSample>(_p.smoothingFactor);
        }break;
        default:{
            return NULL;
        }
    }
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::initNodesPreRun() {
    for (size_t i = 0; i < _activeWorkers.size(); i++) {
        _activeWorkers.at(i)->initPreRun(_p.mammut, _p.archData.ticksPerNs,
                                         NODE_TYPE_WORKER, &_terminated);
    }
    if (_emitter) {
        _emitter->initPreRun(_p.mammut, _p.archData.ticksPerNs,
                             NODE_TYPE_EMITTER, &_terminated,
                             _farm->getlb());
    } else {
        throw runtime_error("Emitter is needed to use the manager.");
    }
    if (_collector) {
        _collector->initPreRun(_p.mammut, _p.archData.ticksPerNs,
                               NODE_TYPE_COLLECTOR, &_terminated,
                               _farm->getgt());
    }
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::initNodesPostRun() {
    for (size_t i = 0; i < _activeWorkers.size(); i++) {
        _activeWorkers.at(i)->initPostRun();
    }
    DEBUG("initNodesPostRun: Workers done.");
    _emitter->initPostRun();
    DEBUG("initNodesPostRun: Emitter done.");
    if (_collector) {
        _collector->initPostRun();
    }
    DEBUG("initNodesPostRun: Collector done.");
}

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::cleanNodes() {
    for (size_t i = 0; i < _activeWorkers.size(); i++) {
        _activeWorkers.at(i)->clean();
    }
    if (_emitter) {
        _emitter->clean();
    }
    if (_collector) {
        _collector->clean();
    }
}

#define PAR_BEGIN_ENV "__PAR_BEGIN"

template <typename lb_t, typename gt_t>
void ManagerFarm<lb_t, gt_t>::run(){
    if(_p.qSize){
        _farm->setFixedSize(true);
        // We need to multiply for the number of workers since FastFlow
        // will divide the size for the number of workers.
        _farm->setInputQueueLength(_p.qSize * _activeWorkers.size());
        _farm->setOutputQueueLength(_p.qSize * _activeWorkers.size());
    }

    DEBUG("Init pre run");
    initNodesPreRun();

    DEBUG("Going to run");
    _farm->run_then_freeze();

    DEBUG("Init post run");
    initNodesPostRun();

    DEBUG("Farm started.");

    _configuration.maxAllKnobs();
    DEBUG("All knobs maxed.");

    /** Creates the parallel section begin file. **/
    char* default_in_roi = (char*) malloc(sizeof(char)*256);
    default_in_roi[0]='\0';
    default_in_roi = strcat(default_in_roi, getenv("HOME"));
    default_in_roi = strcat(default_in_roi, "/roi_in");
    setenv(PAR_BEGIN_ENV, default_in_roi, 0);
    free(default_in_roi);
    FILE* in_roi = fopen(getenv(PAR_BEGIN_ENV), "w");
    fclose(in_roi);

    _startTimeMs = getMillisecondsTime();
    if(_counter){
        _counter->reset();
    }
    _lastStoredSampleMs = _startTimeMs;
    if(_p.observer){
        _p.observer->_startMonitoringMs = _lastStoredSampleMs;
    }

    if(_p.contractType == CONTRACT_PERF_COMPLETION_TIME){
        _remainingTasks = _p.expectedTasksNumber;
        _deadline = getMillisecondsTime()/1000.0 + _p.requiredCompletionTime;
    }

    initPredictors();

    /* Force the first calibration point. **/
    assert(_calibrator);
    changeKnobs();

    ThreadHandler* thisThread = _task->getProcessHandler(getpid())->getThreadHandler(gettid());
    thisThread->move((VirtualCoreId) 0);

    double microsecsSleep = 0;
    double startSample = getMillisecondsTime();
    double overheadMs = 0;

    uint samplingInterval;

    while(!_terminated){
        overheadMs = getMillisecondsTime() - startSample;
        if(_calibrator->isCalibrating()){
            samplingInterval = _p.samplingIntervalCalibration;
        }else{
            samplingInterval = _p.samplingIntervalSteady;
        }
        microsecsSleep = ((double)samplingInterval - overheadMs)*
                          (double)MAMMUT_MICROSECS_IN_MILLISEC;
        if(microsecsSleep < 0){
            microsecsSleep = 0;
        }else{
            usleep(microsecsSleep);
        }

        startSample = getMillisecondsTime();
        DEBUG("Storing new sample.");
        storeNewSample();
        DEBUG("New sample stored.");

        if(_p.contractType == CONTRACT_PERF_COMPLETION_TIME){
            double now = getMillisecondsTime(); 
            if(now/1000.0 >= _deadline){
                _p.requiredBandwidth = numeric_limits<double>::max();
            }else{
                _p.requiredBandwidth = _remainingTasks / ((_deadline*1000.0 - now) / 1000.0);
            }
        }

        observe();

        if(!persist()){
            assert(_calibrator);
            DEBUG("Changing knobs.");
            changeKnobs();
            _configuration.trigger();
            startSample = getMillisecondsTime();
        }
    }
    DEBUG("Terminating...wait freezing.");
    _farm->wait_freezing();
    _farm->wait();
    DEBUG("Terminated.");

    double duration = _farm->ffTime();

    unlink(getenv(PAR_BEGIN_ENV));
    if(_p.observer){
        vector<CalibrationStats> cs;
        if(_calibrator){
            cs = _calibrator->getCalibrationsStats();
            _p.observer->calibrationStats(cs, duration);
        }
        _p.observer->summaryStats(cs, duration);
    }

    cleanNodes();
}

}
