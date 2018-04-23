/*
 * selectors.cpp
 *
 * Created on: 10/07/2015
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

#include "selectors.hpp"
#include "utils.hpp"
#include "external/riff/src/external/cppnanomsg/nn.hpp"
#include "external/riff/src/external/nanomsg/src/pair.h"
#include <cfloat>
#include <string>
#include <iostream>
#include <sstream>

#undef DEBUG
#undef DEBUGB

#ifdef DEBUG_SELECTORS
#define DEBUG(x) do { std::cerr << "[Selectors] " << x << std::endl; } while (0)
#define DEBUGB(x) do {x;} while (0)
#else
#define DEBUG(x)
#define DEBUGB(x)
#endif

#define NOT_VALID DBL_MIN

#define STORE_PREDICTIONS 1

using namespace mammut::cpufreq;
using namespace mammut::energy;
using namespace mammut::utils;
using namespace mammut::topology;
using namespace std;

namespace nornir{

Selector::Selector(const Parameters& p,
                   const Configuration& configuration,
                   const Smoother<MonitoredSample>* samples):
        _calibrationStartMs(0),
        _calibrationStartTasks(0),
        _totalCalibrationTime(0),
        _calibrating(false),
        _ignoreViolations(false),
        _p(p),
        _configuration(configuration),
        _samples(samples),
        _numCalibrationPoints(0),
        _forced(false),
        _forcedReturned(false),
        _calibrationCoordination(false),
        _calibrationAllowed(false),
        _totalTasks(0){
    _joulesCounter = _localMammut.getInstanceEnergy()->getCounter();
    //TODO Fare meglio con mammut
    //TODO Assicurarsi che il numero totale di configurazioni possibili sia maggiore del numero minimo di punti
#if 0
    // Input bandwidth smoother
    if(p.strategySmoothing == STRATEGY_SMOOTHING_EXPONENTIAL){
        _bandwidthIn = new MovingAverageExponential<double>(p.smoothingFactor);
    }else{
        _bandwidthIn = new MovingAverageSimple<double>(p.smoothingFactor);
    }
#endif
    _bandwidthIn = new MovingAverageSimple<double>(3);
}


bool Selector::isFeasibleThroughput(double value, bool conservative) const{
    if(isPrimaryRequirement(_p.requirements.throughput)){
        double conservativeOffset = 0;
        if(conservative && _p.conservativeValue){
            conservativeOffset = _p.requirements.throughput *
                                        (_p.conservativeValue / 100.0);
        }
        if(value < _p.requirements.throughput + conservativeOffset){
            return false;
        }
    }
    return true;
}

bool Selector::isFeasibleLatency(double value, bool conservative) const{
    return true;
}

bool Selector::isFeasibleUtilization(double value, bool conservative) const{
    if(isPrimaryRequirement(_p.requirements.minUtilization)){
        double conservativeOffset = 0;
        if(conservative && _p.conservativeValue){
            conservativeOffset = (_p.requirements.maxUtilization - _p.requirements.minUtilization) *
                                 (_p.conservativeValue / 100.0) / 2.0;
        }
        if(value < _p.requirements.minUtilization + conservativeOffset ||
           value > _p.requirements.maxUtilization - conservativeOffset){
            return false;
        }
    }
    return true;
}


bool Selector::isFeasiblePower(double value, bool conservative) const{
    if(isPrimaryRequirement(_p.requirements.powerConsumption)){
        double conservativeOffset = 0;
        if(conservative && _p.conservativeValue){
            conservativeOffset = _p.requirements.powerConsumption *
                                 (_p.conservativeValue / 100.0);
        }
        return value < _p.requirements.powerConsumption - conservativeOffset;
    }
    return true;
}

bool Selector::isContractViolated() const{
    if(_ignoreViolations){return false;}
    MonitoredSample avg = _samples->average();
    return !isFeasibleThroughput(avg.throughput, false) ||
           !isFeasibleLatency(avg.latency, false)     ||
           !isFeasibleUtilization(avg.loadPercentage, false) ||
           !isFeasiblePower(avg.watts, false);
}

void Selector::startCalibration(){
    if(!_calibrating){
        DEBUG("Starting calibration.");
        _calibrating = true;
        if(_calibrationCoordination){
            while(!_calibrationAllowed){;}
            _calibrationAllowed = false;
        }
        _numCalibrationPoints = 0;
        _calibrationStartMs = getMillisecondsTime();
        _calibrationStartTasks = _totalTasks;
        if(_joulesCounter){
            _joulesCounter->reset();
        }
    }
}

void Selector::stopCalibration(){
    DEBUG("Stopping calibration.");
    _calibrating = false;
    if(_numCalibrationPoints){
        CalibrationStats cs;
        cs.numSteps = _numCalibrationPoints;
        cs.duration = (getMillisecondsTime() - _calibrationStartMs);
        _totalCalibrationTime += cs.duration;
        cs.numTasks = _totalTasks - _calibrationStartTasks;
        if(_joulesCounter){
            cs.joules = _joulesCounter->getJoules();
        }
        _calibrationStats.push_back(cs);
        _numCalibrationPoints = 0;
    }
}

double Selector::getTotalCalibrationTime() const{
    return _totalCalibrationTime;
}

void Selector::resetTotalCalibrationTime(){
    _totalCalibrationTime = 0;
}

void Selector::setCalibrationCoordination(){
    _calibrationCoordination = true;
}

void Selector::allowCalibration(){
    _calibrationAllowed = true;
}

void Selector::ignoreViolations(){
    _ignoreViolations = true;
}

void Selector::acceptViolations(){
    _ignoreViolations = false;
}

std::vector<CalibrationStats> Selector::getCalibrationsStats() const{
    return _calibrationStats;
}

bool Selector::isCalibrating() const{
    return _calibrating;
}

void Selector::forceConfiguration(KnobsValues& kv){
    _forced = true;
    _forcedReturned = false;
    _forcedConfiguration = kv;
}

void Selector::updateTotalTasks(u_int64_t totalTasks){
    _totalTasks = totalTasks;
}

void Selector::updateBandwidthIn(){
    if(_samples->average().loadPercentage < MAX_RHO &&
       !_samples->average().inconsistent){
        if(_bandwidthIn->average() == numeric_limits<double>::max()){
            _bandwidthIn->reset();
        }
        _bandwidthIn->add(_samples->getLastSample().throughput);
    }else if(!_bandwidthIn->size()){
        _bandwidthIn->add(numeric_limits<double>::max());
    }
}

double Selector::initBestValue() const{
    if(_p.requirements.throughput == NORNIR_REQUIREMENT_MAX){
        return NORNIR_REQUIREMENT_MIN;
    }else{
        return NORNIR_REQUIREMENT_MAX;
    }
}

double Selector::initBestSuboptimalValue() const{
    if(isPrimaryRequirement(_p.requirements.throughput)){
        return NORNIR_REQUIREMENT_MIN;
    }

    if(isPrimaryRequirement(_p.requirements.latency)){
        return NORNIR_REQUIREMENT_MAX;
    }

    if(isPrimaryRequirement(_p.requirements.maxUtilization)){
        return NORNIR_REQUIREMENT_MAX;
    }

    if(isPrimaryRequirement(_p.requirements.powerConsumption)){
        return NORNIR_REQUIREMENT_MAX;
    }

    /**
     * If no primary requirement (i.e. we only have a MIN/MAX
     * requirement) we can return anything, since we will never
     * check the suboptimal values (all values are always feasible).
     **/
    return 0;
}


