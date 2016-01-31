// main.cpp
//
// Created by Daniel Schwartz-Narbonne on 13/04/07.
// Modified by Christian Bienia
//
// Copyright 2007-2008 Princeton University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

#include <iostream>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

#ifdef ENABLE_THREADS
#include <pthread.h>
#endif

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif

#include "annealer_types.h"
#include "annealer_thread.h"
#include "netlist.h"
#include "rng.h"

#include "../../src/manager.hpp"

using namespace std;
using namespace adpff;

void* entry_pt(void*);

typedef struct{
    int workerId;
    int accepted_good_moves;
    int accepted_bad_moves;
}CTask;

static CTask dummyTask;
static CTask* allTasks;

class Emitter: public AdaptiveNode{
private:
    int _temp_steps_completed;
    uint _activeWorkers;
    uint _receivedTasks;
    int _number_temp_steps;
    bool _keep_going_global_flag;
public:
    Emitter(uint maxNumWorkers, int number_temp_steps):
        _temp_steps_completed(0),
        _activeWorkers(maxNumWorkers),
        _receivedTasks(0),
        _number_temp_steps(number_temp_steps),
        _keep_going_global_flag(true){
        allTasks = new CTask[maxNumWorkers];
    }

    bool keep_going(int temp_steps_completed, int accepted_good_moves, int accepted_bad_moves)
    {
        bool rv;

        if(_number_temp_steps == -1) {
            //run until design converges
            rv = _keep_going_global_flag && (accepted_good_moves > accepted_bad_moves);
            if(!rv) _keep_going_global_flag = false; // signal we have converged
        } else {
            //run a fixed amount of steps
            rv = temp_steps_completed < _number_temp_steps;
        }

        return rv;
    }


    void notifyWorkersChange(size_t oldNumWorkers, size_t newNumWorkers){
        _activeWorkers = newNumWorkers;
    }

    void* svc(void* task){
        if(task == NULL){
            for(size_t i = 0; i < _activeWorkers; i++){
                ff_send_out((void*) &dummyTask);
            }
        }else{
            if(++_receivedTasks < _activeWorkers){
                return GO_ON;
            }

            ++_temp_steps_completed;

            for(size_t i = 0; i < _activeWorkers; i++){
                if(!keep_going(_temp_steps_completed, allTasks[i].accepted_good_moves, allTasks[i].accepted_bad_moves)){
                    TERMINATE_APPLICATION;
                }
            }
            _receivedTasks = 0;

            for(size_t i = 0; i < _activeWorkers; i++){
                ff_send_out((void*) &dummyTask);
            }
        }
    }
};

class Worker: public AdaptiveNode{
private:
    Rng _rng;
    netlist* _netlist;
    double _T;
    int _swapsPerTemp;
    int _movesPerThreadTemp;
    long a_id;
    long b_id;
    netlist_elem* a;
    netlist_elem* b;
public:
    enum move_decision_t{
        move_decision_accepted_good,
        move_decision_accepted_bad,
        move_decision_rejected
    };

    move_decision_t accept_move(routing_cost_t delta_cost, double T, Rng* rng)
    {
        //always accept moves that lower the cost function
        if (delta_cost < 0){
            return move_decision_accepted_good;
        } else {
            double random_value = rng->drand();
            double boltzman = exp(- delta_cost/T);
            if (boltzman > random_value){
                return move_decision_accepted_bad;
            } else {
                return move_decision_rejected;
            }
        }
    }


    //*****************************************************************************************
    //  If get turns out to be expensive, I can reduce the # by passing it into the swap cost fcn
    //*****************************************************************************************
    routing_cost_t calculate_delta_routing_cost(netlist_elem* a, netlist_elem* b)
    {
        location_t* a_loc = a->present_loc.Get();
        location_t* b_loc = b->present_loc.Get();

        routing_cost_t delta_cost = a->swap_cost(a_loc, b_loc);
        delta_cost += b->swap_cost(b_loc, a_loc);

        return delta_cost;
    }

    Worker(netlist* netlist, double startTemp, int swapsPerTemp, int maxNumWorkers):
        _netlist(netlist), _T(startTemp), _swapsPerTemp(swapsPerTemp),
        _movesPerThreadTemp(swapsPerTemp/maxNumWorkers),
        a_id(0), b_id(0){
        a = _netlist->get_random_element(&a_id, NO_MATCHING_ELEMENT, &_rng);
        b = _netlist->get_random_element(&b_id, NO_MATCHING_ELEMENT, &_rng);
    }

    void notifyWorkersChange(size_t oldNumWorkers, size_t newNumWorkers){
        _movesPerThreadTemp = _swapsPerTemp/newNumWorkers;
    }

