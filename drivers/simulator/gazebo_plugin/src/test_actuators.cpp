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
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
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

#include <gazebo_plugin/test_actuators.h>
#include <fstream>
#include <iostream>
#include <set>
#include <math.h>
#include <unistd.h>
#include <stl_utils/stl_utils.h>
#include <math_utils/angles.h>
#include <gazebo/Global.hh>
#include <gazebo/XMLConfig.hh>
#include <gazebo/Model.hh>
#include <gazebo/HingeJoint.hh>
#include <gazebo/SliderJoint.hh>
#include <gazebo/Simulator.hh>
#include <gazebo/gazebo.h>
#include <gazebo/GazeboError.hh>
#include <gazebo/ControllerFactory.hh>

// new parser by stu
#include <urdf/parser.h>

namespace gazebo {

  GZ_REGISTER_DYNAMIC_CONTROLLER("test_actuators", TestActuators);

  TestActuators::TestActuators(Entity *parent)
    : Controller(parent) , mc_(&hw_), mcn_(&mc_), fake_state_(NULL) , hw_(0)
  {
     this->parent_model_ = dynamic_cast<Model*>(this->parent);

     if (!this->parent_model_)
        gzthrow("TestActuators controller requires a Model as its parent");

    rosnode_ = ros::g_node; // comes from where?
    int argc = 0;
    char** argv = NULL;
    if (rosnode_ == NULL)
    {
      // this only works for a single camera.
      ros::init(argc,argv);
      rosnode_ = new ros::node("ros_gazebo",ros::node::DONT_HANDLE_SIGINT);
      printf("-------------------- starting node in test actuators \n");
    }

    // for publishing frame transforms
    tfs = new rosTFServer(*rosnode_); //, true, 1 * 1000000000ULL, 0ULL);

    // uses info from wg_robot_description_parser/send.xml
    std::string pr2Content;

    AdvertiseSubscribeMessages();

  }

  TestActuators::~TestActuators()
  {
    //deleteElements(&gazebo_joints_);
  }

  void TestActuators::LoadChild(XMLConfigNode *node)
  {
    // if we were doing some ros publishing...
    //this->topicName = node->GetString("topicName","default_ros_camera",0); //read from xml file
    //this->frameName = node->GetString("frameName","default_ros_camera",0); //read from xml file
    //std::cout << "================= " << this->topicName << std::endl;
    //rosnode_->advertise<std_msgs::Image>(this->topicName);

    //---------------------------------------------------------------------
    // setup mechanism control - non-gazebo stuff,
    // same as implemented on realtime system
    //
    // for the topside, we need to setup:
    //   robot->joints
    //   actuators
    //   transmissions
    //
    //---------------------------------------------------------------------
    LoadMC(node);

  }

  void TestActuators::InitChild()
  {
    // TODO: mc_.init();

    hw_.current_time_ = Simulator::Instance()->GetSimTime();

    // simulator time
    currentTime = Simulator::Instance()->GetSimTime();
    // for debug display
    lastTime    = Simulator::Instance()->GetSimTime();


    controller::Controller* tlc = mc_.getControllerByName( "tilt_laser_controller" );
    controller::LaserScannerControllerNode* tlcn = dynamic_cast<controller::LaserScannerControllerNode*>(tlc);
    if (tlcn)
    {
      std::cout << " initializing tile laser scanner\n" << std::endl;
      //Typical scan of 100 degrees yields the following amplitudes
      //tlcn->setProfile(controller::LaserScannerController::SINEWAVE, 20, 0.872, 100, 0.3475);
      tlcn->setProfile(controller::LaserScannerController::SAWTOOTH, 20, 0.872, 100, 0.3475); //FIXME: sawtooth disappeared in r4180, I reverted it for now.
    }

  }

  void TestActuators::UpdateChild()
  {
    //--------------------------------------------------
    //  run the reattime mc update
    //--------------------------------------------------
    UpdateMC();

  }

  void TestActuators::FiniChild()
  {
    // TODO: will be replaced by global ros node eventually
    if (rosnode_ != NULL)
    {
      std::cout << "shutdown rosnode in test_actuators" << std::endl;
      //ros::fini();
      rosnode_->shutdown();
      //delete rosnode_;
    }
  }

