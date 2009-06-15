
/*
 * Copyright (c) 2008, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *W
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sbpl_arm_planner_node/sbpl_arm_planner_node.h>

using namespace std;
using namespace sbpl_arm_planner_node;

SBPLArmPlannerNode::SBPLArmPlannerNode(std::string node_name):ros::Node(node_name),node_name_(node_name)
{
  param<std::string>("~arm_name", arm_name_, "right_arm");
  param ("~allocated_time", allocated_time_, 1.0);
  param ("~forward_search", forward_search_, true);

  param ("~use_collision_map",use_collision_map_,false);

  param ("~search_mode", search_mode_, true);
  param ("~num_joints", num_joints_, 7);
  param ("~torso_arm_offset_x", torso_arm_offset_x_, 0.0);
  param ("~torso_arm_offset_y", torso_arm_offset_y_, 0.0);
  param ("~torso_arm_offset_z", torso_arm_offset_z_, 0.0);
  param<std::string>("~collision_map_topic", collision_map_topic_, "collision_map");
  advertiseService(arm_name_ + "/sbpl_planner/plan_path/GetPlan", &SBPLArmPlannerNode::planPath, this);
  subscribe(collision_map_topic_, collision_map_, &SBPLArmPlannerNode::collisionMapCallback,1);

  advertise<robot_msgs::CollisionMap> ("sbpl_collision_map", 1);

  planner_ = new ARAPlanner(&pr2_arm_env_, forward_search_);
  initializePlannerAndEnvironment();
};

SBPLArmPlannerNode::~SBPLArmPlannerNode()
{
  delete planner_;
  unsubscribe(collision_map_topic_);
  unadvertise("sbpl_collision_map");
  unadvertiseService(arm_name_ + "/sbpl_planner/plan_path/GetPlan");
};

void PrintUsage(char *argv[])
{
  printf("USAGE: %s <cfg file>\n", argv[0]);
}

bool SBPLArmPlannerNode::initializePlannerAndEnvironment()
{
  std::string env_config_string;
  std::string planner_config_string;

  this->param<std::string>("~env_config", env_config_string, " ");
  env_config_fp_ = fopen("sbpl_env_cfg_tmp.txt","wt");
  fprintf(env_config_fp_,"%s",env_config_string.c_str());

  ROS_INFO("\n\nEnvironment");
  ROS_INFO("%s",env_config_string.c_str());
  ROS_INFO("Environment");

  this->param<std::string>("~planner_config", planner_config_string, " ");
  planner_config_fp_ = fopen("sbpl_planner_cfg_tmp.txt","wt");
  fprintf(planner_config_fp_,"%s",planner_config_string.c_str());

  ROS_INFO("\n\nPlanner");
  ROS_INFO("%s",planner_config_string.c_str());
  ROS_INFO("Planner\n");

  fclose(env_config_fp_);
  fclose(planner_config_fp_);

  env_config_fp_ = fopen("sbpl_env_cfg_tmp.txt","rt");
  planner_config_fp_ = fopen("sbpl_planner_cfg_tmp.txt","rt");

  if(!pr2_arm_env_.InitEnvFromFilePtr(env_config_fp_, planner_config_fp_))
  {
    printf("ERROR: InitEnvFromFilePtr failed\n");
    return false;
  }

  fclose(env_config_fp_);
  fclose(planner_config_fp_);

  if(!pr2_arm_env_.InitializeMDPCfg(&mdp_cfg_))  //Initialize MDP Info
  {
    printf("ERROR: InitializeMDPCfg failed\n");
    return false;
  }

  //set epsilon
  planner_->set_initialsolution_eps(pr2_arm_env_.GetEpsilon());

  //set search mode (true - settle with first solution)
  planner_->set_search_mode(search_mode_);

  return true;
}

void SBPLArmPlannerNode::collisionMapCallback()
{
  if(!use_collision_map_)
    return;

//   printf("[collisionMapCallback] collision_map returned %i boxes.\n",collision_map_.boxes.size());

  std::vector<std::vector<double> > sbpl_boxes(collision_map_.boxes.size());
  for(unsigned int i=0; i < collision_map_.boxes.size(); i++)
  {
    sbpl_boxes[i].resize(6);
    sbpl_boxes[i][0] = collision_map_.boxes[i].center.x - torso_arm_offset_x_;
    sbpl_boxes[i][1] = collision_map_.boxes[i].center.y - torso_arm_offset_y_;
    sbpl_boxes[i][2] = collision_map_.boxes[i].center.z - torso_arm_offset_z_;

    sbpl_boxes[i][3] = collision_map_.boxes[i].extents.x;
    sbpl_boxes[i][4] = collision_map_.boxes[i].extents.y;
    sbpl_boxes[i][5] = collision_map_.boxes[i].extents.z;

/*    printf("[SBPLArmPlannerNode] obstacle %i: %.3f %.3f %.3f %.3f %.3f %.3f\n",i,sbpl_boxes[i][0],sbpl_boxes[i][1],
           sbpl_boxes[i][2],sbpl_boxes[i][3],sbpl_boxes[i][4],sbpl_boxes[i][5]);*/
  }

  //NOTE: clear map for dynamic obstacle support
  pr2_arm_env_.ClearEnv();
  pr2_arm_env_.AddObstacles(sbpl_boxes); //TODO: BEN - change AddObstacles to take in a pointer 

  //get sbpl collision map and publish it
  getSBPLCollisionMap();
}

