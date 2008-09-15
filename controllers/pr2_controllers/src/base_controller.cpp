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

#include <pr2_controllers/base_controller.h>
#include <math_utils/angles.h>

using namespace std;
using namespace controller;
using namespace libTF;
using namespace NEWMAT;
using namespace math_utils;

ROS_REGISTER_CONTROLLER(BaseController)

BaseController::BaseController() : num_wheels_(0), num_casters_(0), odom_publish_count_(10), odom_publish_counter_(0)
{
  cmd_vel_.x = 0;
  cmd_vel_.y = 0;
  cmd_vel_.z = 0;
  cmd_vel_t_.x = 0;
  cmd_vel_t_.y = 0;
  cmd_vel_t_.z = 0;
  kp_speed_ = KP_SPEED_DEFAULT;
  base_odom_position_.x = 0;
  base_odom_position_.y = 0;
  base_odom_position_.z = 0;
  base_odom_velocity_.x = 0;
  base_odom_velocity_.y = 0;
  base_odom_velocity_.z = 0;

  pthread_mutex_init(&base_controller_lock_,NULL);
}

BaseController::~BaseController()
{
}

// Set the joint position command
void BaseController::setCommand(libTF::Vector cmd_vel)
{
  pthread_mutex_lock(&base_controller_lock_);
  //std::cout << "command received : " << cmd_vel_t_ << std::endl;
  cmd_vel_t_.x = cmd_vel.x;
  cmd_vel_t_.y = cmd_vel.y;
  cmd_vel_t_.z = cmd_vel.z;
  pthread_mutex_unlock(&base_controller_lock_);
}

libTF::Vector BaseController::getCommand()// Return the current position command
{
  libTF::Vector cmd_vel;
  pthread_mutex_lock(&base_controller_lock_);
  cmd_vel.x = cmd_vel_t_.x;
  cmd_vel.y = cmd_vel_t_.y;
  cmd_vel.z = cmd_vel_t_.z;
  pthread_mutex_unlock(&base_controller_lock_);
  return cmd_vel;
}

void BaseController::setPublishCount(int publish_count)
{
  odom_publish_count_ = publish_count;
};


void BaseController::init(std::vector<JointControlParam> jcp, mechanism::RobotState *robot_state)
{
  std::vector<JointControlParam>::iterator jcp_iter;
  robot_desc::URDF::Link *link;
  std::string joint_name;

//  ros::g_node->advertise<std_msgs::RobotBase2DOdom>("odom");
  std::string xml_content;
  (ros::g_node)->get_param("robotdesc/pr2",xml_content);
  if(!urdf_model_.loadString(xml_content.c_str()))
    return;

  for(jcp_iter = jcp.begin(); jcp_iter != jcp.end(); jcp_iter++)
  {
    joint_name = jcp_iter->joint_name;
    link = urdf_model_.getJointLink(joint_name);

    BaseParam base_object;
    base_object.pos_.x = link->xyz[0];
    base_object.pos_.y = link->xyz[1];
    base_object.pos_.z = link->xyz[2];
    base_object.name_ = joint_name;
    base_object.parent_ = NULL;
    base_object.joint_state_ = robot_state->getJointState(joint_name);
    if (base_object.joint_state_==NULL)
      std::cout << " unsuccessful getting joint state for " << joint_name << std::endl;

    base_object.controller_.init(robot_state, joint_name, Pid(jcp_iter->p_gain,jcp_iter->i_gain,jcp_iter->d_gain,jcp_iter->windup));

    if(joint_name.find("caster") != string::npos)
    {
      std::cout << " assigning casters " << joint_name << std::endl;
      base_object.local_id_ = num_casters_;
      base_casters_.push_back(base_object);
      steer_angle_actual_.push_back(0);
      steer_velocity_desired_.push_back(0);
      num_casters_++;
      cout << "base_casters" << "::  " << base_object;
    }

    if(joint_name.find("wheel") != string::npos)
    {
      std::cout << " assigning wheels " << joint_name << std::endl;
      base_object.local_id_ = num_wheels_;
      if(joint_name.find("_r_") != string::npos)
        base_object.direction_multiplier_ = 1;

      base_wheels_.push_back(base_object);
      wheel_speed_actual_.push_back(0);
      libTF::Vector *v=new libTF::Vector();
      base_wheels_position_.push_back(*v);
      num_wheels_++;
    }
  }

  for(int i =0; i < num_wheels_; i++)
  {
    link = urdf_model_.getJointLink(base_wheels_[i].name_);
    std::string parent_name = link->parent->joint->name;
//    cout << "parent_name from urdf:: " << parent_name << endl;
    for(int j =0; j < num_casters_; j++)
    {
      if(parent_name == base_casters_[j].name_)
      {
//        cout <<  "base_casters matched name  ::" << base_casters_[j].name_ << endl;
        base_wheels_[i].parent_ = &base_casters_[j];
        break;
      }
    }
//      cout << "parent assigned name ::" << base_wheels_[i].parent_->name_ << endl << endl;
//      cout << "base_wheels" << endl << base_wheels_[i];
  }

  if(num_wheels_ > 0)
  {
    link = urdf_model_.getJointLink(base_wheels_[0].name_);
    robot_desc::URDF::Link::Geometry::Cylinder *wheel_geom = dynamic_cast<robot_desc::URDF::Link::Geometry::Cylinder*> (link->collision->geometry->shape);
    wheel_radius_ = wheel_geom->radius;
  }
  else
  {
    wheel_radius_ = DEFAULT_WHEEL_RADIUS;
  }
  robot_state_ = robot_state;

  last_time_ = robot_state_->hw_->current_time_;
}

