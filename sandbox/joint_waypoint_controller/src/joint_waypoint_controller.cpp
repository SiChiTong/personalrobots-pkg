/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2009, Willow Garage, Inc.
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

/** \author Mrinal Kalakrishnan */

#include <joint_waypoint_controller/joint_waypoint_controller.h>
#include <manipulation_srvs/SetSplineTraj.h>
#include <manipulation_srvs/QuerySplineTraj.h>
#include <manipulation_srvs/CancelSplineTraj.h>
#include <manipulation_msgs/WaypointTrajWithLimits.h>
#include <spline_smoother/splines.h>
#include <algorithm>
#include <string>
#include <limits>

namespace joint_waypoint_controller
{

JointWaypointController::JointWaypointController():
  filter_chain_("manipulation_msgs::WaypointTrajWithLimits")
{

}

JointWaypointController::~JointWaypointController()
{

}

bool JointWaypointController::init()
{
  // get some params:
  std::string trajectory_type_str;
  node_handle_.param("~spline_controller_prefix", spline_controller_prefix_, std::string(""));
  node_handle_.param("~trajectory_type", trajectory_type_str, std::string("cubic"));

  // convert it to lower case:
  std::transform(trajectory_type_str.begin(), trajectory_type_str.end(), trajectory_type_str.begin(), ::tolower);
  if (trajectory_type_str=="cubic")
  {
    trajectory_type_ = TRAJECTORY_TYPE_CUBIC;
  }
  else if (trajectory_type_str=="quintic")
  {
    trajectory_type_ = TRAJECTORY_TYPE_QUINTIC;
  }
  else
  {
    ROS_ERROR("Invalid trajectory type %s", trajectory_type_str.c_str());
    return false;
  }

  // initialize the filter chain:
  XmlRpc::XmlRpcValue filter_config;
  if(!node_handle_.getParam("~filter_chain", filter_config))
  {
    ROS_ERROR("Could not load the filter configuration, are you sure it was pushed to the parameter server?");
    return false;
  }
  if (!filter_chain_.configure(filter_config))
    return false;

  // create service clients:
  set_spline_traj_client_ = node_handle_.serviceClient<manipulation_srvs::SetSplineTraj>(spline_controller_prefix_+"SetSplineTrajectory", false);
  query_spline_traj_client_ = node_handle_.serviceClient<manipulation_srvs::QuerySplineTraj>(spline_controller_prefix_+"QuerySplineTrajectory", false);
  cancel_spline_traj_client_ = node_handle_.serviceClient<manipulation_srvs::CancelSplineTraj>(spline_controller_prefix_+"CancelSplineTrajectory", false);

  // advertise services:
  trajectory_start_server_ = node_handle_.advertiseService("~TrajectoryStart", &JointWaypointController::trajectoryStart, this);
  trajectory_query_server_ = node_handle_.advertiseService("~TrajectoryQuery", &JointWaypointController::trajectoryQuery, this);
  trajectory_cancel_server_ = node_handle_.advertiseService("~TrajectoryCancel", &JointWaypointController::trajectoryCancel, this);

  return true;
}

int JointWaypointController::run()
{
  ros::spin();
  return 0;
}

bool JointWaypointController::trajectoryStart(experimental_controllers::TrajectoryStart::Request &req,
                                            experimental_controllers::TrajectoryStart::Response &resp)
{
  // first convert the input into a "WaypointTrajWithLimits" message
  manipulation_msgs::WaypointTrajWithLimits trajectory;
  manipulation_msgs::WaypointTrajWithLimits trajectory_out;
  jointTrajToWaypointTraj(req.traj, trajectory);

  // run the filters on it:
  if (!filter_chain_.update(trajectory, trajectory_out))
  {
    ROS_ERROR("Filter chain failed to process trajectory");
    return false;
  }

  // then convert it into splines:
  manipulation_srvs::SetSplineTraj::Request sreq;
  manipulation_srvs::SetSplineTraj::Response sresp;
  waypointTrajToSplineTraj(trajectory_out, sreq.spline);

  // make the request
  if (!set_spline_traj_client_.call(sreq, sresp))
    return false;

  // copy the response back:
  resp.trajectoryid = sresp.trajectory_id;

  return true;
}

bool JointWaypointController::trajectoryQuery(experimental_controllers::TrajectoryQuery::Request &req,
                                              experimental_controllers::TrajectoryQuery::Response &resp)
{
  manipulation_srvs::QuerySplineTraj::Request sreq;
  manipulation_srvs::QuerySplineTraj::Response sresp;

  // copy the request
  sreq.trajectory_id = req.trajectoryid;

  // call the service
  bool result = query_spline_traj_client_.call(sreq, sresp);

  // copy the response and return
  resp.done = sresp.trajectory_status;
  resp.jointnames = sresp.joint_names;
  resp.jointpositions = sresp.joint_positions;
  resp.trajectorytime = sresp.trajectory_time;

  return result;
}

bool JointWaypointController::trajectoryCancel(experimental_controllers::TrajectoryCancel::Request &req,
                                               experimental_controllers::TrajectoryCancel::Response &resp)
{
  manipulation_srvs::CancelSplineTraj::Request sreq;
  manipulation_srvs::CancelSplineTraj::Response sresp;

  // copy the request
  sreq.trajectory_id = req.trajectoryid;

  // call the service
  bool result = cancel_spline_traj_client_.call(sreq, sresp);

  // copy the response and return

  return result;
}

void JointWaypointController::jointTrajToWaypointTraj(const manipulation_msgs::JointTraj& joint_traj, manipulation_msgs::WaypointTrajWithLimits& waypoint_traj)
{
  waypoint_traj.names = joint_traj.names;
  int size = joint_traj.points.size();
  int num_joints = joint_traj.names.size();

  waypoint_traj.points.resize(size);
  for (int i=0; i<size; ++i)
  {
    waypoint_traj.points[i].positions = joint_traj.points[i].positions;
    waypoint_traj.points[i].time = joint_traj.points[i].time;
    waypoint_traj.points[i].velocities.resize(num_joints, 0.0);
    waypoint_traj.points[i].accelerations.resize(num_joints, 0.0);
  }

  // fill in the joint limits in a "lazy loading" fashion:
  waypoint_traj.limits.resize(num_joints);
  for (int i=0; i<num_joints; ++i)
  {
    std::map<std::string, manipulation_msgs::Limits>::const_iterator limit_it = joint_limits_.find(joint_traj.names[i]);
    manipulation_msgs::Limits limits;
    if (limit_it == joint_limits_.end())
    {
      // load the limits from the param server
      node_handle_.param("~joint_limits/"+joint_traj.names[i]+"/min_position", limits.min_position, -std::numeric_limits<double>::max());
      node_handle_.param("~joint_limits/"+joint_traj.names[i]+"/max_position", limits.max_position, std::numeric_limits<double>::max());
      node_handle_.param("~joint_limits/"+joint_traj.names[i]+"/max_velocity", limits.max_velocity, std::numeric_limits<double>::max());
      bool boolean;
      node_handle_.param("~joint_limits/"+joint_traj.names[i]+"/has_position_limits", boolean, false);
      limits.has_position_limits = boolean?1:0;
      node_handle_.param("~joint_limits/"+joint_traj.names[i]+"/has_velocity_limits", boolean, false);
      limits.has_velocity_limits = boolean?1:0;
      node_handle_.param("~joint_limits/"+joint_traj.names[i]+"/angle_wraparound", boolean, false);
      limits.angle_wraparound = boolean?1:0;
      joint_limits_.insert(make_pair(joint_traj.names[i], limits));
    }
    else
    {
      limits = limit_it->second;
    }
    waypoint_traj.limits[i] = limits;
  }
}

void JointWaypointController::waypointTrajToSplineTraj(const manipulation_msgs::WaypointTrajWithLimits& waypoint_traj, manipulation_msgs::SplineTraj& spline_traj) const
{
  spline_traj.names = waypoint_traj.names;
  int num_joints = waypoint_traj.names.size();

  int num_points = waypoint_traj.points.size();
  int num_segments = num_points-1;

  std::vector<double> coeffs(6);

  spline_traj.segments.resize(num_segments);
  for (int i=0; i<num_segments; i++)
  {
    double duration = waypoint_traj.points[i+1].time - waypoint_traj.points[i].time;
    spline_traj.segments[i].duration = ros::Duration(duration);

    spline_traj.segments[i].a.resize(num_joints);
    spline_traj.segments[i].b.resize(num_joints);
    spline_traj.segments[i].c.resize(num_joints);
    spline_traj.segments[i].d.resize(num_joints);
    spline_traj.segments[i].e.resize(num_joints);
    spline_traj.segments[i].f.resize(num_joints);

    // for each joint, get the spline coefficients:
    for (int j=0; j<num_joints; j++)
    {
      if (trajectory_type_ == TRAJECTORY_TYPE_QUINTIC)
      {
        spline_smoother::getQuinticSplineCoefficients(
            waypoint_traj.points[i].positions[j],
            waypoint_traj.points[i].velocities[j],
            waypoint_traj.points[i].accelerations[j],
            waypoint_traj.points[i+1].positions[j],
            waypoint_traj.points[i+1].velocities[j],
            waypoint_traj.points[i+1].accelerations[j],
            duration,
            coeffs);

        spline_traj.segments[i].a[j] = coeffs[0];
        spline_traj.segments[i].b[j] = coeffs[1];
        spline_traj.segments[i].c[j] = coeffs[2];
        spline_traj.segments[i].d[j] = coeffs[3];
        spline_traj.segments[i].e[j] = coeffs[4];
        spline_traj.segments[i].f[j] = coeffs[5];
      }
      else if (trajectory_type_ == TRAJECTORY_TYPE_CUBIC)
      {
        spline_smoother::getCubicSplineCoefficients(
            waypoint_traj.points[i].positions[j],
            waypoint_traj.points[i].velocities[j],
            waypoint_traj.points[i+1].positions[j],
            waypoint_traj.points[i+1].velocities[j],
            duration,
            coeffs);

        spline_traj.segments[i].a[j] = coeffs[0];
        spline_traj.segments[i].b[j] = coeffs[1];
        spline_traj.segments[i].c[j] = coeffs[2];
        spline_traj.segments[i].d[j] = coeffs[3];
        spline_traj.segments[i].e[j] = 0.0;
        spline_traj.segments[i].f[j] = 0.0;
      }
    }
  }
}

} // namespace joint_waypoint_controller

using namespace joint_waypoint_controller;

int main(int argc, char** argv)
{
  ros::init(argc, argv, "joint_waypoint_controller");
  JointWaypointController jwc;

  if (jwc.init())
    return jwc.run();
  else
    return 1;
}
