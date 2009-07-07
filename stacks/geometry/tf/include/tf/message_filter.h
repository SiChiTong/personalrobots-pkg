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

/** \author Josh Faust */

#ifndef TF_MESSAGE_FILTER_H
#define TF_MESSAGE_FILTER_H

#include <ros/ros.h>
#include <tf/tf.h>
#include <tf/tfMessage.h>
#include <tf/message_notifier_base.h>

#include <string>
#include <list>
#include <vector>
#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/signals.hpp>

#include <ros/callback_queue.h>

#include <message_filters/connection.h>

#define TF_MESSAGEFILTER_DEBUG(fmt, ...) \
  ROS_DEBUG_NAMED("message_filter", "MessageFilter [target=%s]: "fmt, getTargetFramesString().c_str(), __VA_ARGS__)

#define TF_MESSAGEFILTER_WARN(fmt, ...) \
  ROS_WARN_NAMED("message_filter", "MessageFilter [target=%s]: "fmt, getTargetFramesString().c_str(), __VA_ARGS__)

namespace tf
{

class MessageFilterBase
{
public:
  virtual ~MessageFilterBase(){}
  virtual void clear() = 0;
  virtual void setTargetFrame(const std::string& target_frame) = 0;
  virtual void setTargetFrames(const std::vector<std::string>& target_frames) = 0;
  virtual void setTolerance(const ros::Duration& tolerance) = 0;
};

/**
 * \brief Follows the patterns set by the message_filters package to implement a filter which only passes messages through once there is transform data available
 *
 * The callbacks used in this class are of the same form as those used by roscpp's message callbacks.
 *
 * MessageFilter is templated on a message type.
 */
template<class M>
class MessageFilter : public MessageFilterBase
{
public:
  typedef boost::shared_ptr<M const> MConstPtr;
  typedef boost::function<void (const MConstPtr&)> Callback;
  typedef boost::signal<void (const MConstPtr&)> Signal;
  typedef boost::shared_ptr<Signal> SignalPtr;
  typedef boost::weak_ptr<Signal> SignalWPtr;

  /**
   * \brief Constructor
   *
   * \param tf The tf::Transformer this filter should use
   * \param target_frame The frame this filter should attempt to transform to.  To use multiple frames, pass an empty string here and use the setTargetFrames() function.
   * \param queue_size The number of messages to queue up before throwing away old ones.  0 means infinite (dangerous).
   * \param nh The NodeHandle to use for any necessary operations
   * \param max_rate The maximum rate to check for newly transformable messages
   * \param min_rate The minimum time between checks for newly transformable messages, in case a previous check that would have succeeded was skipped because of max_rate.  A duration of 0 here means no minimum.
   */
  MessageFilter(Transformer& tf, const std::string& target_frame, uint32_t queue_size, ros::NodeHandle nh = ros::NodeHandle(), ros::Duration max_rate = ros::Duration(0.01), ros::Duration min_rate = ros::Duration(0.1))
  : tf_(tf)
  , nh_(nh)
  , min_rate_(min_rate)
  , max_rate_(max_rate)
  , queue_size_(queue_size)
  {
    init();

    setTargetFrame(target_frame);
  }

  /**
   * \brief Constructor
   *
   * \param f The filter to connect this filter's input to.  Often will be a message_filters::Subscriber.
   * \param tf The tf::Transformer this filter should use
   * \param target_frame The frame this filter should attempt to transform to.  To use multiple frames, pass an empty string here and use the setTargetFrames() function.
   * \param queue_size The number of messages to queue up before throwing away old ones.  0 means infinite (dangerous).
   * \param nh The NodeHandle to use for any necessary operations
   * \param max_rate The maximum rate to check for newly transformable messages
   * \param min_rate The minimum time between checks for newly transformable messages, in case a previous check that would have succeeded was skipped because of max_rate.  A duration of 0 here means no minimum.
   */
  template<class F>
  MessageFilter(F& f, Transformer& tf, const std::string& target_frame, uint32_t queue_size, ros::NodeHandle nh = ros::NodeHandle(), ros::Duration max_rate = ros::Duration(0.01), ros::Duration min_rate = ros::Duration(0.1))
  : tf_(tf)
  , nh_(nh)
  , min_rate_(min_rate)
  , max_rate_(max_rate)
  , queue_size_(queue_size)
  {
    init();

    setTargetFrame(target_frame);

    connectTo(f);
  }

  /**
   * \brief Connect this filter's input to another filter's output.  If this filter is already connected, disconnects first.
   */
  template<class F>
  void connectTo(F& f)
  {
    message_connection_.disconnect();
    message_connection_ = f.connect(boost::bind(&MessageFilter::incomingMessage, this, _1));
  }

  /**
   * \brief Destructor
   */
  ~MessageFilter()
  {
    message_connection_.disconnect();
    tf_.removeTransformsChangedListener(tf_connection_);

    clear();

    TF_MESSAGEFILTER_DEBUG("Successful Transforms: %llu, Failed Transforms: %llu, Discarded due to age: %llu, Transform messages received: %llu, Messages received: %llu, Total dropped: %llu",
                        successful_transform_count_, failed_transform_count_, failed_out_the_back_count_, transform_message_count_, incoming_message_count_, dropped_message_count_);

  }

