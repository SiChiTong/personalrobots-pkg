/*********************************************************************
*
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
*
* Author: Eitan Marder-Eppstein
*********************************************************************/
#include <teleop_anti_collision/safe_teleop.h>
#include <cstdlib>
#include <ctime>

namespace safe_teleop{

  SafeTeleop::SafeTeleop(std::string name, tf::TransformListener& tf) :
    tf_(tf),
    as_(ros::NodeHandle(), name, boost::bind(&SafeTeleop::executeCb, this, _1)),
    tc_(NULL), planner_costmap_ros_(NULL), controller_costmap_ros_(NULL),
    planner_(NULL), bgp_loader_("nav_core", "nav_core::BaseGlobalPlanner"),
    blp_loader_("nav_core", "nav_core::BaseLocalPlanner"){

    //get some parameters that will be global to the move base node
    std::string global_planner, local_planner;
    ros_node_.param("~base_global_planner", global_planner, std::string("NavfnROS"));
    ros_node_.param("~base_local_planner", local_planner, std::string("TrajectoryPlannerROS"));
    ros_node_.param("~global_costmap/robot_base_frame", robot_base_frame_, std::string("base_link"));
    ros_node_.param("~global_costmap/global_frame", global_frame_, std::string("/map"));
    ros_node_.param("~controller_frequency", controller_frequency_, 20.0);
    ros_node_.param("~planner_patience", planner_patience_, 5.0);
    ros_node_.param("~controller_patience", controller_patience_, 15.0);

    //for comanding the base
    vel_pub_ = ros_node_.advertise<geometry_msgs::Twist>("cmd_vel", 1);
    vis_pub_ = ros_node_.advertise<visualization_msgs::Marker>( "visualization_marker", 0 );
    position_pub_ = ros_node_.advertise<geometry_msgs::PoseStamped>("~current_position", 1);

    ros::NodeHandle action_node(ros_node_, name);
    action_goal_pub_ = action_node.advertise<move_base_msgs::MoveBaseActionGoal>("goal", 1);

    //we'll provide a mechanism for some people to send goals as PoseStamped messages over a topic
    //they won't get any useful information back about its status, but this is useful for tools
    //like nav_view and rviz
    goal_sub_ = ros_node_.subscribe<geometry_msgs::PoseStamped>("~activate", 1, boost::bind(&SafeTeleop::goalCB, this, _1));

    //we'll assume the radius of the robot to be consistent with what's specified for the costmaps
    ros_node_.param("~local_costmap/inscribed_radius", inscribed_radius_, 0.325);
    ros_node_.param("~local_costmap/circumscribed_radius", circumscribed_radius_, 0.46);
    ros_node_.param("~clearing_radius", clearing_radius_, circumscribed_radius_);
    ros_node_.param("~conservative_reset_dist", conservative_reset_dist_, 3.0);

    ros_node_.param("~shutdown_costmaps", shutdown_costmaps_, false);

    //create the ros wrapper for the planner's costmap... and initializer a pointer we'll use with the underlying map
    planner_costmap_ros_ = new costmap_2d::Costmap2DROS("global_costmap", tf_);

    //initialize the global planner
    try {
      planner_ = bgp_loader_.createClassInstance(global_planner);
      planner_->initialize(global_planner, planner_costmap_ros_);
    } catch (const std::runtime_error& ex)
    {
      ROS_FATAL("Failed to create the %s planner, are you sure it is properly registered and that the containing library is built? Exception: %s", global_planner.c_str(), ex.what());
      exit(0);
    }

    ROS_INFO("MAP SIZE: %d, %d", planner_costmap_ros_->getSizeInCellsX(), planner_costmap_ros_->getSizeInCellsY());

    //create the ros wrapper for the controller's costmap... and initializer a pointer we'll use with the underlying map
    controller_costmap_ros_ = new costmap_2d::Costmap2DROS("local_costmap", tf_);

    //create a local planner
    try {
      tc_ = blp_loader_.createClassInstance(local_planner);
      tc_->initialize(local_planner, &tf_, controller_costmap_ros_);
    } catch (const std::runtime_error& ex)
    {
      ROS_FATAL("Failed to create the %s planner, are you sure it is properly registered and that the containing library is built? Exception: %s", local_planner.c_str(), ex.what());
      exit(0);
    }


    //initially clear any unknown space around the robot
    planner_costmap_ros_->clearNonLethalWindow(circumscribed_radius_ * 2, circumscribed_radius_ * 2);
    controller_costmap_ros_->clearNonLethalWindow(circumscribed_radius_ * 2, circumscribed_radius_ * 2);

    //if we shutdown our costmaps when we're deactivated... we'll do that now
    if(shutdown_costmaps_){
      ROS_DEBUG("Stopping costmaps initially");
      planner_costmap_ros_->stop();
      controller_costmap_ros_->stop();
    }

    //initially, we'll need to make a plan
    state_ = PLANNING;

    //the initial clearing state will be to conservatively clear the costmaps
    clearing_state_ = CONSERVATIVE_RESET;

    planner_costmap_ros_->getCostmapCopy(costmap_);
    world_model_ = new base_local_planner::CostmapModel(costmap_); 
    //we'll get the parameters for the robot radius from the costmap we're associated with
    inscribed_radius_ = planner_costmap_ros_->getInscribedRadius();
    circumscribed_radius_ = planner_costmap_ros_->getCircumscribedRadius();
    footprint_spec_ = planner_costmap_ros_->getRobotFootprint();
  }