  void TestActuators::LoadMC(XMLConfigNode *node)
  {


    //-----------------------------------------------------------------------------------------
    //
    // Read XML's and normalize const and const_blocks
    //
    //-----------------------------------------------------------------------------------------
    TiXmlDocument *pr2_xml = new TiXmlDocument();
    TiXmlDocument *controller_xml = new TiXmlDocument();
    TiXmlDocument *actuator_xml = new TiXmlDocument();

    std::string pr2_xml_filename = getenv("MC_RESOURCE_PATH"); pr2_xml_filename += "/"; pr2_xml_filename += node->GetString("robot_filename","",1);
    std::string controller_xml_filename = getenv("MC_RESOURCE_PATH"); controller_xml_filename += "/"; controller_xml_filename += node->GetString("controller_filename","",1);
    std::string actuator_xml_filename = getenv("MC_RESOURCE_PATH"); actuator_xml_filename += "/"; actuator_xml_filename += node->GetString("actuator_filename","",1);

    std::cout << " pr2 robot xml file name: " << pr2_xml_filename << std::endl;
    std::cout << " controller    file name: " << controller_xml_filename << std::endl;
    std::cout << " actuator      file name: " << actuator_xml_filename << std::endl;

    pr2_xml         ->LoadFile(pr2_xml_filename);
    controller_xml  ->LoadFile(controller_xml_filename);
    actuator_xml    ->LoadFile(actuator_xml_filename);

    urdf::normalizeXml( pr2_xml->RootElement() );
    //urdf::normalizeXml( controller_xml->RootElement() );
    //urdf::normalizeXml( actuator_xml->RootElement() );

    //-------------------------------------------------------------------------------------------
    //
    // GET INFORMATION FROM PR2.XML FROM PARAM SERVER
    // AND PUT IT INTO ROBOT_
    //
    // create a set of mech_joint_ for the robot and
    //
    // create a set of reverse_mech_joint_ for the reverse transmission results
    //
    //-------------------------------------------------------------------------------------------

    // parse pr2.xml from filename specified
    pr2Description.loadFile(pr2_xml_filename.c_str());

    // get all links in pr2.xml
    pr2Description.getLinks(pr2Links);
    std::cout << " pr2.xml contains " << pr2Links.size() << " links." << std::endl;

    // as the name states
    LoadFrameTransformOffsets();
    //-----------------------------------------------------------------------------------------
    //
    // ACTUATOR XML
    //
    // Pulls out the list of actuators used in the robot configuration.
    //
    //-----------------------------------------------------------------------------------------
    struct GetActuators : public TiXmlVisitor
    {
      std::set<std::string> actuators;
      virtual bool VisitEnter(const TiXmlElement &elt, const TiXmlAttribute *)
      {
        if (elt.ValueStr() == std::string("actuator") && elt.Attribute("name"))
          actuators.insert(elt.Attribute("name"));
        return true;
      }
    } get_actuators;
    actuator_xml->RootElement()->Accept(&get_actuators);

    // Places the found actuators into the hardware interface.
    std::set<std::string>::iterator it;
    for (it = get_actuators.actuators.begin(); it != get_actuators.actuators.end(); ++it)
    {
      std::cout << "adding actuator " << (*it) << std::endl;
      hw_.actuators_.push_back(new Actuator(*it));
    }
    //-----------------------------------------------------------------------------------------
    //
    //  parse for MechanismControl joints
    //
    //-----------------------------------------------------------------------------------------
    mcn_.initXml(pr2_xml->FirstChildElement("robot"));

    fake_state_ = new mechanism::RobotState(&mc_.model_, &hw_);

    //-----------------------------------------------------------------------------------------
    //
    //  how the mechanism joints relate to the gazebo_joints
    //
    //-----------------------------------------------------------------------------------------
    // The gazebo joints and mechanism joints should match up.
    std::cout << " Loading gazebo joints : " <<  std::endl;

    for (XMLConfigNode *jNode = node->GetChild("robot")->GetChild("joint"); jNode; )
    {
      std::string *joint_name = new std::string(jNode->GetString("name","",1));
      std::cout << "processing mech joint (" << *joint_name << ") map to gazebo joint. " << std::endl;

      // joint exist in model, proceed to create gazebo joint and mapping
      Gazebo_joint_* gj = new Gazebo_joint_();
      gj->name_      = joint_name;


      // add a link to the mechanism control joint
      gj->fake_joint_state_ = fake_state_->getJointState(*joint_name);
      if (gj->fake_joint_state_==NULL) {std::cout << " no joint name found in joint state " << *joint_name << std::endl; abort();}; // make sure no NULL fake_joint_state_

      // read gazebo specific joint properties
      gj->saturationTorque           = jNode->GetDouble("saturationTorque",0.0,0);
      gj->explicitDampingCoefficient = jNode->GetDouble("explicitDampingCoefficient",0.0,0);

      // initialize gazebo joint parameters
      gazebo::Joint* ggj = (gazebo::Joint*)parent_model_->GetJoint(*joint_name);
      if (ggj)
      {
        gj->gaz_joints_.push_back(ggj);
        // initialize for torque control mode
        ggj->SetParam(dParamVel , 0);
        ggj->SetParam(dParamFMax, 0);
      }
      else // maybe a bad joint in gazebo?
      {
        std::cout << " cannot find joint (" << *joint_name << "> in Gazebo " << std::endl;
      }

      gazebo_joints_.push_back(gj);
      jNode = jNode->GetNext("joint");
    }


    //-----------------------------------------------------------------------------------------
    //
    // CONTROLLER XML
    //
    //  spawn controllers
    //
    //-----------------------------------------------------------------------------------------
    // make mc parse xml for controllers
    std::cout << " Loading controllers : " <<  std::endl;
    for (TiXmlElement *xit = controller_xml->FirstChildElement("robot"); xit ; xit = xit->NextSiblingElement("robot") )
    for (TiXmlElement *zit = xit->FirstChildElement("controller"); zit ; zit = zit->NextSiblingElement("controller") )
    {
      std::string* controller_name = new std::string(zit->Attribute("name"));
      std::string* controller_type = new std::string(zit->Attribute("type"));
      std::cout << " LoadChild controller name: " <<  *controller_name << " type " << *controller_type << std::endl;

      // initialize controller
      std::cout << " adding to mc_ " << *controller_name << "(" << *controller_type << ")" << std::endl;
      mc_.spawnController(*controller_type,
                          *controller_name,
                          zit);
    }

  }