    void* svc(void* task){
        _T = _T / 1.5;
        int accepted_good_moves = 0;
        int accepted_bad_moves = 0;

        for (int i = 0; i < _movesPerThreadTemp; i++){
            //get a new element. Only get one new element, so that reuse should help the cache
            a = b;
            a_id = b_id;
            b = _netlist->get_random_element(&b_id, a_id, &_rng);

            routing_cost_t delta_cost = calculate_delta_routing_cost(a,b);
            move_decision_t is_good_move = accept_move(delta_cost, _T, &_rng);

            //make the move, and update stats:
            if (is_good_move == move_decision_accepted_bad){
                accepted_bad_moves++;
                _netlist->swap_locations(a,b);
            } else if (is_good_move == move_decision_accepted_good){
                accepted_good_moves++;
                _netlist->swap_locations(a,b);
            } else if (is_good_move == move_decision_rejected){
                //no need to do anything for a rejected move
            }
        }
        allTasks[get_my_id()].workerId = get_my_id();
        allTasks[get_my_id()].accepted_good_moves = accepted_good_moves;
        allTasks[get_my_id()].accepted_bad_moves = accepted_bad_moves;
        return (void*)&dummyTask;
    }
};


int main (int argc, char * const argv[]) {
#ifdef PARSEC_VERSION
#define __PARSEC_STRING(x) #x
#define __PARSEC_XSTRING(x) __PARSEC_STRING(x)
        cout << "PARSEC Benchmark Suite Version "__PARSEC_XSTRING(PARSEC_VERSION) << endl << flush;
#else
        cout << "PARSEC Benchmark Suite" << endl << flush;
#endif //PARSEC_VERSION
#ifdef ENABLE_PARSEC_HOOKS
	__parsec_bench_begin(__parsec_canneal);
#endif

	srandom(3);

	if(argc != 5 && argc != 6) {
		cout << "Usage: " << argv[0] << " NTHREADS NSWAPS TEMP NETLIST [NSTEPS]" << endl;
		exit(1);
	}	
	
	//argument 1 is numthreads
	int num_threads = atoi(argv[1]);
	cout << "Threadcount: " << num_threads << endl;
#ifndef ENABLE_THREADS
#ifndef ENABLE_FF
	if (num_threads != 1){
		cout << "NTHREADS must be 1 (serial version)" <<endl;
		exit(1);
	}
#endif
#endif
		
	//argument 2 is the num moves / temp
	int swaps_per_temp = atoi(argv[2]);
	cout << swaps_per_temp << " swaps per temperature step" << endl;

	//argument 3 is the start temp
	int start_temp =  atoi(argv[3]);
	cout << "start temperature: " << start_temp << endl;
	
	//argument 4 is the netlist filename
	string filename(argv[4]);
	cout << "netlist filename: " << filename << endl;
	
	//argument 5 (optional) is the number of temperature steps before termination
	int number_temp_steps = -1;
        if(argc == 6) {
		number_temp_steps = atoi(argv[5]);
		cout << "number of temperature steps: " << number_temp_steps << endl;
        }

	//now that we've read in the commandline, run the program
	netlist my_netlist(filename);
	
	annealer_thread a_thread(&my_netlist,num_threads,swaps_per_temp,start_temp,number_temp_steps);
	
#ifdef ENABLE_PARSEC_HOOKS
	__parsec_roi_begin();
#endif

#ifdef ENABLE_FF
    std::vector<ff_node*> W;
    for(int i = 0; i < num_threads; i++){
        W.push_back((ff_node*)new Worker(&my_netlist, start_temp, swaps_per_temp, num_threads));
    }

    ff_farm<> farm(W);
    farm.add_emitter((ff_node*)new Emitter(num_threads, number_temp_steps));
    farm.wrap_around();
    //farm.remove_collector();
    //farm.set_scheduling_ondemand();

    adpff::Observer obs;
    adpff::Parameters ap("parameters.xml", "archdata.xml");
    ap.observer = &obs;
    //ap.expectedTasksNumber = numOptions * NUM_RUNS / CHUNKSIZE;
    adpff::ManagerFarm<> amf(&farm, ap);
    amf.start();
    std::cout << "amf started" << std::endl;
    amf.join();
    std::cout << "amf joined" << std::endl;

#elif ENABLE_THREADS
	std::vector<pthread_t> threads(num_threads);
	void* thread_in = static_cast<void*>(&a_thread);
	for(int i=0; i<num_threads; i++){
		pthread_create(&threads[i], NULL, entry_pt, thread_in);
	}
	for (int i=0; i<num_threads; i++){
		pthread_join(threads[i], NULL);
	}
#else
	a_thread.Run();
#endif
#ifdef ENABLE_PARSEC_HOOKS
	__parsec_roi_end();
#endif
	
	cout << "Final routing is: " << my_netlist.total_routing_cost() << endl;

#ifdef ENABLE_PARSEC_HOOKS
	__parsec_bench_end();
#endif

	return 0;
}

void* entry_pt(void* data)
{
	annealer_thread* ptr = static_cast<annealer_thread*>(data);
	ptr->Run();
}