bool BaseController::initXml(mechanism::RobotState *robot_state, TiXmlElement *config)
{
  std::cout << " base controller name: " << config->Attribute("name") << std::endl;
  TiXmlElement *elt = config->FirstChildElement("controller");
//  std::cout << " child " << std::endl << *elt << std::endl;
  std::vector<JointControlParam> jcp_vec;
  JointControlParam jcp;
  while (elt){
    TiXmlElement *jnt = elt->FirstChildElement("joint");
    //std::cout << "joint snippit" << std::endl << *jnt << std::endl;

    // TODO: error check if xml attributes/elements are missing
    jcp.p_gain = atof(jnt->FirstChildElement("pid")->Attribute("p"));
    jcp.i_gain = atof(jnt->FirstChildElement("pid")->Attribute("i"));
    jcp.d_gain = atof(jnt->FirstChildElement("pid")->Attribute("d"));
    jcp.windup = atof(jnt->FirstChildElement("pid")->Attribute("iClamp"));
    jcp.control_type = (std::string) elt->Attribute("type");
    jcp.joint_name = jnt->Attribute("name");
    jcp_vec.push_back(jcp);

//    std::cout << "name:" << jcp.joint_name << std::endl;
//    std::cout << "controller type:" << jcp.control_type << std::endl;
//    std::cout << std::endl << *elt << std::endl;
//    std::cout << " sub controller : " << elt->Attribute("name") << std::endl;

    elt = elt->NextSiblingElement("controller");

  }

  elt = config->FirstChildElement("map");
  while(elt)
  {
    if(elt->Attribute("name") == std::string("velocity_control"))
    {
      TiXmlElement *elt_key = elt->FirstChildElement("elem");
//      cout << "elt_key" << elt_key->Attribute("key") << endl;
      while(elt_key)
      {
        if(elt_key->Attribute("key") == std::string("kp_speed"))
        {
          kp_speed_ = atof(elt_key->GetText());
          break;
        }
        elt_key = elt->NextSiblingElement("elem");
      }
    }
//    std::cout << " sub map : " << elt->Attribute("name") << std::endl;
    elt = config->NextSiblingElement("map");
  }
  init(jcp_vec,robot_state);
  return true;
}

