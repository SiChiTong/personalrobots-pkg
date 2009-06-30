#! /usr/bin/python
# Copyright (c) 2008, Willow Garage, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the Willow Garage, Inc. nor the names of its
#       contributors may be used to endorse or promote products derived from
#       this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# This script brings up a position controller on your joint of choice
# and allows you to type in the desired position setpoints.
#
# Author: Wim Meeussen

import random
CONTROLLER_NAME = "quick_position_controller_%08d" % random.randint(0,10**8-1)

import sys

import roslib
roslib.load_manifest('robot_mechanism_controllers')
import rospy
from std_msgs.msg import *
from mechanism_control import mechanism
from mechanism_msgs.srv import SpawnController, KillController


def xml_for(joint, p, i, d, iClamp):
    return "\
<controller name=\"%s\" type=\"JointPositionControllerNode\">\
  <joint name=\"%s\" >\
    <pid p=\"%f\" i=\"%f\" d=\"%f\" iClamp=\"%d\" \>\
  </joint>\
</controller>" % (CONTROLLER_NAME, joint, p, i, d, iClamp)

def main():
    if len(sys.argv) < 5:
        print "Usage:  position.py <joint> <p> <i> <d> <iClamp>"
        sys.exit(1)
    joint = sys.argv[1]
    p = float(sys.argv[2])
    i = float(sys.argv[3])
    d = float(sys.argv[4])
    iClamp = float(sys.argv[5])

    print "initialize node for position control on joint %s" %joint
    rospy.init_node('position', anonymous=True)
    rospy.wait_for_service('spawn_controller')
    spawn_controller = rospy.ServiceProxy('spawn_controller', SpawnController)
    kill_controller = rospy.ServiceProxy('kill_controller', KillController)

    resp = spawn_controller(xml_for(joint, p, i, d, iClamp), 1)
    if len(resp.ok) < 1 or not resp.ok[0]:
        print "Failed to spawn position controller"
        sys.exit(1)

    pub = rospy.Publisher("%s/set_command" % CONTROLLER_NAME, Float64)

    try:
        print "Enter positions:"
        while not rospy.is_shutdown():
            position = float(sys.stdin.readline().strip())
            pub.publish(Float64(position))
    finally:
        kill_controller(CONTROLLER_NAME)


if __name__ == '__main__':
    main()