  void SafeTeleop::goalCB(const geometry_msgs::PoseStamped::ConstPtr& goal){
    ROS_DEBUG("In ROS goal callback, wrapping the PoseStamped in the action message and re-sending to the server.");
    move_base_msgs::MoveBaseActionGoal action_goal;
    action_goal.goal.target_pose = *goal;

    action_goal_pub_.publish(action_goal);
  }

  void SafeTeleop::clearCostmapWindows(double size_x, double size_y){
    tf::Stamped<tf::Pose> global_pose;

    //clear the planner's costmap
    planner_costmap_ros_->getRobotPose(global_pose);

    std::vector<geometry_msgs::Point> clear_poly;
    double x = global_pose.getOrigin().x();
    double y = global_pose.getOrigin().y();
    geometry_msgs::Point pt;

    pt.x = x - size_x / 2;
    pt.y = y - size_x / 2;
    clear_poly.push_back(pt);

    pt.x = x + size_x / 2;
    pt.y = y - size_x / 2;
    clear_poly.push_back(pt);

    pt.x = x + size_x / 2;
    pt.y = y + size_x / 2;
    clear_poly.push_back(pt);

    pt.x = x - size_x / 2;
    pt.y = y + size_x / 2;
    clear_poly.push_back(pt);

    planner_costmap_ros_->setConvexPolygonCost(clear_poly, costmap_2d::FREE_SPACE);

    //clear the controller's costmap
    controller_costmap_ros_->getRobotPose(global_pose);

    clear_poly.clear();
    x = global_pose.getOrigin().x();
    y = global_pose.getOrigin().y();

    pt.x = x - size_x / 2;
    pt.y = y - size_x / 2;
    clear_poly.push_back(pt);

    pt.x = x + size_x / 2;
    pt.y = y - size_x / 2;
    clear_poly.push_back(pt);

    pt.x = x + size_x / 2;
    pt.y = y + size_x / 2;
    clear_poly.push_back(pt);

    pt.x = x - size_x / 2;
    pt.y = y + size_x / 2;
    clear_poly.push_back(pt);

    controller_costmap_ros_->setConvexPolygonCost(clear_poly, costmap_2d::FREE_SPACE);
  }


  SafeTeleop::~SafeTeleop(){

    if(planner_ != NULL)
      delete planner_;

    if(tc_ != NULL)
      delete tc_;

    if(planner_costmap_ros_ != NULL)
      delete planner_costmap_ros_;

    if(controller_costmap_ros_ != NULL)
      delete controller_costmap_ros_;
  }

  void SafeTeleop::publishGoal(const geometry_msgs::PoseStamped& goal){
    visualization_msgs::Marker marker;
    marker.header = goal.header;
    marker.ns = "safe_teleop";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::ARROW;
    marker.pose = goal.pose;
    marker.scale.x = 0.5;
    marker.scale.y = 0.4;
    marker.scale.z = 0.4;
    marker.color.a = 1.0;
    marker.color.r = 0.0;
    marker.color.g = 1.0;
    marker.color.b = 0.0;
    vis_pub_.publish(marker);
  }

