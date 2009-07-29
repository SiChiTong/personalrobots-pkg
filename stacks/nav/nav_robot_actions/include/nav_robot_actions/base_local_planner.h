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
*   * Neither the name of Willow Garage, Inc. nor the names of its
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
#ifndef NAV_ROBOT_ACTIONS_BASE_LOCAL_PLANNER_
#define NAV_ROBOT_ACTIONS_BASE_LOCAL_PLANNER_

#include <geometry_msgs/PoseStamped.h>
#include <robot_msgs/PoseDot.h>
#include <costmap_2d/costmap_2d_ros.h>
#include <loki/Factory.h>
#include <loki/Sequence.h>

namespace nav_robot_actions {
  class BaseLocalPlanner{
    public:
      virtual bool computeVelocityCommands(robot_msgs::PoseDot& cmd_vel) = 0;
      virtual bool goalReached() = 0;
      virtual bool updatePlan(const std::vector<geometry_msgs::PoseStamped>& plan) = 0;

    protected:
      BaseLocalPlanner(){}
  };

  //create an associated factory
  typedef Loki::SingletonHolder
  <
    Loki::Factory< BaseLocalPlanner, std::string, 
      Loki::Seq< std::string, 
      tf::TransformListener&, 
      costmap_2d::Costmap2DROS& > >,
    Loki::CreateUsingNew,
    Loki::LongevityLifetime::DieAsSmallObjectParent
  > BLPFactory;

#define ROS_REGISTER_BLP(c) \
  nav_robot_actions::BaseLocalPlanner* ROS_New_##c(std::string name, tf::TransformListener& tf, \
      costmap_2d::Costmap2DROS& costmap_ros){ \
    return new c(name, tf, costmap_ros); \
  }  \
  class RosBLP##c { \
    public: \
    RosBLP##c() \
    { \
      nav_robot_actions::BLPFactory::Instance().Register(#c, ROS_New_##c); \
    } \
    ~RosBLP##c() \
    { \
      nav_robot_actions::BLPFactory::Instance().Unregister(#c); \
    } \
  }; \
  static RosBLP##c ROS_BLP_##c;
};

#endif
