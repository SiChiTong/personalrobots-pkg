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

#include "ompl_planning/planners/kinematicLBKPIECESetup.h"

ompl_planning::kinematicLBKPIECESetup::kinematicLBKPIECESetup(ModelBase *m) : PlannerSetup(m)
{
    name = "kinematic::LBKPIECE";	    
    priority = 11;
}

ompl_planning::kinematicLBKPIECESetup::~kinematicLBKPIECESetup(void)
{
    if (dynamic_cast<ompl::kinematic::LBKPIECE1*>(mp))
    {
	ompl::base::ProjectionEvaluator *pe = dynamic_cast<ompl::kinematic::LBKPIECE1*>(mp)->getProjectionEvaluator();
	if (pe)
	    delete pe;
    }
}

bool ompl_planning::kinematicLBKPIECESetup::setup(boost::shared_ptr<planning_environment::RobotModels::PlannerConfig> &options)
{
    preSetup(options);
    
    ompl::kinematic::LBKPIECE1 *kpiece = new ompl::kinematic::LBKPIECE1(dynamic_cast<ompl::kinematic::SpaceInformationKinematic*>(si));
    mp                                 = kpiece;
    
    if (options->hasParam("range"))
    {
	kpiece->setRange(options->getParamDouble("range", kpiece->getRange()));
	ROS_DEBUG("Range is set to %g", kpiece->getRange());
    }

    kpiece->setProjectionEvaluator(getProjectionEvaluator(options));
    
    if (kpiece->getProjectionEvaluator() == NULL)
    {
	ROS_WARN("Adding %s failed: need to set both 'projection' and 'celldim' for %s", name.c_str(), model->groupName.c_str());
	return false;
    }
    else
    {
	postSetup(options);
	return true;
    }
}