  bool SafeTeleop::makePlan(const geometry_msgs::PoseStamped& goal, std::vector<geometry_msgs::PoseStamped>& plan){
    //make sure to set the plan to be empty initially
    plan.clear();

    //since this gets called on handle activate
    if(planner_costmap_ros_ == NULL)
      return false;

    //get the starting pose of the robot
    tf::Stamped<tf::Pose> global_pose;
    if(!planner_costmap_ros_->getRobotPose(global_pose))
      return false;

    geometry_msgs::PoseStamped start;
    tf::poseStampedTFToMsg(global_pose, start);

    //if the planner fails or returns a zero length plan, planning failed
    if(!planner_->makePlan(start, goal, plan) || plan.empty()){
      ROS_DEBUG("Failed to find a  plan to point (%.2f, %.2f)", goal.pose.position.x, goal.pose.position.y);
      return false;
    }

    return true;
  }

  bool SafeTeleop::set180RotationGoal(){
    ROS_DEBUG("180 rotation");
    double angle = M_PI; //rotate 180 degrees
    tf::Stamped<tf::Pose> rotate_goal = tf::Stamped<tf::Pose>(tf::Pose(tf::Quaternion(angle, 0.0, 0.0), tf::Point(0.0, 0.0, 0.0)), ros::Time(), robot_base_frame_);
    geometry_msgs::PoseStamped rotate_goal_msg;

    try{
      tf_.transformPose(global_frame_, rotate_goal, rotate_goal);
    }
    catch(tf::TransformException& ex){
      ROS_ERROR("This tf error should never happen: %s", ex.what());
      return false;

    }

    poseStampedTFToMsg(rotate_goal, rotate_goal_msg);
    std::vector<geometry_msgs::PoseStamped> rotate_plan;
    rotate_plan.push_back(rotate_goal_msg);

    //pass the rotation goal to the controller
    if(!tc_->updatePlan(rotate_plan)){
      ROS_ERROR("Failed to pass global plan to the controller, aborting in place rotation attempt.");
      return false;
    }

    return true;
  }

  bool SafeTeleop::rotateRobot(){
      geometry_msgs::Twist cmd_vel;
      if(tc_->computeVelocityCommands(cmd_vel)){
        //make sure that we send the velocity command to the base
        vel_pub_.publish(cmd_vel);
        ROS_DEBUG("Velocity commands produced by controller: vx: %.2f, vy: %.2f, vth: %.2f", cmd_vel.linear.x, cmd_vel.linear.y, cmd_vel.angular.z);
        return true;
      }

      return false;

  }

  void SafeTeleop::publishZeroVelocity(){
    geometry_msgs::Twist cmd_vel;
    cmd_vel.linear.x = 0.0;
    cmd_vel.linear.y = 0.0;
    cmd_vel.angular.z = 0.0;
    vel_pub_.publish(cmd_vel);

  }

  geometry_msgs::PoseStamped SafeTeleop::goalToGlobalFrame(const geometry_msgs::PoseStamped& goal_pose_msg){
    std::string global_frame = planner_costmap_ros_->getGlobalFrameID();
    tf::Stamped<tf::Pose> goal_pose, global_pose;
    poseStampedMsgToTF(goal_pose_msg, goal_pose);

    //just get the latest available transform... for accuracy they should send
    //goals in the frame of the planner
    goal_pose.stamp_ = ros::Time();

    try{
      tf_.transformPose(global_frame, goal_pose, global_pose);
    }
    catch(tf::TransformException& ex){
      ROS_WARN("Failed to transform the goal pose from %s into the %s frame: %s",
          goal_pose.frame_id_.c_str(), global_frame.c_str(), ex.what());
      return goal_pose_msg;
    }

    geometry_msgs::PoseStamped global_pose_msg;
    tf::poseStampedTFToMsg(global_pose, global_pose_msg);
    return global_pose_msg;

  }