SelectorManualCli::SelectorManualCli(const Parameters& p,
               const Configuration& configuration,
               const Smoother<MonitoredSample>* samples):
                 Selector(p, configuration, samples){
    ;
}

SelectorManualCli::~SelectorManualCli(){;}

KnobsValues SelectorManualCli::getNextKnobsValues(){
    std::ifstream instream(getSelectorManualCliControlFile());
    if(!instream.is_open()){
        // If the file where the configuration should be specified
        // has not yet been created, remain in the same configuration.
        return _configuration.getRealValues();
    }else{
        KnobsValues kv(KNOB_VALUE_RELATIVE);
        instream >> kv;
        instream.close();
        return kv;
    }
}

SelectorManualWeb::SelectorManualWeb(const Parameters& p,
               const Configuration& configuration,
               const Smoother<MonitoredSample>* samples):
                 Selector(p, configuration, samples), _connected(false){
    _socket = nn_socket (AF_SP, NN_PAIR);
    assert(_socket >= 0);
}

SelectorManualWeb::~SelectorManualWeb(){;}

KnobsValues SelectorManualWeb::getNextKnobsValues(){
    if(!_connected){
        if(nn_connect(_socket, "ws://0.0.0.0:3001") >= 0){
            _connected = true;
        }
    }

    if(_connected){
        char *buf = NULL;
        KnobsValues kv(KNOB_VALUE_RELATIVE);
        bool stop = false;
        do{
            // We need to execute in a loop since the interface
            // may have sent multiple messages and we are interested
            // only in the last one (the most recent one).
            if(nn_recv(_socket, &buf, NN_MSG, NN_DONTWAIT) < 0){
                if(errno == EAGAIN){
                    stop = true;
                }else{
                    throw std::runtime_error("nn_recv failed.");
                }
            }else{
                std::stringstream ss(buf);
                ss >> kv;
                nn_freemsg(buf);
            }
        }while(!stop);
        if(buf){
            return kv;
        }else{
            return _configuration.getRealValues();
        }
    }else{
        return _configuration.getRealValues();
    }
}

SelectorFixed::SelectorFixed(const Parameters& p,
         const Configuration& configuration,
         const Smoother<MonitoredSample>* samples):
                 Selector(p, configuration, samples){
    ;
}

SelectorFixed::~SelectorFixed(){;}

KnobsValues SelectorFixed::getNextKnobsValues(){
    _previousConfiguration = _configuration.getRealValues();
    return _configuration.getRealValues();
}

SelectorPredictive::SelectorPredictive(const Parameters& p,
                   const Configuration& configuration,
                   const Smoother<MonitoredSample>* samples,
                   std::unique_ptr<Predictor> throughputPredictor,
                   std::unique_ptr<Predictor> powerPredictor):
                       Selector(p, configuration, samples),
                       _throughputPredictor(std::move(throughputPredictor)),
                       _powerPredictor(std::move(powerPredictor)),
                       _feasible(true),
                       _throughputPrediction(NOT_VALID),
                       _powerPrediction(NOT_VALID){
    /****************************************/
    /*              Predictors              */
    /****************************************/
    _maxPerformance = -1;
#if STORE_PREDICTIONS
    // Just to let the multi manager work even if this manager terminates
    // before making some predictions.
    const vector<KnobsValues>& combinations = _configuration.getAllRealCombinations();
    for(size_t i = 0; i < combinations.size(); i++){
        _performancePredictions[combinations.at(i)] = -1;
        _powerPredictions[combinations.at(i)] = -1;
    }
#endif
}

SelectorPredictive::~SelectorPredictive(){
    ;
}

double SelectorPredictive::getRealThroughput(double predicted) const{
    if(_bandwidthIn->size()){
        return min(predicted, _bandwidthIn->average());
    }else{
        return predicted;
    }
}

