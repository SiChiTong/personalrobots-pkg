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

//! \author Vijay Pradeep

#include "ros/node.h"
#include "tf/transform_listener.h"
#include "tf/message_notifier.h"
#include "robot_msgs/PointCloud.h"

#include <deque>

// Service
#include "point_cloud_assembler/BuildCloud.h"

#include "boost/thread.hpp"
#include "math.h"

namespace point_cloud_assembler
{

/**
 * \brief Maintains a history of point clouds and generates an aggregate point cloud upon request
 * \todo Clean up the doxygen part of this header
 *
 * @section parameters ROS Parameters
 *
 * Reads the following parameters from the parameter server
 *  - \b "~tf_cache_time_secs" (double) - The cache time (in seconds) to holds past transforms
 *  - \b "~max_scans" (unsigned int) - The number of scans to store in the assembler's history, until they're thrown away
 *  - \b "~fixed_frame" (string) - The frame to which received data should immeadiately be transformed to
 *  - \b "~downsampling_factor" (int) - Specifies how often to sample from a scan. 1 preserves all the data. 3 keeps only 1/3 of the points.
 *
 *  @section services ROS Service Calls
 *  - \b "~build_cloud" (BuildCloud.srv) - Accumulates scans between begin time and
 *              end time and returns the aggregated data as a point cloud
 */
template<class T>
class BaseAssemblerSrv : public ros::Node
{
public:
  BaseAssemblerSrv(const std::string& node_name) ;
  ~BaseAssemblerSrv() ;

  /** \brief Returns the number of points in the current scan
   * \param scan The scan for for which we want to know the number of points
   * \return the number of points in scan
   */
  virtual unsigned int GetPointsInScan(const T& scan) = 0 ;

  /** \brief Converts the current scan into a cloud in the specified fixed frame
   *
   * Note: Once implemented, ConvertToCloud should NOT catch TF exceptions. These exceptions are caught by
   * BaseAssemblerSrv, and will be counted for diagnostic information
   * \param fixed_frame_id The name of the frame in which we want cloud_out to be in
   * \param scan_in The scan that we want to convert
   * \param cloud_out The result of transforming scan_in into a cloud in frame fixed_frame_id
   */
  virtual void ConvertToCloud(const std::string& fixed_frame_id, const T& scan_in, robot_msgs::PointCloud& cloud_out) = 0 ;

protected:
  tf::TransformListener* tf_ ;

private:
  //! \brief Callback function for every time we receive a new scan
  //void scansCallback(const tf::MessageNotifier<T>::MessagePtr& scan_ptr, const T& testA)
  void scansCallback(const boost::shared_ptr<T>& scan_ptr) ;

  //! \brief Service Callback function called whenever we need to build a cloud
  bool buildCloud(BuildCloud::Request& req, BuildCloud::Response& resp) ;

  tf::MessageNotifier<T>* scan_notifier_ ;

  //! \brief Stores history of scans
  std::deque<robot_msgs::PointCloud> scan_hist_ ;
  boost::mutex scan_hist_mutex_ ;

  //! \brief The number points currently in the scan history
  unsigned int total_pts_ ;

  //! \brief The max number of scans to store in the scan history
  unsigned int max_scans_ ;

  //! \brief The frame to transform data into upon receipt
  std::string fixed_frame_ ;

