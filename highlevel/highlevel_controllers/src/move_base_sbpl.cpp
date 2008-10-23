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

/**
 * @mainpage
 *
 * @htmlinclude manifest.html
 *
 * @b move_base is...
 *
 * <hr>
 *
 *  @section usage Usage
 *  @verbatim
 *  $ move_base
 *  @endverbatim
 *
 * <hr>
 *
 * @section topic ROS topics
 *
 * Subscribes to (name/type):
 * - @b 
 *
 * Publishes to (name / type):
 * - @b 
 *
 *  <hr>
 *
 * @section parameters ROS parameters
 *
 * - None
 **/

#include <MoveBase.hh>

//sbpl headers file
#include <headers.h>

/**
 * @todo Resolve issue with blocking ino locking
 */

namespace ros {
  namespace highlevel_controllers {

    /**
     * @brief Specialization for the SBPL planner
     */
    class MoveBaseSBPL: public MoveBase {
    public:
      MoveBaseSBPL();

      virtual ~MoveBaseSBPL();

    protected:

      /**
       * @brief Called during update of cost map. Will just buffer and handle in batch.
       * @see applyMapUpdates
       */
      virtual void handleMapUpdates(const std::vector<unsigned int>& updates);

      /**
       * @brief Builds a plan from current state to goal state
       */
      virtual bool makePlan();

    private:

      bool isMapDataOK();

      MDPConfig mdpCfg_;
      EnvironmentNAV2D envNav2D_;
      ARAPlanner* araPlanner_;
      double plannerTimeLimit_; /* The amount of time given to the planner to find a plan */
    };

    MoveBaseSBPL::MoveBaseSBPL()
      : MoveBase(){
      param(get_name() + "/plannerTimeLimit", plannerTimeLimit_, 1.0);

      lock();
      const CostMap2D& cm = getCostMap();
      unsigned char* initialMapData = new unsigned char[cm.getWidth() * cm.getHeight()];
      // Fill with cost map data
      memcpy(initialMapData, cm.getMap(), cm.getWidth() * cm.getHeight());

      // Initial Configuration is set with the threshold for obstacles set to the inscribed obstacle threshold. These, lethal obstacles, and cells with
      // no information will thus be regarded as obstacles
      envNav2D_.InitializeEnv(cm.getWidth(), cm.getHeight(), initialMapData, 0, 0, 0, 0, CostMap2D::INSCRIBED_INFLATED_OBSTACLE);

      // Cleanup
      delete[] initialMapData;
  
      bool success = envNav2D_.InitializeMDPCfg(&mdpCfg_);

      isMapDataOK();

      unlock();

      if(!success){
	ROS_INFO("ERROR: InitializeMDPCfg failed\n");
	exit(1);
      }

      araPlanner_ = new ARAPlanner(&envNav2D_, false);
    }

    MoveBaseSBPL::~MoveBaseSBPL(){
      if(araPlanner_ != NULL)
	delete araPlanner_;
    }

    /**
     * @brief This is called during a cost map update. Will insert new updates, possibly overwriting prior values
     */
    void MoveBaseSBPL::handleMapUpdates(const std::vector<unsigned int>& updates){
      
      const CostMap2D& cm = getCostMap();

      for(std::vector<unsigned int>::const_iterator it = updates.begin(); it != updates.end(); ++it){
	unsigned int x, y; // Cell coordinates
	cm.IND_MC(*it, x, y);
	envNav2D_.UpdateCost(x, y, cm.getCost(x, y));
      }
    }

    bool MoveBaseSBPL::isMapDataOK() {
      const CostMap2D& cm = getCostMap();
      
      for(unsigned int i = 0; i<cm.getWidth(); i++){
	for(unsigned int j = 0; j < cm.getHeight(); j++){
	  if(envNav2D_.IsObstacle(i, j) && cm.getCost(i, j) < CostMap2D::INSCRIBED_INFLATED_OBSTACLE){
	    ROS_DEBUG("Extra obstacle at <%d, %d>", i, j);
	    throw "Extra obstacle in sbpl";
	  }
	  if(!envNav2D_.IsObstacle(i, j) && cm.getCost(i, j) >= CostMap2D::INSCRIBED_INFLATED_OBSTACLE){
	    ROS_DEBUG("Missing obstacle at <%d, %d>", i, j);
	    throw "Missing obstacle in sbpl";
	  }
	}
      }

      return true;
    }

    bool MoveBaseSBPL::makePlan(){
      ROS_DEBUG("Planning for new goal...\n");

      unsigned int x, y;
      const CostMap2D& cm = getCostMap();

      // Set start state based on global pose.
      cm.WC_MC(stateMsg.pos.x, stateMsg.pos.y, x, y);
      envNav2D_.SetStart(x, y);
      araPlanner_->set_start(envNav2D_.GetStateFromCoord(x, y));

      // Set goal state
      cm.WC_MC(stateMsg.goal.x, stateMsg.goal.y, x, y);
      envNav2D_.SetGoal(x, y);
      araPlanner_->set_goal(envNav2D_.GetStateFromCoord(x, y));

      // Invoke the planner
      std::vector<int> solutionStateIDs;

      // Extract the solution, if available
      if(araPlanner_->replan(plannerTimeLimit_, &solutionStateIDs)){
	std::list<std_msgs::Pose2DFloat32> plan;
	for(std::vector<int>::const_iterator it = solutionStateIDs.begin(); it != solutionStateIDs.end(); ++it){
	  int state = *it;
	  int mx, my;
	  envNav2D_.GetCoordFromState(state, mx, my);

	  double wx, wy;
	  cm.MC_WC(mx, my, wx, wy);
	  std_msgs::Pose2DFloat32 waypoint;
	  waypoint.x = wx;
	  waypoint.y = wy;
	  plan.push_back(waypoint);
	}

	updatePlan(plan);
	return true;
      }
      else{
	ROS_DEBUG("No plan found\n");
	return false;
      }
    }
  }
}


int main(int argc, char** argv)
{
  ros::init(argc,argv); 

  ros::highlevel_controllers::MoveBaseSBPL node;

  try {
    node.run();
  }
  catch(char const* e){
    std::cout << e << std::endl;
  }

  ros::fini();

  return(0);
}
