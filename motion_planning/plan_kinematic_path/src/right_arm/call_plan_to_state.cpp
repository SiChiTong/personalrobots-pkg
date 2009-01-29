/*********************************************************************
* Software License Agreement (BSD License)
* 
*  Copyright (c) 2008, Willow Garage, Inc.
*  All rights reserved.
* 
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
* 
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
* 
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/


/** \author Ioan Sucan */

/** This is a simple program that shows how to plan a motion to a
    state.  The minimum number of things to get the arm to move is
    done. This code only interfaces with a highlevel controller, so
    getting the motion executed is no longer our responsibility.
 */

#include <kinematic_planning/KinematicStateMonitor.h>

// interface to a high level controller
#include <pr2_msgs/MoveArmGoal.h>
    
class Example : public ros::Node,
		public kinematic_planning::KinematicStateMonitor
{
public:
    
    Example() : ros::Node("example_call_plan_to_state"),
		kinematic_planning::KinematicStateMonitor(dynamic_cast<ros::Node*>(this))
    {
	// we use the topic for sending commands to the controller, so we need to advertise it
	advertise<pr2_msgs::MoveArmGoal>("right_arm_goal", 1);
    }

    void runExample(void)
    {
	// construct the request for the highlevel controller
	pr2_msgs::MoveArmGoal ag;
	ag.implicit_goal = 0;
	ag.enable = 1;
	ag.timeout = 10.0;

	ag.goal_state.set_vals_size(7);
	for (unsigned int i = 0 ; i <ag.goal_state.get_vals_size(); ++i)
            ag.goal_state.vals[i] = 0.0;
	ag.goal_state.vals[0] = -0.5;
	ag.goal_state.vals[1] = -0.2;
	publish("right_arm_goal", ag);
    }
    
};


int main(int argc, char **argv)
{  
    ros::init(argc, argv);
    
    Example *plan = new Example();
    plan->loadRobotDescription();
    if (plan->loadedRobot())
    {
	sleep(1);
	plan->runExample();
    }
    sleep(1);
    
    plan->shutdown();
    delete plan;
    
    return 0;    
}
