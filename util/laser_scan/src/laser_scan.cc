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

#include "laser_scan/laser_scan.h"
#include <algorithm>

namespace laser_scan
{

  void
    LaserProjection::projectLaser (const laser_scan::LaserScan& scan_in, robot_msgs::PointCloud & cloud_out, double range_cutoff, 
                                   bool preservative, int mask)
  {
    boost::numeric::ublas::matrix<double> ranges(2, scan_in.get_ranges_size());

    // Fill the ranges matrix
    for (unsigned int index = 0; index < scan_in.get_ranges_size(); index++)
      {
        ranges(0,index) = (double) scan_in.ranges[index];
        ranges(1,index) = (double) scan_in.ranges[index];
      }
    

    //Do the projection
    //    NEWMAT::Matrix output = NEWMAT::SP(ranges, getUnitVectors(scan_in.angle_min, scan_in.angle_max, scan_in.angle_increment));
    boost::numeric::ublas::matrix<double> output = element_prod(ranges, getUnitVectors(scan_in.angle_min, scan_in.angle_max, scan_in.angle_increment));

    //Stuff the output cloud
    cloud_out.header = scan_in.header;
    cloud_out.set_pts_size (scan_in.get_ranges_size ());
    
    // Define 4 indices in the channel array for each possible value type
    int idx_intensity = -1, idx_index = -1, idx_distance = -1, idx_timestamp = -1;
    
    // Check if the intensity bit is set
    if ((mask & MASK_INTENSITY) && scan_in.get_intensities_size () > 0)
    {
      cloud_out.set_chan_size (1);
      cloud_out.chan[0].name = "intensities";
      cloud_out.chan[0].set_vals_size (scan_in.get_intensities_size ());
      idx_intensity = 0;
    }
    
    // Check if the index bit is set
    if (mask & MASK_INDEX)
    {
      int chan_size = cloud_out.get_chan_size ();
      cloud_out.set_chan_size (chan_size + 1);
      cloud_out.chan[chan_size].name = "index";
      cloud_out.chan[chan_size].set_vals_size (scan_in.get_ranges_size ());
      idx_index = chan_size;
    }

    // Check if the distance bit is set
    if (mask & MASK_DISTANCE)
    {
      int chan_size = cloud_out.get_chan_size ();
      cloud_out.set_chan_size (chan_size + 1);
      cloud_out.chan[chan_size].name = "distances";
      cloud_out.chan[chan_size].set_vals_size (scan_in.get_ranges_size ());
      idx_distance = chan_size;
    }

    if (mask & MASK_TIMESTAMP)
    {
      int chan_size = cloud_out.get_chan_size ();
      cloud_out.set_chan_size (chan_size + 1);
      cloud_out.chan[chan_size].name = "stamps";
      cloud_out.chan[chan_size].set_vals_size (scan_in.get_ranges_size ());
      idx_timestamp = chan_size;
    }

    if (range_cutoff < 0)
      range_cutoff = scan_in.range_max;
    else
      range_cutoff = std::min(range_cutoff, (double)scan_in.range_max); 
    
    unsigned int count = 0;
    for (unsigned int index = 0; index< scan_in.get_ranges_size(); index++)
    {
      if (!preservative) //Default behaviour will throw out invalid data
      {
        if ((ranges(0,index) < range_cutoff) && (ranges(0,index) > scan_in.range_min)) //only valid
        {
          cloud_out.pts[count].x = output(0,index);
          cloud_out.pts[count].y = output(1,index);
          cloud_out.pts[count].z = 0.0;

          // Save the original point index
          if (idx_index != -1)
            cloud_out.chan[idx_index].vals[count] = index;

          // Save the original point distance
          if (idx_distance != -1)
            cloud_out.chan[idx_distance].vals[count] = ranges (0, index);

          if (scan_in.get_intensities_size() >= index)
          { /// \todo optimize and catch length difference better
            if (idx_intensity != -1)
              cloud_out.chan[idx_intensity].vals[count] = scan_in.intensities[index];
          }

          if( idx_timestamp != -1)
              cloud_out.chan[idx_timestamp].vals[count] = (float)index*scan_in.time_increment;

          count++;
        }
      }
      else //Keep all points
      {
        cloud_out.pts[count].x = output(0,index);
        cloud_out.pts[count].y = output(1,index);
        cloud_out.pts[count].z = 0.0;

        // Save the original point index
        if (idx_index != -1)
          cloud_out.chan[idx_index].vals[count] = index;

        // Save the original point distance
        if (idx_distance != -1)
          cloud_out.chan[idx_distance].vals[count] = ranges (0, index);

        if (scan_in.get_intensities_size() >= index)
        { /// \todo optimize and catch length difference better
          if (idx_intensity != -1)
            cloud_out.chan[idx_intensity].vals[count] = scan_in.intensities[index];
        }

        count++;
      }
        
    }

    //downsize if necessary
    cloud_out.set_pts_size (count);
    if (count == 0)
    {
      cloud_out.chan.resize (0);
    }
    else
    {
      for (unsigned int d = 0; d < cloud_out.get_chan_size (); d++)
        cloud_out.chan[d].set_vals_size(count);
    }
 
  };

