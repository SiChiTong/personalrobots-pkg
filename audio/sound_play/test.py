#!/usr/bin/env python

#***********************************************************
#* Software License Agreement (BSD License)
#*
#*  Copyright (c) 2008, Willow Garage, Inc.
#*  All rights reserved.
#*
#*  Redistribution and use in source and binary forms, with or without
#*  modification, are permitted provided that the following conditions
#*  are met:
#*
#*   * Redistributions of source code must retain the above copyright
#*     notice, this list of conditions and the following disclaimer.
#*   * Redistributions in binary form must reproduce the above
#*     copyright notice, this list of conditions and the following
#*     disclaimer in the documentation and/or other materials provided
#*     with the distribution.
#*   * Neither the name of the Willow Garage nor the names of its
#*     contributors may be used to endorse or promote products derived
#*     from this software without specific prior written permission.
#*
#*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
#*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
#*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
#*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
#*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
#*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
#*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#*  POSSIBILITY OF SUCH DAMAGE.
#***********************************************************

# Author: Blaise Gassend

import roslib; roslib.load_manifest('sound_play')
import rospy
from sound_play.msg import SoundRequest

from sound_play.libsoundplay import SoundHandle

if __name__ == '__main__':
    rospy.init_node('soundplay_test', anonymous = True)
    soundhandle = SoundHandle()

    rospy.sleep(1)
    
    soundhandle.stopall()

    while not rospy.is_shutdown():
        soundhandle.playwave('17')
        soundhandle.playwave('dummy')
        
        print 'say'
        soundhandle.say('Hello world!')
        rospy.sleep(3)
        
        print 'wave'
        soundhandle.playwave('/usr/share/xemacs21/xemacs-packages/etc/sounds/cuckoo.wav')

        rospy.sleep(3)
        
        print 'wave2'
        soundhandle.playwave('/usr/share/xemacs21/xemacs-packages/etc/sounds/say-beep.wav')

        rospy.sleep(3)

        print 'plugging'
        soundhandle.play(SoundRequest.NEEDS_PLUGGING)
        soundhandle.play(SoundRequest.NEEDS_PLUGGING)

        rospy.sleep(2)

        #start(SoundRequest.BACKINGUP)

        rospy.sleep(1)

        print 'unplugging'
        soundhandle.play(SoundRequest.NEEDS_UNPLUGGING)

        rospy.sleep(1)
        print 'plugging badly'
        soundhandle.play(SoundRequest.NEEDS_PLUGGING_BADLY)
        rospy.sleep(1)
        #stop(SoundRequest.BACKINGUP)

        rospy.sleep(2)
        print 'unplugging badly'
        soundhandle.play(SoundRequest.NEEDS_UNPLUGGING_BADLY)

        rospy.sleep(3)
        
