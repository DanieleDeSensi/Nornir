/*
 * explorers.hpp
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

#ifndef NORNIR_EXPLORERS_HPP_
#define NORNIR_EXPLORERS_HPP_

#include "configuration.hpp"
#include <gsl/gsl_qrng.h>

namespace nornir{

class Explorer{
protected:
    const Configuration& _configuration;
public:
    Explorer(const Configuration& configuration);
    virtual ~Explorer(){;}

    /**
     * Resets the explorer.
     */
    virtual void reset() = 0;

    /**
     * Generates the next configuration to be explored during
     * calibration phase.
     * @return The next configuration to be explored during
     * calibration phase (relative values).
     */
    virtual KnobsValues nextRelativeKnobsValues() const = 0;
};

/**
 * Explorer that selects the configuration randomly.
 */
class ExplorerRandom: public Explorer{
public:
    ExplorerRandom(const Configuration& configuration);
    virtual ~ExplorerRandom(){;}
    void reset();
    KnobsValues nextRelativeKnobsValues() const;
};

/**
 * Explorer that selects the configuration by using a low discrepancy
 * generator.
 */
class ExplorerLowDiscrepancy: public Explorer{
private:
    gsl_qrng* _generator;
    double* _normalizedPoint;
    StrategyExploration _explorationStrategy;
public:
    ExplorerLowDiscrepancy(const Configuration& configuration,
                           StrategyExploration explorationStrategy);
    virtual ~ExplorerLowDiscrepancy();
    void reset();
    KnobsValues nextRelativeKnobsValues() const;
};

/**
 * Explorer to be used when building multiple models together.
 */
class ExplorerMultiple: public Explorer{
private:
    Explorer* _explorer;
    KnobType _kt;
    size_t _numValues;
    mutable size_t _nextValue;
    mutable KnobsValues _lastkv;
public:
    ExplorerMultiple(const Configuration& configuration,
                     Explorer* explorer,
                     KnobType kt,
                     size_t numValues);
    virtual ~ExplorerMultiple();
    void reset();
    KnobsValues nextRelativeKnobsValues() const;
};

}



#endif /* NORNIRs_EXPLORERS_HPP_ */
