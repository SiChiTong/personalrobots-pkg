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

#ifndef OMPL_PLANNING_MODEL_BASE_
#define OMPL_PLANNING_MODEL_BASE_

#include <planning_environment/monitors/planning_monitor.h>
#include <planning_environment/util/kinematic_state_constraint_evaluator.h>
#include <string>

namespace ompl_planning
{
    
    struct EnvironmentDescription
    {
	collision_space::EnvironmentModel                           *collisionSpace;
	planning_models::KinematicModel                             *kmodel;
	const planning_environment::KinematicConstraintEvaluatorSet *constraintEvaluator;	
    };
    
    class ModelBase
    {
    public:
	ModelBase(void)
	{
	    groupID = -1;
	    planningMonitor = NULL;
	}
	
	virtual ~ModelBase(void)
	{
	    clearEnvironmentDescriptions();
	}
	
	/** \brief Thread safe function that returns the environment description corresponding to the active thread */
	EnvironmentDescription* getEnvironmentDescription(void) const;
	
	/** \brief Clear the created environment descriptions */
	void clearEnvironmentDescriptions(void) const;
	
	planning_environment::PlanningMonitor                 *planningMonitor;
	planning_environment::KinematicConstraintEvaluatorSet  constraintEvaluator;
	
	std::string                                            groupName;
	int                                                    groupID;
    };

} // ompl_planning

#endif