void BaseController::getJointValues()
{
  for(int i=0; i < num_casters_; i++)
    steer_angle_actual_[i] = base_casters_[i].joint_state_->position_;

  for(int i=0; i < num_wheels_; i++)
    wheel_speed_actual_[i] = base_wheels_[i].controller_.getMeasuredVelocity();

//   for(int i=0; i < num_casters_; i++)
//     std::cout << " caster angles " << i << " : " << steer_angle_actual_[i] << std::endl;
//   for(int i=0; i < num_wheels_; i++)
//     std::cout << " wheel rates " << i << " : " << wheel_speed_actual_[i] << std::endl;
}

void BaseController::computeWheelPositions()
{
  libTF::Vector res1;
  double steer_angle;
  for(int i=0; i < num_wheels_; i++)
  {
    steer_angle = base_wheels_[i].parent_->joint_state_->position_;
//    res1 = rotate2D(base_wheels_[i].pos_,steer_angle);
//    cout << "init position" << base_wheels_[i].pos_.x << " " << base_wheels_[i].pos_.y << " " << base_wheels_[i].pos_.z << endl;
    res1 = base_wheels_[i].pos_.rot2D(steer_angle);
//    cout << "rotated position" << res1;
    res1 += base_wheels_[i].parent_->pos_;
//    cout << "added position" << res1;
    base_wheels_position_[i] = res1;
//    cout << "base_wheels_position_(" << i << ")" << base_wheels_position_[i];
  }
//  exit(-1);
}

void BaseController::update()
{
  double current_time = robot_state_->hw_->current_time_;

  if(pthread_mutex_trylock(&base_controller_lock_)==0)
  {
    cmd_vel_.x = cmd_vel_t_.x;
    cmd_vel_.y = cmd_vel_t_.y;
    cmd_vel_.z = cmd_vel_t_.z;
    pthread_mutex_unlock(&base_controller_lock_);
  }
  //std::cout << "command received in update : " << cmd_vel_ << std::endl;

  getJointValues();

  computeWheelPositions();

  computeAndSetCasterSteer();

  computeAndSetWheelSpeeds();

  computeOdometry(current_time);

  if(odom_publish_counter_ > odom_publish_count_) // FIXME: time based rate limiting
  {
    (ros::g_node)->publish("odom", odom_msg_);
    odom_publish_counter_ = 0;
  }

  updateJointControllers();

  last_time_ = current_time;

  odom_publish_counter_++;
}

void BaseController::computeAndSetCasterSteer()
{
  libTF::Vector result;
  double steer_angle_desired;
  double error_steer;
  for(int i=0; i < num_casters_; i++)
  {
    result = computePointVelocity2D(base_casters_[i].pos_, cmd_vel_);
    steer_angle_desired = atan2(result.y,result.x);
//     steer_angle_desired =  modNPiBy2(steer_angle_desired);//Clean steer Angle

    error_steer = steer_angle_actual_[i] - steer_angle_desired;
    steer_velocity_desired_[i] = -kp_speed_*error_steer;
//    std::cout << "setting steering velocity " << i << " : " << steer_velocity_desired_[i] << " kp: " << kp_speed_ << "error_steer" << error_steer << std::endl;
    base_casters_[i].controller_.setCommand(steer_velocity_desired_[i]);
  }
}

