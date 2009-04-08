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

#include <ros/node.h>
#include <tf/tf.h>
#include <tf/transform_datatypes.h>
#include <robot_srvs/IKService.h>

#include <robot_msgs/Pose.h>

static int done = 0;

void finalize(int donecare)
{
  done = 1;
}

robot_msgs::Pose RPYToTransform(double roll, double pitch, double yaw, double x, double y, double z)
{
  robot_msgs::Pose pose;
  tf::Quaternion quat_trans = tf::Quaternion(yaw,pitch,roll);

  pose.orientation.x = quat_trans.x();
  pose.orientation.y = quat_trans.y();
  pose.orientation.z = quat_trans.z();
  pose.orientation.w = quat_trans.w();

  pose.position.x = x;
  pose.position.y = y;
  pose.position.z = z;

  return pose;
}



int main( int argc, char** argv )
{

  /*********** Initialize ROS  ****************/
  ros::init(argc,argv);
  ros::Node *node = new ros::Node("test_pr2_ik"); 

  signal(SIGINT,  finalize);
  signal(SIGQUIT, finalize);
  signal(SIGTERM, finalize);

  robot_srvs::IKService::Request  req;
  robot_srvs::IKService::Response res;

  req.pose = RPYToTransform(0.0,0.0,0.0,0.75,-0.188,0.0);

//  tf::PoseTFToMsg(pose,req.pose);
  req.joint_pos.set_positions_size(7);

  req.joint_pos.positions[0] = 0.0;
  req.joint_pos.positions[1] = 0.0;
  req.joint_pos.positions[2] = 0.0;
  req.joint_pos.positions[3] = 0.0;
  req.joint_pos.positions[4] = 0.0;
  req.joint_pos.positions[5] = 0.0;
  req.joint_pos.positions[6] = 0.0;

  if (ros::service::call("perform_pr2_ik", req, res))
  {
    ROS_INFO("Done");
    for(int i=0; i < (int) res.traj.points.size(); i++)
    {
      ROS_INFO(" ");
      ROS_INFO(" ");
      for(int j=0; j < (int) res.traj.points[i].positions.size(); j++)
      {
        ROS_INFO("%f ",res.traj.points[i].positions[j]);
      }
    }
  }
  else
  {
    ROS_INFO("service call failed");
  }  

  delete node;
}
