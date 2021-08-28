#!/bin/bash
## submit with
## qsub -q dssc -l select=1:ncpus=24:mpiprocs=24:kind=thin,walltime=30:00:00
#PBS -l walltime=30:00:00
#PBS -q dssc
#PBS -N weak11

cd $PBS_O_WORKDIR 
module load   openmpi/4.0.3/gnu/9.3.0


for procs in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24; do
echo "executing on " ${procs} "  processors." >> weak_recording.txt 
 ./create_image.x ${procs}
sleep 10
/usr/bin/time mpirun  --mca btl '^openib' -np ${procs} mpi0.x  > ${procs}proc.out 2> ubt_${procs}proc.out
done
