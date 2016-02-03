import math
import subprocess
import shlex
import socket
import os

run = "cpupower frequency-set -g userspace"
process = subprocess.Popen(shlex.split(run), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
out, err = process.communicate()

parametersFile = open("parameters.xml", "w")
parametersFile.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
parametersFile.write("<adaptivityParameters>\n")
parametersFile.write("<qSize>4</qSize>\n")
parametersFile.write("<samplingIntervalSteady>1000</samplingIntervalSteady>\n")
parametersFile.write("<contractType>NONE</contractType>\n")
parametersFile.write("<strategyPolling>SLEEP_SMALL</strategyPolling>\n")
parametersFile.write("</adaptivityParameters>\n")
parametersFile.close()

outfile = open("REPARA_results.csv", "w")
outfile.write("#Workers\tFrequency\tTime\tWatts\n")

for c in xrange(1, 24):
    for f in [1200000, 1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000]:
        os.remove("summary.csv")
        run = "cpupower frequency-set -f " + str(f)
        process = subprocess.Popen(shlex.split(run), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        out, err = process.communicate()

        run = "./canneal " + str(c) + " 15000 2000 2500000.nets 6000"
        process = subprocess.Popen(shlex.split(run), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        out, err = process.communicate()

        run = "tail -1 summary.csv"
        process = subprocess.Popen(shlex.split(run), stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        out, err = process.communicate()
        res = out.split("\t")

        watts = res[0]
        time = res[2]
        outfile.write("{}\t{}\t{}\t{}\n".format(c, f, time, watts))
        outfile.flush()
        os.fsync(outfile.fileno())

outfile.close()