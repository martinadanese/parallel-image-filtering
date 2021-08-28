#!/bin/bash
## submit with
## qsub -q dssc -l select=1:ncpus=24:ompthreads=24:kind=thin,walltime=30:00:00
#PBS -l walltime=30:00:00
#PBS -q dssc
#PBS -N omp_1weak

cd $PBS_O_WORKDIR 
module load   openmpi/4.0.3/gnu/9.3.0


for procs in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24; do
echo "executing on " ${procs} "  processors." >> strong_recording.txt 
 ./create_image.x ${procs}
sleep 10
/usr/bin/time ./openmp0.x ${procs} > ${procs}proc.out 2> ubt_${procs}proc.out
done