  //-------------------------------------------------------------------------------------------//
  //                                                                                           //
  //                                                                                           //
  //                                                                                           //
  //  MAIN UPDATE LOOP FOR MC AND ROS PUBLISH                                                  //
  //                                                                                           //
  //                                                                                           //
  //                                                                                           //
  //-------------------------------------------------------------------------------------------//
  void TestActuators::UpdateMC()
  {
    // pass time to robot
    currentTime = Simulator::Instance()->GetSimTime();
    hw_.current_time_ = currentTime;

    /***************************************************************/
    /*                                                             */
    /*  check simulator performance                                */
    /*                                                             */
    /***************************************************************/
    static double currentRealTime, lastRealTime;
    if (getenv("CHECK_PERFORMANCE")) // if this environment var is set, dump outputs
    {
      currentRealTime = Simulator::Instance()->GetRealTime();
      std::cout << "performance measure: "
                << " elapsed simu time: " <<  currentTime
                << " elapsed real time: " <<  currentRealTime
                << " elapsed simu dt: " <<  currentTime - lastTime
                << " elapsed real dt: " <<  currentRealTime - lastRealTime
                << " simulator speedup: " <<  currentTime/currentRealTime
                << std::endl;
      lastRealTime = currentRealTime;
    }

    PublishROS();

    //---------------------------------------------------------------------
    // Real time update calls to mechanism control
    // this is what the hard real time loop does,
    // minus the tick() call to etherCAT
    //---------------------------------------------------------------------
    // fetch joint info into fake_state_ from gazebo joints
    UpdateMCJoints();
    // push reverse_mech_joint_ stuff back toward actuators
    fake_state_->propagateStateBackwards();
    fake_state_->propagateEffort();


    // -------------------------------------------------------------------------------------------------
    // -                                                                                               -
    // -   test section PLEASE IGNORE                                                                  -
    // -   test some controllers set points by hardcode for debug                                      -
    // -                                                                                               -
    // -------------------------------------------------------------------------------------------------
    // set through ros?
    // artifically set command
    // controller::Controller* mcc = mc_.getControllerByName( "shoulder_pitch_right_controller" );
    // dynamic_cast<controller::JointPositionController*>(mcc)->setCommand(-0.2);
    // // sample read back angle
    // controller::Controller* mc2 = mc_.getControllerByName( "shoulder_pitch_left_controller" );
    // std::cout << " angle = " << dynamic_cast<controller::JointPositionController*>(mcc)->getActual() << std::endl;

    // controller::Controller* mc5 = mc_.getControllerByName( "shoulder_pitch_left_controller" );
    // dynamic_cast<controller::JointPositionController*>(mc5)->setCommand(-0.5);
    // controller::Controller* mc3 = mc_.getControllerByName( "gripper_left_controller" );
    // dynamic_cast<controller::JointPositionController*>(mc3)->setCommand(0.2);

    //controller::Controller* cc = mc_.getControllerByName( "base_controller" );
    //controller::BaseControllerNode* bc = dynamic_cast<controller::BaseControllerNode*>(cc);
    //bc->setCommand(0.0,0.0,0.5);
    //mechanism::Joint* joint =mc_.model_.getJoint("forearm_roll_left_joint");
    // mechanism::Joint* joint =mc_.model_.getJoint("shoulder_pan_left_joint");
    //controller::Controller* mcc = mc_.getControllerByName( "shoulder_pan_left_controller" );
    //dynamic_cast<controller::JointPositionController*>(mcc)->setCommand(-1);
    /*
    joint->effort_limit_ = 10;
    joint->velocity_constant_ = 8;
    joint->equilibrium_length_ = 0.1;
    joint->joint_limit_max_ = 1;
    joint->joint_limit_min_ = -1;
    */
    //dynamic_cast<controller::JointVelocityController*>(mcc)->setCommand(-1);
    // // sample read back angle
    // std::cout<<hw_.current_time_<<" "<<joint->position_<<" "<<joint->velocity_<<" "<<joint->commanded_effort_<<"\n";


    // -------------------------------------------------------------------------------------------------
    // -                                                                                               -
    // -  update each controller, this updates the joint that the controller was initialized with      -
    // -                                                                                               -
    // -  update mc given the actuator states are filled from above                                    -
    // -                                                                                               -
    // -  update actuators from robot joints via forward transmission propagation                      -
    // -                                                                                               -
    // -------------------------------------------------------------------------------------------------
    mcn_.update();


    //============================================================================================
    // below is when the actuator stuff goes to the hardware
    //============================================================================================

    // -------------------------------------------------------------------------------------------------
    // -                                                                                               -
    // -    reverse transmission, get joint data from actuators                                        -
    // -                                                                                               -
    // -------------------------------------------------------------------------------------------------
    // propagate actuator data back to reverse-joints
    // assign reverse joint states from actuator states
    fake_state_->propagateState();
    // assign joint effort
    fake_state_->propagateEffortBackwards();

    UpdateGazeboJoints();


    lastTime = currentTime;

  }