  boost::numeric::ublas::matrix<double>& LaserProjection::getUnitVectors(float angle_min, float angle_max, float angle_increment)
  {
    //construct string for lookup in the map
    std::stringstream anglestring;
    anglestring <<angle_min<<","<<angle_max<<","<<angle_increment;
    std::map<std::string, boost::numeric::ublas::matrix<double>* >::iterator it;
    it = unit_vector_map_.find(anglestring.str());
    //check the map for presense
    if (it != unit_vector_map_.end())
      return *((*it).second);     //if present return
    //else calculate
    unsigned int length = (unsigned int) round((angle_max - angle_min)/angle_increment) + 1; ///\todo Codify how this parameter will be calculated in all cases
    boost::numeric::ublas::matrix<double> * tempPtr = new boost::numeric::ublas::matrix<double>(2,length);
    for (unsigned int index = 0;index < length; index++)
      {
        (*tempPtr)(0,index) = cos(angle_min + (double) index * angle_increment);
        (*tempPtr)(1,index) = sin(angle_min + (double) index * angle_increment);
      }
    //store 
    unit_vector_map_[anglestring.str()] = tempPtr;
    //and return
    return *tempPtr;
  };


  LaserProjection::~LaserProjection()
  {
    std::map<std::string, boost::numeric::ublas::matrix<double>*>::iterator it;
    it = unit_vector_map_.begin();
    while (it != unit_vector_map_.end())
      {
        delete (*it).second;
        it++;
      }
  };

