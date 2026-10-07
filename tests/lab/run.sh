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

"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DMM1_LAB=1 -I tests/lab/stubs -I src -I include \
    tests/lab/test_calibration.cpp src/lab/calibration_service.cpp -o "$test_dir/calibration"
"$test_dir/calibration"

"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-old-style-declaration -Wno-sign-compare \
    -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DMM1_LAB=1 -I src tests/lab/test_sh2_async.c src/board/p4/bno08x/sh2_util.c -o "$test_dir/sh2-async"
"$test_dir/sh2-async"

"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-old-style-declaration -Wno-sign-compare \
    -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DMM1_LAB=1 -I src tests/lab/test_sh2_boot.c src/board/p4/bno08x/sh2_util.c -o "$test_dir/sh2-boot"
"$test_dir/sh2-boot"

"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DMM1_LAB=1 -I src tests/lab/test_shtp_boot.c src/board/p4/bno08x/sh2_util.c -o "$test_dir/shtp-boot"
"$test_dir/shtp-boot"

"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-old-style-declaration -Wno-sign-compare \
    -fsanitize=address,undefined -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -DMM1_LAB=1 -I src tests/lab/test_sh2_protocol.c src/board/p4/bno08x/sh2.c \
    src/board/p4/bno08x/shtp.c src/board/p4/bno08x/sh2_util.c -o "$test_dir/sh2-protocol"
"$test_dir/sh2-protocol"
