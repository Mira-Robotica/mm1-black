#!/usr/bin/env python3
"""ROS executable for the source-mounted coordinator in docker/scripts."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts"))
import rospy
from experiment import main

if __name__ == "__main__":
    sys.exit(main(rospy.myargv()[1:], experiment_number=2))