void SBPLArmPlannerNode::getSBPLCollisionMap()
{
    std::vector<std::vector<double> > sbpl_cubes = (*(pr2_arm_env_.getCollisionMap()));

//     for(unsigned int i=0; i < sbpl_cubes.size(); i++)
//     {
//         printf("[getSBPLCollisionMap] cube %i:, ",i);
//         for(unsigned int k=0; k < sbpl_cubes[i].size(); k++)
//             printf("%.3f ", sbpl_cubes[i][k]);
//         printf("\n");
//     }

    sbpl_collision_map_.header.frame_id = "torso_lift_link";
    sbpl_collision_map_.header.stamp = ros::Time ();

    sbpl_collision_map_.set_boxes_size(sbpl_cubes.size());
    for(unsigned int i=0; i < sbpl_cubes.size(); i++)
    {
        sbpl_collision_map_.boxes[i].center.x = sbpl_cubes[i][0] + torso_arm_offset_x_;
        sbpl_collision_map_.boxes[i].center.y = sbpl_cubes[i][1] + torso_arm_offset_y_;
        sbpl_collision_map_.boxes[i].center.z = sbpl_cubes[i][2] + torso_arm_offset_z_;

        sbpl_collision_map_.boxes[i].extents.x = sbpl_cubes[i][3];
        sbpl_collision_map_.boxes[i].extents.y = sbpl_cubes[i][4];
        sbpl_collision_map_.boxes[i].extents.z = sbpl_cubes[i][5];

        sbpl_collision_map_.boxes[i].axis.x = 0.0;
        sbpl_collision_map_.boxes[i].axis.y = 0.0;
        sbpl_collision_map_.boxes[i].axis.z = 0.0;

        sbpl_collision_map_.boxes[i].angle = 0.0;
    }

//     printf("[getSBPLCollisionMap] publishing %i obstacles.\n",sbpl_cubes.size());
    publish ("sbpl_collision_map", sbpl_collision_map_);
}

bool SBPLArmPlannerNode::replan(robot_msgs::JointTraj &arm_path)
{
  ROS_INFO("[replan] replanning...");
  bool b_ret(false);
  double angles_r[num_joints_];
  clock_t start_time = clock();  //plan a path
  vector<int> solution_state_ids_v;

  planner_->costs_changed();
  b_ret = planner_->replan(allocated_time_, &solution_state_ids_v);

  ROS_INFO("Planning completed in %.4f seconds", double(clock()-start_time) / CLOCKS_PER_SEC);
  ROS_INFO("Size of solution = %d",solution_state_ids_v.size());

  if(b_ret)
  {
    arm_path.set_points_size(solution_state_ids_v.size());
    for(int i = 0; i < (int) solution_state_ids_v.size(); i++)
      arm_path.points[i].set_positions_size(num_joints_);
    ROS_INFO("Path:");
    for(int i = 0; i < (int) solution_state_ids_v.size(); i++) 
    {
      pr2_arm_env_.StateID2Angles(solution_state_ids_v[i], angles_r);
      for (unsigned int p = 0; p < (unsigned int) num_joints_; p++)
      {
        arm_path.points[i].positions[p] = angles_r[p];
      }
//       ROS_INFO("step %d: %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n",
//                i,arm_path.points[i].positions[0],arm_path.points[i].positions[1],arm_path.points[i].positions[2],arm_path.points[i].positions[3],
//                arm_path.points[i].positions[4],arm_path.points[i].positions[5],arm_path.points[i].positions[6]);
      arm_path.points[i].time = 0.0;
    }
  }
  return b_ret;
}

