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

#ifndef KINEMATIC_PLANNING_RKP_STATE_VALIDATOR
#define KINEMATIC_PLANNING_RKP_STATE_VALIDATOR

#include <ompl/extension/samplingbased/State.h>
#include <ompl/base/StateValidityChecker.h>
#include <collision_space/environment.h>
#include <planning_environment/kinematic_state_constraint_evaluator.h>
#include "kinematic_planning/RKPModelBase.h"
#include "kinematic_planning/ompl_extensions/RKPSpaceInformation.h"

namespace kinematic_planning
{
    
    class StateValidityPredicate : public ompl::base::StateValidityChecker
    {
    public:
        StateValidityPredicate(SpaceInformationRKPModel *si, RKPModelBase *model) : ompl::base::StateValidityChecker()
	{
	    si_ = si;
	    model_ = model;
	}
	
	virtual bool operator()(const ompl::base::State *s) const
	{
	    const double *state = static_cast<const ompl::sb::State*>(s)->values;
	    model_->kmodel->computeTransformsGroup(state, model_->groupID);
	    
	    bool valid = kce_.decide(state, model_->groupID);
	    if (valid)
	    {
		model_->collisionSpace->updateRobotModel();
		valid = !model_->collisionSpace->isCollision();
	    }
	    
	    return valid;
	}
	
	void setConstraints(const motion_planning_msgs::KinematicConstraints &kc)
	{
	    kce_.clear();
	    kce_.add(model_->kmodel, kc.pose);
	    // joint constraints simply update the state space bounds
	    si_->clearJointConstraints();
	    si_->setJointConstraints(kc.joint);
	}
	
	void clearConstraints(void)
	{
	    kce_.clear();
	}
	
	const planning_environment::KinematicConstraintEvaluatorSet& getKinematicConstraintEvaluatorSet(void) const
	{
	    return kce_;
	}
	
    protected:
	
	RKPModelBase                                          *model_;
	SpaceInformationRKPModel                              *si_;
	planning_environment::KinematicConstraintEvaluatorSet  kce_;
    };  
    
} // kinematic_planning

#endif
    