  void SafeTeleop::executeCb(const move_base_msgs::MoveBaseGoalConstPtr& move_base_goal)
  {
    geometry_msgs::PoseStamped goal = move_base_goal->target_pose;
    std::vector<geometry_msgs::PoseStamped> global_plan;

    ros::Rate r(controller_frequency_);
    if(shutdown_costmaps_){
      ROS_DEBUG("Starting up costmaps that were shut down previously");
      planner_costmap_ros_->start();
      controller_costmap_ros_->start();
    }

    while(as_.isActive())
    {
      if (!ros_node_.ok())
      {
        as_.setAborted();
        break;
      }

      if(!as_.isPreemptRequested()){
        if(as_.isNewGoalAvailable()){
          //if we're active and a new goal is available, we'll accept it, but we won't shut anything down
          goal = goalToGlobalFrame((*as_.acceptNewGoal()).target_pose);

          tf::Stamped<tf::Pose> global_pose;
          planner_costmap_ros_->getRobotPose(global_pose);
          geometry_msgs::PoseStamped valid_goal,start;
          tf::poseStampedTFToMsg(global_pose,start);
          if(!findSafeGoal(start,goal,valid_goal))
          {
            ROS_WARN("Could not find safe goal, staying at current position");
          }
          else
          {
            goal = valid_goal;
          }

          //we'll make sure that we reset our state for the next execution cycle
          clearing_state_ = CONSERVATIVE_RESET;
          state_ = PLANNING;

          //publish the goal point to the visualizer
          ROS_DEBUG("move_base has received a goal of x: %.2f, y: %.2f", goal.pose.position.x, goal.pose.position.y);
          publishGoal(goal);

          //make sure to reset our timeouts
          last_valid_control_ = ros::Time::now();
          last_valid_plan_ = ros::Time::now();
        }
        //for timing that gives real time even in simulation
        struct timeval start, end;
        double start_t, end_t, t_diff;
        gettimeofday(&start, NULL);

        //the real work on pursuing a goal is done here
        executeCycle(goal, global_plan);

        gettimeofday(&end, NULL);
        start_t = start.tv_sec + double(start.tv_usec) / 1e6;
        end_t = end.tv_sec + double(end.tv_usec) / 1e6;
        t_diff = end_t - start_t;
        ROS_DEBUG("Full control cycle time: %.9f\n", t_diff);
      }
      else{
        //if we've been preempted explicitly we need to shut things down
        resetState();

        //notify the ActionServer that we've successfully preemted
        ROS_DEBUG("Move base preempting the current goal");
        as_.setPreempted();
      }

      //make sure to sleep for the remainder of our cycle time
      if(!r.sleep() && state_ == CONTROLLING)
        ROS_WARN("Control loop missed its desired rate of %.4fHz... the loop actually took %.4f seconds", controller_frequency_, r.cycleTime().toSec());
    }
  }