  /**
   * \brief Set the frame you need to be able to transform to before getting a message callback
   */
  void setTargetFrame(const std::string& target_frame)
  {
    std::vector<std::string> frames;
    frames.push_back(target_frame);
    setTargetFrames(frames);
  }

  /**
   * \brief Set the frames you need to be able to transform to before getting a message callback
   */
  void setTargetFrames(const std::vector<std::string>& target_frames)
  {
    boost::mutex::scoped_lock list_lock(messages_mutex_);
    boost::mutex::scoped_lock string_lock(target_frames_string_mutex_);

    target_frames_ = target_frames;

    std::stringstream ss;
    for (std::vector<std::string>::const_iterator it = target_frames_.begin(); it != target_frames_.end(); ++it)
    {
      ss << *it << " ";
    }
    target_frames_string_ = ss.str();
  }
  /**
   * \brief Get the target frames as a string for debugging
   */
  std::string getTargetFramesString()
  {
    boost::mutex::scoped_lock lock(target_frames_string_mutex_);
    return target_frames_string_;
  };

  /**
   * \brief Set the required tolerance for the notifier to return true
   */
  void setTolerance(const ros::Duration& tolerance)
  {
    time_tolerance_ = tolerance;
  }

  /**
   * \brief Clear any messages currently in the queue
   */
  void clear()
  {
    boost::mutex::scoped_lock lock(messages_mutex_);

    messages_.clear();
    message_count_ = 0;
  }

  /**
   * \brief Manually add a message into this filter.
   * \note If the message is immediately transformable, this will immediately call through to its output callback
   */
  void add(const MConstPtr& message)
  {
    if (testMessage(message))
    {
      return;
    }

    boost::mutex::scoped_lock lock(messages_mutex_);

    // If this message is about to push us past our queue size, erase the oldest message
    if (queue_size_ != 0 && message_count_ + 1 > queue_size_)
    {
      ++dropped_message_count_;
      TF_MESSAGEFILTER_DEBUG("Removed oldest message because buffer is full, count now %d (frame_id=%s, stamp=%f)", message_count_, messages_.front()->header.frame_id.c_str(), messages_.front()->header.stamp.toSec());
      messages_.pop_front();
      --message_count_;
    }

    // Add the message to our list
    messages_.push_back(message);
    ++message_count_;

    TF_MESSAGEFILTER_DEBUG("Added message in frame %s at time %.3f, count now %d", message->header.frame_id.c_str(), message->header.stamp.toSec(), message_count_);

    ++incoming_message_count_;
  }

  /**
   * \brief Connect a callback to the output of this filter
   */
  message_filters::Connection connect(const Callback& callback)
  {
    boost::mutex::scoped_lock lock(signal_mutex_);
    return message_filters::Connection(boost::bind(&MessageFilter::disconnect, this, _1), signal_.connect(callback));
  }

private:

  void init()
  {
    message_count_ = 0;
    new_transforms_ = false;
    successful_transform_count_ = 0;
    failed_transform_count_ = 0;
    failed_out_the_back_count_ = 0;
    transform_message_count_ = 0;
    incoming_message_count_ = 0;
    dropped_message_count_ = 0;
    time_tolerance_ = ros::Duration(0.0);

    tf_connection_ = tf_.addTransformsChangedListener(boost::bind(&MessageFilter::transformsChanged, this));

    if (min_rate_ > ros::Duration(0))
    {
      min_rate_timer_ = nh_.createTimer(min_rate_, &MessageFilter::minRateTimerCallback, this);
    }
  }

  typedef std::list<MConstPtr> L_Message;

  void signal(const MConstPtr& message)
  {
    boost::mutex::scoped_lock lock(signal_mutex_);
    signal_(message);
  }

  bool testMessage(const MConstPtr& message)
  {
    //Throw out messages which are too old
    //! \todo combine getLatestCommonTime call with the canTransform call
    for (std::vector<std::string>::iterator target_it = target_frames_.begin(); target_it != target_frames_.end(); ++target_it)
    {
      const std::string& target_frame = *target_it;

      if (target_frame != message->header.frame_id)
      {
        ros::Time latest_transform_time ;
        std::string error_string ;

        tf_.getLatestCommonTime(message->header.frame_id, target_frame, latest_transform_time, &error_string) ;
        if (message->header.stamp + tf_.getCacheLength() < latest_transform_time)
        {
          ++failed_out_the_back_count_;
          ++dropped_message_count_;
          TF_MESSAGEFILTER_DEBUG("Discarding Message, in frame %s, Out of the back of Cache Time(stamp: %.3f + cache_length: %.3f < latest_transform_time %.3f.  Message Count now: %d", message->header.frame_id.c_str(), message->header.stamp.toSec(),  tf_.getCacheLength().toSec(), latest_transform_time.toSec(), message_count_);

          last_out_the_back_stamp_ = message->header.stamp;
          last_out_the_back_frame_ = message->header.frame_id;
          return true;
        }
      }

    }

    bool ready = !target_frames_.empty();
    for (std::vector<std::string>::iterator target_it = target_frames_.begin(); ready && target_it != target_frames_.end(); ++target_it)
    {
      std::string& target_frame = *target_it;
      if (time_tolerance_ != ros::Duration(0.0))
      {
        ready = ready && (tf_.canTransform(target_frame, message->header.frame_id, message->header.stamp) &&
                          tf_.canTransform(target_frame, message->header.frame_id, message->header.stamp + time_tolerance_) );
      }
      else
      {
        ready = ready && tf_.canTransform(target_frame, message->header.frame_id, message->header.stamp);
      }
    }

    if (ready)
    {
      TF_MESSAGEFILTER_DEBUG("Message ready in frame %s at time %.3f, count now %d", message->header.frame_id.c_str(), message->header.stamp.toSec(), message_count_);

      ++successful_transform_count_;

      signal(message);
    }
    else
    {
      ++failed_transform_count_;
    }

    return ready;
  }

