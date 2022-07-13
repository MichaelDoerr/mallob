#!/bin/bash
kList=(5 10 20 30 40)
npList=(8 4 2 1)
for k in ${kList[@]}; do
    > ./Testing/times-${k}.txt
    for n in ${npList[@]}; do
        PATH=build/:$PATH RDMAV_FORKSAVE=1 mpirun -np ${n} build/mallob -mono-application=KMEANS -mono=./instances/covtypeShuffle${k}.csv -v=0 |grep "Got Result"|awk '{print '$n',$2}' >> ./Testing/times-${k}.txt
    done
done