  void SafeTeleop::executeCycle(geometry_msgs::PoseStamped& goal, std::vector<geometry_msgs::PoseStamped>& global_plan){
    //we need to be able to publish velocity commands
    geometry_msgs::Twist cmd_vel;

    //update feedback to correspond to our curent position
    tf::Stamped<tf::Pose> global_pose;
    planner_costmap_ros_->getRobotPose(global_pose);
    geometry_msgs::PoseStamped current_position;
    tf::poseStampedTFToMsg(global_pose, current_position);

    //push the feedback out
    position_pub_.publish(current_position);

    //check that the observation buffers for the costmap are current, we don't want to drive blind
    if(!controller_costmap_ros_->isCurrent()){
      ROS_WARN("[%s]:Sensor data is out of date, we're not going to allow commanding of the base for safety",ros::this_node::getName().c_str());
      publishZeroVelocity();
      return;
    }

    //the move_base state machine, handles the control logic for navigation
    switch(state_){
      //if we are in a planning state, then we'll attempt to make a plan
      case PLANNING:
        ROS_DEBUG("In planning state");
        if(makePlan(goal, global_plan)){
          if(!tc_->updatePlan(global_plan)){
            //ABORT and SHUTDOWN COSTMAPS
            ROS_ERROR("Failed to pass global plan to the controller, aborting.");
            resetState();
            as_.setAborted();
            return;
          }
          ROS_DEBUG("Generated a plan from the base_global_planner");
          last_valid_plan_ = ros::Time::now();
          state_ = CONTROLLING;

          //make sure to reset clearing state since we were able to find a valid plan
          clearing_state_ = CONSERVATIVE_RESET;

        }
        else{
          ros::Time attempt_end = last_valid_plan_ + ros::Duration(planner_patience_);

          //check if we've tried to make a plan for over our time limit
          if(ros::Time::now() > attempt_end){
            //we'll move into our obstacle clearing mode
            state_ = CLEARING;
            publishZeroVelocity();
          }

          //we don't want to attempt to control if planning failed
          break;
        }

      //if we're controlling, we'll attempt to find valid velocity commands
      case CONTROLLING:
        ROS_DEBUG("In controlling state");

        //check to see if we've reached our goal
        if(tc_->goalReached()){
          ROS_DEBUG("Goal reached!");
          resetState();
          as_.setSucceeded();
          return;
        }

        if(tc_->computeVelocityCommands(cmd_vel)){
          last_valid_control_ = ros::Time::now();
          //make sure that we send the velocity command to the base
          vel_pub_.publish(cmd_vel);
        }
        else {
          ros::Time attempt_end = last_valid_control_ + ros::Duration(controller_patience_);

          //check if we've tried to find a valid control for longer than our time limit
          if(ros::Time::now() > attempt_end){
            ROS_ERROR("Aborting because of failure to find a valid control for %.2f seconds", controller_patience_);
            resetState();
            as_.setAborted();
            return;
          }

          //otherwise, if we can't find a valid control, we'll go back to planning
          last_valid_plan_ = ros::Time::now();
          state_ = PLANNING;
          publishZeroVelocity();
        }

        break;

        //we'll try to clear out space with the following actions
      case CLEARING:
        switch(clearing_state_){
          //first, we'll try resetting the costmaps conservatively to see if we find a plan
          case CONSERVATIVE_RESET:
            ROS_DEBUG("In conservative reset state");
            resetCostmaps(conservative_reset_dist_, conservative_reset_dist_);
            clearing_state_ = IN_PLACE_ROTATION_1;
            state_ = PLANNING;
            break;
            //next, we'll try an in-place rotation to try to clear out space
          case IN_PLACE_ROTATION_1:
            //we need to set the rotation goal for the robot
            if(set180RotationGoal()){
              clearing_state_ = EXECUTE_ROTATE_1;
            }
            else{
              clearing_state_ = AGGRESSIVE_RESET;
              state_ = PLANNING;
            }
            break;
          case EXECUTE_ROTATE_1:
            ROS_DEBUG("In in-place rotation state 1");
            if(tc_->goalReached()){
              clearing_state_ = IN_PLACE_ROTATION_2;
            }
            else if(!rotateRobot()){
              clearing_state_ = AGGRESSIVE_RESET;
              state_ = PLANNING;
            }
            break;
          case IN_PLACE_ROTATION_2:
            //we need to set the rotation goal for the robot
            if(set180RotationGoal()){
              clearing_state_ = EXECUTE_ROTATE_2;
            }
            else{
              clearing_state_ = AGGRESSIVE_RESET;
              state_ = PLANNING;
            }
            break;
          case EXECUTE_ROTATE_2:
            ROS_DEBUG("In in-place rotation state 2");
            if(tc_->goalReached() || !rotateRobot()){
              clearing_state_ = AGGRESSIVE_RESET;
              state_ = PLANNING;
            }
            break;
            //finally, we'll try resetting the costmaps aggresively to clear out space
          case AGGRESSIVE_RESET:
            ROS_DEBUG("In aggressive reset state");
            resetCostmaps(circumscribed_radius_ * 2, circumscribed_radius_ * 2);
            clearing_state_ = ABORT;
            state_ = PLANNING;
            break;
            //if all of the above fail, we can't drive safely, so we'll abort
          case ABORT:
            ROS_ERROR("Aborting because a valid plan could not be found. Even after attempting to reset costmaps and rotating in place");
            resetState();
            as_.setAborted();
            return;
          default:
            ROS_ERROR("This case should never be reached, something is wrong, aborting");
            resetState();
            as_.setAborted();
            return;
        }
        break;
      default:
        ROS_ERROR("This case should never be reached, something is wrong, aborting");
        resetState();
        as_.setAborted();
        return;
    }



  }

  void SafeTeleop::resetState(){
    state_ = PLANNING;
    clearing_state_ = CONSERVATIVE_RESET;
    publishZeroVelocity();

    //if we shutdown our costmaps when we're deactivated... we'll do that now
    if(shutdown_costmaps_){
      ROS_DEBUG("Stopping costmaps");
      planner_costmap_ros_->stop();
      controller_costmap_ros_->stop();
    }
  }

  void SafeTeleop::resetCostmaps(double size_x, double size_y){
    planner_costmap_ros_->resetMapOutsideWindow(size_x, size_y);
    controller_costmap_ros_->resetMapOutsideWindow(size_x, size_y);
  }