  void
    LaserProjection::transformLaserScanToPointCloud (const std::string &target_frame, robot_msgs::PointCloud &cloud_out, const laser_scan::LaserScan &scan_in,
                                                     tf::Transformer& tf, int mask)
  {
    cloud_out.header = scan_in.header;
    cloud_out.header.frame_id = target_frame;
    cloud_out.set_pts_size (scan_in.get_ranges_size());
    
    // Define 4 indices in the channel array for each possible value type
    int idx_intensity = -1, idx_index = -1, idx_distance = -1, idx_timestamp = -1;
    
    // Check if the intensity bit is set
    if ((mask & MASK_INTENSITY) && scan_in.get_intensities_size () > 0)
    {
      cloud_out.set_chan_size (1);
      cloud_out.chan[0].name = "intensities";
      cloud_out.chan[0].set_vals_size (scan_in.get_intensities_size ());
      idx_intensity = 0;
    }
    
    // Check if the index bit is set
    if (mask & MASK_INDEX)
    {
      int chan_size = cloud_out.get_chan_size ();
      cloud_out.set_chan_size (chan_size + 1);
      cloud_out.chan[chan_size].name = "index";
      cloud_out.chan[chan_size].set_vals_size (scan_in.get_ranges_size ());
      idx_index = chan_size;
    }

    // Check if the distance bit is set
    if (mask & MASK_DISTANCE)
    {
      int chan_size = cloud_out.get_chan_size ();
      cloud_out.set_chan_size (chan_size + 1);
      cloud_out.chan[chan_size].name = "distances";
      cloud_out.chan[chan_size].set_vals_size (scan_in.get_ranges_size ());
      idx_distance = chan_size;
    }

    if (mask & MASK_TIMESTAMP)
    {
      int chan_size = cloud_out.get_chan_size ();
      cloud_out.set_chan_size (chan_size + 1);
      cloud_out.chan[chan_size].name = "stamps";
      cloud_out.chan[chan_size].set_vals_size (scan_in.get_ranges_size ());
      idx_timestamp = chan_size;
    }

    tf::Stamped<tf::Point> pointIn;
    tf::Stamped<tf::Point> pointOut;

    pointIn.frame_id_ = scan_in.header.frame_id;

    ///\todo this can be optimized
    robot_msgs::PointCloud intermediate; //optimize out

    projectLaser (scan_in, intermediate, -1.0, true, mask);

    // Extract transforms for the beginning and end of the laser scan
    ros::Time start_time = scan_in.header.stamp ;
    ros::Time end_time   = scan_in.header.stamp + ros::Duration().fromSec(scan_in.get_ranges_size()*scan_in.time_increment) ;

    tf::Stamped<tf::Transform> start_transform ;
    tf::Stamped<tf::Transform> end_transform ;
    tf::Stamped<tf::Transform> cur_transform ;

    tf.lookupTransform(target_frame, scan_in.header.frame_id, start_time, start_transform) ;
    tf.lookupTransform(target_frame, scan_in.header.frame_id, end_time, end_transform) ;

    unsigned int count = 0;  
    for (unsigned int i = 0; i < scan_in.get_ranges_size(); i++)
    {
      if (scan_in.ranges[i] < scan_in.range_max && scan_in.ranges[i] > scan_in.range_min) //only when valid
      {
        // Looking up transforms in tree is too expensive. Need more optimized way
        /*
           pointIn = tf::Stamped<tf::Point>(btVector3(intermediate.pts[i].x, intermediate.pts[i].y, intermediate.pts[i].z), 
           ros::Time(scan_in.header.stamp.to_ull() + (uint64_t) (scan_in.time_increment * 1000000000)),
           pointIn.frame_id_ = scan_in.header.frame_id);///\todo optimize to no copy
           transformPoint(target_frame, pointIn, pointOut);
           */

        // Instead, assume constant motion during the laser-scan, and use slerp to compute intermediate transforms
        btScalar ratio = i / ( (double) scan_in.get_ranges_size() - 1.0) ;

        //! \todo Make a function that performs both the slerp and linear interpolation needed to interpolate a Full Transform (Quaternion + Vector)

        //Interpolate translation
        btVector3 v (0, 0, 0);
        v.setInterpolate3(start_transform.getOrigin(), end_transform.getOrigin(), ratio) ;
        cur_transform.setOrigin(v) ;

        //Interpolate rotation
        btQuaternion q1, q2 ;
        start_transform.getBasis().getRotation(q1) ;
        end_transform.getBasis().getRotation(q2) ;

        // Compute the slerp-ed rotation
        cur_transform.setRotation( slerp( q1, q2 , ratio) ) ;

        // Apply the transform to the current point
        btVector3 pointIn(intermediate.pts[i].x, intermediate.pts[i].y, intermediate.pts[i].z) ;
        btVector3 pointOut = cur_transform * pointIn ;

        // Copy transformed point into cloud
        cloud_out.pts[count].x  = pointOut.x();
        cloud_out.pts[count].y  = pointOut.y();
        cloud_out.pts[count].z  = pointOut.z();


        // Copy index over from projected point cloud
        if (idx_index != -1)
          cloud_out.chan[idx_index].vals[count] = intermediate.chan[idx_index].vals[i];
        
        // Save the original point distance
        if (idx_distance != -1)
          cloud_out.chan[idx_distance].vals[count] = scan_in.ranges[i];

        if (scan_in.get_intensities_size() >= i)
        { /// \todo optimize and catch length difference better
          if (idx_intensity != -1)
            cloud_out.chan[idx_intensity].vals[count] = scan_in.intensities[i];
        }

        if (idx_timestamp != -1)
          cloud_out.chan[idx_timestamp].vals[count] = (float)i * scan_in.time_increment;

        count++;
      }

    }
    //downsize if necessary
    cloud_out.set_pts_size (count);
    if (count == 0)
    {
      cloud_out.chan.resize (0);
    }
    else
    {
      for (unsigned int d = 0; d < cloud_out.get_chan_size (); d++)
        cloud_out.chan[d].set_vals_size (count);
    }
  }


} //laser_scan