  void TestActuators::PublishROS()
  {
    this->lock.lock();
    /***************************************************************/
    /*                                                             */
    /*  publish time to ros                                        */
    /*                                                             */
    /***************************************************************/
    timeMsg.rostime.sec  = (unsigned long)floor(hw_.current_time_);
    timeMsg.rostime.nsec = (unsigned long)floor(  1e9 * (  hw_.current_time_ - timeMsg.rostime.sec) );
    rosnode_->publish("time",timeMsg);


    // FIXEDME: deprecated by mechanism state messages!!!
    /* get left arm position */
    // if( mc_.state_->getJointState("shoulder_pan_left_joint")   ) larm.turretAngle       = mc_.state_->getJointState("shoulder_pan_left_joint")  ->position_;
    // if( mc_.state_->getJointState("shoulder_pitch_left_joint") ) larm.shoulderLiftAngle = mc_.state_->getJointState("shoulder_pitch_left_joint")->position_;
    // if( mc_.state_->getJointState("upperarm_roll_left_joint")  ) larm.upperarmRollAngle = mc_.state_->getJointState("upperarm_roll_left_joint") ->position_;
    // if( mc_.state_->getJointState("elbow_flex_left_joint")     ) larm.elbowAngle        = mc_.state_->getJointState("elbow_flex_left_joint")    ->position_;
    // if( mc_.state_->getJointState("forearm_roll_left_joint")   ) larm.forearmRollAngle  = mc_.state_->getJointState("forearm_roll_left_joint")  ->position_;
    // if( mc_.state_->getJointState("wrist_flex_left_joint")     ) larm.wristPitchAngle   = mc_.state_->getJointState("wrist_flex_left_joint")    ->position_;
    // if( mc_.state_->getJointState("gripper_roll_left_joint")   ) larm.wristRollAngle    = mc_.state_->getJointState("gripper_roll_left_joint")  ->position_;
    // if( mc_.state_->getJointState("gripper_left_joint")        ) larm.gripperForceCmd   = mc_.state_->getJointState("gripper_left_joint")       ->applied_effort_;
    // if( mc_.state_->getJointState("gripper_left_joint")        ) larm.gripperGapCmd     = mc_.state_->getJointState("gripper_left_joint")       ->position_;
    // rosnode_->publish("left_pr2arm_pos", larm);
    // /* get right arm position */
    // if( mc_.state_->getJointState("shoulder_pan_right_joint")   ) rarm.turretAngle       = mc_.state_->getJointState("shoulder_pan_right_joint")  ->position_;
    // if( mc_.state_->getJointState("shoulder_pitch_right_joint") ) rarm.shoulderLiftAngle = mc_.state_->getJointState("shoulder_pitch_right_joint")->position_;
    // if( mc_.state_->getJointState("upperarm_roll_right_joint")  ) rarm.upperarmRollAngle = mc_.state_->getJointState("upperarm_roll_right_joint") ->position_;
    // if( mc_.state_->getJointState("elbow_flex_right_joint")     ) rarm.elbowAngle        = mc_.state_->getJointState("elbow_flex_right_joint")    ->position_;
    // if( mc_.state_->getJointState("forearm_roll_right_joint")   ) rarm.forearmRollAngle  = mc_.state_->getJointState("forearm_roll_right_joint")  ->position_;
    // if( mc_.state_->getJointState("wrist_flex_right_joint")     ) rarm.wristPitchAngle   = mc_.state_->getJointState("wrist_flex_right_joint")    ->position_;
    // if( mc_.state_->getJointState("gripper_roll_right_joint")   ) rarm.wristRollAngle    = mc_.state_->getJointState("gripper_roll_right_joint")  ->position_;
    // if( mc_.state_->getJointState("gripper_right_joint")        ) rarm.gripperForceCmd   = mc_.state_->getJointState("gripper_right_joint")       ->applied_effort_;
    // if( mc_.state_->getJointState("gripper_right_joint")        ) rarm.gripperGapCmd     = mc_.state_->getJointState("gripper_right_joint")       ->position_;
    // rosnode_->publish("right_pr2arm_pos", rarm);

    PublishFrameTransforms();

    this->lock.unlock();
  }


