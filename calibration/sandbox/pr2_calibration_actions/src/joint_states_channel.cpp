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

#include "pr2_calibration_actions/joint_states_channel.h"
#include "calibration_message_filters/stationary_utils.h"


using namespace calibration_message_filters;
using namespace pr2_calibration_actions;
using namespace std;

static const unsigned int SYNC_QUEUE_SIZE = 5;

JointStatesChannel::JointStatesChannel(const JointStatesChannelConfig& config, StationaryCallback cb)
{
  registerStationaryCallback(cb);

  cache_.setMaxSize(config.cache_size);
  tol_.tol_ = config.tolerances;

  deflater_.setDeflationJointNames(config.joint_names);

  if (config.padding < ros::Duration(0,0))
  {
    ROS_WARN("Received a negative padding. Negating it");
    padding_ = -config.padding;
  }
  else
    padding_ = config.padding;

  min_samples_ = config.min_samples;
  if (min_samples_ == 0)
  {
    ROS_WARN("min_samples cannot be 0. Setting to 1");
    min_samples_ = 1;
  }

  joint_states_sub_ = nh_.subscribe("joint_states", 1, &JointStatesChannel::jointStatesCallback, this);
}

void JointStatesChannel::registerStationaryCallback(StationaryCallback cb)
{
  stationary_callback_ = cb;
}

void JointStatesChannel::jointStatesCallback(const mechanism_msgs::JointStatesConstPtr& msg)
{
  DeflatedJointStates deflated;
  deflater_.deflate(msg, deflated);
  cache_.add(deflated);
  need_to_process_.add(deflated);
  processElems();
}

void JointStatesChannel::processElems()
{
  if (cache_.size() == 0)
  {
    ROS_DEBUG("Cache is empty. Going to skip processElems()");
    return;
  }

  // Remove the old stuff in the cache
  if (need_to_process_.front().header.stamp > ros::Time().fromSec(padding_.toSec()))
  {
    ROS_WARN("oldest time is %.3f. Padding is %.3f. Funny stuff might happen",
             need_to_process_.front().header.stamp.toSec(), padding_.toSec());
  }
  cache_.removeAllBeforeTime( need_to_process_.front().header.stamp - padding_);
  SortedDeque<DeflatedJointStates>::iterator it = need_to_process_.begin();

  vector<DeflatedJointStates> interval_elems;
  while (it != need_to_process_.end())
  {
    ros::Time start = it->header.stamp - padding_;
    ros::Time end   = it->header.stamp + padding_;
    if (start < cache_.front().header.stamp)
      need_to_process_.erase(it++);
    else
    {
      interval_elems = cache_.getSurroundingInterval(start, end);
      if (interval_elems.back().header.stamp < end)
        ++it;
      else
      {
        if(interval_elems.size() >= min_samples_)
        {
          vector<double> range;
          StationaryUtils::computeChannelRange<DeflatedJointStates>(interval_elems, range);
          if (StationaryUtils::isChannelStationary(range, tol_))
            processStationary(*it);
        }
        need_to_process_.erase(it++);
      }
    }
  }
}

void JointStatesChannel::processStationary(const DeflatedJointStates& deflated)
{
  //stationary_list_.add(deflated);
  if (stationary_callback_)
    stationary_callback_(deflated);
}






