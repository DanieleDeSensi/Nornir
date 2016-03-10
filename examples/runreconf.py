import math
import subprocess
import shlex
import socket
import os
import os.path
import shutil
import argparse 
from subprocess import Popen, PIPE, STDOUT
import numpy as np

RESULTS_FILE = 'REPARA_results.csv'
powersList = []
timesList = []
bandwidthList = []

iterations = 2 #5

def getLastLine(fileName):
    fh = open(fileName, "r")
    for line in fh:
        pass
    last = line
    fh.close()
    return last

class cd:
    """Context manager for changing the current working directory"""
    def __init__(self, newPath):
        self.newPath = os.path.expanduser(newPath)

    def __enter__(self):
        self.savedPath = os.getcwd()
        os.chdir(self.newPath)

    def __exit__(self, etype, value, traceback):
        os.chdir(self.savedPath)

def run(bench, contractType, fieldName, fieldValue, percentile, dir_results, calstrategy, fastreconf, aging, conservative, knobworkers, polling, itnum):
    if 'INTEL' in args.prediction:
        process = subprocess.Popen(shlex.split("sh intelpowercapenable.sh " + str(fieldValue)), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        out, err = process.communicate()
        print out
        print err
    with cd(bench):
        parametersFile = open("parameters.xml", "w")
        parametersFile.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
        parametersFile.write("<adaptivityParameters>\n")
        parametersFile.write("<strategyPolling>" + polling + "</strategyPolling>\n")
        parametersFile.write("<statsReconfiguration>true</statsReconfiguration>\n")
        parametersFile.write("<strategyPrediction>" + args.prediction + "</strategyPrediction>\n")
        if 'INTEL' in args.prediction:
            parametersFile.write("<contractType>NONE</contractType>\n")
        else:
            parametersFile.write("<contractType>" + contractType + "</contractType>\n")
        parametersFile.write("<" + fieldName + ">" + str(fieldValue) + "</" + fieldName + ">\n")
        parametersFile.write("<samplingIntervalSteady>1000</samplingIntervalSteady>\n")
        parametersFile.write("<strategyPersistence>SAMPLES</strategyPersistence>\n")
        parametersFile.write("<persistenceValue>1</persistenceValue>\n")

        #parametersFile.write("<maxCalibrationTime>1000</maxCalibrationTime>\n")

        maxPerformanceError = 10.0
        maxPowerError = 5.0 

        if bench == 'simple_mandelbrot':
            maxPowerError = 10.0
            maxPerformanceError = 20.0
            if aging is not None:
                parametersFile.write("<regressionAging>" + str(aging) + "</regressionAging>\n")

        parametersFile.write("<regressionAging>10</regressionAging>\n")
        parametersFile.write("<tolerableSamples>3</tolerableSamples>\n") #TODO 

        if contractType == "PERF_COMPLETION_TIME" or contractType == "PERF_BANDWIDTH":
            parametersFile.write("<maxPrimaryPredictionError>" + str(maxPerformanceError) + "</maxPrimaryPredictionError>\n")
            parametersFile.write("<maxSecondaryPredictionError>" + str(maxPowerError) + "</maxSecondaryPredictionError>\n")
        else:
            parametersFile.write("<maxPrimaryPredictionError>" + str(maxPowerError) + "</maxPrimaryPredictionError>\n")
            parametersFile.write("<maxSecondaryPredictionError>" + str(maxPerformanceError) + "</maxSecondaryPredictionError>\n")

        if fastreconf:
            print "Using fast reconf."
            parametersFile.write("<fastReconfiguration>true</fastReconfiguration>\n")

        if calstrategy is not None:
            print "Using strategy " + calstrategy
            parametersFile.write("<strategyCalibration>" + calstrategy + "</strategyCalibration>\n")

        if knobworkers is not None:
            parametersFile.write("<knobWorkers>" + knobworkers + "</knobWorkers>\n")
            if 'MAPPING' in knobworkers:
                parametersFile.write("<knobHyperthreading>KNOB_HT_SOONER</knobHyperthreading>\n")

        if bench == 'blackscholes':
            run = "./blackscholes 23 inputs/in_10M.txt tmp.txt"
            parametersFile.write("<qSize>4</qSize>\n")
            parametersFile.write("<samplingIntervalCalibration>100</samplingIntervalCalibration>\n")
            parametersFile.write("<smoothingFactor>0.1</smoothingFactor>\n")
            parametersFile.write("<minTasksPerSample>300</minTasksPerSample>\n")
        elif bench == 'pbzip2':
            run = "./pbzip2_ff -f -k -p22 /home/desensi/enwiki-20151201-abstract.xml"
            parametersFile.write("<qSize>1</qSize>\n")
            parametersFile.write("<samplingIntervalCalibration>100</samplingIntervalCalibration>\n")
            parametersFile.write("<smoothingFactor>0.1</smoothingFactor>\n")
        elif bench == 'canneal':
            run = "./canneal 23 15000 2000 2500000.nets 6000"
            parametersFile.write("<qSize>1</qSize>\n")
            parametersFile.write("<samplingIntervalCalibration>10</samplingIntervalCalibration>\n")
            parametersFile.write("<smoothingFactor>0.1</smoothingFactor>\n")
            #parametersFile.write("<minTasksPerSample>10</minTasksPerSample>\n")
        elif bench == 'videoprocessing':
            #run = "./video 0 1 0 22 VIRAT/960X540.mp4 VIRAT/480X270.mp4 VIRAT/480X270.mp4 VIRAT/960X540.mp4"
            run = "./video 0 1 0 22 VIRAT/480X270.mp4"
            parametersFile.write("<qSize>1</qSize>\n")
            parametersFile.write("<samplingIntervalCalibration>200</samplingIntervalCalibration>\n")
            parametersFile.write("<smoothingFactor>0.1</smoothingFactor>\n")
            parametersFile.write("<expectedTasksNumber>117402</expectedTasksNumber>\n")
        elif bench == 'simple_mandelbrot':
            run = "./mandel_ff 8192 4096 1 22"
            parametersFile.write("<qSize>1</qSize>\n")
            parametersFile.write("<samplingIntervalCalibration>100</samplingIntervalCalibration>\n")
            parametersFile.write("<smoothingFactor>0.9</smoothingFactor>\n")
            parametersFile.write("<minTasksPerSample>1</minTasksPerSample>\n")
            if conservative is not None:
                parametersFile.write("<conservativeValue>" + str(conservative) + "</conservativeValue>\n")
            else:
                parametersFile.write("<conservativeValue>5</conservativeValue>\n")

        parametersFile.write("</adaptivityParameters>\n")
        parametersFile.close()

        process = subprocess.Popen(shlex.split(run), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        out, err = process.communicate()

        logfile = open("log.csv", "w")
        logfile.write(out)
        logfile.write(err)
        logfile.close()

        time = -1
        watts = -1
        bandwidth = -1
        calibrationSteps = -1
        calibrationTime = -1
        calibrationTimePerc = -1
        calibrationTasks = -1
        calibrationTasksPerc = -1
        calibrationWatts = -1
        reconfigurationTimeWorkersAvg = -1
        reconfigurationTimeWorkersStddev = -1
        reconfigurationTimeFrequencyAvg = -1
        reconfigurationTimeFrequencyStddev = -1
        reconfigurationTimeTotalAvg = -1
        reconfigurationTimeTotalStddev = -1
        try:
            result = getLastLine("summary.csv")
            time = float(result.split('\t')[2])
            watts = float(result.split('\t')[0])
            bandwidth = float(result.split('\t')[1])

            calibrationSteps = float(result.split('\t')[3])
            calibrationTime = float(result.split('\t')[4])
            calibrationTimePerc = float(result.split('\t')[5])
            calibrationTasks = float(result.split('\t')[6])
            calibrationTasksPerc = float(result.split('\t')[7])
            calibrationWatts = float(result.split('\t')[8])
            reconfigurationTimeWorkersAvg = float(result.split('\t')[9])
            reconfigurationTimeWorkersStddev = float(result.split('\t')[10])
            reconfigurationTimeFrequencyAvg = float(result.split('\t')[12])
            reconfigurationTimeFrequencyStddev = float(result.split('\t')[12])
            reconfigurationTimeTotalAvg = float(result.split('\t')[15])
            reconfigurationTimeTotalStddev = float(result.split('\t')[16])
        except:
            print "nothing"

        for outfile in ["calibration.csv", "stats.csv", "summary.csv", "parameters.xml", "log.csv"]:
            if os.path.isfile(outfile):
                os.rename(outfile, dir_results + "/" + str(percentile) + "." + str(itnum) + "." + outfile)
 
    if 'INTEL' in args.prediction:
        process = subprocess.Popen(shlex.split("sh intelpowercapdisable.sh"), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        out, err = process.communicate()
        print out 
        print err

    # Returns data
    return bandwidth, time, watts, calibrationSteps, calibrationTime, calibrationTimePerc, calibrationTasks, calibrationTasksPerc, calibrationWatts, reconfigurationTimeWorkersAvg, reconfigurationTimeFrequencyAvg, reconfigurationTimeTotalAvg

def loadPerfPowerData(bench):
    with cd(bench):
        fh = open(args.resultsfile, "r")
        for line in fh:
            #Workers Frequency Time Watts
            if line[0] != '#':
                fields = line.split("\t")
                timesList.append(float(fields[2]))
                powersList.append(float(fields[3]))
                bandwidthList.append(float(fields[4]))

        fh.close()

def getOptimalBandwidthBound(bw, bench):
    with cd(bench):
        fh = open(args.resultsfile, "r")
        bestPower = 99999999999
        for line in fh:
            #Workers Frequency Time Watts Bandwidth
            if line[0] != '#':
                fields = line.split("\t")
                curbw = float(fields[4])
                curpower = float(fields[3])
                if curpower < bestPower and curbw >= float(bw):
                    bestPower = curpower

        fh.close()
        return bestPower

def getOptimalTimeBound(time, bench):
    with cd(bench):
        fh = open(args.resultsfile, "r")
        bestPower = 99999999999
        for line in fh:
            #Workers Frequency Time Watts Bandwidth
            if line[0] != '#':
                fields = line.split("\t")
                curtime = float(fields[2])
                curpower = float(fields[3])
                if curpower < bestPower and curtime <= float(time):
                    bestPower = curpower
        
        fh.close()
        return bestPower

def getOptimalPowerBound(power, bench):
    with cd(bench):
        fh = open(args.resultsfile, "r")
        bestBw = 0
        for line in fh:
            #Workers Frequency Time Watts Bandwidth
            if line[0] != '#':
                fields = line.split("\t")
                curbw = float(fields[4])
                curpower = float(fields[3])
                if curbw > bestBw and curpower <= float(power):
                    bestBw = curbw
        fh.close()
        return bestBw

############################################################################################################

parser = argparse.ArgumentParser(description='Runs the application to check the accuracy of the reconfiguration.', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('-r', '--resultsfile', help='Post mortem results file.', required=False, default=RESULTS_FILE)
parser.add_argument('-b', '--benchmark', help='Benchmark.', required=True)
parser.add_argument('-p', '--prediction', help='Prediction strategy.', required=True)
parser.add_argument('-c', '--contract', help='Contract type.', required=True)
parser.add_argument('-k', '--calstrategy', help='Calibration strategy.', required=False, default='HALTON')
parser.add_argument('-f', '--fastreconf', help='Fast reconfiguration.', required=False,  action='store_true')
parser.add_argument('-a', '--aging', help='Aging factor.', required=False)
parser.add_argument('-o', '--conservative', help='Conservative value.', required=False)
parser.add_argument('-w', '--knobworkers', help='Knob Workers.', required=False)
parser.add_argument('-l', '--polling', help='Polling strategy.', required=False, default="SLEEP_SMALL")

args = parser.parse_args()

loadPerfPowerData(args.benchmark)

rdir = args.contract + '_'
rdir += args.prediction 
if args.prediction == 'REGRESSION_LINEAR':
    rdir += '_' + args.calstrategy
if args.fastreconf:
    rdir += '_FAST'
if args.knobworkers:
    rdir += '_' + args.knobworkers
if args.aging:
    rdir += '_AGING' + str(args.aging)
if args.conservative:
    rdir += '_CONSERVATIVE' + str(args.conservative)
if args.polling:
    rdir += '_' + str(args.polling)

with cd(args.benchmark):
    shutil.rmtree(rdir, ignore_errors=True)
    os.makedirs(rdir)
    outFile = open(rdir + "/results.csv", "w")

outFile.write("#Percentile\tPrimaryRequired\tPrimaryLossCnt\tPrimaryAvg\tPrimaryStddev\tPrimaryLossAvg\tPrimaryLossStddev\t" \
              "SecondaryOptimal\tSecondaryAvg\tSecondaryStddev\tSecondaryLossAvg\tSecondaryLossStddev\t" \
              "CalibrationStepsAvg\tCalibrationStepsStddev\t" \
              "CalibrationTimeAvg\tCalibrationTimeStddev\tCalibrationTimePercAvg\tCalibrationTimePercStddev\t"
              "CalibrationTaksAvg\tCalibrationTasksStddev\tCalibrationTasksPercAvg\tCalibrationTasksPercStddev\t"
              "CalibrationWattsAvg\tCalibrationWattsStddev\t"
              "ReconfigurationTimeWorkersAvg\tReconfigurationTimeWorkersStddev\t"
              "ReconfigurationTimeFrequencyAvg\tReconfigurationTimeFrequencyStddev\t"
              "ReconfigurationTimeTotalAvg\tReconfigurationTimeTotalStddev\n")

for p in xrange(10, 100, 20):
    cts = []
    bws = []
    wattses = []
    opts = []
    calibrationsSteps = []
    calibrationsTime = []
    calibrationsTimePerc = []
    calibrationsTasks = []
    calibrationsTasksPerc = []
    calibrationsWatts = []
    primaryLosses = []
    secondaryLosses = []
    reconfigurationTimesWorkers = []
    reconfigurationTimesFrequency = []
    reconfigurationTimesTotal = []
    avgPrimary = 0
    stddevPrimary = 0
    avgSecondary = 0
    stddevSecondary = 0
    avgLoss = 0
    stddevLoss = 0
    avgCalibrationSteps = 0
    stddevCalibrationSteps = 0
    avgCalibrationTime = 0
    stddevCalibrationTime = 0
    avgCalibrationTimePerc = 0
    stddevCalibrationTimePerc = 0
    avgCalibrationTasks = 0
    stddevCalibrationTasks = 0
    avgCalibrationTasksPerc = 0
    stddevCalibrationTasksPerc = 0
    avgCalibrationWatts = 0
    stddevCalibrationWatts = 0
    avgReconfigurationTimeWorkers = 0
    stddevReconfigurationTimeWorkers = 0
    avgReconfigurationTimeFrequency = 0
    stddevReconfigurationTimeFrequency = 0
    avgReconfigurationTimeTotal = 0
    stddevReconfigurationTimeTotal = 0
    primaryReq = 0
    target = 0
    
    if args.benchmark == 'pbzip2':
        iterations = 2

    for i in xrange(0, iterations):
        ct = -1
        if args.contract == 'PERF_BANDWIDTH':
            minBw = min(bandwidthList) 
            maxBw = max(bandwidthList) 
            targetBw = minBw + (maxBw - minBw)*(p/100.0)
            target = targetBw
            while ct == -1:
                bw, ct, watts, calibrationSteps, calibrationTime, calibrationTimePerc, calibrationTasks, calibrationTasksPerc, calibrationWatts, reconfigurationTimeWorkersAvg, reconfigurationTimeFrequencyAvg, reconfigurationTimeTotalAvg = run(args.benchmark, "PERF_BANDWIDTH", "requiredBandwidth", targetBw, p, rdir, args.calstrategy, args.fastreconf, args.aging, args.conservative, args.knobworkers, args.polling, i)
            opt = getOptimalBandwidthBound(targetBw, args.benchmark)
            primaryReq = targetBw
            primaryLoss = ((targetBw - bw) / targetBw ) * 100.0
            secondaryLoss = ((watts - opt) / opt) * 100.0
        elif args.contract == "PERF_COMPLETION_TIME":
            ############################
            # targetTime = np.percentile(np.array(timesList), p)
            ############################
            minTime = min(timesList)
            maxTime = max(timesList)
            targetTime = minTime + (maxTime - minTime)*(p/100.0)  
            target = targetTime
            while ct == -1:
                bw, ct, watts, calibrationSteps, calibrationTime, calibrationTimePerc, calibrationTasks, calibrationTasksPerc, calibrationWatts, reconfigurationTimeWorkersAvg, reconfigurationTimeFrequencyAvg, reconfigurationTimeTotalAvg = run(args.benchmark, "PERF_COMPLETION_TIME", "requiredCompletionTime", targetTime, p, rdir, args.calstrategy, args.fastreconf, args.aging, args.conservative, args.knobworkers, args.polling, i)
            opt = getOptimalTimeBound(targetTime, args.benchmark)
            primaryReq = targetTime
            primaryLoss = ((ct - targetTime) / targetTime ) * 100.0
            secondaryLoss = ((watts - opt) / opt) * 100.0
        elif args.contract == "POWER_BUDGET":
            #############################
            # targetPower = np.percentile(np.array(powersList), p)
            #############################
            minPower = min(powersList)
            maxPower = max(powersList)
            targetPower = minPower + (maxPower - minPower)*(p/100.0)
            if 'INTEL' in args.prediction:
                # The tool by Intel can only set the bound to an integer value. Since
                # the script we use divide the targetPower by the number of sockets and
                # since we have two sockets, we round the target power to the closest
                # even number 
                targetPower = round(targetPower / 2.) * 2
            target = targetPower
            while ct == -1:
                bw, ct, watts, calibrationSteps, calibrationTime, calibrationTimePerc, calibrationTasks, calibrationTasksPerc, calibrationWatts, reconfigurationTimeWorkersAvg, reconfigurationTimeFrequencyAvg, reconfigurationTimeTotalAvg = run(args.benchmark, "POWER_BUDGET", "powerBudget", targetPower, p, rdir, args.calstrategy, args.fastreconf, args.aging, args.conservative, args.knobworkers, args.polling, i)
            opt = getOptimalPowerBound(targetPower, args.benchmark)
            primaryReq = targetPower
            primaryLoss = ((watts - targetPower) / targetPower) * 100.0
            secondaryLoss = ((opt - bw) / opt) * 100.0
        cts.append(ct)
        bws.append(bw)
        wattses.append(watts)
        opts.append(opt)
        calibrationsSteps.append(calibrationSteps)
        calibrationsTime.append(calibrationTime)
        calibrationsTimePerc.append(calibrationTimePerc)
        calibrationsTasks.append(calibrationTasks)
        calibrationsTasksPerc.append(calibrationTasksPerc)
        calibrationsWatts.append(calibrationWatts)
        if primaryLoss > 3:
            primaryLosses.append(primaryLoss)
        else:
            secondaryLosses.append(secondaryLoss)
        reconfigurationTimesWorkers.append(reconfigurationTimeWorkersAvg)
        reconfigurationTimesFrequency.append(reconfigurationTimeFrequencyAvg)
        reconfigurationTimesTotal.append(reconfigurationTimeTotalAvg)

    if args.contract == "PERF_BANDWIDTH":
        avgPrimary = np.average(bws)
        stddevPrimary = np.std(bws)
        avgSecondary = np.average(wattses)
        stddevSecondary = np.std(wattses)
    elif args.contract == "PERF_COMPLETION_TIME":
        avgPrimary = np.average(cts)
        stddevPrimary = np.std(cts)
        avgSecondary = np.average(wattses)
        stddevSecondary = np.std(wattses)
    elif args.contract == "POWER_BUDGET":    
        avgPrimary = np.average(wattses)
        stddevPrimary = np.std(wattses)
        avgSecondary = np.average(bws)
        stddevSecondary = np.std(bws)
    
    if len(primaryLosses):
        avgPrimaryLoss = np.average(primaryLosses)
        stddevPrimaryLoss = np.std(primaryLosses)
    else:
        avgPrimaryLoss = 0
        stddevPrimaryLoss = 0

    if len(secondaryLosses):
        avgSecondaryLoss = np.average(secondaryLosses)
        stddevSecondaryLoss = np.std(secondaryLosses)
    else:
        avgSecondaryLoss = 0
        stddevSecondaryLoss = 0

    avgCalibrationSteps = np.average(calibrationsSteps)
    stddevCalibrationSteps = np.std(calibrationsSteps)
    avgCalibrationTime = np.average(calibrationsTime)
    stddevCalibrationTime = np.std(calibrationsTime)
    avgCalibrationTimePerc = np.average(calibrationsTimePerc)
    stddevCalibrationTimePerc = np.std(calibrationsTimePerc)
    avgCalibrationTasks = np.average(calibrationsTasks)
    stddevCalibrationTasks = np.std(calibrationsTasks)
    avgCalibrationTasksPerc = np.average(calibrationsTasksPerc)
    stddevCalibrationTasksPerc = np.std(calibrationsTasksPerc)
    avgCalibrationWatts = np.average(calibrationsWatts)
    stddevCalibrationWatts = np.std(calibrationsWatts)
    avgReconfigurationTimeWorkers = np.average(reconfigurationTimesWorkers)
    stddevReconfigurationTimeWorkers = np.std(reconfigurationTimesWorkers)
    avgReconfigurationTimeFrequency = np.average(reconfigurationTimesFrequency)
    stddevReconfigurationTimeFrequency = np.std(reconfigurationTimesFrequency)
    avgReconfigurationTimeTotal = np.average(reconfigurationTimesTotal)
    stddevReconfigurationTimeTotal = np.std(reconfigurationTimesTotal)

    outFile.write(str(p) + "\t" + str(target) + "\t" + str(len(primaryLosses)) + "\t" + str(avgPrimary) + "\t" + str(stddevPrimary) + "\t" + str(avgPrimaryLoss) + "\t" + str(stddevPrimaryLoss) + "\t" + 
                                  str(opt) + "\t" + str(avgSecondary) + "\t" + str(stddevSecondary) + "\t" + str(avgSecondaryLoss) + "\t" + str(stddevSecondaryLoss) + "\t" + 
                                  str(avgCalibrationSteps) + "\t" + str(stddevCalibrationSteps) + "\t" + 
                                  str(avgCalibrationTime) + "\t" + str(stddevCalibrationTime) + "\t" + str(avgCalibrationTimePerc) + "\t" + str(stddevCalibrationTimePerc) + "\t" + 
                                  str(avgCalibrationTasks) + "\t" + str(stddevCalibrationTasks) + "\t" + str(avgCalibrationTasksPerc) + "\t" + str(stddevCalibrationTasksPerc) + "\t" +
                                  str(avgCalibrationWatts) + "\t" + str(stddevCalibrationWatts) + "\t" +
                                  str(avgReconfigurationTimeWorkers) + "\t" + str(stddevReconfigurationTimeWorkers) + "\t" +
                                  str(avgReconfigurationTimeFrequency) + "\t" + str(stddevReconfigurationTimeFrequency) + "\t" + 
                                  str(avgReconfigurationTimeTotal) + "\t" + str(stddevReconfigurationTimeTotal) + "\n")
    outFile.flush()
    os.fsync(outFile.fileno())

outFile.close()