  void TestActuators::UpdateGazeboJoints()
  {
    // -------------------------------------------------------------------------------------------------
    // -                                                                                               -
    // -     udpate gazebo joint for this controller joint                                             -
    // -                                                                                               -
    // -------------------------------------------------------------------------------------------------
    for (std::vector<Gazebo_joint_*>::iterator gji = gazebo_joints_.begin(); gji != gazebo_joints_.end() ; gji++)
    {
      // normal joints
      switch ((*gji)->gaz_joints_[0]->GetType())
      {
        case gazebo::Joint::SLIDER:
        {
          gazebo::SliderJoint* gjs  = dynamic_cast<gazebo::SliderJoint*>((*gji)->gaz_joints_[0]);
          if (gjs)
          {
            double dampForce    = -(*gji)->explicitDampingCoefficient * gjs->GetPositionRate();
            gjs->SetSliderForce( (*gji)->fake_joint_state_->commanded_effort_+dampForce);
            break;
          }
        }
        case gazebo::Joint::HINGE:
        {
          gazebo::HingeJoint* gjh  = dynamic_cast<gazebo::HingeJoint*>((*gji)->gaz_joints_[0]);
          if (gjh)
          {
            double dampForce    = -(*gji)->explicitDampingCoefficient * gjh->GetAngleRate();
            gjh->SetTorque( (*gji)->fake_joint_state_->commanded_effort_+dampForce);
            //std::cout << " hinge " << *((*gji)->name_) << " torque: " << (*gji)->fake_joint_state_->commanded_effort_ << " damping " << dampForce << std::endl;
            break;
          }
        }
        case gazebo::Joint::HINGE2:
        case gazebo::Joint::BALL:
        case gazebo::Joint::UNIVERSAL:
          break;
      }

    }
  }
  void TestActuators::UpdateMCJoints()
  {
    // update joint status from hardware
    for (std::vector<Gazebo_joint_*>::iterator gji = gazebo_joints_.begin(); gji != gazebo_joints_.end() ; gji++)
    {
      // normal joints
      switch((*gji)->gaz_joints_[0]->GetType())
      {
        case gazebo::Joint::SLIDER:
        {
          gazebo::SliderJoint* gjs  = dynamic_cast<gazebo::SliderJoint*>((*gji)->gaz_joints_[0]);
          (*gji)->fake_joint_state_->position_       = gjs->GetPosition();
          (*gji)->fake_joint_state_->velocity_       = gjs->GetPositionRate();
          (*gji)->fake_joint_state_->applied_effort_ = (*gji)->fake_joint_state_->commanded_effort_;
          break;
        }
        case gazebo::Joint::HINGE:
        {
          gazebo::HingeJoint* gjh  = dynamic_cast<gazebo::HingeJoint*>((*gji)->gaz_joints_[0]);
          (*gji)->fake_joint_state_->position_       = gjh->GetAngle();
          (*gji)->fake_joint_state_->velocity_       = gjh->GetAngleRate();
          (*gji)->fake_joint_state_->applied_effort_ = (*gji)->fake_joint_state_->commanded_effort_;
          //std::cout << " hinge " << *((*gji)->name_) << " angle " << (*gji)->fake_joint_state_->position_ << std::endl;
          break;
        }
        case gazebo::Joint::HINGE2:
        case gazebo::Joint::BALL:
        case gazebo::Joint::UNIVERSAL:
          break;
      }
    }

  }