bool SBPLArmPlannerNode::setStart(const robot_msgs::JointTrajPoint &start)
{
  double sbpl_start[num_joints_];

  for(int i=0; i< num_joints_; i++)
  {
    sbpl_start[i] = start.positions[i];
  }
   ROS_INFO("start: %1.2f %1.2f %1.2f %1.2f %1.2f %1.2f %1.2f", sbpl_start[0],sbpl_start[1],sbpl_start[2],sbpl_start[3],sbpl_start[4],sbpl_start[5],sbpl_start[6]);

  if(pr2_arm_env_.SetStartJointConfig(sbpl_start, true) == 0)
  {
      ROS_ERROR("Environment failed to set start state\n");
      return false;
  }

  if(planner_->set_start(mdp_cfg_.startstateid) == 0)
  {
    ROS_ERROR("Failed to set start state\n");
    return false;
  }
  return true;
}

/*bool SBPLArmPlannerNode::setGoals(const std::vector<robot_msgs::Pose> &goals)
{
  int num_goals = (int) goals.size();
  tf::Pose tf_pose;

  double **sbpl_goal = new double*[num_goals];

  for(int i=0; i<num_goals; i++)
  {
    sbpl_goal[i] = new double[12];

    sbpl_goal[i][0] = goals[i].position.x;
    sbpl_goal[i][1] = goals[i].position.y;
    sbpl_goal[i][2] = goals[i].position.z;

    tf::PoseMsgToTF(goals[i],tf_pose);
    btScalar m[16];
    tf_pose.getOpenGLMatrix(m);

    sbpl_goal[i][3]= m[0];
    sbpl_goal[i][4]= m[4];
    sbpl_goal[i][5]= m[8];

    sbpl_goal[i][6]= m[1];
    sbpl_goal[i][7]= m[5];
    sbpl_goal[i][8]= m[9];

    sbpl_goal[i][9]= m[2];
    sbpl_goal[i][10]= m[6];
    sbpl_goal[i][11]= m[10];
  }

  pr2_arm_env_.SetEndEffGoals(sbpl_goal, 0, num_goals,1);

  for(int i=0; i < num_goals; i++)
  {
    delete sbpl_goal[i];
  }
  delete sbpl_goal;

  if(planner_->set_goal(mdp_cfg_.goalstateid) == 0)
  {
    ROS_ERROR("Failed to set goal state\n");
    return false;
  }
  return true;
}
*/

// set cartesian goals
bool SBPLArmPlannerNode::setGoals(const std::vector<robot_msgs::Pose> &goals)
{
  double yaw(0.0), pitch(0.0), roll(0.0);
  int num_goals = (int) goals.size();
  tf::Pose tf_pose;

  double **sbpl_goal = new double*[num_goals];

  for(int i=0; i<num_goals; i++)
  {
    sbpl_goal[i] = new double[6];

    sbpl_goal[i][0] = goals[i].position.x;
    sbpl_goal[i][1] = goals[i].position.y;
    sbpl_goal[i][2] = goals[i].position.z;

    tf::PoseMsgToTF(goals[i],tf_pose);
    btMatrix3x3 mat = tf_pose.getBasis();

    mat.getEulerZYX(yaw,pitch,roll);

    sbpl_goal[i][3] = roll;
    sbpl_goal[i][4] = pitch;
    sbpl_goal[i][5] = yaw;

/*  tf_pose.getOpenGLMatrix(m);

    sbpl_goal[i][3]= m[0];
    sbpl_goal[i][4]= m[4];
    sbpl_goal[i][5]= m[8];

    sbpl_goal[i][6]= m[1];
    sbpl_goal[i][7]= m[5];
    sbpl_goal[i][8]= m[9];

    sbpl_goal[i][9]= m[2];
    sbpl_goal[i][10]= m[6];
    sbpl_goal[i][11]= m[10];
*/
  }

  pr2_arm_env_.SetEndEffGoals(sbpl_goal, num_goals);

  for(int i=0; i < num_goals; i++)
  {
    delete sbpl_goal[i];
  }
  delete sbpl_goal;

  if(planner_->set_goal(mdp_cfg_.goalstateid) == 0)
  {
    ROS_ERROR("Failed to set goal state\n");
    return false;
  }
  return true;
}