double SelectorPredictive::getThroughputPrediction(const KnobsValues& values){
    auto observation = _observedValues.find(_configuration.getRealValues(values));
    double maxThroughput = 0.0;
    if(observation != _observedValues.end()){
        maxThroughput = observation->second.getMaximumThroughput();
    }else{
        _throughputPredictor->prepareForPredictions();
        maxThroughput = _throughputPredictor->predict(values);
    }
    if(isPrimaryRequirement(_p.requirements.minUtilization)){
        return maxThroughput;
    }else{
        return getRealThroughput(maxThroughput);
    }
}

double SelectorPredictive::getPowerPrediction(const KnobsValues& values){
    auto observation = _observedValues.find(_configuration.getRealValues(values));
    if(observation != _observedValues.end()){
        return observation->second.watts;
    }else{
        _powerPredictor->prepareForPredictions();
        return _powerPredictor->predict(values);
    }
}

const std::map<KnobsValues, double>& SelectorPredictive::getPrimaryPredictions() const{
#if STORE_PREDICTIONS
    return _performancePredictions;
#else
    throw std::runtime_error("Please define STORE_PREDICTIONS macro to 1.");
#endif
}

const std::map<KnobsValues, double>& SelectorPredictive::getSecondaryPredictions() const{
#if STORE_PREDICTIONS
    return _powerPredictions;
#else
    throw std::runtime_error("Please define STORE_PREDICTIONS macro to 1.");
#endif
}

bool SelectorPredictive::isBestMinMax(double throughput, double latency, double utilization,
                                double power, double& best){
    // Throughput maximization
    if(_p.requirements.throughput == NORNIR_REQUIREMENT_MAX){
        if(throughput > best){
            best = throughput;
            return true;
        }else{
            return false;
        }
    }

    // Latency minimization
    if(_p.requirements.latency == NORNIR_REQUIREMENT_MIN){
        throw std::runtime_error("Latency minimization not yet supported.");
        /*
        if(latency < best){
            best = latency;
            return true;
        }else{
            return false;
        }
        */
    }

    // Utilization maximization
    if(_p.requirements.maxUtilization == NORNIR_REQUIREMENT_MAX){
        if(utilization > best){
            best = utilization;
            return true;
        }else{
            return false;
        }
    }

    // Power minimization
    if(_p.requirements.powerConsumption == NORNIR_REQUIREMENT_MIN){
        if(power < best){
            best = power;
            return true;
        }else{
            return false;
        }
    }

    // If no MIN/MAX requirements, return true.
    return true;
}

bool SelectorPredictive::isBestSuboptimal(double throughput, double latency,
                                          double utilization, double power,
                                          double& best){
    // Throughput requirement
    if(isPrimaryRequirement(_p.requirements.throughput)){
        if(throughput > best){
            best = throughput;
            return true;
        }
    }

    // Latency requirement
    if(isPrimaryRequirement(_p.requirements.latency)){
        throw std::runtime_error("Latency control not yet supported.");
        /*
        if(latency < best){
            best = latency;
            return true;
        }
        */
    }

    // Utilization requirement
    if(isPrimaryRequirement(_p.requirements.maxUtilization)){
        // The best is the closest to minUtilization
        double distanceNew = _p.requirements.minUtilization - utilization;
        double distanceBest = _p.requirements.minUtilization - best;
        if(distanceNew > 0 && distanceBest < 0){
            best = utilization;
            return true;
        }else if(!(distanceNew < 0 && distanceBest > 0) &&
                 abs(distanceNew) < abs(distanceBest)){
            best = utilization;
            return true;
        }
    }

    // Power requirement
    if(isPrimaryRequirement(_p.requirements.powerConsumption)){
        if(power < best){
            best = power;
            return true;
        }
    }

    return false;
}


void SelectorPredictive::updateMaxPerformanceConfiguration(KnobsValues values,
                                                           double performance){
    if(performance > _maxPerformance){
        _maxPerformance = performance;
        _maxPerformanceConfiguration = values;
    }
}

KnobsValues SelectorPredictive::getBestKnobsValues(){
    KnobsValues bestKnobs(KNOB_VALUE_REAL);
    KnobsValues bestSuboptimalKnobs = _configuration.getRealValues();
    double bestValue = initBestValue();
    double bestSuboptimalValue = initBestSuboptimalValue();
    _feasible = false;

#ifdef DEBUG_SELECTORS
    double bestThroughputPrediction = 0;
    //double bestLatencyPrediction = 0;
    double bestPowerPrediction = 0;

    double bestSuboptimalThroughputPrediction = 0;
    //double bestSuboptimalLatencyPrediction = 0;
    double bestSuboptimalPowerPrediction = 0;
#endif

    const vector<KnobsValues>& combinations = _configuration.getAllRealCombinations();
    for(const KnobsValues& currentValues : combinations){
        double throughputPrediction = getThroughputPrediction(currentValues);
        double powerPrediction = getPowerPrediction(currentValues);
        double utilizationPrediction = _bandwidthIn->average() /
                                       throughputPrediction * 100.0;
        // Skip negative predictions
        if(throughputPrediction < 0 ||
           powerPrediction < 0 ||
           utilizationPrediction < 0){
            continue;
        }

        updateMaxPerformanceConfiguration(currentValues, throughputPrediction);
#if STORE_PREDICTIONS
        _performancePredictions[currentValues] = throughputPrediction;
        _powerPredictions[currentValues] = powerPrediction;
#endif

        DEBUG("Prediction: " << currentValues << " "
                             << throughputPrediction << " "
                             << powerPrediction);

        if(isFeasibleThroughput(throughputPrediction, true) &&
           isFeasibleLatency(0, true) &&
           isFeasibleUtilization(utilizationPrediction, true) &&
           isFeasiblePower(powerPrediction, true)){
            _feasible = true;
            if(isBestMinMax(throughputPrediction, 0, utilizationPrediction,
                            powerPrediction, bestValue)){
#ifdef DEBUG_SELECTORS
                bestThroughputPrediction = throughputPrediction;
                bestPowerPrediction = powerPrediction;
#endif
                bestKnobs = currentValues;
            }
        }else if(isBestSuboptimal(throughputPrediction, 0, utilizationPrediction,
                                  powerPrediction, bestSuboptimalValue)){
            // TODO In realta' per controllare se e' un sottoottimale
            // migliore bisognerebbe prendere la configurazione che soddisfa
            // il maggior numero di constraints fra quelli specificati.
            // Solo in un secondo momento, se non ne soddisfa nessuno, si va
            // a cercare quello piu' 'vicino'
#ifdef DEBUG_SELECTORS
            bestSuboptimalThroughputPrediction = throughputPrediction;
            bestSuboptimalPowerPrediction = powerPrediction;
#endif
            bestSuboptimalKnobs = currentValues;
        }
    }

    if(_feasible){
        DEBUG("Best solution found: " << bestKnobs);
        DEBUG("Throughput prediction: " << bestThroughputPrediction);
        DEBUG("Power prediction: " << bestPowerPrediction);
        return bestKnobs;
    }else{
        DEBUG("Suboptimal solution found: " << bestSuboptimalKnobs);
        DEBUG("Throughput prediction: " << bestSuboptimalThroughputPrediction);
        DEBUG("Power prediction: " << bestSuboptimalPowerPrediction);
        return bestSuboptimalKnobs;
    }
}

