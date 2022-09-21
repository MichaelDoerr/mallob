#!/bin/bash

num_hosts=$(cat $1|wc -l)
processes_per_host=$(cat $1|grep -oE "slots=[0-9]+"|head -1|grep -oE "[0-9]+")

# Set to 1 to enable distributed proof assembly
distributed_proof_assembly=0

# Old command
#command="mpirun --mca btl_tcp_if_include eth0 --allow-run-as-root --hostfile $1 --use-hwthread-cpus --map-by numa:PE=4 --bind-to hwthread --report-bindings /mallob ..."

echo "Cleaning up previous proofs..."
short_log_name=${1##*/} 
rm -rf "/logs/processes"
mkdir "/logs/processes"
mkdir -p "/logs/tracedir"
mkdir -p "/logs/extmem"

command="mpirun -np $processes_per_host /mallob -mono=$2 -log-directory=/logs/processes	-trace-dir=/logs/tracedir/ -t=4 -sleep=1000 -mempanic=0 -v=3 -max-lits-per-thread=50000000 -strict-clause-length-limit=20 -clause-filter-clear-interval=500 -max-lbd-partition-size=2 -export-chunks=20 -clause-buffer-discount=1.0 -satsolver=c -extmem-disk-dir=/logs/extmem/ -distributed-proof-assembly=$distributed_proof_assembly -proof-output-file=/logs/processes/proof.lrat -remove-units-preprocessing=$distributed_proof_assembly -interleave-proof-merging=0"

# echo "run_mallob.sh : $num_hosts hosts, $processes_per_host processes per host => $(($num_hosts * $processes_per_host)) MPI processes"
echo "run_mallob.sh : EXECUTE $command"

# Workaround for MPI error messages: Read -1, expected <some number>, errno = 1
OMPI_MCA_btl_vader_single_copy_mechanism=none
export OMPI_MCA_btl_vader_single_copy_mechanism=none

# Allow MPI processes to spawn (non-communicating) subprocesses
RDMAV_FORK_SAFE=1
export RDMAV_FORK_SAFE=1

# Run the actual command
OMPI_MCA_btl_vader_single_copy_mechanism=none RDMAV_FORK_SAFE=1 $command

echo "run_mallob.sh : DONE"