  int
  TestActuators::AdvertiseSubscribeMessages()
  {
    //rosnode_->advertise<std_msgs::PR2Arm>("left_pr2arm_pos");
    //rosnode_->advertise<std_msgs::PR2Arm>("right_pr2arm_pos");
    rosnode_->advertise<rostools::Time>("time");

    //rosnode_->subscribe("cmd_vel", velMsg, &TestActuators::CmdBaseVelReceived, this);
    //rosnode_->subscribe("cmd_leftarmconfig", leftarmMsg, &TestActuators::CmdLeftarmconfigReceived,this);
    //rosnode_->subscribe("cmd_rightarmconfig", rightarmMsg, &TestActuators::CmdRightarmconfigReceived,this);
    //rosnode_->subscribe("cmd_leftarm_cartesian", leftarmcartesianMsg, &TestActuators::CmdLeftarmcartesianReceived);
    //rosnode_->subscribe("cmd_rightarm_cartesian", rightarmcartesianMsg, &TestActuators::CmdRightarmcartesianReceived);
    
    return(0);
  }


  void
  TestActuators::LoadFrameTransformOffsets()
  {
    // get all links in pr2.xml
    robot_desc::URDF::Link* link;

    // get all the parameters needed for frame transforms
    link = pr2Description.getLink("base");
    if (link)
    {
      base_center_offset_z = link->collision->xyz[2];
    }
    // get all the parameters needed for frame transforms
    link = pr2Description.getLink("base_laser");
    if (link)
    {
      base_laser_offset_x = link->xyz[0];
      base_laser_offset_y = link->xyz[1];
      base_laser_offset_z = link->xyz[2];
    }
    link = pr2Description.getLink("tilt_laser");
    if (link)
    {
      tilt_laser_offset_x = link->xyz[0];
      tilt_laser_offset_y = link->xyz[1];
      tilt_laser_offset_z = link->xyz[2];
    }

  }