void SelectorPredictive::refine(){
    _throughputPredictor->refine();
    _powerPredictor->refine();
    if(_p.strategySelection == STRATEGY_SELECTION_LEARNING){
        _observedValues[_configuration.getRealValues()] = _samples->average();
    }
}

void SelectorPredictive::updatePredictions(const KnobsValues& next){
    const KnobsValues real = _configuration.getRealValues(next);
    _throughputPrediction = getThroughputPrediction(real);
    _powerPrediction = getPowerPrediction(real);
}

bool SelectorPredictive::predictorsReady() const{
    return _throughputPredictor->readyForPredictions() &&
           _powerPredictor->readyForPredictions();
}

bool SelectorPredictive::predictionsDone() const{
    return _throughputPrediction != NOT_VALID &&
           _powerPrediction != NOT_VALID;
}

void SelectorPredictive::clearPredictors(){
    _throughputPredictor->clear();
    _powerPredictor->clear();
    _throughputPrediction = NOT_VALID;
    _powerPrediction = NOT_VALID;
    _observedValues.clear();
}

bool SelectorPredictive::isMaxPerformanceConfiguration() const{
    if(_maxPerformanceConfiguration.areUndefined()){return false;}
    if(!_maxPerformanceConfiguration.areReal()){
        throw std::runtime_error("[FATAL ERROR] _maxPerformanceConfiguration "
                                 "must be of type REAL.");
    }
    return _configuration.getRealValues() != _maxPerformanceConfiguration;
}

bool SelectorLearner::isAccurate(){
    double predictedMaxThroughput = _throughputPrediction;
    double predictedPower = _powerPrediction;

    double maxThroughput = _samples->average().getMaximumThroughput();
    double power = _samples->average().watts;

    if(_p.requirements.minUtilization != NORNIR_REQUIREMENT_UNDEF){
        predictedMaxThroughput = _bandwidthIn->average() /
                                (_throughputPrediction / 100.0);
    }

    double performanceError = std::abs((maxThroughput - predictedMaxThroughput) /
                                        maxThroughput*100.0);
    double powerError = std::abs((power - predictedPower) / power*100.0);

    DEBUG("Perf error: " << performanceError);
    DEBUG("Power error: " << powerError);

    // For multi applications scenario we ignore power consumption model inaccuracies.
    if(performanceError > _p.maxPerformancePredictionError ||
       (!_calibrationCoordination && powerError > _p.maxPowerPredictionError) /* ||
       _primaryPredictor->getModelError() > 10 || //TODO
       _secondaryPredictor->getModelError() > 10*/){
        return false;
    }else{
        return true;
    }
}

bool SelectorPredictive::isBestSolutionFeasible() const{
    return _feasible;
}

SelectorAnalytical::SelectorAnalytical(const Parameters& p,
               const Configuration& configuration,
               const Smoother<MonitoredSample>* samples):
    SelectorPredictive(p, configuration, samples,
                       std::unique_ptr<Predictor>(new PredictorAnalytical(PREDICTION_THROUGHPUT, p, configuration, samples)),
                       std::unique_ptr<Predictor>(new PredictorAnalytical(PREDICTION_POWER, p, configuration, samples))),
    _violations(0), _firstPointGenerated(false){
    ;
}

KnobsValues SelectorAnalytical::getNextKnobsValues(){
    _previousConfiguration = _configuration.getRealValues();
    KnobsValues kv;
    if(!_firstPointGenerated){
        kv = getBestKnobsValues();
    }

    if(isContractViolated() || (isPrimaryRequirement(_p.requirements.throughput) &&
                                _samples->average().throughput < _p.requirements.throughput)){
        if(++_violations > _p.tolerableSamples){
            _violations = 0;
            kv = getBestKnobsValues();
            _bandwidthIn->reset();
        }else{
            ++_violations;
            kv = _configuration.getRealValues();
        }
    }else{
        stopCalibration();
        if(_violations){
            --_violations;
        }
        kv = _configuration.getRealValues();
    }

    if(_configuration.equal(kv)){
        stopCalibration();
    }else{
        if(!_firstPointGenerated){
            _firstPointGenerated = true;
        }else{
            startCalibration();
            ++_numCalibrationPoints;
            _bandwidthIn->reset();
        }
    }
    return kv;
}

