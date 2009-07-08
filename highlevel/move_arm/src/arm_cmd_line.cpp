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
 *   * Neither the name of Willow Garage nor the names of its
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
 *
 *********************************************************************/

/* \author: Ioan Sucan */

#include <ros/ros.h>
#include <robot_actions/action_client.h>

#include <planning_environment/kinematic_model_state_monitor.h>
#include <pr2_robot_actions/MoveArmGoal.h>
#include <pr2_robot_actions/MoveArmState.h>

#include <boost/thread/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <map>

void printHelp(void)
{
    std::cout << "Basic commands:" << std::endl;
    std::cout << "   - help       : this screen" << std::endl;
    std::cout << "   - list       : prints the list of arm joints" << std::endl;
    std::cout << "   - time       : shows the allowed execution time for arm movement" << std::endl;
    std::cout << "   - time <val> : sets the allowed execution time for arm movement" << std::endl;
    std::cout << "   - quit       : quits this program" << std::endl;
    std::cout << "Arm configuration commands:" << std::endl;
    std::cout << "   - show                    : shows the available configs" << std::endl;
    std::cout << "   - show <config>           : shows the values in <config>" << std::endl;
    std::cout << "   - clear                   : clears all stored configs" << std::endl;
    std::cout << "   - current                 : show the values of the current configuration" << std::endl;
    std::cout << "   - current <config>        : set <config> to the current position of the arm" << std::endl;
    std::cout << "   - diff <config>           : show the difference from current position of the arm to <config>" << std::endl;
    std::cout << "   - go <config>             : sends the command <config> to the arm" << std::endl;
    std::cout << "   - go <px> <py> <pz>       : move the end effector to pose (<px>, <py>, <pz>, 0, 0, 0, 1)" << std::endl;
    std::cout << "   - <config>[<idx>] = <val> : sets the joint specified by <idx> to <val> in <config>" << std::endl;
    std::cout << "   - <config2> = <config1>   : copy <config1> to <config2>" << std::endl;
    std::cout << "   - <config>                : same as show(<config>)" << std::endl;
}

void printJoints(const std::vector<std::string> &names)
{
    for (unsigned int i = 0 ; i < names.size(); ++i)
	std::cout << "  " << i << " = " << names[i] << std::endl;
}

void printConfig(const pr2_robot_actions::MoveArmGoal &goal)
{
    for (unsigned int i = 0 ; i < goal.goal_constraints.joint_constraint.size(); ++i)
	std::cout << "  " << goal.goal_constraints.joint_constraint[i].joint_name << " = " << goal.goal_constraints.joint_constraint[i].value[0] << std::endl;
}

void printConfigs(const std::map<std::string, pr2_robot_actions::MoveArmGoal> &goals)
{
    for (std::map<std::string, pr2_robot_actions::MoveArmGoal>::const_iterator it = goals.begin() ; it != goals.end() ; ++it)
	std::cout << "  " << it->first << std::endl;
}

void setupGoal(const std::vector<std::string> &names, pr2_robot_actions::MoveArmGoal &goal)
{
    goal.goal_constraints.joint_constraint.resize(names.size());
    for (unsigned int i = 0 ; i < goal.goal_constraints.joint_constraint.size(); ++i)
    {
	goal.goal_constraints.joint_constraint[i].header.stamp = ros::Time::now();
	goal.goal_constraints.joint_constraint[i].header.frame_id = "/base_link";
	goal.goal_constraints.joint_constraint[i].joint_name = names[i];
	goal.goal_constraints.joint_constraint[i].value.resize(1);
	goal.goal_constraints.joint_constraint[i].toleranceAbove.resize(1);
	goal.goal_constraints.joint_constraint[i].toleranceBelow.resize(1);
	goal.goal_constraints.joint_constraint[i].value[0] = 0.0;
	goal.goal_constraints.joint_constraint[i].toleranceBelow[0] = 0.0;
	goal.goal_constraints.joint_constraint[i].toleranceAbove[0] = 0.0;
    }
}