  void
  TestActuators::PublishFrameTransforms()
  {


    // FIXME: the frame transforms should be published by individual mechanism joints, not here
    // FIXME: the frame transforms should be published by individual mechanism joints, not here
    // FIXME: the frame transforms should be published by individual mechanism joints, not here
    /***************************************************************/
    /*                                                             */
    /*  frame transforms                                           */
    /*                                                             */
    /*  TODO: should we send z, roll, pitch, yaw? seems to confuse */
    /*        localization                                         */
    /*                                                             */
    /***************************************************************/
    double x=0,y=0,z=0,roll=0,pitch=0,yaw=0,vx,vy,vyaw;
    controller::Controller* bc = mc_.getControllerByName( "base_controller" );
    controller::BaseControllerNode* bcn = dynamic_cast<controller::BaseControllerNode*>(bc);
    if (bcn) bcn->getOdometry(x,y,yaw,vx,vy,vyaw);

    // FIXME: z, roll, pitch not accounted for
    tfs->sendInverseEuler("FRAMEID_ODOM",
                 "base",
                 x,
                 y,
                 z, //z - base_center_offset_z, /* get infor from xml: half height of base box */
                 yaw,
                 pitch,
                 roll,
                 timeMsg.rostime);

    /***************************************************************/
    /*                                                             */
    /*  frame transforms                                           */
    /*                                                             */
    /*  x,y,z,yaw,pitch,roll                                       */
    /*                                                             */
    /***************************************************************/
    tfs->sendEuler("base",
                 "FRAMEID_ROBOT",
                 0,
                 0,
                 0, 
                 0,
                 0,
                 0,
                 timeMsg.rostime);


    // base laser location
    tfs->sendEuler("base_laser",
                 "base",
                 base_laser_offset_x,
                 base_laser_offset_y,
                 base_laser_offset_z,
                 0.0,
                 0.0,
                 0.0,
                 timeMsg.rostime);

    // tilt laser location
    double tilt_laser_angle=0;
    controller::Controller* tlc = mc_.getControllerByName( "tilt_laser_controller" );
    controller::LaserScannerControllerNode* tlcn = dynamic_cast<controller::LaserScannerControllerNode*>(tlc);
    if (tlcn) tilt_laser_angle = tlcn->getMeasuredPosition();
    tfs->sendEuler("tilt_laser",
                 "torso",
                 tilt_laser_offset_x,
                 tilt_laser_offset_y,
                 tilt_laser_offset_z,
                 0.0,
                 tilt_laser_angle,
                 0.0,
                 timeMsg.rostime);
  }





#if 0
  void
  TestActuators::CmdLeftarmconfigReceived()
  {
    this->lock.lock();
    printf("hoo!\n");

    //Note: this API should not be used
    pr2_controllers::SetJointPosCmd::request req;
    pr2_controllers::SetJointPosCmd::response resp;

    std::vector<double> goals;
    goals.push_back(leftarmMsg.turretAngle       );
    goals.push_back(leftarmMsg.shoulderLiftAngle );
    goals.push_back(leftarmMsg.upperarmRollAngle );
    goals.push_back(leftarmMsg.elbowAngle        );
    goals.push_back(leftarmMsg.forearmRollAngle  );
    goals.push_back(leftarmMsg.wristPitchAngle   );
    goals.push_back(leftarmMsg.wristRollAngle    );

    req.set_positions_vec(goals);

    controller::Controller* j1 = mc_.getControllerByName( "left_arm_controller" );
    dynamic_cast<controller::ArmPositionControllerNode*>(j1)->setJointPosArray(goals);

    //Note: this is how the arm position controller should be used
    //TODO the names of the joints are incorrect
    pr2_controllers::JointPosCmd cmd;
    std::vector<std::string> arm_joint_names;
    arm_joint_names.push_back("left_turret_joint");
    arm_joint_names.push_back("left_shoulder_lift_joint");
    arm_joint_names.push_back("left_upperarm_roll_joint");
    arm_joint_names.push_back("left_elbow_joint");
    arm_joint_names.push_back("left_forearm_joint");
    arm_joint_names.push_back("left_wrist_pitch_joint");
    arm_joint_names.push_back("left_wrist_roll_joint");
    cmd.set_positions_vec(goals);
    cmd.set_names_vec(arm_joint_names);
//    dynamic_cast<controller::ArmPositionControllerNode*>(j1)->setJointPosSingleHeadless(cmd);

    controller::Controller* j8 = mc_.getControllerByName( "gripper_left_controller" );
    dynamic_cast<controller::JointPositionControllerNode*>(j8)->setCommand(leftarmMsg.gripperGapCmd);

    this->lock.unlock();
  }
  void
  TestActuators::CmdRightarmconfigReceived()
  {
    this->lock.lock();
    printf("hoo!\n");

    //Note: this API should not be used
    // see CmdLefarmconfigReceived
    pr2_controllers::SetJointPosCmd::request req;
    pr2_controllers::SetJointPosCmd::response resp;

    std::vector<double> goals;
    goals.push_back(rightarmMsg.turretAngle       );
    goals.push_back(rightarmMsg.shoulderLiftAngle );
    goals.push_back(rightarmMsg.upperarmRollAngle );
    goals.push_back(rightarmMsg.elbowAngle        );
    goals.push_back(rightarmMsg.forearmRollAngle  );
    goals.push_back(rightarmMsg.wristPitchAngle   );
    goals.push_back(rightarmMsg.wristRollAngle    );

    req.set_positions_vec(goals);

    controller::Controller* j1 = mc_.getControllerByName( "right_arm_controller" );
    dynamic_cast<controller::ArmPositionControllerNode*>(j1)->setJointPosArray(goals);

    controller::Controller* j8 = mc_.getControllerByName( "gripper_right_controller" );
    dynamic_cast<controller::JointPositionControllerNode*>(j8)->setCommand(rightarmMsg.gripperGapCmd);

    this->lock.unlock();
  }