  bool SafeTeleop::findSafeGoal(const geometry_msgs::PoseStamped& start, 
                                const geometry_msgs::PoseStamped& goal, geometry_msgs::PoseStamped& valid_goal)
  {
    ROS_DEBUG("Got a start: %.2f, %.2f, and a goal: %.2f, %.2f", start.pose.position.x, start.pose.position.y, goal.pose.position.x, goal.pose.position.y);
    planner_costmap_ros_->getCostmapCopy(costmap_);
    if(goal.header.frame_id != planner_costmap_ros_->getGlobalFrameID()){
      ROS_ERROR("This planner as configured will only accept goals in the %s frame, but a goal was sent in the %s frame.", 
                planner_costmap_ros_->getGlobalFrameID().c_str(), goal.header.frame_id.c_str());
      return false;
    }

    tf::Stamped<tf::Pose> goal_tf;
    tf::Stamped<tf::Pose> start_tf;

    poseStampedMsgToTF(goal,goal_tf);
    poseStampedMsgToTF(start,start_tf);

    double useless_pitch, useless_roll, goal_yaw, start_yaw;
    start_tf.getBasis().getEulerZYX(start_yaw, useless_pitch, useless_roll);
    goal_tf.getBasis().getEulerZYX(goal_yaw, useless_pitch, useless_roll);

    //we want to step back along the vector created by the robot's position and the goal pose until we find a legal cell
    double goal_x = goal.pose.position.x;
    double goal_y = goal.pose.position.y;
    double start_x = start.pose.position.x;
    double start_y = start.pose.position.y;

    double diff_x = goal_x - start_x;
    double diff_y = goal_y - start_y;
    double diff_yaw = angles::normalize_angle(goal_yaw-start_yaw);

    double target_x = goal_x;
    double target_y = goal_y;
    double target_yaw = goal_yaw;

    bool done = false;
    double scale = 1.0;
    double dScale = 0.01;

    while(!done)
    {
      if(scale < 0)
      {
        target_x = start_x;
        target_y = start_y;
        target_yaw = start_yaw;
        ROS_WARN("The carrot planner could not find a valid plan for this goal");
        break;
      }
      target_x = start_x + scale * diff_x;
      target_y = start_y + scale * diff_y;
      target_yaw = angles::normalize_angle(start_yaw + scale * diff_yaw);
      
      double footprint_cost = footprintCost(target_x, target_y, target_yaw);
      if(footprint_cost >= 0)
      {
        done = true;
      }
      scale -=dScale;
    }
    ROS_DEBUG("Planning to location: %f, %f, Start: %f, %f to Desired goal: %f, %f", target_x, target_y, start.pose.position.x, start.pose.position.y, goal.pose.position.x, goal.pose.position.y);

    geometry_msgs::PoseStamped new_goal = goal;
    tf::Quaternion goal_quat = tf::Quaternion(target_yaw,0,0);

    new_goal.pose.position.x = target_x;
    new_goal.pose.position.y = target_y;

    new_goal.pose.orientation.x = goal_quat.x();
    new_goal.pose.orientation.y = goal_quat.y();
    new_goal.pose.orientation.z = goal_quat.z();
    new_goal.pose.orientation.w = goal_quat.w();

    valid_goal = new_goal;

    if(done)
      ROS_INFO("New goal: (x,y): (%f %f), yaw: %f",target_x,target_y,target_yaw);

    return (done);
  }

  //we need to take the footprint of the robot into account when we calculate cost to obstacles
  double SafeTeleop::footprintCost(double x_i, double y_i, double theta_i)
  {
    //if we have no footprint... do nothing
    if(footprint_spec_.size() < 3)
      return -1.0;

    //build the oriented footprint
    double cos_th = cos(theta_i);
    double sin_th = sin(theta_i);
    std::vector<geometry_msgs::Point> oriented_footprint;
    for(unsigned int i = 0; i < footprint_spec_.size(); ++i){
      geometry_msgs::Point new_pt;
      new_pt.x = x_i + (footprint_spec_[i].x * cos_th - footprint_spec_[i].y * sin_th);
      new_pt.y = y_i + (footprint_spec_[i].x * sin_th + footprint_spec_[i].y * cos_th);
      oriented_footprint.push_back(new_pt);
    }

    geometry_msgs::Point robot_position;
    robot_position.x = x_i;
    robot_position.y = y_i;

    //check if the footprint is legal
    double footprint_cost = world_model_->footprintCost(robot_position, oriented_footprint, inscribed_radius_, circumscribed_radius_);
    return footprint_cost;
  }

};

int main(int argc, char** argv){
  ros::init(argc, argv, "move_base");
  tf::TransformListener tf(ros::Duration(10));
  safe_teleop::SafeTeleop safe_teleop(ros::this_node::getName(), tf);

  //ros::MultiThreadedSpinner s;
  ros::spin();

  return(0);
}