void SelectorAnalytical::updateBandwidthIn(){
    // For this selector we do not consider input bandwidth for contracts
    // different from the utilization one.
    if(isPrimaryRequirement(_p.requirements.minUtilization)){
        Selector::updateBandwidthIn();
    }else{
        ;
    }
}

std::unique_ptr<Predictor> SelectorLearner::getPredictor(PredictorType type,
                                                         const Parameters& p,
                                                         const Configuration& configuration,
                                                         const Smoother<MonitoredSample>* samples) const{
    Predictor* predictor;
    switch(type){
        case PREDICTION_THROUGHPUT:{
            switch(p.strategyPredictionPerformance){
                case STRATEGY_PREDICTION_PERFORMANCE_AMDAHL:{
                    if(p.knobMappingEnabled){
                        predictor = new PredictorRegressionMapping<PredictorLinearRegression>(type, p, configuration, samples);
                    }else{
                        predictor = new PredictorLinearRegression(type, p, configuration, samples);
                    }
                }break;
                case STRATEGY_PREDICTION_PERFORMANCE_USL:
                case STRATEGY_PREDICTION_PERFORMANCE_USLP:{
                    if(p.knobMappingEnabled){
                        predictor = new PredictorRegressionMapping<PredictorUsl>(type, p, configuration, samples);
                    }else{
                        predictor = new PredictorUsl(type, p, configuration, samples);
                    }
                }break;
                case STRATEGY_PREDICTION_PERFORMANCE_LEO:{
                    predictor = new PredictorLeo(type, p, configuration, samples);
                }break;
                default:{
                    throw std::runtime_error("Unknown prediction strategy.");
                }break;
            }
        }break;
        case PREDICTION_POWER:{
            switch(p.strategyPredictionPower){
                case STRATEGY_PREDICTION_POWER_LINEAR:{
                    if(p.knobMappingEnabled){
                        predictor = new PredictorRegressionMapping<PredictorLinearRegression>(type, p, configuration, samples);
                    }else{
                        predictor = new PredictorLinearRegression(type, p, configuration, samples);
                    }
                }break;
                case STRATEGY_PREDICTION_POWER_LEO:{
                    predictor = new PredictorLeo(type, p, configuration, samples);
                }break;
                default:{
                    throw std::runtime_error("Unknown prediction strategy.");
                }break;
            }
        }break;
    }
    return std::unique_ptr<Predictor>(predictor);
}

SelectorLearner::SelectorLearner(const Parameters& p,
                   const Configuration& configuration,
                   const Smoother<MonitoredSample>* samples):
             SelectorPredictive(p, configuration, samples,
                                getPredictor(PREDICTION_THROUGHPUT, p, configuration, samples),
                                getPredictor(PREDICTION_POWER, p, configuration, samples)),
             _explorer(NULL), _firstPointGenerated(false),
             _contractViolations(0), _accuracyViolations(0), _totalCalPoints(0),
             _updatingInterference(false){
    /***************************************/
    /*              Explorers              */
    /***************************************/
    vector<bool> knobsFlags;
    vector<KnobsValues> additionalPoints;
    knobsFlags.resize(KNOB_NUM, false);

    if(_p.strategyPredictionPerformance == STRATEGY_PREDICTION_PERFORMANCE_USL ||
       _p.strategyPredictionPerformance == STRATEGY_PREDICTION_PERFORMANCE_USLP){
        KnobsValues kv(KNOB_VALUE_RELATIVE);
        // For the precise version of USL, we need to also take the throughput with
        // parallelism degree equal to one.
        if(_p.strategyPredictionPerformance == STRATEGY_PREDICTION_PERFORMANCE_USLP){
            kv.reset();
            kv[KNOB_VIRTUAL_CORES] = 0.0;
            kv[KNOB_FREQUENCY] = 0.0;
            kv[KNOB_CLKMOD_EMULATED] = 0.0;
            additionalPoints.push_back(kv);
        }
        kv.reset();
        kv[KNOB_VIRTUAL_CORES] = 100.0;
        kv[KNOB_FREQUENCY] = 100;
        kv[KNOB_CLKMOD_EMULATED] = 100.0;
        additionalPoints.push_back(kv);
        if(_p.knobFrequencyEnabled || 
           _p.knobClkModEmulatedEnabled){
            kv.reset();
            kv[KNOB_VIRTUAL_CORES] = 100.0;
            kv[KNOB_FREQUENCY] = 0.0;
            kv[KNOB_CLKMOD_EMULATED] = 0.0;
            additionalPoints.push_back(kv);
        }

        // I only need to explore on virtual cores.
        knobsFlags[KNOB_VIRTUAL_CORES] = true;
    }else if((_p.strategyPredictionPerformance == STRATEGY_PREDICTION_PERFORMANCE_AMDAHL ||
              _p.strategyPredictionPower == STRATEGY_PREDICTION_POWER_LINEAR)){
        // I only need to explore on virtual cores and frequency.
        knobsFlags[KNOB_VIRTUAL_CORES] = true;
        knobsFlags[KNOB_FREQUENCY] = true;
    }else{
        for(size_t i = 0; i < KNOB_NUM; i++){
            knobsFlags[i] = _p.isKnobEnabled((KnobType) i);
        }
    }

    switch(_p.strategyExploration){
        case STRATEGY_EXPLORATION_RANDOM:{
            _explorer = new ExplorerRandom(knobsFlags, additionalPoints);
        }break;
        case STRATEGY_EXPLORATION_HALTON:
        case STRATEGY_EXPLORATION_HALTON_REVERSE:
        case STRATEGY_EXPLORATION_NIEDERREITER:
        case STRATEGY_EXPLORATION_SOBOL:{
            _explorer = new ExplorerLowDiscrepancy(knobsFlags, _p.strategyExploration, additionalPoints);
        }break;
        default:{
            throw std::runtime_error("Unknown exploration strategy.");
        }
    }

    if(_p.knobMappingEnabled &&
       (_p.strategyPredictionPerformance == STRATEGY_PREDICTION_PERFORMANCE_AMDAHL ||
        _p.strategyPredictionPerformance == STRATEGY_PREDICTION_PERFORMANCE_USL ||
        _p.strategyPredictionPerformance == STRATEGY_PREDICTION_PERFORMANCE_USLP ||
        _p.strategyPredictionPower == STRATEGY_PREDICTION_POWER_LINEAR)){
        _explorer = new ExplorerMultiple(knobsFlags, _explorer, KNOB_MAPPING, MAPPING_TYPE_NUM);
    }
}