void BaseController::computeAndSetWheelSpeeds()
{
  libTF::Vector wheel_point_velocity;
  libTF::Vector wheel_point_velocity_projected;
  libTF::Vector wheel_caster_steer_component;
  libTF::Vector caster_2d_velocity;

  caster_2d_velocity.x = 0;
  caster_2d_velocity.y = 0;
  caster_2d_velocity.z = 0;

  double wheel_speed_cmd = 0;
  double steer_angle_actual = 0;
  for(int i=0; i < (int) num_wheels_; i++)
  {

    caster_2d_velocity.z = steer_velocity_desired_[base_wheels_[i].parent_->local_id_];
    steer_angle_actual = base_wheels_[i].parent_->joint_state_->position_;
    wheel_point_velocity = computePointVelocity2D(base_wheels_position_[i],cmd_vel_);

//    cout << "wheel_point_velocity" << wheel_point_velocity << ",pos::" << base_wheels_position_[i] << ",cmd::" << cmd_vel_ << endl;

    wheel_caster_steer_component = computePointVelocity2D(base_wheels_[i].pos_,caster_2d_velocity);
//    wheel_point_velocity_projected = rotate2D(wheel_point_velocity,-steer_angle_actual);
    wheel_point_velocity_projected = wheel_point_velocity.rot2D(-steer_angle_actual);
    wheel_speed_cmd = (wheel_point_velocity_projected.x + wheel_caster_steer_component.x)/wheel_radius_;
//    std::cout << "setting wheel speed " << i << " : " << wheel_speed_cmd << " r:" << wheel_radius_ << std::endl;
    base_wheels_[i].controller_.setCommand(base_wheels_[i].direction_multiplier_*wheel_speed_cmd);
  }

}

void BaseController::updateJointControllers()
{
  for(int i=0; i < num_wheels_; i++)
    base_wheels_[i].controller_.update();
  for(int i=0; i < num_casters_; i++)
    base_casters_[i].controller_.update();
}


ROS_REGISTER_CONTROLLER(BaseControllerNode)
  BaseControllerNode::BaseControllerNode()
{
  c_ = new BaseController();
}

BaseControllerNode::~BaseControllerNode()
{
  delete c_;
}

void BaseControllerNode::update()
{
  c_->update();
}

bool BaseControllerNode::setCommand(
  pr2_controllers::SetBaseCommand::request &req,
  pr2_controllers::SetBaseCommand::response &resp)
{
  libTF::Vector command;
  command.x = req.x_vel;
  command.y = req.y_vel;
  command.z = req.theta_vel;
  c_->setCommand(command);
  command = c_->getCommand();
  resp.x_vel = command.x;
  resp.y_vel = command.y;
  resp.theta_vel = command.z;

  return true;
}

void BaseControllerNode::setCommand(double vx, double vy, double vw)
{
  libTF::Vector command;
  command.x = vx;
  command.y = vy;
  command.z = vw;
  c_->setCommand(command);
  //std::cout << "command received : " << vx << "," << vy << "," << vw << std::endl;
}


bool BaseControllerNode::getCommand(
  pr2_controllers::GetBaseCommand::request &req,
  pr2_controllers::GetBaseCommand::response &resp)
{
  libTF::Vector command;
  command = c_->getCommand();
  resp.x_vel = command.x;
  resp.y_vel = command.y;
  resp.theta_vel = command.z;

  return true;
}

bool BaseControllerNode::initXml(mechanism::RobotState *robot_state, TiXmlElement *config)
{
  ros::node *node = ros::node::instance();
  string prefix = config->Attribute("name");

  if(!c_->initXml(robot_state, config))
    return false;

  node->advertise_service(prefix + "/set_command", &BaseControllerNode::setCommand, this);
  node->advertise_service(prefix + "/get_actual", &BaseControllerNode::getCommand, this); //FIXME: this is actually get command, just returning command for testing.

  node->advertise<std_msgs::RobotBase2DOdom>("odom");

  // receive messages from 2dnav stack
  node->subscribe("cmd_vel", velMsg, &BaseControllerNode::CmdBaseVelReceived, this);

  return true;
}

void BaseControllerNode::CmdBaseVelReceived()
{
  this->ros_lock_.lock();
  this->setCommand(velMsg.vx,0.0,velMsg.vw);
  this->ros_lock_.unlock();
}

void BaseControllerNode::getOdometry(double &x, double &y, double &w, double &vx, double &vy, double &vw)
{
  c_->getOdometry(x,y,w,vx,vy,vw);
}