// set joint space goals 
bool SBPLArmPlannerNode::setGoals(const robot_msgs::JointTrajPoint &goal_joint_positions_)
{
//     int num_goals = (int) goal_joint_positions_.size();
    int num_goals = 1;
    double **sbpl_goal = new double*[num_goals];

//     for(int i=0; i<num_goals; i++)
//     {
        int i =0;
        sbpl_goal[i] = new double[7];
        sbpl_goal[i][0] = goal_joint_positions_.positions[0];
        sbpl_goal[i][1] = goal_joint_positions_.positions[1];
        sbpl_goal[i][2] = goal_joint_positions_.positions[2];
        sbpl_goal[i][3] = goal_joint_positions_.positions[3];
        sbpl_goal[i][4] = goal_joint_positions_.positions[4];
        sbpl_goal[i][5] = goal_joint_positions_.positions[5];
        sbpl_goal[i][6] = goal_joint_positions_.positions[6];
//     }

    pr2_arm_env_.SetJointSpaceGoals(sbpl_goal, num_goals);

    for(int i=0; i < num_goals; i++)
    {
        delete sbpl_goal[i];
    }
    delete sbpl_goal;

    if(planner_->set_goal(mdp_cfg_.goalstateid) == 0)
    {
        ROS_ERROR("Failed to set goal state\n");
        return false;
    }
    return true;
}

bool SBPLArmPlannerNode::planPath(sbpl_arm_planner_node::PlanPathSrv::Request &req, sbpl_arm_planner_node::PlanPathSrv::Response &resp)
{
  ROS_INFO("[planPath] Planning...");
//   ROS_INFO("goal: %1.2f %1.2f %1.2f", req.cartesian_goals[0].position.x,req.cartesian_goals[0].position.y,req.cartesian_goals[0].position.z);
//   ROS_INFO("goal orientation: %1.2f %1.2f %1.2f %1.2f", req.cartesian_goals[0].orientation.x, req.cartesian_goals[0].orientation.y, req.cartesian_goals[0].orientation.z, req.cartesian_goals[0].orientation.w);

  printf("[planPath] start configuration: %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", req.start.positions[0],req.start.positions[1],req.start.positions[2],req.start.positions[3],req.start.positions[4],req.start.positions[5],req.start.positions[6]);
  robot_msgs::JointTraj traj;

  if(setStart(req.start))
  {
    ROS_INFO("[planPath] Start configuration has been set.");
    if(setGoals(req.cartesian_goals))
    {
        ROS_INFO("[planPath] Goal configuration has been set.");

//     if(setGoals(req.joint_goal))
//     {
//      nodHead(3);
      if(replan(traj))
      {
        ROS_INFO("Planning successful");
        resp.traj = traj;
        return true;
      }
      else
      {
        ROS_INFO("Planning unsuccessful");
      }
    }
    else
    {
      ROS_INFO("Set goal unsuccessful");
    }
  }
  else
  {
    ROS_INFO("Set start unsuccessful");
  }
  return false;
}


int main(int argc, char *argv[])
{
  ros::init(argc,argv); 
  SBPLArmPlannerNode node("plan_path_node");

  try 
  {
    node.spin();
  }
  catch(char const* e)
  {
    std::cout << e << std::endl;
  }
  return(0);
}

