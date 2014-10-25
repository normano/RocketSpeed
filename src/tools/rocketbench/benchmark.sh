#!/bin/bash
if [ $# -ne 1 ]; then
  echo "./benchmark.sh [produce|readwrite]"
  exit 0
fi

if [ -f rocketbench ]; then
  BENCH=./rocketbench
elif [ -f _build/opt/rocketspeed/github/src/tools/rocketbench/rocketbench ]; then
  BENCH=_build/opt/rocketspeed/github/src/tools/rocketbench/rocketbench
else
  echo "Must have either: "
  echo "  rocketbench, or"
  echo "  _build/opt/rocketspeed/github/src/tools/rocketbench/rocketbench"
  echo "from current directory"
  exit 0
fi

# size constants
K=1024
M=$((1024 * K))
G=$((1024 * M))

output_dir=${OUTPUT_DIR:-/tmp}
if [ ! -d $output_dir ]; then
  mkdir -p $output_dir
fi

storage_url=${STORAGE_URL:-\"configerator:logdevice/rocketspeed.logdevice.primary.conf\"}
message_rate=${MESSAGE_RATE:-$((50 * K))}
num_messages=${NUM_MESSAGES:-$((1 * M))}

const_params="
  --storage_url=$storage_url"

function run_produce {
  echo "Burst writing $num_messages messages into log storage..."
  cmd="$BENCH $const_params \
       --num_messages=$num_messages \
       --message_rate=$message_rate \
       --start_producer=true \
       --start_consumer=false \
       2>&1 | tee $output_dir/benchmark_produce.log"
  echo $cmd | tee $output_dir/benchmark_produce.log
  eval $cmd
}

function run_readwrite {
  echo "Writing and reading $num_messages simultaneously..."
  cmd="$BENCH $const_params \
       --num_messages=$num_messages \
       --message_rate=$message_rate \
       --start_producer=true \
       --start_consumer=true \
       2>&1 | tee $output_dir/benchmark_readwrite.log"
  echo $cmd | tee $output_dir/benchmark_readwrite.log
  eval $cmd
}

function now() {
  echo `date +"%s"`
}

report="$output_dir/report.txt"

# print start time
echo "===== Benchmark ====="

# Run!!!
IFS=',' read -a jobs <<< $1
for job in ${jobs[@]}; do
  echo "Start $job at `date`" | tee -a $report
  start=$(now)
  if [ $job = produce ]; then
    run_produce
  elif [ $job = readwrite ]; then
    run_readwrite
  else
    echo "unknown job $job"
    exit 1
  fi
  end=$(now)

  echo "Complete $job in $((end-start)) seconds" | tee -a $report
done