  //! \brief Specify how much to downsample the data. A value of 1 preserves all the data. 3 would keep 1/3 of the data.
  unsigned int downsample_factor_ ;

} ;

template <class T>
BaseAssemblerSrv<T>::BaseAssemblerSrv(const std::string& node_name) : ros::Node(node_name)
{
  // **** Initialize TransformListener ****
  double tf_cache_time_secs ;
  param("~tf_cache_time_secs", tf_cache_time_secs, 10.0) ;
  if (tf_cache_time_secs < 0)
    ROS_ERROR("Parameter tf_cache_time_secs<0 (%f)", tf_cache_time_secs) ;
  tf_ = new tf::TransformListener(*this, true, ros::Duration(tf_cache_time_secs)) ;
  ROS_INFO("TF Cache Time: %f Seconds", tf_cache_time_secs) ;

  // ***** Set max_scans *****
  const int default_max_scans = 400 ;
  int tmp_max_scans ;
  param("~max_scans", tmp_max_scans, default_max_scans) ;
  if (tmp_max_scans < 0)
  {
    ROS_ERROR("Parameter max_scans<0 (%i)", tmp_max_scans) ;
    tmp_max_scans = default_max_scans ;
  }
  max_scans_ = tmp_max_scans ;
  ROS_INFO("Max Scans in History: %u", max_scans_) ;
  total_pts_ = 0 ;    // We're always going to start with no points in our history

  // ***** Set fixed_frame *****
  param("~fixed_frame", fixed_frame_, std::string("ERROR_NO_NAME")) ;
  ROS_INFO("Fixed Frame: %s", fixed_frame_.c_str()) ;
  if (fixed_frame_ == "ERROR_NO_NAME")
    ROS_ERROR("Need to set parameter fixed_frame") ;

  // ***** Set fixed_frame *****
  int tmp_downsample_factor ;
  param("~downsample_factor", tmp_downsample_factor, 1) ;
  if (tmp_downsample_factor < 1)
  {
    ROS_ERROR("Parameter downsample_factor<1: %i", tmp_downsample_factor) ;
    tmp_downsample_factor = 1 ;
  }
  downsample_factor_ = tmp_downsample_factor ;
  ROS_INFO("Downsample Factor: %u", downsample_factor_) ;

  // ***** Start Services *****
  advertiseService(getName()+"/build_cloud", &BaseAssemblerSrv<T>::buildCloud, this, 0) ;

  // ***** Start Listening to Data *****
  scan_notifier_ = new tf::MessageNotifier<T>(tf_, this, boost::bind(&BaseAssemblerSrv<T>::scansCallback, this, _1), "scan_in", fixed_frame_, 10) ;
}

template <class T>
BaseAssemblerSrv<T>::~BaseAssemblerSrv()
{
  delete scan_notifier_ ;
  unadvertiseService(getName()+"/build_cloud") ;
  delete tf_ ;
}

template <class T>
void BaseAssemblerSrv<T>::scansCallback(const boost::shared_ptr<T>& scan_ptr)
{
  const T scan = *scan_ptr ;

  robot_msgs::PointCloud cur_cloud ;

  // Convert the scan data into a universally known datatype: PointCloud
  try
  {
    ConvertToCloud(fixed_frame_, scan, cur_cloud) ;              // Convert scan into a point cloud
  }
  catch(tf::TransformException& ex)
  {
    ROS_WARN("Transform Exception %s", ex.what()) ;
    return ;
  }

  // Add the current scan (now of type PointCloud) into our history of scans
  scan_hist_mutex_.lock() ;
  if (scan_hist_.size() == max_scans_)                           // Is our deque full?
  {
    total_pts_ -= scan_hist_.front().get_pts_size() ;            // We're removing an elem, so this reduces our total point count
    scan_hist_.pop_front() ;                                     // The front of the deque has the oldest elem, so we can get rid of it
  }
  scan_hist_.push_back(cur_cloud) ;                              // Add the newest scan to the back of the deque
  total_pts_ += cur_cloud.get_pts_size() ;                       // Add the new scan to the running total of points

  //printf("Scans: %4u  Points: %10u\n", scan_hist_.size(), total_pts_) ;

  scan_hist_mutex_.unlock() ;
}

template <class T>
bool BaseAssemblerSrv<T>::buildCloud(BuildCloud::Request& req, BuildCloud::Response& resp)
{
  //printf("Starting Service Request\n") ;

  scan_hist_mutex_.lock() ;
  // Determine where in our history we actually are
  unsigned int i = 0 ;

  // Find the beginning of the request. Probably should be a search
  while ( i < scan_hist_.size() &&                                                    // Don't go past end of deque
          scan_hist_[i].header.stamp < req.begin )                                    // Keep stepping until we've exceeded the start time
  {
    i++ ;
  }
  unsigned int start_index = i ;

  unsigned int req_pts = 0 ;                                                          // Keep a total of the points in the current request
  // Find the end of the request
  while ( i < scan_hist_.size() &&                                                    // Don't go past end of deque
          scan_hist_[i].header.stamp < req.end )                                      // Don't go past the end-time of the request
  {
    req_pts += (scan_hist_[i].get_pts_size()+downsample_factor_-1)/downsample_factor_ ;
    i += downsample_factor_ ;
  }
  unsigned int past_end_index = i ;

  if (start_index == past_end_index)
  {
    resp.cloud.header.frame_id = fixed_frame_ ;
    resp.cloud.header.stamp = req.end ;
    resp.cloud.set_pts_size(0) ;
    resp.cloud.set_chan_size(0) ;
  }
  else
  {
    // Note: We are assuming that channel information is consistent across multiple scans. If not, then bad things (segfaulting) will happen
    // Allocate space for the cloud
    resp.cloud.set_pts_size( req_pts ) ;
    const unsigned int num_channels = scan_hist_[start_index].get_chan_size() ;
    resp.cloud.set_chan_size(num_channels) ;
    for (i = 0; i<num_channels; i++)
    {
      resp.cloud.chan[i].name = scan_hist_[start_index].chan[i].name ;
      resp.cloud.chan[i].set_vals_size(req_pts) ;
    }
    //resp.cloud.header.stamp = req.end ;
    resp.cloud.header.frame_id = fixed_frame_ ;
    unsigned int cloud_count = 0 ;
    for (i=start_index; i<past_end_index; i+=downsample_factor_)
    {
      for(unsigned int j=0; j<scan_hist_[i].get_pts_size(); j+=downsample_factor_)
      {
        resp.cloud.pts[cloud_count].x = scan_hist_[i].pts[j].x ;
        resp.cloud.pts[cloud_count].y = scan_hist_[i].pts[j].y ;
        resp.cloud.pts[cloud_count].z = scan_hist_[i].pts[j].z ;
        for (unsigned int k=0; k<num_channels; k++)
          resp.cloud.chan[k].vals[cloud_count] = scan_hist_[i].chan[k].vals[j] ;

        cloud_count++ ;
      }
      resp.cloud.header.stamp = scan_hist_[i].header.stamp;
    }
  }
  scan_hist_mutex_.unlock() ;

  ROS_DEBUG("Point Cloud Results: Aggregated from index %u->%u. BufferSize: %u", start_index, past_end_index, scan_hist_.size()) ;
  return true ;
}

}
