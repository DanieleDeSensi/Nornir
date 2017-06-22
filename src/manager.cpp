/*
 * manager.cpp
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

#include "manager.hpp"

#include "./ffincs.hpp"
#include "./parameters.hpp"
#include "./predictors.hpp"
#include "./node.hpp"
#include "./utils.hpp"

#include "external/mammut/mammut/module.hpp"
#include "external/mammut/mammut/utils.hpp"
#include "external/mammut/mammut/mammut.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <stdlib.h>
#include <sys/wait.h>

#undef DEBUG
#undef DEBUGB

#ifdef DEBUG_MANAGER
#define DEBUG(x) do { cerr << "[Manager] " << x << endl; } while (0)
#define DEBUGB(x) do {x;} while(0)
#else
#define DEBUG(x)
#define DEBUGB(x)
#endif

namespace nornir{

class Parameters;

using namespace std;
using namespace ff;
using namespace mammut::cpufreq;
using namespace mammut::energy;
using namespace mammut::task;
using namespace mammut::topology;
using namespace mammut::utils;

static Parameters& validate(Parameters& p){
    ParametersValidation apv = p.validate();
    if(apv != VALIDATION_OK){
        throw runtime_error("Invalid adaptivity parameters: " + std::to_string(apv));
    }
    return p;
}

Manager::Manager(Parameters nornirParameters):
        _terminated(false),
        _p(validate(nornirParameters)),
        _counter(NULL),
        _task(NULL),
        _topology(NULL),
        _samples(initSamples()),
        _variations(new MovingAverageExponential<double>(0.5)),
        _totalTasks(0),
        _remainingTasks(0),
        _deadline(0),
        _lastStoredSampleMs(0),
        _inhibited(false),
        _configuration(NULL),
        _selector(NULL),
        _pid(0),
        _toSimulate(false)
{
    DEBUGB(samplesFile.open("samples.csv"));
}

Manager::~Manager(){
    DEBUGB(samplesFile.close());
}

void Manager::run(){
    if(!_toSimulate){
        _p.mammut.getInstanceCpuFreq()->removeTurboFrequencies();
        _counter = _p.mammut.getInstanceEnergy()->getCounter();
        _task = _p.mammut.getInstanceTask();
        _topology = _p.mammut.getInstanceTopology();
    }

    if(isPrimaryRequirement(_p.requirements.executionTime)){
        _remainingTasks = _p.requirements.expectedTasksNumber;
        _deadline = getMillisecondsTime()/1000.0 + _p.requirements.executionTime;
    }

    waitForStart();

    /** Wait for the disinhibition from global manager. **/
    while(_inhibited){;}

    // Reset joules counter
    getAndResetJoules();
    //TODO RESET BANDWIDTHIN
    _lastStoredSampleMs = getMillisecondsTime();
    if(_p.observer){
        _p.observer->_startMonitoringMs = _lastStoredSampleMs;
    }

    /* Force the first calibration point. **/
    KnobsValues kv = decide();
    act(kv);

    if(!_toSimulate){
        ThreadHandler* thisThread = _task->getProcessHandler(getpid())->getThreadHandler(gettid());
        thisThread->move(NORNIR_MANAGER_VIRTUAL_CORE);
    }

    double microsecsSleep = 0;
    double startSample = getMillisecondsTime();
    double overheadMs = 0;

    uint samplingInterval;
    uint steadySamples = 0;

    while(!_terminated){
        overheadMs = getMillisecondsTime() - startSample;
        if(_selector->isCalibrating()){
            samplingInterval = _p.samplingIntervalCalibration;
            steadySamples = 0;
        }else if(steadySamples < _p.steadyThreshold){
            samplingInterval = _p.samplingIntervalCalibration;
            steadySamples++;
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
        if(!_inhibited){
            observe();
            updateRequiredBandwidth();
            logObservation();

            if(!persist()){
                DEBUG("Asking selector.");
                KnobsValues kv = decide();
                act(kv);
                startSample = getMillisecondsTime();
            }
        }else{
            // If inhibited, we need to discard the previous samples
            // (they were taken with less/more applications running).
            _samples->reset();
        }
    }

    terminationManagement();
    ulong duration = getExecutionTime();

    if(_p.observer){
        vector<CalibrationStats> cs;
        if(_selector){
            _selector->updateTotalTasks(_totalTasks);
            _selector->stopCalibration();
            cs = _selector->getCalibrationsStats();
            _p.observer->calibrationStats(cs, duration, _totalTasks);
        }
        ReconfigurationStats rs = _configuration->getReconfigurationStats();
        _p.observer->summaryStats(cs, rs, duration, _totalTasks);
    }
}

