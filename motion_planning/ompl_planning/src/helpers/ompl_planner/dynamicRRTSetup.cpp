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

#include "ompl_planning/planners/dynamicRRTSetup.h"

ompl_planning::dynamicRRTSetup::dynamicRRTSetup(ModelBase *m) : PlannerSetup(m)
{
    name = "dynamic::RRT";
    priority = 2;
}

ompl_planning::dynamicRRTSetup::~dynamicRRTSetup(void)
{
}

bool ompl_planning::dynamicRRTSetup::setup(boost::shared_ptr<planning_environment::RobotModels::PlannerConfig> &options)
{
    preSetup(options);
    
    ompl::dynamic::RRT *rrt = new ompl::dynamic::RRT(dynamic_cast<ompl::dynamic::SpaceInformationControlsIntegrator*>(si));
    mp                      = rrt;
    
    if (options->hasParam("goal_bias"))
    {
	rrt->setGoalBias(options->getParamDouble("goal_bias", rrt->getGoalBias()));
	ROS_DEBUG("Goal bias is set to %g", rrt->getGoalBias());
    }

    if (options->hasParam("hint_bias"))
    {
	rrt->setHintBias(options->getParamDouble("hint_bias", rrt->getHintBias()));
	ROS_DEBUG("Goal bias is set to %g", rrt->getHintBias());
    }

    postSetup(options);
    
    return true;
}

