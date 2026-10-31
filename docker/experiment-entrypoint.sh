#!/usr/bin/env bash
set -e
export ROS_HOME="${ROS_HOME:-/tmp/mm1-ros}"
mkdir -p "${ROS_HOME}/log"
# Compose supplies private, writable paths instead of the image's root defaults.
# RViz writes ~/.rviz; Qt requires an owner-only runtime directory.
mkdir -p "${HOME}/.rviz" "${XDG_RUNTIME_DIR}"
chmod 700 "${XDG_RUNTIME_DIR}"
source /opt/ros/noetic/setup.bash
source /catkin_ws/devel/setup.bash
export ROS_PACKAGE_PATH="/mm1/docker/ros:${ROS_PACKAGE_PATH}"
exec "$@"