void Manager::terminate(){
    _terminated = true;
}

void Manager::inhibit(){
    _inhibited = true;
}

void Manager::disinhibit(){
    _inhibited = false;
}

void Manager::shrink(CalibrationShrink type){
    switch(type){
        case CALIBRATION_SHRINK_AGGREGATE:{
            if(_pid){
                _task->getProcessHandler(_pid)->move(_topology->getVirtualCores().back());
            }
        }break;
        case CALIBRATION_SHRINK_PAUSE:{
            shrinkPause();
        }break;
        default:{
            ;
        }break;
    }
}

void Manager::stretch(CalibrationShrink type){
    switch(type){
        case CALIBRATION_SHRINK_AGGREGATE:{
            if(_pid){
                std::vector<VirtualCore*> allowedCores = ((KnobMapping*) _configuration->getKnob(KNOB_MAPPING))->getAllowedCores();
                _task->getProcessHandler(_pid)->move(allowedCores);
            }
        }break;
        case CALIBRATION_SHRINK_PAUSE:{
            stretchPause();
        }break;
        default:{
            ;
        }break;
    }
}

void Manager::updateModelsInterference(){
    if(_p.strategySelection != STRATEGY_SELECTION_LEARNING){
        throw std::runtime_error("updateModelsInterference can only be "
                "used on LEARNING selectors.");
    }
    // Temporarely disinhibit.
    _samples->reset();
    disinhibit();
    ((SelectorLearner*) _selector)->updateModelsInterference();
}

void Manager::waitModelsInterferenceUpdate(){
    if(_p.strategySelection != STRATEGY_SELECTION_LEARNING){
        throw std::runtime_error("waitModelsInterferenceUpdate can only be "
                        "used on LEARNING selectors.");
    }
    while(!((SelectorLearner*) _selector)->areModelsUpdated() && !_terminated){
        // If in the meanwhile the selector is waiting for calibration,
        // allow him to calibrate.
        if(_selector->isCalibrating()){
           _selector->allowCalibration();
        }
    }
    // Inhibit again.
    inhibit();
}

std::vector<VirtualCoreId> Manager::getUsedCores(){
    while(_selector->isCalibrating() || _selector->getTotalCalibrationTime() == 0){;}
    vector<VirtualCore*> vc = ((KnobMapping*) _configuration->getKnob(KNOB_MAPPING))->getActiveVirtualCores();
    vector<VirtualCoreId> vcid;
    for(VirtualCore* v : vc){
        vcid.push_back(v->getVirtualCoreId());
    }
    return vcid;
}

void Manager::allowCores(std::vector<mammut::topology::VirtualCoreId> ids){
    vector<VirtualCore*> allowedVc;
    for(auto id : ids){
        allowedVc.push_back(_topology->getVirtualCore(id));
    }
    ((KnobVirtualCores*) _configuration->getKnob(KNOB_VIRTUAL_CORES))->changeMax(ids.size() - _configuration->getNumServiceNodes());
    ((KnobMapping*) _configuration->getKnob(KNOB_MAPPING))->setAllowedCores(allowedVc);
}

void Manager::setSimulationParameters(std::string samplesFileName){
    _toSimulate = true;
    std::ifstream file(samplesFileName);
    MonitoredSample sample;

    while(file >> sample){
        _simulationSamples.push_back(sample);
    }
}