SelectorLearner::~SelectorLearner(){
    if(_explorer){
        delete _explorer;
    }
}

KnobsValues SelectorLearner::getNextKnobsValues(){
    KnobsValues kv;
    if(_updatingInterference){
        // It can only be done for PERF_* contract (so is primary) and on USL predictors.
        // (Check already done when the flag is set).
        PredictorUsl* pred = dynamic_cast<PredictorUsl*>(getPrimaryPredictor());
        pred->updateInterference();
        if(!_interferenceUpdatePoints.empty()){
            kv = _interferenceUpdatePoints.back();
            _interferenceUpdatePoints.pop_back();
            return kv;
        }else{
            _updatingInterference = false;
            pred->updateCoefficients();
            return _beforeInterferenceConf;
        }
    }
    _previousConfiguration = _configuration.getRealValues();
    if(_forced && !_forcedReturned){
        _forcedReturned = true;
        return _forcedConfiguration;
    }

    bool contractViolated = isContractViolated();
    bool accurate = isAccurate();

    /**
     * The first point is generated as soon as the application starts.
     * Accordingly, we do not executed tasks in the original configuration
     * used to create the application. For this reason, we do not use
     * it to refine the model.
     * E.g. The application has been created with configuration X
     * and as soon as it starts we move it to configuration Y.
     * We do not refine with configuration X since it has never
     * been real executed.
     **/
    if(!_firstPointGenerated){
        _firstPointGenerated = true;
        startCalibration();
    }else{
        if(isCalibrating()){
            refine();
            ++_totalCalPoints;
        }
    }

    if(isCalibrating()){
        if(!predictorsReady()){
            kv = _explorer->nextRelativeKnobsValues();
            ++_numCalibrationPoints;
        }else{
            if(predictionsDone() && accurate){
                kv = getBestKnobsValues();
                updatePredictions(kv);
                DEBUG("Finished in " << _numCalibrationPoints <<
                      " steps with configuration " << kv);
                stopCalibration();
                // In this case I do not update _numCalibrationPoints
                // since the next one will be (hopefully) the definitive
                // configuration.
            }else{
                kv = _explorer->nextRelativeKnobsValues();
                updatePredictions(kv);
                ++_numCalibrationPoints;
            }
        }
    }else{
        if(contractViolated){++_contractViolations;}
        if(!accurate){++_accuracyViolations;}

        if(phaseChanged()){
            /******************* Phase change. *******************/
            _explorer->reset();
            kv = _explorer->nextRelativeKnobsValues();

            // Drop old models.
            clearPredictors();

            startCalibration();
            ++_numCalibrationPoints;
            resetTotalCalibrationTime();
            _accuracyViolations = 0;
            _contractViolations = 0;
            _totalCalPoints = 0;
            _forced = false;
            DEBUG("Phase changed, recalibrating.");
        }else if(_bandwidthIn->coefficientVariation() > 100.0){ //TODO Remove magic number
            /******************* Bandwidth change. *******************/
            refine();
            ++_totalCalPoints;
            kv = getBestKnobsValues();
            updatePredictions(kv);
            _accuracyViolations = 0;
            _contractViolations = 0;
            _bandwidthIn->reset();
            _forced = false;
            DEBUG("Input bandwidth fluctuations, recomputing best solution.");
        }else if((!_p.maxCalibrationTime || getTotalCalibrationTime() < _p.maxCalibrationTime) &&
                 (!_p.maxCalibrationSteps || _totalCalPoints < _p.maxCalibrationSteps) &&
                 ((!accurate && _accuracyViolations > _p.tolerableSamples) ||
                  (isBestSolutionFeasible() && !_forced && contractViolated && _contractViolations > _p.tolerableSamples))){
            /******************* More calibration points. *******************/
            kv = _explorer->nextRelativeKnobsValues();
            updatePredictions(kv);
            refine();
            ++_totalCalPoints;
            startCalibration();
            ++_numCalibrationPoints;
            _accuracyViolations = 0;
            _contractViolations = 0;
            if(!accurate){
                DEBUG("Inaccurate model, adding more points.");
            }else{
                DEBUG("Contract violated, adding more points.");
            }
        }else{
            /******************* Stable. *******************/
            if(accurate && _accuracyViolations){ --_accuracyViolations;}
            if(!contractViolated && _contractViolations){ --_contractViolations;}
            kv = _configuration.getRealValues();
        }
    }
    return kv;
}

bool SelectorLearner::phaseChanged() const{
    switch(_p.strategyPhaseDetection){
        case STRATEGY_PHASE_DETECTION_NONE:{
            return false;
        }break;
        case STRATEGY_PHASE_DETECTION_TRIVIAL:{
            if(!_configuration.equal(_previousConfiguration)){
                // We need to check that this configuration is equal to the previous one
                // to avoid to detect as a phase change a configuration change.
                return false;
            }
            // For multi applications scenario we ignore power consumption variation.
            return _samples->coefficientVariation().latency > 20.0 ||
                   (!_calibrationCoordination && _samples->coefficientVariation().watts > 20.0);
        }break;
        default:{
            return false;
        }break;
    }
}

