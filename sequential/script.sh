#!/bin/bash
#SBATCH --job-name=haileyC
#SBATCH --partition=Centaurus
#SBATCH --time=01:00:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --mem=16G

make clean

make

mkdir -p output

echo "Running sequential version..."
./level_client "Tom Hanks" 3 > output/sequential_output_3.txt 
./level_client "The Green Mile" 2 > output/sequential_output_2.txt 
./level_client "Michael Schumacher" 1 > output/sequential_output_1.txt 

echo "Running parallel version..."
./par_level_client "Tom Hanks" 3 > output/parallel_output_3.txt 
./par_level_client "The Green Mile" 2 > output/parallel_output_2.txt 
./par_level_client "Michael Schumacher" 1 > output/parallel_output_1.txt 

echo "Execution complete. Outputs saved in output/ directory"