void BaseController::getOdometry(double &x, double &y, double &w, double &vx, double &vy, double &vw)
{
  x = base_odom_position_.x;
  y = base_odom_position_.y;
  w = base_odom_position_.z;

  vx = base_odom_velocity_.x;
  vy = base_odom_velocity_.y;
  vw = base_odom_velocity_.z;
}


Vector BaseController::computePointVelocity2D(const Vector& pos, const Vector& vel)
{
  Vector result;

  result.x = vel.x - pos.y * vel.z;
  result.y = vel.y + pos.x * vel.z;
  result.z = 0;

  return result;
}

void BaseController::computeOdometry(double time)
{
  double dt = time-last_time_;
//   libTF::Vector base_odom_delta = rotate2D(base_odom_velocity_*dt,base_odom_position_.z);

  computeBaseVelocity();

  libTF::Vector base_odom_delta = (base_odom_velocity_*dt).rot2D(base_odom_position_.z);

//   if(isnan(dt))
//   {
//     cout << "time nan" << endl;
//     exit(-1);
//   }

//   if(isnan(base_odom_velocity_.x) || isnan(base_odom_velocity_.y) || isnan(base_odom_velocity_.z))
//   {
//     cout << "base odometry velocity :: nan" << endl;
//     exit(-1);
//   }

  base_odom_delta.z = base_odom_velocity_.z * dt;
  base_odom_position_ += base_odom_delta;

  odom_msg_.header.frame_id   = "FRAMEID_ODOM";
  odom_msg_.header.stamp.sec  = (unsigned long)floor(robot_state_->hw_->current_time_);
  odom_msg_.header.stamp.nsec = (unsigned long)floor(  1e9 * (  robot_state_->hw_->current_time_ - odom_msg_.header.stamp.sec) );

  odom_msg_.pos.x  = base_odom_position_.x;
  odom_msg_.pos.y  = base_odom_position_.y;
  odom_msg_.pos.th = base_odom_position_.z;

  odom_msg_.vel.x  = base_odom_velocity_.x;
  odom_msg_.vel.y  = base_odom_velocity_.y;
  odom_msg_.vel.th = base_odom_velocity_.z;

//   cout << "Base Odometry: Velocity " << base_odom_velocity_;
//   cout << "Base Odometry: Position " << base_odom_position_;
}

void BaseController::computeBaseVelocity()
{
  Matrix A(2*num_wheels_,1);
  Matrix C(2*num_wheels_,3);
  Matrix D(3,1);
  double steer_angle;

  for(int i = 0; i < num_wheels_; i++) {
    steer_angle = base_wheels_[i].parent_->joint_state_->position_;
    A.element(i*2,0)   = cos(steer_angle)*wheel_radius_*(wheel_speed_actual_[i]);
    A.element(i*2+1,0) = sin(steer_angle)*wheel_radius_*(wheel_speed_actual_[i]);
//     if(isnan(steer_angle))
//     {
//       cout << "steer angle - nan for wheel " << i << endl;
//       exit(-1);
//     }
//     if(isnan(wheel_speed_actual_[i]))
//     {
//       cout << "wheel speed actual - nan for wheel " << i << endl;
//       exit(-1);
//     }
  }

  for(int i = 0; i < num_wheels_; i++) {
    C.element(i*2, 0)   = 1;
    C.element(i*2, 1)   = 0;
    C.element(i*2, 2)   = -base_wheels_position_[i].y;
    C.element(i*2+1, 0) = 0;
    C.element(i*2+1, 1) = 1;
    C.element(i*2+1, 2) =  base_wheels_position_[i].x;
//     if(isnan(base_wheels_position_[i].y) || isnan(base_wheels_position_[i].x))
//     {
//       cout << "base odom position - nan for wheel " << i  << endl;
//       exit(-1);
//     }
  }
  D = pseudoInverse(C)*A;

//   if(isnan(base_odom_velocity_.x) || isnan(base_odom_velocity_.y) || isnan(base_odom_velocity_.z))
//   {
//     cout << "base odom velocity - nan" << endl;
//     exit(-1);
//   }

  base_odom_velocity_.x = (double)D.element(0,0);
  base_odom_velocity_.y = (double)D.element(1,0);
  base_odom_velocity_.z = (double)D.element(2,0);
}