void setupGoalEEf(const std::string &link, double x, double y, double z, pr2_robot_actions::MoveArmGoal &goal)
{
    goal.goal_constraints.pose_constraint.resize(1);
    goal.goal_constraints.pose_constraint[0].type = motion_planning_msgs::PoseConstraint::POSITION_XYZ + motion_planning_msgs::PoseConstraint::ORIENTATION_RPY;
    goal.goal_constraints.pose_constraint[0].link_name = link;
    goal.goal_constraints.pose_constraint[0].pose.header.stamp = ros::Time::now();
    goal.goal_constraints.pose_constraint[0].pose.header.frame_id = "/base_link";
    goal.goal_constraints.pose_constraint[0].pose.pose.position.x = x;
    goal.goal_constraints.pose_constraint[0].pose.pose.position.y = y;	
    goal.goal_constraints.pose_constraint[0].pose.pose.position.z = z;	
    
    goal.goal_constraints.pose_constraint[0].pose.pose.orientation.x = 0.0;
    goal.goal_constraints.pose_constraint[0].pose.pose.orientation.y = 0.0;
    goal.goal_constraints.pose_constraint[0].pose.pose.orientation.z = 0.0;
    goal.goal_constraints.pose_constraint[0].pose.pose.orientation.w = 1.0;
    
    goal.goal_constraints.pose_constraint[0].position_distance = 0.0001;
    goal.goal_constraints.pose_constraint[0].orientation_distance = 0.01;
    goal.goal_constraints.pose_constraint[0].orientation_importance = 0.001;
}

void setConfig(const planning_models::StateParams *sp, const std::vector<std::string> &names, pr2_robot_actions::MoveArmGoal &goal)
{
    setupGoal(names, goal);
    for (unsigned int i = 0 ; i < names.size() ; ++i)
    {
	goal.goal_constraints.joint_constraint[i].value[0] = 
	    sp->getParamsJoint(goal.goal_constraints.joint_constraint[i].joint_name)[0];
    }
}

void diffConfig(const planning_models::StateParams *sp, pr2_robot_actions::MoveArmGoal &goal)
{
    for (unsigned int i = 0 ; i < goal.goal_constraints.joint_constraint.size(); ++i)
    {
	std::cout << "  " << goal.goal_constraints.joint_constraint[i].value[0] - sp->getParamsJoint(goal.goal_constraints.joint_constraint[i].joint_name)[0]
		  << std::endl;
    }
}

void setConfigJoint(const unsigned int pos, const double value, pr2_robot_actions::MoveArmGoal &goal)
{
    goal.goal_constraints.joint_constraint[pos].value[0] = value;
}