void SelectorLearner::updateModelsInterference(){
    if((isPrimaryRequirement(_p.requirements.powerConsumption) ||
        isPrimaryRequirement(_p.requirements.latency)) ||
       (_p.strategyPredictionPerformance != STRATEGY_PREDICTION_PERFORMANCE_USL &&
        _p.strategyPredictionPerformance != STRATEGY_PREDICTION_PERFORMANCE_USLP)){
        throw std::runtime_error("updateModelForInterference is only supported for "
                                 "PERF_* contracts and for USL* performance predictors.");
    }
    _beforeInterferenceConf = _configuration.getRealValues();
    _updatingInterference = true;
    // We only add 2 points (the third is the current one).
    KnobsValues kv(KNOB_VALUE_RELATIVE);
    kv[KNOB_FREQUENCY] = 0;

    kv[KNOB_VIRTUAL_CORES] = 0.5;
    _interferenceUpdatePoints.push_back(kv);
    kv[KNOB_VIRTUAL_CORES] = 0;
    _interferenceUpdatePoints.push_back(kv);
}

bool SelectorLearner::areModelsUpdated() const{
    return !_updatingInterference;
}

SelectorFixedExploration::SelectorFixedExploration(const Parameters& p,
               const Configuration& configuration,
               const Smoother<MonitoredSample>* samples,
               std::unique_ptr<Predictor> throughputPredictor,
               std::unique_ptr<Predictor> powerPredictor,
               size_t numSamples):
            SelectorPredictive(p, configuration, samples, std::move(throughputPredictor), std::move(powerPredictor)){
    const std::vector<KnobsValues>& combinations = _configuration.getAllRealCombinations();
    size_t numConfigurations = combinations.size();
    for(size_t i = 0; i < numConfigurations; i += ceil((double)numConfigurations/numSamples)){
        _confToExplore.push_back(combinations.at(i));
    }
}

SelectorFixedExploration::~SelectorFixedExploration(){
    ;
}

KnobsValues SelectorFixedExploration::getNextKnobsValues(){
    _previousConfiguration = _configuration.getRealValues();
    if(_confToExplore.size()){
        if(!isCalibrating()){
            startCalibration();
        }else{
            refine();
        }
        KnobsValues r = _confToExplore.back();
        _confToExplore.pop_back();
        ++_numCalibrationPoints;
        return r;
    }else{
        if(isCalibrating()){
            refine();
            KnobsValues kv = getBestKnobsValues();
            stopCalibration();
            return kv;
        }else{
            return _configuration.getRealValues();
        }
    }
}

SelectorLeo::SelectorLeo(const Parameters& p,
               const Configuration& configuration,
               const Smoother<MonitoredSample>* samples):
        SelectorFixedExploration(p, configuration, samples,
                       std::unique_ptr<Predictor>(new PredictorLeo(PREDICTION_THROUGHPUT, p, configuration, samples)),
                       std::unique_ptr<Predictor>(new PredictorLeo(PREDICTION_POWER, p, configuration, samples)),
                       p.leo.numSamples){
    ;
}

SelectorLeo::~SelectorLeo(){
    ;
}

SelectorFullSearch::SelectorFullSearch(const Parameters& p,
               const Configuration& configuration,
               const Smoother<MonitoredSample>* samples):
        SelectorFixedExploration(p, configuration, samples,
                       std::unique_ptr<Predictor>(new PredictorFullSearch(PREDICTION_THROUGHPUT, p, configuration, samples)),
                       std::unique_ptr<Predictor>(new PredictorFullSearch(PREDICTION_POWER, p, configuration, samples)),
                       configuration.getAllRealCombinations().size()){
    ;
}

SelectorFullSearch::~SelectorFullSearch(){
    ;
}

Frequency SelectorLiMartinez::findNearestFrequency(Frequency f) const{
    Frequency bestDistance = _availableFrequencies.back();
    Frequency bestFrequency = _availableFrequencies.back();
    for(size_t i = 0; i < _availableFrequencies.size(); i++){
        Frequency distance = std::abs(_availableFrequencies.at(i) - f);
        if(distance < bestDistance){
            bestDistance = distance;
            bestFrequency = _availableFrequencies.at(i);
        }
    }
    return bestFrequency;
}

void SelectorLiMartinez::goRight(){
    _low1 = _low2;
    _high1 = _mid2 - 1;
    _low2 = _mid2 + 1;
    _mid1 = (_low1 + _high1) / 2.0;
    _mid2 = (_low2 + _high2) / 2.0;
}

void SelectorLiMartinez::goLeft(){
    _high1 = _mid1 - 1;
    _low2 = _mid1 + 1;
    _high2 = _high1;
    _mid1 = (_low1 + _high1) / 2.0;
    _mid2 = (_low2 + _high2) / 2.0;
}

bool SelectorLiMartinez::isMaxPerformanceConfiguration() const{
    return false;
}

SelectorLiMartinez::SelectorLiMartinez(const Parameters& p,
                                           const Configuration& configuration,
                                           const Smoother<MonitoredSample>* samples):
        Selector(p, configuration, samples),
        _firstPointGenerated(false), _low1(0), _mid1(0), _high1(0),
        _low2(0), _mid2(0), _high2(0), _midId(1),
        _currentWatts(DBL_MAX), _optimalWatts(DBL_MAX),
        _optimalWorkers(configuration.getKnob(KNOB_VIRTUAL_CORES)->getAllowedValues().back()),
        _currentBw(0),_leftBw(0), _rightBw(0), _improved(false), _optimalFound(false){

    for(auto it : _configuration.getKnob(KNOB_FREQUENCY)->getAllowedValues()){
        _availableFrequencies.push_back((Frequency) it);
    }
    _optimalFrequency = _availableFrequencies.back();
    _allowedCores = configuration.getKnob(KNOB_VIRTUAL_CORES)->getAllowedValues();
}

