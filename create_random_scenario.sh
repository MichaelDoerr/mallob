#!/bin/bash

function rand_arrival() {
    awk -v n=1 -v seed="$RANDOM" 'BEGIN { srand(seed); for (i=0; i<n; ++i) printf("%.4f\n", '$2'*rand()+'$1') }'
}
function rand_priority() {
    awk -v n=1 -v seed="$RANDOM" 'BEGIN { srand(seed); for (i=0; i<n; ++i) printf("%.4f\n", 0.998*rand()+0.001) }'
}

numjobs="$1"

ls /global_data/schreiber/sat_instances/*.cnf | shuf | head -$numjobs > selection

id=1
arrival=0
echo "# ID Arv Prio File"
while read -r filename; do

    arrival=`rand_arrival $arrival 10`
    echo "$id $arrival `rand_priority` $filename"
    id=$((id+1))

done < selection

rm selection

arrival=0
for i in {1..1000}; do
    arrival=`rand_arrival $arrival 2`
    echo $(($id+100000))" $arrival "`rand_priority`" /global_data/schreiber/sat_instances/ridiculouslysimple_sat_1.cnf"
    id=$((id+1))
done