void Manager::postConfigurationManagement(){;}

void Manager::terminationManagement(){;}

void Manager::updateRequiredBandwidth() {
    if(isPrimaryRequirement(_p.requirements.executionTime)){
        double now = getMillisecondsTime();
        if(now / 1000.0 >= _deadline){
            _p.requirements.bandwidth = numeric_limits<double>::max();
        }else{
            _p.requirements.bandwidth = _remainingTasks / ((_deadline * 1000.0 - now) / 1000.0);
        }
    }
}

void Manager::setDomainToHighestFrequency(const Domain* domain){
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

bool Manager::persist() const{
    bool r = false;
    switch(_p.strategyPersistence){
        case STRATEGY_PERSISTENCE_SAMPLES:{
            r = _samples->size() < _p.persistenceValue;
        }break;
        case STRATEGY_PERSISTENCE_VARIATION:{
            const MonitoredSample& variation = _samples->coefficientVariation();
            r = _samples->size() < 1 ||
                variation.bandwidth > _p.persistenceValue ||
                variation.latency > _p.persistenceValue ||
                variation.watts > _p.persistenceValue;
        }break;
    }
    return r;
}

void Manager::lockKnobs() const{
    if(!_p.knobCoresEnabled){
        _configuration->getKnob(KNOB_VIRTUAL_CORES)->lockToMax();
    }
    if(!_p.knobMappingEnabled){
        _configuration->getKnob(KNOB_MAPPING)->lock(MAPPING_TYPE_LINEAR);
    }
    if(!_p.knobFrequencyEnabled){
        _configuration->getKnob(KNOB_FREQUENCY)->lockToMax();
    }
    if(!_p.knobHyperthreadingEnabled){
        _configuration->getKnob(KNOB_HYPERTHREADING)->lockToMin();
    }
}

Selector* Manager::createSelector() const{
    if(!_p.requirements.anySpecified()){
        return new SelectorFixed(_p, *_configuration, _samples);
    }else{
        switch(_p.strategySelection){
            case STRATEGY_SELECTION_ANALYTICAL:{
                return new SelectorAnalytical(_p, *_configuration, _samples);
            }break;
            case STRATEGY_SELECTION_LEARNING:{
                return new SelectorLearner(_p, *_configuration, _samples);
            }break;
            case STRATEGY_SELECTION_LIMARTINEZ:{
                return new SelectorLiMartinez(_p, *_configuration, _samples);
            }break;
            case STRATEGY_SELECTION_MISHRA:{
                return new SelectorMishra(_p, *_configuration, _samples);
            }break;
            case STRATEGY_SELECTION_FULLSEARCH:{
                return new SelectorFullSearch(_p, *_configuration, _samples);
            }break;
            default:{
                throw std::runtime_error("Selector not yet implemented.");
            }break;
        }
    }
    return NULL;
}

Smoother<MonitoredSample>* Manager::initSamples() const{
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

void Manager::updateTasksCount(MonitoredSample &sample){
    _totalTasks += sample.numTasks;
    if(isPrimaryRequirement(_p.requirements.executionTime)){
        if(_remainingTasks > sample.numTasks){
            _remainingTasks -= sample.numTasks;
        }else{
            _remainingTasks = 0;
        }
    }
}

void Manager::observe(){
    Joules joules = 0.0;
    MonitoredSample sample;
    if(_toSimulate){
        if(_simulationSamples.empty()){
            _terminated = true;
            return;
        }else{
            sample = _simulationSamples.front();
            _simulationSamples.pop_front();
        }
    }else{
        sample = getSample();
        double now = getMillisecondsTime();
        joules = getAndResetJoules();
        if(_p.observer){
            _p.observer->addJoules(joules);
        }

        double durationSecs = (now - _lastStoredSampleMs) / 1000.0;
        _lastStoredSampleMs = now;

        // Add watts to the sample
        sample.watts = joules / durationSecs;

        if(_p.synchronousWorkers){
            // When we have synchronous workers we need to divide
            // for the number of workers since we do it for the totalTasks
            // count. When this flag is set we count iterations, not real
            // tasks.
            sample.bandwidth /= _configuration->getKnob(KNOB_VIRTUAL_CORES)->getRealValue();
            // When we have synchronous workers we need to count the iterations,
            // not the real tasks (indeed in this case each worker will receive
            // the same amount of tasks, e.g. in canneal) since they are sent in
            // broadcast.
            sample.numTasks /= _configuration->getKnob(KNOB_VIRTUAL_CORES)->getRealValue();
        }
    }

    updateTasksCount(sample);
    _samples->add(sample);
    _variations->add(_samples->coefficientVariation().bandwidth);

    DEBUGB(samplesFile << sample << "\n");
}

KnobsValues Manager::decide(){
    if(!_configuration->knobsChangeNeeded()){
        return _configuration->getRealValues();
    }

    if(_totalTasks){
        // We need to update the input bandwidth only if we already processed
        // some tasks.  By doing this check, we avoid updating bandwidth for
        // the first forced reconfiguration.
        _selector->updateBandwidthIn();
        _selector->updateTotalTasks(_totalTasks);
    }
    return _selector->getNextKnobsValues();
}

void Manager::act(KnobsValues kv, bool force){
    if(force || !_configuration->equal(kv)){
        _configuration->setValues(kv);
        postConfigurationManagement();

        /****************** Clean state ******************/
        _samples->reset();
        _variations->reset();
        DEBUG("Resetting sample.");
        // Don't store this sample since it may be inbetween 2
        // different configurations.
        if(_p.cooldownPeriod){
            usleep(_p.cooldownPeriod * 1000);
        }

        if(!_toSimulate){
            MonitoredSample sample = getSample();
            updateTasksCount(sample);
            _lastStoredSampleMs = getMillisecondsTime();
            Joules joules = getAndResetJoules();
            if(_p.observer){
                _p.observer->addJoules(joules);
            }
        }
        _configuration->trigger();
    }
}

Joules Manager::getAndResetJoules(){
    Joules joules = 0.0;
    if(!_toSimulate){
        if(_counter){
            joules = _counter->getJoules();
            _counter->reset();
        }
    }
    return joules;
}

void Manager::logObservation(){
    if(_p.observer){
        const KnobMapping* kMapping = dynamic_cast<const KnobMapping*>(_configuration->getKnob(KNOB_MAPPING));
        MonitoredSample ms = _samples->average();
        _p.observer->observe(_lastStoredSampleMs,
                             _configuration->getRealValue(KNOB_VIRTUAL_CORES),
                             _configuration->getRealValue(KNOB_FREQUENCY),
                             kMapping->getActiveVirtualCores(),
                             _samples->getLastSample().bandwidth,
                             ms.bandwidth,
                             _samples->coefficientVariation().bandwidth,
                             ms.latency,
                             ms.loadPercentage,
                             _samples->getLastSample().watts,
                             ms.watts);
    }
}

ManagerInstrumented::ManagerInstrumented(const std::string& knarrChannel,
                                 Parameters nornirParameters):
        Manager(nornirParameters), _monitor(knarrChannel){
    Manager::_configuration = new ConfigurationExternal(_p);
    lockKnobs();
    _configuration->createAllRealCombinations();
    _selector = createSelector();
    // For instrumented application we do not care if synchronous of not
    // (we count iterations).
    _p.synchronousWorkers = false;
}

ManagerInstrumented::ManagerInstrumented(nn::socket& knarrSocket,
                                 int chid,
                                 Parameters nornirParameters):
            Manager(nornirParameters), _monitor(knarrSocket, chid){
    Manager::_configuration = new ConfigurationExternal(_p);
    lockKnobs();
    _configuration->createAllRealCombinations();
    _selector = createSelector();
    // For instrumented application we do not care if synchronous of not (we
    // count iterations).
    _p.synchronousWorkers = false;
}

ManagerInstrumented::~ManagerInstrumented(){
    if(Manager::_configuration){
        delete Manager::_configuration;
    }
    if(_selector){
        delete _selector;
    }
}

void ManagerInstrumented::waitForStart(){
    Manager::_pid = _monitor.waitStart();
    ((KnobMappingExternal*)_configuration->getKnob(KNOB_MAPPING))->setPid(_pid);
}

MonitoredSample ManagerInstrumented::getSample(){
    MonitoredSample sample;
    if(!_monitor.getSample(sample)){
        _terminated = true;
    }
    return sample;
}

ulong ManagerInstrumented::getExecutionTime(){
    return _monitor.getExecutionTime();
}

void ManagerInstrumented::shrinkPause(){
    kill(_pid, SIGSTOP);
}

void ManagerInstrumented::stretchPause(){
    kill(_pid, SIGCONT);
}

ManagerBlackBox::ManagerBlackBox(pid_t pid, Parameters nornirParameters):
        Manager(nornirParameters), _process(nornirParameters.mammut.getInstanceTask()->getProcessHandler(pid)){
    Manager::_pid = pid;
    Manager::_configuration = new ConfigurationExternal(_p);
    lockKnobs();
    _configuration->createAllRealCombinations();
    _selector = createSelector();
    // For blackbox application we do not care if synchronous of not
    // (we count instructions).
    _p.synchronousWorkers = false;
    // Check supported requirements.
    if(isPrimaryRequirement(_p.requirements.bandwidth) ||
       _p.requirements.maxUtilization != NORNIR_REQUIREMENT_UNDEF ||
       isPrimaryRequirement(_p.requirements.executionTime) ||
       _p.requirements.latency != NORNIR_REQUIREMENT_UNDEF){
        throw std::runtime_error("ManagerBlackBox. Unsupported requirement.");
    }
    _startTime = 0;
}

ManagerBlackBox::~ManagerBlackBox(){
    if(Manager::_configuration){
        delete Manager::_configuration;
    }
    if(_selector){
        delete _selector;
    }
}

pid_t ManagerBlackBox::getPid() const{
    return _pid;
}

void ManagerBlackBox::waitForStart(){
    // We know for sure that when the Manager is created the process
    // already started.
    // However if we have been required to monitor only the ROI, we
    // wait for ROI start by waiting for the roiFile creation.
    if(_p.roiFile.compare("")){
        while(!mammut::utils::existsFile(_p.roiFile)){
            usleep(1000);
        }
    }
    _startTime = getMillisecondsTime();
    ((KnobMappingExternal*)_configuration->getKnob(KNOB_MAPPING))->setProcessHandler(_process);
    _process->resetInstructions(); // To remove those executed before entering ROI
}

static bool isRunning(pid_t pid) {
    return !kill(pid, 0);
}

MonitoredSample ManagerBlackBox::getSample(){
    MonitoredSample sample;
    double instructions = 0;
    if(!isRunning(_process->getId()) ||
       (_p.roiFile.compare("") && !mammut::utils::existsFile(_p.roiFile))){
        _terminated = true;
        return sample;
    }
    _process->getAndResetInstructions(instructions);
    sample.bandwidth = instructions / ((getMillisecondsTime() - _lastStoredSampleMs) / 1000.0);
    sample.latency = -1; // Not used.
    sample.loadPercentage = 100.0; // We do not know what's the input bandwidth.
    sample.numTasks = instructions; // We consider a task to be an instruction.
    return sample;
}

ulong ManagerBlackBox::getExecutionTime(){
    return getMillisecondsTime() - _startTime;
}

void ManagerBlackBox::shrinkPause(){
    pid_t pid = _process->getId();
    kill(pid, SIGSTOP);
}

void ManagerBlackBox::stretchPause(){
    pid_t pid = _process->getId();
    kill(pid, SIGCONT);
}

}