SelectorLiMartinez::~SelectorLiMartinez(){
    ;
}

KnobsValues SelectorLiMartinez::getNextKnobsValues(){
    _previousConfiguration = _configuration.getRealValues();
    KnobsValues kv(KNOB_VALUE_REAL);

    if(!_firstPointGenerated){
        _firstPointGenerated = true;
        uint maxWorkers = _allowedCores.size(); //_configuration.getKnob(KNOB_TYPE_WORKERS)->getRealValue();
        _low2 = 1;
        _mid2 = maxWorkers / 2.0;
        _high2 = maxWorkers;

        DEBUG("Max workers: " << maxWorkers << " mid2: " << _mid2);
        kv[KNOB_VIRTUAL_CORES] = _allowedCores[_mid2 - 1];
        kv[KNOB_FREQUENCY] = _availableFrequencies.back();
        _midId = 2;

        startCalibration();

        DEBUG("Generating first point: " << kv);
        ++_numCalibrationPoints;
    }else{
        if(!isCalibrating()){
            return _optimalKv;
        }else if(!isContractViolated()){
            ++_numCalibrationPoints;
            _currentWatts = _samples->average().watts;

            if(_currentWatts < _optimalWatts){
                _improved = true;
                DEBUG("Found a new optimal watts: " << _currentWatts << " vs. " << _optimalWatts);
                _optimalWatts = _currentWatts;
                _optimalFrequency = _configuration.getKnob(KNOB_FREQUENCY)->getRealValue();
                _optimalWorkers = _configuration.getKnob(KNOB_VIRTUAL_CORES)->getRealValue();
                _optimalFound = true;
                DEBUG("Optimal: " << _optimalWorkers << ", " << _optimalFrequency);
            }

            // We should keep decreasing the frequency
            Frequency currentFrequency = _configuration.getKnob(KNOB_FREQUENCY)->getRealValue();
            kv[KNOB_VIRTUAL_CORES] = _configuration.getKnob(KNOB_VIRTUAL_CORES)->getRealValue();

            Frequency nextFrequency = currentFrequency * (_p.requirements.throughput / _samples->average().throughput);
            nextFrequency = findNearestFrequency(nextFrequency);
            if(nextFrequency == currentFrequency){
                --_numCalibrationPoints;
                goto changeworkers;
            }else{
                kv[KNOB_FREQUENCY] = nextFrequency;
                DEBUG("Keeping going down on frequencies. We move to: " << kv);
            }
        }else{
changeworkers:
            ++_numCalibrationPoints;
            // I have to change the number of workers
            kv[KNOB_FREQUENCY] = _availableFrequencies.back();

            if(_optimalWatts == DBL_MAX){
                // Still I have not found a number of workers that satisfied
                // the time requirement. I increase workers. (Go right).
                kv[KNOB_VIRTUAL_CORES] = _allowedCores[_mid2 - 1];
                goRight();
                _midId = 2;
                DEBUG("New interval 1: [" << _low1 << "," << _mid1 << "," << _high1 << "]");
                DEBUG("New interval 2: [" << _low2 << "," << _mid2 << "," << _high2 << "]");
                if(_low1 > _high1 || _low2 > _high2){
                    kv[KNOB_VIRTUAL_CORES] = _optimalWorkers;
                    kv[KNOB_FREQUENCY] = _optimalFrequency;
                    _optimalKv = kv;
                    stopCalibration();
                    DEBUG("Exploration finished with: " << kv);
                }

            }else if(_currentWatts > _optimalWatts || !_improved){
                DEBUG("This number of workers is worst than the best we found "
                      "up to now.");
                // This number of workers is not ok
                if(_midId == 1){
                    kv[KNOB_VIRTUAL_CORES] = _allowedCores[_mid2 - 1];
                    _midId = 2;
                    DEBUG("Trying with the right side. We move to " << kv);
                }else{
                    // Both explored and both are not ok, finished
                    kv[KNOB_VIRTUAL_CORES] = _optimalWorkers;
                    kv[KNOB_FREQUENCY] = _optimalFrequency;
                    _optimalKv = kv;
                    stopCalibration();
                    DEBUG("Both side are worst. Terminated with: " << kv);
                }
            }else{
                _improved = false;
                if(_midId == 1){
                    goLeft();
                }else{
                    goRight();
                }

                DEBUG("New interval 1: [" << _low1 << "," << _mid1 << "," << _high1 << "]");
                DEBUG("New interval 2: [" << _low2 << "," << _mid2 << "," << _high2 << "]");

                if(_low1 <= _high1){
                    _midId = 1;
                    kv[KNOB_VIRTUAL_CORES] = _allowedCores[_mid1 - 1];
                    DEBUG("We move to " << kv);
                }else if(_low2 <= _high2){
                    _midId = 2;
                    kv[KNOB_VIRTUAL_CORES] = _allowedCores[_mid2 - 1];
                    DEBUG("We move to " << kv);
                }else{
                    if(_optimalFound){
                        kv[KNOB_VIRTUAL_CORES] = _optimalWorkers;
                        kv[KNOB_FREQUENCY] = _optimalFrequency;
                    }else{
                        // Suboptimal solution for perf contract is maximum
                        // frequency and last visited cores.
                        kv[KNOB_VIRTUAL_CORES] = _configuration.getKnob(KNOB_VIRTUAL_CORES)->getRealValue();
                        kv[KNOB_FREQUENCY] = _availableFrequencies.back();
                    }
                    _optimalKv = kv;
                    stopCalibration();
                    DEBUG("Exploration finished with: " << kv);
                }
            }
        }
    }
    return kv;
}

}