void spinThread(void)
{
    ros::spin();
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "cmd_line_move_arm", ros::init_options::NoSigintHandler | ros::init_options::AnonymousName);
    
    std::string arm = "r";
    if (argc >= 2)
	if (argv[1][0] == 'l')
	    arm = "l";
    
    ros::NodeHandle nh;
    robot_actions::ActionClient<pr2_robot_actions::MoveArmGoal, pr2_robot_actions::MoveArmState, int32_t> move_arm("move_arm");
    
    int32_t                                               feedback;
    std::map<std::string, pr2_robot_actions::MoveArmGoal> goals;
    
    std::vector<std::string> names(7);
    names[0] = arm + "_shoulder_pan_joint";
    names[1] = arm + "_shoulder_lift_joint";
    names[2] = arm + "_upper_arm_roll_joint";
    names[3] = arm + "_elbow_flex_joint";
    names[4] = arm + "_forearm_roll_joint";
    names[5] = arm + "_wrist_flex_joint";
    names[6] = arm + "_wrist_roll_joint";
    

    planning_environment::RobotModels rm("robot_description");
    if (!rm.loadedModels())
	return 0;

    boost::thread th(&spinThread);    

    planning_environment::KinematicModelStateMonitor km(&rm);
    km.waitForState();
    
    std::cout << std::endl << std::endl << "Using joints:" << std::endl;
    printJoints(names);
    std::cout << std::endl;
    double allowed_time = 10.0;

    while (nh.ok() && std::cin.good() && !std::cin.eof())
    {
	std::cout << "command> ";
	std::string cmd;
	std::getline(std::cin, cmd);
	boost::trim(cmd);
	ros::spinOnce();
	
	if (!nh.ok())
	    break;

	if (cmd == "")
	    continue;

	if (cmd == "help")
	    printHelp();
	else
	if (cmd == "list")
	    printJoints(names);
	else
	if (cmd == "quit")
	    break;
	else
	if (cmd == "show")
	    printConfigs(goals);
	else
	if (cmd == "current")
	{
	    pr2_robot_actions::MoveArmGoal temp;
	    setConfig(km.getRobotState(), names, temp);
	    printConfig(temp);
	}
	else
	if (cmd == "time")
	{
	    std::cout << "  Allowed execution time is " << allowed_time << " seconds" << std::endl;
	}
	else
	if (cmd.substr(0, 5) == "time " && cmd.length() > 5)
	{
	    std::stringstream ss(cmd.substr(5));
	    if (ss.good() && !ss.eof())
	        ss >> allowed_time;
	    else
	        std::cerr << "Unable to parse time value " << ss.str() << std::endl;
	    if (allowed_time <= 0.0)
	        allowed_time = 10.0;
	    std::cout << "  Allowed execution time is " << allowed_time << " seconds" << std::endl;
	}
	else
	if (cmd == "clear")
	    goals.clear();
	else
	if (cmd.length() > 5 && cmd.substr(0, 5) == "diff ")
	{
	    std::string config = cmd.substr(5);
	    boost::trim(config);
	    if (goals.find(config) == goals.end())
		std::cout << "Configuration '" << config << "' not found" << std::endl;
	    else
		diffConfig(km.getRobotState(), goals[config]);
	}
	else
	if (cmd.length() > 5 && cmd.substr(0, 5) == "show ")
	{
	    std::string config = cmd.substr(5);
	    boost::trim(config);
	    if (goals.find(config) == goals.end())
		std::cout << "Configuration '" << config << "' not found" << std::endl;
	    else
		printConfig(goals[config]);
	}
	else
	if (cmd.length() > 8 && cmd.substr(0, 8) == "current ")
	{
	    std::string config = cmd.substr(8);
	    boost::trim(config);
	    setConfig(km.getRobotState(), names, goals[config]);
	}
	else
	if (cmd.length() > 3 && cmd.substr(0, 3) == "go ")
	{
	    std::string config = cmd.substr(3);
	    boost::trim(config);
	    if (goals.find(config) == goals.end())
	    {
		std::stringstream ss(config);
		double x, y, z;
		bool err = true;
		if (ss.good() && !ss.eof())
		{
		    ss >> x;
		    if (ss.good() && !ss.eof())
		    {
			ss >> y;
			if (ss.good() && !ss.eof())
			{
			    ss >> z;
			    err = false;
			    std::string link = km.getKinematicModel()->getJoint(names.back())->after->name;
			    std::cout << "Moving " << link << " to " << x << ", " << y << ", " << z << ", 0, 0, 0, 1..." << std::endl;
			    pr2_robot_actions::MoveArmGoal g;
			    setupGoalEEf(link, x, y, z, g);
			    if (move_arm.execute(g, feedback, ros::Duration(allowed_time)) != robot_actions::SUCCESS)
				std::cerr << "Failed achieving goal" << std::endl;
			    else
				std::cout << "Success!" << std::endl;
			}
		    }
		}
		if (err)
		    std::cout << "Configuration '" << config << "' not found" << std::endl;
	    }
	    else
	    {
		std::cout << "Moving to " << config << "..." << std::endl;
		if (move_arm.execute(goals[config], feedback, ros::Duration(allowed_time)) != robot_actions::SUCCESS)
		    std::cerr << "Failed achieving goal" << std::endl;
		else
		    std::cout << "Success!" << std::endl;
	    }
	}
	else
	if (cmd.length() > 5 && cmd.find_first_of("[") != std::string::npos)
	{
	    std::size_t p1 = cmd.find_first_of("[");
	    std::size_t p2 = cmd.find_last_of("]");
	    std::size_t p3 = cmd.find_first_of("=");
	    if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos && p2 > p1 + 1 && p3 > p2)
	    {
		std::string config = cmd.substr(0, p1);
		boost::trim(config);
		if (goals.find(config) == goals.end())
		    std::cout << "Configuration '" << config << "' not found" << std::endl;
		else
		{
		    std::stringstream ss(cmd.substr(p1 + 1, p2 - p1 - 1));
		    unsigned int joint = names.size();
		    if (ss.good() && !ss.eof())
			ss >> joint;
		    if (joint >= names.size())
			std::cerr << "Joint index out of bounds" << std::endl;
		    else
		    {
			std::stringstream ss(cmd.substr(p3 + 1));
			if (ss.good() && !ss.eof())
			{
			    double value;
			    ss >> value;
			    setConfigJoint(joint, value, goals[config]);
			}
			else
			    std::cerr << "Cannot parse value: '" << ss.str() << "'" << std::endl;
		    }
		}
	    }
	    else
		std::cerr << "Incorrect command format" << std::endl;
	}
	else
	if (cmd.length() > 2 && cmd.find_first_of("=") != std::string::npos)
	{
	    std::size_t p = cmd.find_first_of("=");
	    std::string c1 = cmd.substr(0, p);
	    std::string c2 = cmd.substr(p + 1);
	    boost::trim(c1);
	    boost::trim(c2);
	    if (goals.find(c2) != goals.end())
		goals[c1] = goals[c2];
	    else
		std::cout << "Configuration '" << c2 << "' not found" << std::endl;
	}
	else
	if (goals.find(cmd) != goals.end())
	    printConfig(goals[cmd]);
	else
	    std::cerr << "Unknown command. Try 'help'" << std::endl;
    }
    
    return 0;
}
