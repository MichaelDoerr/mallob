#!/bin/bash

solvers=$1

# MergeSAT
if echo $solvers|grep -q "m"; then
    if [ ! -d mergesat ]; then
        if [ ! -f mergesat-patched.tar.gz ]; then
            wget -nc https://dominikschreiber.de/mergesat-patched.tar.gz
        fi
        tar xzvf mergesat-patched.tar.gz
    fi
fi

# Glucose
if echo $solvers|grep -q "g"; then
    if [ ! -d glucose ]; then
        if [ ! -f glucose-syrup-4.1.tgz ]; then
            wget -nc https://www.labri.fr/perso/lsimon/downloads/softwares/glucose-syrup-4.1.tgz
        fi
        tar xzvf glucose-syrup-4.1.tgz
        rm ._glucose-syrup-4.1
        mv glucose-syrup-4.1 glucose
    fi
fi

# YalSAT
if echo $solvers|grep -q "y"; then
    if [ ! -d yalsat ]; then
        if [ ! -f yalsat-03v.zip ]; then
            wget -nc http://fmv.jku.at/yalsat/yalsat-03v.zip
        fi
        unzip yalsat-03v.zip
        mv yalsat-03v yalsat
    fi
fi

# Lingeling
if echo $solvers|grep -q "l"; then
    if [ ! -d lingeling ]; then
        if [ ! -f lingeling.zip ]; then
            # for fixing a branch instead of a commit, prepend "refs/heads/"
	    branchorcommit="89a167d0d2efe98d983c87b5b84175b40ea55842" # version 1.0.0, March 2024
            wget -nc https://github.com/arminbiere/lingeling/archive/${branchorcommit}.zip -O lingeling.zip
        fi
        unzip lingeling.zip
        mv lingeling-* lingeling
    fi
fi

# Kissat
if echo $solvers|grep -q "k"; then
    if [ ! -d kissat ]; then
        if [ ! -f kissat.zip ]; then
            # for fixing a branch instead of a commit, prepend "refs/heads/"
            branchorcommit="414fc53b824e51da3d256cc234d484036d84d886" # updated from A. Biere's 2023 state
            wget -nc https://github.com/domschrei/kissat/archive/${branchorcommit}.zip -O kissat.zip
        fi
        unzip kissat.zip
        mv kissat-* kissat
    fi
fi

# CaDiCaL (supports LRAT proof production)
if echo $solvers|grep -q "c"; then
    if [ ! -d cadical ]; then
        if [ ! -f cadical.zip ]; then
            # for fixing a branch instead of a commit, prepend "refs/heads/"
            branchorcommit="0ef9def35e2e25da84f5b421909d0e5bb28aa59f"
            wget -nc https://github.com/domschrei/cadical/archive/${branchorcommit}.zip -O cadical.zip
        fi
        unzip cadical.zip
        mv cadical-* cadical
    fi
fi
