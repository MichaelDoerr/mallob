#!/bin/bash
pcName="i10pc135"
folder="maxDemandDefaultTest" #latestResults
np="80"
J="60"
ajpc="6"
jobDescTemplate="instances/kmeansTest" 
jobTemplate="templates/job-template-kmeans.json" 
clientTemplate="templates/client-template-KR.json"
v="2"
demand="Unrestricted" #Unrestricted Restricted
time="60"

mkdir -p ./Testing/${folder}
echo "pcName:$pcName np:$np J:$J ajpc:$ajpc $jobDescTemplate $jobTemplate $clientTemplate v:$v time:$time"> ./Testing/${folder}/info${demand}.txt


./Testing/killAfter.sh ${time} &
PATH=build/:$PATH RDMAV_FORKSAVE=1 mpirun -np $np --use-hwthread-cpus --map-by numa:PE=2 --bind-to hwthread build/mallob -c=1 -J=$J -ajpc=$ajpc -job-desc-template=$jobDescTemplate -job-template=$jobTemplate -client-template=$clientTemplate -pls=0 -v=$v 2>&1 > ./Testing/${folder}/out${demand}.txt
    
cat ./Testing/${folder}/out${demand}.txt |grep "Got Result"|awk '{print $1}' > ./Testing/${folder}/plain-runtimes${demand}.txt
cat ./Testing/${folder}/plain-runtimes${demand}.txt | sort -g | awk '{print $1,NR}' > ./Testing/${folder}/cdf-runtimes${demand}.txt


kill -9 `ps -aux | grep "./Testing/killHung.sh" | grep -v grep | awk '{ print $2 }'`
