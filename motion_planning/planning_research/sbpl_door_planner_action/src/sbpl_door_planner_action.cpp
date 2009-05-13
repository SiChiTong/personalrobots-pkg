/*
 * Copyright (c) 2008, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
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

#include <sbpl_door_planner_action/sbpl_door_planner_action.h>
#include <kdl/frames.hpp>
#include "door_msgs/Door.h"
#include <ros/node.h>
#include <visualization_msgs/Marker.h>

using namespace std;
using namespace ros;
using namespace KDL;
using namespace robot_actions;
using namespace door_functions;

SBPLDoorPlanner::SBPLDoorPlanner(ros::Node& ros_node, tf::TransformListener& tf) : 
  Action<door_msgs::Door, door_msgs::Door>(ros_node.getName()), ros_node_(ros_node), tf_(tf),
  cost_map_ros_(NULL),cm_getter_(this)
{
  ros_node_.param("~allocated_time", allocated_time_, 1.0);
  ros_node_.param("~forward_search", forward_search_, true);
  ros_node_.param("~animate", animate_, false);

  ros_node_.param("~planner_type", planner_type_, string("ARAPlanner"));
  ros_node_.param("~plan_stats_file", plan_stats_file_, string("/tmp/move_base_sbpl.log"));

  double circumscribed_radius;
  //we'll assume the radius of the robot to be consistent with what's specified for the costmaps
  ros_node_.param("~costmap/inscribed_radius", inscribed_radius_, 0.325);
  ros_node_.param("~costmap/inflation_radius", inflation_radius_, 0.46);
  ros_node_.param("~costmap/circumscribed_radius", circumscribed_radius, 0.46);
  ros_node_.param("~global_frame", global_frame_, std::string("odom_combined"));
  ros_node_.param("~robot_base_frame", robot_base_frame_, std::string("base_link"));

  ros_node_.param("~arm_control_topic_name",arm_control_topic_name_,std::string("r_arm_cartesian_pose_controller/command"));
  ros_node_.param("~base_control_topic_name",base_control_topic_name_, std::string("base/trajectory_controller/command"));

  cost_map_ros_ = new costmap_2d::Costmap2DROS(ros_node_,tf_,std::string(""));
  cost_map_ros_->getCostmapCopy(cost_map_);

  sleep(2.0);
  ros_node_.param<double>("~door_thickness", door_env_.door_thickness, 1.0);
  ros_node_.param<double>("~arm/min_workspace_angle",   door_env_.arm_min_workspace_angle, -M_PI);
  ros_node_.param<double>("~arm/max_workspace_angle",   door_env_.arm_max_workspace_angle, M_PI);
  ros_node_.param<double>("~arm/min_workspace_radius",  door_env_.arm_min_workspace_radius, 0.0);
  ros_node_.param<double>("~arm/max_workspace_radius",  door_env_.arm_max_workspace_radius, 0.85);
  ros_node_.param<double>("~arm/discretization_angle",  door_env_.door_angle_discretization_interval, 0.01);

  double shoulder_x, shoulder_y;
  ros_node_.param<double>("~arm/shoulder/x",shoulder_x, 0.0);
  ros_node_.param<double>("~arm/shoulder/y",shoulder_y, -0.188);
  door_env_.shoulder.x = shoulder_x;
  door_env_.shoulder.y = shoulder_y;

  ros_node_.advertise<visualization_msgs::Polyline>("~start", 1);
  ros_node_.advertise<visualization_msgs::Polyline>("~goal", 1);
  ros_node_.advertise<visualization_msgs::Polyline>("~robot_footprint", 1);
  ros_node_.advertise<visualization_msgs::Polyline>("~global_plan", 1);
  ros_node_.advertise<visualization_msgs::Polyline>("~door/frame", 1);
  ros_node_.advertise<visualization_msgs::Polyline>("~door/door", 1);
  ros_node_.advertise<visualization_msgs::Marker>( "visualization_marker",1);

  costmap_publisher_ = new costmap_2d::Costmap2DPublisher(ros_node_, 1.0, global_frame_, "costmap_publisher");
  if(costmap_publisher_->active())
    costmap_publisher_->updateCostmapData(cost_map_);

  ros_node_.advertise<robot_msgs::PoseStamped>(arm_control_topic_name_,1);
  ros_node_.advertise<robot_msgs::JointTraj>(base_control_topic_name_,1);

  robot_msgs::Point pt;
  //create a square footprint
  pt.x = inscribed_radius_;
  pt.y = -1 * (inscribed_radius_);
  footprint_.push_back(pt);
  pt.x = -1 * (inscribed_radius_);
  pt.y = -1 * (inscribed_radius_);
  footprint_.push_back(pt);
  pt.x = -1 * (inscribed_radius_);
  pt.y = inscribed_radius_;
  footprint_.push_back(pt);
  pt.x = inscribed_radius_;
  pt.y = inscribed_radius_;
  footprint_.push_back(pt);
};

SBPLDoorPlanner::~SBPLDoorPlanner()
{
  ros_node_.unadvertise("~robot_footprint");
  ros_node_.unadvertise("~start");
  ros_node_.unadvertise("~goal");
  ros_node_.unadvertise("~global_plan");
  ros_node_.unadvertise("~door/frame");
  ros_node_.unadvertise("~door/door");
  ros_node_.unadvertise("visualization_marker");

  ros_node_.unadvertise(arm_control_topic_name_);
  ros_node_.unadvertise(base_control_topic_name_);

  if(costmap_publisher_ != NULL)
    delete costmap_publisher_;

  if(cost_map_ros_ != NULL)
    delete cost_map_ros_;
}

void PrintUsage(char *argv[])
{
  printf("USAGE: %s <cfg file>\n", argv[0]);
}

bool SBPLDoorPlanner::initializePlannerAndEnvironment(const door_msgs::Door &door)
{
  door_env_.door = door;
  // First set up the environment
  try 
  {
    // We're throwing int exit values if something goes wrong, and
    // clean up any new instances in the catch clause. The sentry
    // gets destructed when we go out of scope, so unlock() gets
    // called no matter what.
    // sentry<SBPLPlannerNode> guard(this);  
    cm_access_.reset(mpglue::createCostmapAccessor(&cm_getter_));
    cm_index_.reset(mpglue::createIndexTransform(&cm_getter_));
    
    FILE *action_fp;
    std::string filename = "sbpl_action_tmp.txt";
    std::string sbpl_action_string;
    if(!ros::Node::instance()->getParam("~sbpl_action_file", sbpl_action_string))
      return false;
    action_fp = fopen(filename.c_str(),"wt");
    fprintf(action_fp,"%s",sbpl_action_string.c_str());
    fclose(action_fp);

    double nominalvel_mpersecs, timetoturn45degsinplace_secs;
    ros::Node::instance()->param("~nominalvel_mpersecs", nominalvel_mpersecs, 0.4);
    ros::Node::instance()->param("~timetoturn45degsinplace_secs", timetoturn45degsinplace_secs, 0.6);
    // Could also sanity check the other parameters...
    env_.reset(mpglue::SBPLEnvironment::createXYThetaDoor(cm_access_, cm_index_, footprint_, nominalvel_mpersecs,timetoturn45degsinplace_secs, filename, 0, door));
	
    boost::shared_ptr<SBPLPlanner> sbplPlanner;
    if ("ARAPlanner" == planner_type_)
    {
      sbplPlanner.reset(new ARAPlanner(env_->getDSI(), forward_search_));
    }
    else if ("ADPlanner" == planner_type_)
    {
      sbplPlanner.reset(new ADPlanner(env_->getDSI(), forward_search_));
    }
    else 
    {
      ROS_ERROR("in MoveBaseSBPL ctor: invalid planner_type_ \"%s\",use ARAPlanner or ADPlanner",planner_type_.c_str());
      throw int(5);
    }
    pWrap_.reset(new mpglue::SBPLPlannerWrap(env_, sbplPlanner));
  }
  catch (int ii) 
  {
    exit(ii);
  }
  return true;
}

bool SBPLDoorPlanner::removeDoor()
{
  const std::vector<robot_msgs::Point> door_polygon = door_functions::getPolygon(door_env_.door,door_env_.door_thickness); 

  for(int i=0; i < (int) door_polygon.size(); i++)
  {
    ROS_INFO("DOOR POLYGON %d:: %f, %f",i,door_polygon[i].x,door_polygon[i].y);
  }
  if(cost_map_.setConvexPolygonCost(door_polygon,costmap_2d::FREE_SPACE))
    return true;

  ROS_INFO("Could not remove door");
  return false;
}

bool SBPLDoorPlanner::createLinearPath(const pr2_robot_actions::Pose2D &cp,const pr2_robot_actions::Pose2D &fp, std::vector<pr2_robot_actions::Pose2D> &return_path)
{
  double dist_waypoints_max = 0.025;
  double dist_rot_waypoints_max = 0.1;

  ROS_DEBUG("Creating trajectory from: (%f,%f) to (%f,%f)",cp.x,cp.y,fp.x,fp.y);
  pr2_robot_actions::Pose2D temp;
  double dist_trans = sqrt(pow(cp.x-fp.x,2)+pow(cp.y-fp.y,2));        
  double dist_rot = fabs(angles::normalize_angle(cp.th-fp.th));

  int num_intervals = std::max<int>(1,(int) (dist_trans/dist_waypoints_max));
  num_intervals = std::max<int> (num_intervals, (int) (dist_rot/dist_rot_waypoints_max));

  double delta_x = (fp.x-cp.x)/num_intervals;
  double delta_y = (fp.y-cp.y)/num_intervals;
//  double delta_theta = angles::normalize_angle(fp.th-cp.th)/num_intervals;
//  double delta_theta = angles::normalize_angle(fp.th);


  for(int i=0; i< num_intervals; i++)
  {
    temp.x = cp.x + i * delta_x;
    temp.y = cp.y + i * delta_y;
    temp.th =  angles::normalize_angle(fp.th);
    return_path.push_back(temp);
  }

  temp.x = fp.x;
  temp.y = fp.y;
  temp.th = angles::normalize_angle(fp.th);
  return_path.push_back(temp);
  return true;
}


bool SBPLDoorPlanner::makePlan(const pr2_robot_actions::Pose2D &start, const pr2_robot_actions::Pose2D &goal, robot_msgs::JointTraj &path)
{
  ROS_INFO("[replan] getting fresh copy of costmap");
  lock_.lock();
  cost_map_ros_->getCostmapCopy(cost_map_);
  ROS_INFO("Getting local cost map copy");
  lock_.unlock();
  // XXXX this is where we would cut out the doors in our local copy
  clearRobotFootprint(cost_map_);
  if(!removeDoor())
  {
    return false;
  }

  //after clearing the footprint... we want to push the changes to the costmap publisher
  if(costmap_publisher_->active())
  {
    ROS_INFO("Publish costmap");
    costmap_publisher_->updateCostmapData(cost_map_);
  }

  publishFootprint(start,"~start");

  publishFootprint(goal,"~goal");

  ros::Duration d; // allow the publisher to publish
  d.fromSec(2.0);
  d.sleep();

  ROS_INFO("[replan] replanning...");
  try {
    // Update costs
    cm_access_.reset(mpglue::createCostmapAccessor(&cm_getter_));
    cm_index_.reset(mpglue::createIndexTransform(&cm_getter_));
    for (mpglue::index_t ix(cm_access_->getXBegin()); ix < cm_access_->getXEnd(); ++ix) {
      for (mpglue::index_t iy(cm_access_->getYBegin()); iy < cm_access_->getYEnd(); ++iy) {
	mpglue::cost_t cost;
	if (cm_access_->getCost(ix, iy, &cost)) { // always succeeds though
	  // Note that ompl::EnvironmentWrapper::UpdateCost() will
	  // check if the cost has actually changed, and do nothing
	  // if it hasn't.  It internally maintains a list of the
	  // cells that have actually changed, and this list is what
	  // gets "flushed" to the planner (a couple of lines
	  // further down).
          if(cost == costmap_2d::NO_INFORMATION)
            cost = costmap_2d::FREE_SPACE;
	  env_->UpdateCost(ix, iy, cost);
          ROS_DEBUG("Updated cost");
	}
      }
    }
    
    // Tell the planner about the changed costs. Again, the called
    // code checks whether anything has really changed before
    // embarking on expensive computations.
    pWrap_->flushCostChanges(true);
		
    // Assume the robot is constantly moving, so always set start.
    // Maybe a bit inefficient, but not as bad as "changing" the
    // goal when it hasn't actually changed.
    pWrap_->setStart(start.x, start.y, start.th);	
    pWrap_->setGoal(goal.x, goal.y, goal.th);
	
    // BTW if desired, we could call pWrap_->forcePlanningFromScratch(true)...
	
    // Invoke the planner, updating the statistics in the process.
    // The returned plan might be empty, but it will not contain a
    // null pointer.  On planner errors, the createPlan() method
    // throws a std::exception.
    boost::shared_ptr<mpglue::waypoint_plan_t> plan(pWrap_->createPlan());
    if (plan->empty()) 
    {
      ROS_ERROR("No plan found\n");
      return false;
    }
    else
    {
      path.points.clear();
      ROS_DEBUG("Found a path with %d points",(int) plan->size());
      for(int i=0; i < (int) plan->size(); i++)
      {
        robot_msgs::JointTrajPoint path_point;
        mpglue::waypoint_s const * wpt((*plan)[i].get());
        mpglue::door_waypoint_s const * doorwpt(dynamic_cast<mpglue::door_waypoint_s const *>(wpt));	
        path_point.positions.push_back(doorwpt->x);
        path_point.positions.push_back(doorwpt->y);
        path_point.positions.push_back(doorwpt->theta);
        path_point.positions.push_back(doorwpt->min_door_angle);
        path_point.positions.push_back(doorwpt->plan_interval);
        path.points.push_back(path_point);
        ROS_DEBUG("Path point: %f %f %f %f %d",doorwpt->x,doorwpt->y,doorwpt->theta,doorwpt->min_door_angle,(int)doorwpt->plan_interval);
      }
    }
    //path.points.clear();// just paranoid
    //mpglue::PlanConverter::convertToJointTraj(plan.get(), &path);
    return true;
  }
  catch (std::runtime_error const & ee) {
    ROS_ERROR("runtime_error in makePlan(): %s\n", ee.what());
  }
// Test controller
//   std::vector<pr2_robot_actions::Pose2D> return_path;
//   createLinearPath(start,goal,return_path);
//   ROS_INFO("Return path has %d points",return_path.size());
//   path.points.clear();
//   for(int i=0; i < (int) return_path.size(); i++)
//   {
//     robot_msgs::JointTrajPoint path_point;
//     path_point.positions.push_back(return_path[i].x);
//     path_point.positions.push_back(return_path[i].y);
//     path_point.positions.push_back(return_path[i].th);
//     path_point.positions.push_back(M_PI/3.0);
//     path.points.push_back(path_point);
//   }
//   return true;

  return false;
}

robot_actions::ResultStatus SBPLDoorPlanner::execute(const door_msgs::Door& door_msg_in, door_msgs::Door& feedback)
{
  robot_msgs::JointTraj path;
  door_msgs::Door door;
  if(!updateGlobalPose())
  {
    return robot_actions::ABORTED;
  }

  ROS_DEBUG("Current position: %f %f %f",global_pose_2D_.x,global_pose_2D_.y,global_pose_2D_.th);

  if (!door_functions::transformTo(tf_,global_frame_,door_msg_in,door))
  {
    return robot_actions::ABORTED;
  }
  cout  << door;

  ROS_DEBUG("Initializing planner and environment");
   
  if(!initializePlannerAndEnvironment(door))
  {
    ROS_ERROR("Door planner and environment not initialized");
    return robot_actions::ABORTED;
  }

  ROS_DEBUG("Door planner and environment initialized");
  goal_.x = (door.frame_p1.x+door.frame_p2.x)/2.0;
  goal_.y = (door.frame_p1.y+door.frame_p2.y)/2.0;
//  goal_.th = atan2(door.normal.y,door.normal.x);
  KDL::Vector door_normal = getFrameNormal(door);
  goal_.th = atan2(door_normal(1),door_normal(0));

  ROS_DEBUG("Goal: %f %f %f",goal_.x,goal_.y,goal_.th);
  if(!isPreemptRequested())
  {
    if(!makePlan(global_pose_2D_, goal_, path))
    {
      return robot_actions::ABORTED;      
    }
    else
    {
      ROS_INFO("Found solution");
    }
  }

  if(!isPreemptRequested())
    publishPath(path,"global_plan",0,1,0,0);
  handle_hinge_distance_ = getHandleHingeDistance(door);

  int plan_count = 0;
  ros::Duration d; // allow the publisher to publish
  d.fromSec(0.15);
  while(!isPreemptRequested() && plan_count < (int) path.get_points_size())
  {
    dispatchControl(path,door,plan_count);
    plan_count++;
    d.sleep();
  }
  animate(path);
  return robot_actions::SUCCESS;
}

void SBPLDoorPlanner::dispatchControl(const robot_msgs::JointTraj &path, const door_msgs::Door &door, int index)
{
  robot_msgs::JointTraj base_plan;
  robot_msgs::JointTrajPoint path_point;
  path_point.positions.push_back(path.points[index].positions[0]);
  path_point.positions.push_back(path.points[index].positions[1]);
  path_point.positions.push_back(path.points[index].positions[2]);
  base_plan.points.push_back(path_point);    

  tf::Stamped<tf::Pose> gripper_pose = getGlobalHandlePosition(door,path.points[index].positions[3]);
  robot_msgs::PoseStamped gripper_msg;
  gripper_pose.stamp_ = Time::now();
  PoseStampedTFToMsg(gripper_pose, gripper_msg);

  ros_node_.publish(base_control_topic_name_,base_plan);
  ros_node_.publish(arm_control_topic_name_,gripper_msg);
}

void SBPLDoorPlanner::animate(const robot_msgs::JointTraj &path)
{
  if(!animate_)
  {
    return;
  }
  for(int i=0; i < (int) path.get_points_size(); i++)
  {
    pr2_robot_actions::Pose2D draw;
    draw.x = path.points[i].positions[0];
    draw.y = path.points[i].positions[1];
    draw.th = path.points[i].positions[2];
    publishFootprint(draw,"~robot_footprint");
    publishDoor(door_env_.door,path.points[i].positions[3]);
    sleep(1.0);
  }
}

tf::Stamped<tf::Pose> SBPLDoorPlanner::getGlobalHandlePosition(const door_msgs::Door &door_in, const double &angle)
{
  door_msgs::Door door = door_in;
  door.header.stamp = ros::Time::now();
  tf::Stamped<tf::Pose> handle_pose = getGripperPose(door,angle,handle_hinge_distance_);
  return handle_pose;
}

double SBPLDoorPlanner::getHandleHingeDistance(const door_msgs::Door &door)
{
  robot_msgs::Point32 hinge;
  if(door.hinge == door.HINGE_P1)
    hinge = door.frame_p1;
  else
    hinge = door.frame_p2;
  double result = sqrt((hinge.x-door.handle.x)*(hinge.x-door.handle.x)+(hinge.y-door.handle.y)*(hinge.y-door.handle.y));
  return result;
}

void SBPLDoorPlanner::publishPath(const robot_msgs::JointTraj &path, std::string topic, double r, double g, double b, double a)
{
  visualization_msgs::Polyline gui_path_msg;
  gui_path_msg.header.frame_id = global_frame_;

  //given an empty path we won't do anything
  if(path.get_points_size() > 0){
    // Extract the plan in world co-ordinates, we assume the path is all in the same frame
    gui_path_msg.header.stamp = door_env_.door.header.stamp;
    gui_path_msg.set_points_size(path.get_points_size());
    for(unsigned int i=0; i < path.get_points_size(); i++){
      gui_path_msg.points[i].x = path.points[i].positions[0];
      gui_path_msg.points[i].y = path.points[i].positions[1];
      gui_path_msg.points[i].z = path.points[i].positions[2];
    }
  }

  gui_path_msg.color.r = r;
  gui_path_msg.color.g = g;
  gui_path_msg.color.b = b;
  gui_path_msg.color.a = a;

  ros_node_.publish("~" + topic, gui_path_msg);
}

bool SBPLDoorPlanner::updateGlobalPose()
{
  tf::Stamped<tf::Pose> robot_pose;
  robot_pose.setIdentity();
  robot_pose.frame_id_ = robot_base_frame_;
  robot_pose.stamp_ = ros::Time();

  try
  {
    tf_.transformPose(global_frame_, robot_pose, global_pose_);
  }
  catch(tf::LookupException& ex) {
    ROS_ERROR("No Transform available Error: %s\n", ex.what());
    return false;
  }
  catch(tf::ConnectivityException& ex) {
    ROS_ERROR("Connectivity Error: %s\n", ex.what());
    return false;
  }
  catch(tf::ExtrapolationException& ex) {
    ROS_ERROR("Extrapolation Error: %s\n", ex.what());
    return false;
  }
  global_pose_2D_ = getPose2D(global_pose_);
  return true;
}

pr2_robot_actions::Pose2D SBPLDoorPlanner::getPose2D(const tf::Stamped<tf::Pose> &pose)
{
  pr2_robot_actions::Pose2D tmp_pose;
  double useless_pitch, useless_roll, yaw;
  pose.getBasis().getEulerZYX(yaw, useless_pitch, useless_roll);
  tmp_pose.x = pose.getOrigin().x();
  tmp_pose.y = pose.getOrigin().y();
  tmp_pose.th = yaw;
  return tmp_pose;
}

bool SBPLDoorPlanner::computeOrientedFootprint(const pr2_robot_actions::Pose2D &position, const std::vector<robot_msgs::Point>& footprint_spec, std::vector<robot_msgs::Point>& oriented_footprint)
{
  if(footprint_spec.size() < 3)//if we have no footprint... do nothing
  {
    ROS_ERROR("No footprint available");
    return false;
  }
  oriented_footprint.clear();
  double cos_th = cos(position.th);
  double sin_th = sin(position.th);
  for(unsigned int i = 0; i < footprint_spec.size(); ++i) //build the oriented footprint
  {
    robot_msgs::Point new_pt;
    new_pt.x = position.x + (footprint_spec[i].x * cos_th - footprint_spec[i].y * sin_th);
    new_pt.y = position.y + (footprint_spec[i].x * sin_th + footprint_spec[i].y * cos_th);
    oriented_footprint.push_back(new_pt);
  }
  return true;
}

bool SBPLDoorPlanner::clearRobotFootprint(costmap_2d::Costmap2D& cost_map)
{
  std::vector<robot_msgs::Point> oriented_footprint;
  computeOrientedFootprint(global_pose_2D_,footprint_,oriented_footprint);

  //set the associated costs in the cost map to be free
  if(!cost_map_.setConvexPolygonCost(oriented_footprint, costmap_2d::FREE_SPACE))
  {
    ROS_INFO("Could not clear footprint");
    return false;
  }
  double max_inflation_dist = inflation_radius_ + inscribed_radius_;
  //make sure to re-inflate obstacles in the affected region
  cost_map.reinflateWindow(global_pose_2D_.x, global_pose_2D_.y, max_inflation_dist, max_inflation_dist);
  return true;
}


void SBPLDoorPlanner::publishFootprint(const pr2_robot_actions::Pose2D &position, std::string topic)
{
  std::vector<robot_msgs::Point> oriented_footprint;
  computeOrientedFootprint(position,footprint_,oriented_footprint);

  visualization_msgs::Polyline robot_polygon;
  robot_polygon.header.frame_id = global_frame_;
  robot_polygon.header.stamp = door_env_.door.header.stamp;
  robot_polygon.set_points_size(2*footprint_.size());

  for(int i=0; i < (int) oriented_footprint.size(); ++i)
  {
    int index_2 = (i+1)%((int)oriented_footprint.size());
    robot_polygon.points[2*i].x = oriented_footprint[i].x;
    robot_polygon.points[2*i].y = oriented_footprint[i].y;
    robot_polygon.points[2*i].z = oriented_footprint[i].z;

    robot_polygon.points[2*i+1].x = oriented_footprint[index_2].x;
    robot_polygon.points[2*i+1].y = oriented_footprint[index_2].y;
    robot_polygon.points[2*i+1].z = oriented_footprint[index_2].z;
  }
  robot_polygon.color.r = 0.5;
  robot_polygon.color.g = 0.5;
  robot_polygon.color.b = 0;
  robot_polygon.color.a = 0;

  ros_node_.publish(topic, robot_polygon);
}


void SBPLDoorPlanner::publishDoor(const door_msgs::Door &door_in, const double &angle)
{
  door_msgs::Door door = rotateDoor(door_in,angles::normalize_angle(angle-getFrameAngle(door_in)));

  visualization_msgs::Polyline marker_door;
  marker_door.header.frame_id = global_frame_;
  marker_door.header.stamp = ros::Time();
  marker_door.set_points_size(2);

  marker_door.points[0].x = door.door_p1.x;
  marker_door.points[0].y = door.door_p1.y;
  marker_door.points[0].z = door.door_p1.z;

  marker_door.points[1].x = door.door_p2.x;
  marker_door.points[1].y = door.door_p2.y;
  marker_door.points[1].z = door.door_p2.z;

  visualization_msgs::Polyline marker_frame;
  marker_frame.header.frame_id = global_frame_;
  marker_frame.header.stamp =  ros::Time();
  marker_frame.set_points_size(2);

  marker_frame.points[0].x = door.frame_p1.x;
  marker_frame.points[0].y = door.frame_p1.y;
  marker_frame.points[0].z = door.frame_p1.z;

  marker_frame.points[1].x = door.frame_p2.x;
  marker_frame.points[1].y = door.frame_p2.y;
  marker_frame.points[1].z = door.frame_p2.z;

  marker_door.color.r = 0;
  marker_door.color.g = 0;
  marker_door.color.b = 1;
  marker_door.color.a = 0;

  marker_frame.color.r = 1;
  marker_frame.color.g = 0;
  marker_frame.color.b = 0;
  marker_frame.color.a = 0;

  visualization_msgs::Marker marker;
  marker.header.frame_id = global_frame_;
  marker.header.stamp = ros::Time();
  marker.ns = "~";
  marker.id = 0;
  marker.type = visualization_msgs::Marker::SPHERE;
  marker.action = visualization_msgs::Marker::ADD;
  marker.pose.position.x = door.handle.x;
  marker.pose.position.y = door.handle.y;
  marker.pose.position.z = 0.5;
  marker.pose.orientation.x = 0.0;
  marker.pose.orientation.y = 0.0;
  marker.pose.orientation.z = 0.0;
  marker.pose.orientation.w = 1.0;
  marker.scale.x = 0.03;
  marker.scale.y = 0.03;
  marker.scale.z = 0.03;
  marker.color.a = 1.0;
  marker.color.r = 1.0;
  marker.color.g = 1.0;
  marker.color.b = 1.0;

  ros_node_.publish( "visualization_marker", marker );
  ros_node_.publish("~door/frame", marker_frame);
  ros_node_.publish("~door/door", marker_door);
}