  void
  TestActuators::CmdBaseVelReceived()
  {
    this->lock.lock();
    controller::Controller* cc = mc_.getControllerByName( "base_controller" );
    controller::BaseControllerNode* bc = dynamic_cast<controller::BaseControllerNode*>(cc);
    if (bc)
      bc->setCommand(velMsg.vx,0.0,velMsg.vw);
    this->lock.unlock();
  }




  void
  TestActuators::CmdLeftarmcartesianReceived()
  {
    this->lock.lock();

    KDL::Frame f;
    for(int i = 0; i < 9; i++) {
      f.M.data[i] = cmd_leftarmcartesian.rot[i];
    }
    for(int i = 0; i < 3; i++) {
      f.p.data[i] = cmd_leftarmcartesian.trans[i];
    }

    bool reachable;
    //this->PR2Copy->SetArmCartesianPosition(PR2::PR2_LEFT_ARM,f, reachable);

    this->lock.unlock();
  }
  void
  TestActuators::CmdRightarmcartesianReceived()
  {
    this->lock.lock();

    KDL::Frame f;
    for(int i = 0; i < 9; i++) {
      f.M.data[i] = cmd_rightarmcartesian.rot[i];
    }
    for(int i = 0; i < 3; i++) {
      f.p.data[i] = cmd_rightarmcartesian.trans[i];
    }

    bool reachable;
    //this->PR2Copy->SetArmCartesianPosition(PR2::PR2_RIGHT_ARM,f, reachable);

    this->lock.unlock();
  }

  bool TestActuators::SetRightArmCartesian(gazebo_plugin::MoveCartesian::request &req, gazebo_plugin::MoveCartesian::response &res)
  {
    this->lock.lock();
    KDL::Frame f;
    for(int i = 0; i < 9; i++)
      f.M.data[i] = req.e.rot[i];

    for(int i = 0; i < 3; i++)
      f.p.data[i] = req.e.trans[i];

          bool reachable;
    this->PR2Copy->SetArmCartesianPosition(PR2::PR2_RIGHT_ARM,f,reachable);
          res.reachable = (reachable==false) ? -1 : 0;

    this->lock.unlock();
          return true;
  }

  bool TestActuators::OperateRightGripper(gazebo_plugin::GripperCmd::request &req, gazebo_plugin::GripperCmd::response &res)
  {
          this->lock.lock();
          this->PR2Copy->hw.SetJointServoCmd(PR2::ARM_R_GRIPPER_GAP, req.gap, 0);
          this->lock.unlock();
          return true;
  }

  bool TestActuators::reset_IK_guess(gazebo_plugin::VoidVoid::request &req, gazebo_plugin::VoidVoid::response &res)
  {
    this->lock.lock();
          this->PR2Copy->GetArmJointPositionCmd(PR2::PR2_RIGHT_ARM, *(this->PR2Copy->right_arm_chain_->q_IK_guess));
    this->lock.unlock();
          return true;
  }
#endif



} // namespace gazebo
