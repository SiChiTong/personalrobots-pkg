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

/* Author: Wim Meeussen */

#ifndef RobotModel_PARSER_JOINT_H
#define RobotModel_PARSER_JOINT_H

#include <string>
#include <vector>
#include <tinyxml/tinyxml.h>
#include <boost/shared_ptr.hpp>

#include <robot_model/pose.h>

namespace robot_model{

class Link;

class JointProperties
{
public:
  JointProperties() { this->clear(); };
  double damping;
  double friction;

  void clear()
  {
    damping = 0;
    friction = 0;
  };
  bool initXml(TiXmlElement* config);
};

class JointLimits
{
public:
  JointLimits() { this->clear(); };
  double lower;
  double upper;
  double effort;
  double velocity;

  void clear()
  {
    lower = 0;
    upper = 0;
    effort = 0;
    velocity = 0;
  };
  bool initXml(TiXmlElement* config);
};

class JointSafety
{
public:
  JointSafety() { this->clear(); };
  double soft_upper_limit;
  double soft_lower_limit;
  double k_p;
  double k_v;

  void clear()
  {
    soft_upper_limit = 0;
    soft_lower_limit = 0;
    k_p = 0;
    k_v = 0;
  };
  bool initXml(TiXmlElement* config);
};


class JointCalibration
{
public:
  JointCalibration() { this->clear(); };
  double reference_position;

  void clear()
  {
    reference_position = 0;
  };
  bool initXml(TiXmlElement* config);
};


class Joint
{
public:

  Joint() { this->clear(); };

  std::string name;
  enum
  {
    UNKNOWN, REVOLUTE, CONTINUOUS, PRISMATIC, FLOATING, PLANAR, FIXED
  } type;

  /// \brief     type_       meaning of axis_
  /// ------------------------------------------------------
  ///            UNKNOWN     unknown type
  ///            REVOLUTE    rotation axis
  ///            PRISMATIC   translation axis
  ///            FLOATING    N/A
  ///            PLANAR      plane normal axis
  ///            FIXED       N/A
  Vector3 axis;

  /// child Link element
  //boost::shared_ptr<Link> link; /// @todo: not sure how to assign a shared_ptr link to parent link
  std::string link_name;
  /// transform from Link frame to Joint frame
  Pose origin;

  /// parent Link element
  boost::shared_ptr<Link> parent_link;
  std::string parent_link_name;
  ///   transform from parent Link to Joint frame
  Pose  parent_origin;

  /// Joint Properties
  boost::shared_ptr<JointProperties> properties;

  /// Joint Limits
  boost::shared_ptr<JointLimits> limits;

  /// Unsupported Hidden Feature
  boost::shared_ptr<JointSafety> safety;

  /// Unsupported Hidden Feature
  boost::shared_ptr<JointCalibration> calibration;

  // Joint element has one parent and one child Link
  void setParentLink(boost::shared_ptr<Link> parent) {this->parent_link = parent;};
  void setParentPose(Pose pose) {this->parent_origin = pose;};

  bool initXml(TiXmlElement* xml);
  void clear()
  {
    this->axis.clear();
    this->link_name.clear();
    this->origin.clear();
    this->parent_link.reset();
    this->parent_link_name.clear();
    this->parent_origin.clear();
    //this->link.reset(); /// @todo: not sure how to assign a shared_ptr link to parent link
    this->properties.reset();
    this->limits.reset();
    this->safety.reset();
    this->calibration.reset();
    this->type = UNKNOWN;
  };
};

}

#endif