Matrix BaseController::pseudoInverse(const Matrix M)
{
  Matrix result;
  //int rows = this->rows();
  //int cols = this->columns();
  // calculate SVD decomposition
  Matrix U,V;
  DiagonalMatrix D;
  NEWMAT::SVD(M,D,U,V, true, true);
  Matrix Dinv = D.i();
  result = V * Dinv * U.t();
  return result;
}


std::ostream & controller::operator<<(std::ostream& mystream, const controller::BaseParam &bp)
{
  mystream << "BaseParam" << bp.name_ << endl << "position " << bp.pos_ << "id " << bp.local_id_ << endl << "joint " << bp.joint_state_->joint_->name_ << endl << endl;
  return mystream;
};



// void BaseController::setGeomParams()
// {
//   char *c_filename = getenv("ROS_PACKAGE_PATH");
//   std::stringstream filename;
//   filename << c_filename << "/robot_descriptions/wg_robot_description/pr2/pr2.xml" ;
//   robot_desc::URDF model;
//   if(!model.loadFile(filename.str().c_str()))
//      return;

//   robot_desc::URDF::Group *base_group = model.getGroup("base_control");
//   robot_desc::URDF::Link *base_link;
//   robot_desc::URDF::Link *caster_link;
//   robot_desc::URDF::Link *wheel_link;

// //  double base_caster_x_offset(0), base_caster_y_offset(0), wheel_base_(0);

//   if((int) base_group->linkRoots.size() != 1)
//   {
//     fprintf(stderr,"base_control.cpp::Too many roots in base!\n");
//   }
//   base_link = base_group->linkRoots[0];
//   caster_link = *(base_link->children.begin());
//   wheel_link = *(caster_link->children.begin());

//   base_caster_x_offset_ = fabs(caster_link->xyz[0]);
//   base_caster_y_offset_ = fabs(caster_link->xyz[1]);
//   wheel_base_ =2*sqrt(wheel_link->xyz[0]*wheel_link->xyz[0]+wheel_link->xyz[1]*wheel_link->xyz[1]);

//   robot_desc::URDF::Link::Geometry::Cylinder *wheel_geom = dynamic_cast<robot_desc::URDF::Link::Geometry::Cylinder*> (wheel_link->collision->geometry->shape);
//   wheel_radius_ = wheel_geom->radius;

//   BaseCasterGeomParam caster;
//   libTF::Vector wheel_l;
//   libTF::Vector wheel_r;

//   wheel_l.x = 0;
//   wheel_l.y = wheel_base_/2.0;
//   wheel_l.z = 0;

//   wheel_r.x = 0;
//   wheel_r.y = -wheel_base_/2.0;
//   wheel_r.z = 0;

//   caster.wheel_pos.push_back(wheel_l);
//   caster.wheel_pos.push_back(wheel_r);

// // FRONT LEFT
//   caster.pos.x = base_caster_x_offset_;
//   caster.pos.y = base_caster_y_offset_;
//   caster.pos.z = 0;
//   base_casters_.push_back(caster);

// // FRONT RIGHT
//   caster.pos.x = base_caster_x_offset_;
//   caster.pos.y = -base_caster_y_offset_;
//   caster.pos.z = 0;
//   base_casters_.push_back(caster);

// // REAR LEFT
//   caster.pos.x = -base_caster_x_offset_;
//   caster.pos.y = base_caster_y_offset_;
//   caster.pos.z = 0;
//   base_casters_.push_back(caster);

// // REAR RIGHT
//   caster.pos.x = -base_caster_x_offset_;
//   caster.pos.y = -base_caster_y_offset_;
//   caster.pos.z = 0;
//   base_casters_.push_back(caster);
// }
// void BaseController::computeBaseVelocity()
// {

