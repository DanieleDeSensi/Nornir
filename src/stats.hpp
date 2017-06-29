/*
 * stats.hpp
 *
 * Created on: 09/07/2015
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

/*!
 * \file stats.hpp
 * \brief Statistics collection utilities.
 **/

#ifndef NORNIR_STATS_HPP_
#define NORNIR_STATS_HPP_

#include "knob.hpp"
#include "utils.hpp"

namespace nornir{

class ReconfigurationStats{
private:
    std::vector<double> _knobs[KNOB_NUM];
    std::vector<double> _total;
    bool _storedKnob[KNOB_NUM];
    bool _storedTotal;
public:
    ReconfigurationStats(){
        for(size_t i = 0; i < KNOB_NUM; i++){
            _storedKnob[i] = false;
        }
        _storedTotal = false;
    }

    void swap(ReconfigurationStats& x){
        using std::swap;
        swap(_knobs, x._knobs);
        swap(_total, x._total);
        swap(_storedKnob, x._storedKnob);
        swap(_storedTotal, x._storedTotal);
    }

    inline ReconfigurationStats(const ReconfigurationStats& other){
        for(size_t i = 0; i < KNOB_NUM; i++){
            _knobs[i] = other._knobs[i];
            _storedKnob[i] = other._storedKnob[i];
        }
        _total = other._total;
        _storedTotal = other._storedTotal;
    }

    inline ReconfigurationStats& operator=(ReconfigurationStats other){
        swap(other);
        return *this;
    }

    inline void addSample(KnobType idx, double sample){
        _storedKnob[idx] = true;
        _knobs[idx].push_back(sample);
    }

    inline void addSampleTotal(double total){
        _storedTotal = true;
        _total.push_back(total);
    }

    inline double getAverageKnob(KnobType idx){
        return average(_knobs[idx]);
    }

    inline double getStdDevKnob(KnobType idx){
        return stddev(_knobs[idx]);
    }

    inline double getAverageTotal(){
        return average(_total);
    }

    inline double getStdDevTotal(){
        return stddev(_total);
    }

    inline bool storedKnob(KnobType idx){
        return _storedKnob[idx];
    }

    inline bool storedTotal(){
        return _storedTotal;
    }
};

typedef struct CalibrationStats{
    uint numSteps;
    uint duration;
    uint64_t numTasks;
    mammut::energy::Joules joules;

    CalibrationStats():numSteps(0), duration(0),numTasks(0),joules(0){
        ;
    }

    void swap(CalibrationStats& x){
        using std::swap;

        swap(numSteps, x.numSteps);
        swap(duration, x.duration);
        swap(numTasks, x.numTasks);
        swap(joules, x.joules);
    }

    CalibrationStats& operator=(CalibrationStats rhs){
        swap(rhs);
        return *this;
    }

    CalibrationStats& operator+=(const CalibrationStats& rhs){
        numSteps += rhs.numSteps;
        duration += rhs.duration;
        numTasks += rhs.numTasks;
        joules += rhs.joules;
        return *this;
    }
}CalibrationStats;

class Configuration;
class Selector;

/*!
 * This class can be used to obtain statistics about reconfigurations
 * performed by the manager.
 * It can be extended by a user defined class to customize action to take
 * for each observed statistic.
 */
class Logger{
    friend class Manager;

    template <typename T, typename V>
    friend class ManagerFarm;
private:
    unsigned int _timeOffset;
    unsigned int _startMonitoring;

    void addJoules(mammut::energy::Joules j);
protected:
    mammut::energy::Joules _totalJoules;
    unsigned int getRelativeTimestamp();
    unsigned int getAbsoluteTimestamp();
public:
    Logger(unsigned int timeOffset = 0);
    virtual ~Logger(){;}
    virtual void log(const Configuration& configuration, const Smoother<MonitoredSample>& samples) = 0;
    virtual void logSummary(const Configuration& configuration, Selector* selector, ulong duration, double totalTasks) = 0;
};

class LoggerStream: public Logger{
protected:
    std::ostream* _statsStream;
    std::ostream* _calibrationStream;
    std::ostream* _summaryStream;
    unsigned int _timeOffset;
public:

    LoggerStream(std::ostream* statsStream,
                 std::ostream* calibrationStream,
                 std::ostream* summaryStream,
                 unsigned int timeOffset = 0);

    void log(const Configuration& configuration, const Smoother<MonitoredSample>& samples);
    void logSummary(const Configuration& configuration, Selector* selector, ulong durationMs, double totalTasks);
};

class LoggerFile: public LoggerStream{
public:
    LoggerFile(std::string statsFile = "stats.csv",
               std::string calibrationFile = "calibration.csv",
               std::string summaryFile = "summary.csv",
               unsigned int timeOffset = 0):
        LoggerStream(new std::ofstream(statsFile),
                     new std::ofstream(calibrationFile),
                     new std::ofstream(summaryFile),
                     timeOffset){;}

    ~LoggerFile(){
        dynamic_cast<std::ofstream*>(_statsStream)->close();
        dynamic_cast<std::ofstream*>(_calibrationStream)->close();
        dynamic_cast<std::ofstream*>(_summaryStream)->close();
        delete _statsStream;
        delete _calibrationStream;
        delete _summaryStream;
    }
};

class LoggerGraphite: public Logger{
public:
    LoggerGraphite(const std::string& host, unsigned int port);
    ~LoggerGraphite();

    void log(const Configuration& configuration,
             const Smoother<MonitoredSample>& samples);

    // Doesn't log anything.
    void logSummary(const Configuration& configuration, Selector* selector,
                    ulong durationMs, double totalTasks){;}
};

}

#endif
