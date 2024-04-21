#!/bin/bash

sudo cpupower frequency-set -f 1.6GHz
bash ~/paper_setup/init/read_user_timeline.sh
sleep 2 

# High request rate is 10000
# Base measurements taken at 500
# 25% = 250 50% = 500 75% = 750

i=1
#########--------Experiment 1 (25% spike)------------
while [[ $i -le 2 ]]
do
sudo cpupower frequency-set -f 1.6GHz
bash ~/paper_setup/init/read_user_timeline.sh
sleep 3

sudo ./controllers/parties > parties_25_${i} &
pid=$!
~/DeathStarBench_mod/mediaMicroservices/wrk2/wrk -D exp -t 4 -c 128 -d 30s -L -X 10 -Y 2 -Z 250 -R 1000 -s ./wrk2/scripts/social-network/read-user-timeline.lua http://localhost:8080 > wrk_parties_25_${i}
~/DeathStarBench_mod/mediaMicroservices/wrk2/wrk -D exp -t 4 -c 128 -d 40s -L -X 10 -Y 2 -Z 250 -R 1000 -s ./wrk2/scripts/social-network/read-user-timeline.lua http://localhost:8080 >> wrk_parties_25_${i}
wait pid
echo "-----------Done---------"

#######----------Experiment 2 (50% spike)------------
sudo cpupower frequency-set -f 1.6GHz
bash ~/paper_setup/init/read_user_timeline.sh
sleep 3

sudo ./controllers/parties > parties_50_${i} &
pid2=$!
~/DeathStarBench_mod/mediaMicroservices/wrk2/wrk -D exp -t 4 -c 128 -d 30s -L -X 10 -Y 2 -Z 500 -R 1000 -s ./wrk2/scripts/social-network/read-user-timeline.lua http://localhost:8080 > wrk_parties_50_${i}
~/DeathStarBench_mod/mediaMicroservices/wrk2/wrk -D exp -t 4 -c 128 -d 40s -L -X 10 -Y 2 -Z 500 -R 1000 -s ./wrk2/scripts/social-network/read-user-timeline.lua http://localhost:8080 >> wrk_parties_50_${i}
wait pid2

echo "-----------Done---------"

######----------Experiment 3(75% spike)--------------
sudo cpupower frequency-set -f 1.6GHz
bash ~/paper_setup/init/read_user_timeline.sh
sleep 3

sudo ./controllers/parties > parties_75_${i} &
pid3=$!
~/DeathStarBench_mod/mediaMicroservices/wrk2/wrk -D exp -t 4 -c 128 -d 30s -L -X 10 -Y 2 -Z 750 -R 1000 -s ./wrk2/scripts/social-network/read-user-timeline.lua http://localhost:8080 > wrk_parties_75_${i}
~/DeathStarBench_mod/mediaMicroservices/wrk2/wrk -D exp -t 4 -c 128 -d 40s -L -X 10 -Y 2 -Z 750 -R 1000 -s ./wrk2/scripts/social-network/read-user-timeline.lua http://localhost:8080 >> wrk_parties_75_${i}
wait pid3
((i++))
done