//   Matrix A(2*NUM_WHEELS,1);
//   //Matrix B(NUM_WHEELS,1);
//   Matrix C(2*NUM_WHEELS,3);
//   Matrix D(3,1);

//   for(int i = 0; i < NUM_CASTERS; i++) {
//     A.element(i*4,0) = cos(robot_state->joint[i*3].position) *WHEEL_RADIUS*((double)-1)*robot_state->joint[i*3+1].velocity;
//     A.element(i*4+1,0) = sin(robot_state->joint[i*3].position) *WHEEL_RADIUS*((double)-1)*robot_state->joint[i*3+1].velocity;
//     A.element(i*4+2,0) = cos(robot_state->joint[i*3].position) *WHEEL_RADIUS*robot_state->joint[i*3+2].velocity;
//     A.element(i*4+3,0) = sin(robot_state->joint[i*3].position)* WHEEL_RADIUS*robot_state->joint[i*3+2].velocity;
//   }

//   /*
//     for(int i = 0; i < (NUM_WHEELS + NUM_CASTERS); i++) {
//     printf("i: %i pos : %03f vel: %03f\n", i,robot_state->joint[i].position, robot_state->joint[i].velocity);
//     }
//   */
//   for(int i = 0; i < NUM_CASTERS; i++) {
//     C.element(i*4, 0) = 1;
//     C.element(i*4, 1) = 0;
//     C.element(i*4, 2) = -(Rot2D(CASTER_DRIVE_OFFSET[i*2].x,CASTER_DRIVE_OFFSET[i*2].y,robot_state->joint[i*3].position).y + BASE_CASTER_OFFSET[i].y);
//     C.element(i*4+1, 0) = 0;
//     C.element(i*4+1, 1) = 1;
//     C.element(i*4+1, 2) =  Rot2D(CASTER_DRIVE_OFFSET[i*2].x,CASTER_DRIVE_OFFSET[i*2].y,robot_state->joint[i*3].position).x + BASE_CASTER_OFFSET[i].x;
//     C.element(i*4+2, 0) = 1;
//     C.element(i*4+2, 1) = 0;
//     C.element(i*4+2, 2) =  -(Rot2D(CASTER_DRIVE_OFFSET[i*2+1].x,CASTER_DRIVE_OFFSET[i*2+1].y,robot_state->joint[i*3].position).y + BASE_CASTER_OFFSET[i].y);
//     C.element(i*4+3, 0) = 0;
//     C.element(i*4+3, 1) = 1;
//     C.element(i*4+3, 2) =  Rot2D(CASTER_DRIVE_OFFSET[i*2+1].x,CASTER_DRIVE_OFFSET[i*2+1].y,robot_state->joint[i*3].position).x + BASE_CASTER_OFFSET[i].x;
//   }

//   D = pseudoInverse(C)*A;
//   /*
//     aTest = C*commandTest;
//     cout << "A:" << endl;
//     cout << A;
//     cout << "C :" << endl;
//     cout << C<< endl;
//     cout << "commandTest: "<< endl;
//     cout << commandTest << endl;
//     cout << "aTest: "<< endl;
//     cout << aTest << endl;
//     //
//     //
//   */
//   base_odom_vx_ = (double)D.element(0,0);
//   base_odom_vy_ = (double)D.element(1,0);
//   base_odom_vw_ = (double)D.element(2,0);
//   //cout << "D :" << endl;
//   //cout << D << endl;
// }

// Matrix BaseController::pseudoInverse(const Matrix M)
// {
//   Matrix result;
//   //int rows = this->rows();
//   //int cols = this->columns();
//   // calculate SVD decomposition
//   Matrix U,V;
//   DiagonalMatrix D;
//   SVD(M,D,U,V, true, true);
//   Matrix Dinv = D.i();
//   result = V * Dinv * U.t();
//   return result;
// }


