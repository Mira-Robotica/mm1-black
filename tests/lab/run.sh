#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d /tmp/mm1-lab-tests.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I tests/lab/stubs -I src -I include \
    tests/lab/test_acquisition.cpp src/lab/orientation.cpp src/lab/laser_parser.cpp \
    src/lab/laser_poll.cpp src/lab/capture_service.cpp src/lab/capture_format.cpp \
    -o "$test_dir/acquisition"
"$test_dir/acquisition"

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-function \
    -fsanitize=address,undefined -DMM1_LAB=1 -I tests/lab/stubs -I src \
    tests/lab/test_imu_driver.cpp -o "$test_dir/imu-lab"
"$test_dir/imu-lab"

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I tests/lab/stubs -I src tests/lab/test_imu_driver.cpp -o "$test_dir/imu-original"
"$test_dir/imu-original"