  void testMessages()
  {
    last_test_time_ = ros::Time::now();

    if (!messages_.empty() && getTargetFramesString() == " ")
    {
      ROS_WARN_NAMED("message_notifier", "MessageFilter [target=%s]: empty target frame", getTargetFramesString().c_str());
    }

    int i = 0;

    typename L_Message::iterator it = messages_.begin();
    for (; it != messages_.end(); ++i)
    {
      MConstPtr& message = *it;

      if (testMessage(message))
      {
        --message_count_;
        it = messages_.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  void minRateTimerCallback(const ros::TimerEvent&)
  {
    if (new_transforms_)
    {
      testMessages();
      new_transforms_ = false;
    }

    checkFailures();
  }

  /**
   * \brief Callback that happens when we receive a message on the message topic
   */
  void incomingMessage(const MConstPtr& msg)
  {
    add(msg);
  }

  void transformsChanged()
  {
    if (ros::Time::now() - last_test_time_ >= max_rate_)
    {
      testMessages();
    }
    else
    {
      new_transforms_ = true;
    }

    ++transform_message_count_;
  }

  void checkFailures()
  {
    if (next_failure_warning_.isZero())
    {
      next_failure_warning_ = ros::Time::now() + ros::Duration(15);
    }

    if (ros::Time::now() >= next_failure_warning_)
    {
      if (incoming_message_count_ - message_count_ == 0)
      {
        return;
      }

      double dropped_pct = (double)dropped_message_count_ / (double)(incoming_message_count_ - message_count_);
      if (dropped_pct > 0.95)
      {
        TF_MESSAGEFILTER_WARN("Dropped %.2f%% of messages so far. Please turn the [%s.message_notifier] rosconsole logger to DEBUG for more information.", dropped_pct*100, ROSCONSOLE_DEFAULT_NAME);
        next_failure_warning_ = ros::Time::now() + ros::Duration(60);

        if ((double)failed_out_the_back_count_ / (double)dropped_message_count_ > 0.5)
        {
          TF_MESSAGEFILTER_WARN("  The majority of dropped messages were due to messages growing older than the TF cache time.  The last message's timestamp was: %f, and the last frame_id was: %s", last_out_the_back_stamp_.toSec(), last_out_the_back_frame_.c_str());
        }
      }
    }
  }

  void disconnect(const message_filters::Connection& c)
  {
    boost::mutex::scoped_lock lock(signal_mutex_);
    signal_.disconnect(c.getBoostConnection());
  }

  Transformer& tf_; ///< The Transformer used to determine if transformation data is available
  ros::NodeHandle nh_; ///< The node used to subscribe to the topic
  ros::Duration min_rate_;
  ros::Duration max_rate_;
  ros::Timer min_rate_timer_;
  ros::Time last_test_time_;
  std::vector<std::string> target_frames_; ///< The frames we need to be able to transform to before a message is ready
  std::string target_frames_string_;
  boost::mutex target_frames_string_mutex_;
  uint32_t queue_size_; ///< The maximum number of messages we queue up

  Signal signal_;
  boost::mutex signal_mutex_;

  L_Message messages_; ///< The message list
  uint32_t message_count_; ///< The number of messages in the list.  Used because messages_.size() has linear cost
  boost::mutex messages_mutex_; ///< The mutex used for locking message list operations

  bool new_messages_; ///< Used to skip waiting on new_data_ if new messages have come in while calling back
  volatile bool new_transforms_; ///< Used to skip waiting on new_data_ if new transforms have come in while calling back or transforming data

  uint64_t successful_transform_count_;
  uint64_t failed_transform_count_;
  uint64_t failed_out_the_back_count_;
  uint64_t transform_message_count_;
  uint64_t incoming_message_count_;
  uint64_t dropped_message_count_;

  ros::Time last_out_the_back_stamp_;
  std::string last_out_the_back_frame_;

  ros::Time next_failure_warning_;

  ros::Duration time_tolerance_; ///< Provide additional tolerance on time for messages which are stamped but can have associated duration

  boost::signals::connection tf_connection_;
  message_filters::Connection message_connection_;
};

} // namespace tf

#endif

