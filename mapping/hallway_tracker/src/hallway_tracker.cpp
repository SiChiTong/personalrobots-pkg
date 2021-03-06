//Software License Agreement (BSD License)

//Copyright (c) 2009, Willow Garage, Inc.
//All rights reserved.
//
//Redistribution and use in source and binary forms, with or without
//modification, are permitted provided that the following conditions
//are met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above
//   copyright notice, this list of conditions and the following
//   disclaimer in the documentation and/or other materials provided
//   with the distribution.
// * Neither the name of Willow Garage, Inc. nor the names of its
//   contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.
//
//THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
//ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//POSSIBILITY OF SUCH DAMAGE.

/**
   @mainpage

   @htmlinclude manifest.html

   \author Caroline Pantofaru

   @b hallway_tracker Given that the robot is in a hallway, find the two dominant walls.

**/

#include <sys/time.h>
#include <float.h>

// ROS core
#include <ros/node.h>
// ROS messages
#include <sensor_msgs/PointCloud.h>
#include <geometry_msgs/Polygon.h>
#include <mapping_msgs/PolygonalMap.h>

#include <geometry_msgs/Point32.h>
#include <visualization_msgs/Marker.h>

// Sample Consensus
#include <point_cloud_mapping/sample_consensus/sac.h>
//#include <point_cloud_mapping/sample_consensus/msac.h>
#include <point_cloud_mapping/sample_consensus/ransac.h>
//#include <point_cloud_mapping/sample_consensus/lmeds.h>
#include <point_cloud_mapping/sample_consensus/sac_model_parallel_lines.h>

// Cloud geometry
#include <point_cloud_mapping/geometry/areas.h>
#include <point_cloud_mapping/geometry/point.h>
#include <point_cloud_mapping/geometry/distances.h>
#include <point_cloud_mapping/geometry/nearest.h>
#include <point_cloud_mapping/geometry/statistics.h>

// Transformations
#include <tf/transform_listener.h>
#include <tf/message_notifier.h>

// Clouds and scans
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/LaserScan.h>
#include <laser_geometry/laser_geometry.h>


//Filters
#include "filters/filter_chain.h"

using namespace std;
using namespace geometry_msgs;
using namespace mapping_msgs;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * \brief Assuming that the robot is in a hallway, find the two dominant lines.
 * \note The walls are defined as two parallel lines, appropriately spaced, in the 2D base scan.
 * \note Each scan is processed independently. A possible extension is to add a time filter for more stable approximation.
 * \note Walls may be found at any angle. This could be extended to look for walls only within a certain angle range.
 */
class HallwayTracker
{
public:

  ros::Node *node_;

  sensor_msgs::PointCloud cloud_;
  laser_geometry::LaserProjection projector_; // Used to project laser scans into point clouds

  tf::TransformListener *tf_;
  filters::FilterChain<sensor_msgs::LaserScan> filter_chain_;
  tf::MessageNotifier<sensor_msgs::LaserScan>* message_notifier_;

  /********** Parameters from the param server *******/
  std::string base_laser_topic_; // Topic for the laser scan message.
  int sac_min_points_per_model_; // Minimum number of points in the scan within max_point_dist_m_ of the robot needed for a model.
  double sac_distance_threshold_; // The distance threshold used by the SAC methods for declaring a point an inlier of the model.
  //double eps_angle_; // For possible later use if the method is extended to find oriented parallel lines.
  std::string fixed_frame_; // Frame to work in.
  double min_hallway_width_m_, max_hallway_width_m_; // Minimum and maximum hallway widths.
  double max_point_dist_m_; // Max distance from the robot of points in the model.



  HallwayTracker():filter_chain_("sensor_msgs::LaserScan"), message_notifier_(NULL)
  {
    node_ = ros::Node::instance();
    tf_ = new tf::TransformListener(*node_);

    // Get params from the param server.
    node_->param("~p_base_laser_topic", base_laser_topic_, string("base_scan"));
    node_->param("~p_sac_min_points_per_model", sac_min_points_per_model_, 50);
    node_->param("~p_sac_distance_threshold", sac_distance_threshold_, 0.03);     // 3 cm
    //node_->param("~p_eps_angle", eps_angle_, 10.0);                              // 10 degrees
    node_->param("~p_fixed_frame", fixed_frame_, string("odom_combined"));
    node_->param("~p_min_hallway_width_m", min_hallway_width_m_, 1.0);
    node_->param("~p_max_hallway_width_m", max_hallway_width_m_, 5.0);
    node_->param("~p_max_point_dist_m", max_point_dist_m_, 5.0);

    //eps_angle_ = cloud_geometry::deg2rad (eps_angle_);                      // convert to radians

    //Laser Scan Filtering

    filter_chain_.configure("~filters");


    // Visualization:
    // The visualization markers are the two lines. The start/end points are arbitrary.
    node_->advertise<visualization_msgs::Marker>( "visualization_marker", 0 );
    // A point cloud of model inliers.
    node_->advertise<sensor_msgs::PointCloud>("parallel_lines_inliers",10);

    // Output: a point cloud with 3 points. The first two points lie on the first line, and the third point lies on the second line.
    node_->advertise<sensor_msgs::PointCloud>("parallel_lines_model",0);

    // Subscribe to the scans.
    message_notifier_ = new tf::MessageNotifier<sensor_msgs::LaserScan> (tf_, node_, boost::bind(&HallwayTracker::laserCallBack, this, _1), base_laser_topic_.c_str(), fixed_frame_, 1);
    message_notifier_->setTolerance(ros::Duration(.02));
  }

  ~HallwayTracker()
  {
    node_->unadvertise("visualization_marker");
    node_->unadvertise("parallel_lines_inliers");
    delete message_notifier_;
  }

  /**
   * \brief Laser callback. Processes a laser scan by converting it into a point cloud, removing points that are too far away, finding two parallel lines and publishing the results.
   */
  void laserCallBack(const tf::MessageNotifier<sensor_msgs::LaserScan>::MessagePtr& scan_msg)
  {
    sensor_msgs::LaserScan filtered_scan;
    filter_chain_.update (*scan_msg, filtered_scan);


    // Transform into a PointCloud message
    int mask = laser_geometry::MASK_INTENSITY | laser_geometry::MASK_DISTANCE | laser_geometry::MASK_INDEX | laser_geometry::MASK_TIMESTAMP;
    try {
      projector_.transformLaserScanToPointCloud(fixed_frame_, cloud_, filtered_scan, *tf_, mask);
    }
    catch (tf::TransformException& e) {
      ROS_WARN ("TF exception transforming scan to cloud: %s", e.what());
      return;
    }

    // Check that the cloud is large enough.
    if(cloud_.points.empty())
    {
      ROS_WARN("Received an empty point cloud");
      return;
    }
    if ((int)cloud_.points.size() < sac_min_points_per_model_)
    {
      ROS_WARN("Insufficient points in the scan for the parallel lines model.");
      return;
    }

    int cloud_size = cloud_.points.size();
    vector<int> possible_hallway_points;
    possible_hallway_points.resize(cloud_size);

    // Keep only points that are within max_point_dist_m_ of the robot.
    int iind = 0;
    tf::Stamped<tf::Point> tmp_pt_in, tmp_pt_out;
    tmp_pt_in.stamp_ = cloud_.header.stamp;
    tmp_pt_in.frame_id_ = cloud_.header.frame_id;
    for (int i=0; i<cloud_size; ++i) {
      tmp_pt_in[0] = cloud_.points[i].x;
      tmp_pt_in[1] = cloud_.points[i].y;
      tmp_pt_in[2] = cloud_.points[i].z;
      try {
      tf_->transformPoint("base_laser", tmp_pt_in, tmp_pt_out); // Get distance from the robot.
      }
      catch (tf::TransformException& e) {
        ROS_WARN ("TF exception transforming point: %s", e.what());
      }

      if (tmp_pt_out.length() < max_point_dist_m_) {
	possible_hallway_points[iind] = i;
	iind++;
      }
    }
    possible_hallway_points.resize(iind);

    //// Find the dominant lines ////
    vector<int> inliers;
    vector<double> coeffs;

    // Create and initialize the SAC model
    sample_consensus::SACModelParallelLines *model = new sample_consensus::SACModelParallelLines(min_hallway_width_m_,max_hallway_width_m_);
    // RANSAC works best! For choosing the best model, a strict inlier count is better than the average point->model distance.
    sample_consensus::SAC *sac = new sample_consensus::RANSAC(model, sac_distance_threshold_);
    sac->setMaxIterations (500);
    sac->setProbability (0.98);
    model->setDataSet(&cloud_, possible_hallway_points);


    // Now find the best fit parallel lines to this set of points and a corresponding set of inliers
    if (sac->computeModel()) {
      inliers = sac->getInliers();
      sac->computeCoefficients (coeffs);
      visualization(coeffs, inliers);
      // Publish the result
      sensor_msgs::PointCloud model_cloud;
      model_cloud.points.resize(3);
      model_cloud.header.stamp = cloud_.header.stamp;
      model_cloud.header.frame_id = fixed_frame_;
      model_cloud.points[0].x = coeffs[0];
      model_cloud.points[0].y = coeffs[1];
      model_cloud.points[0].z = coeffs[2];
      model_cloud.points[1].x = coeffs[3];
      model_cloud.points[1].y = coeffs[4];
      model_cloud.points[1].z = coeffs[5];
      model_cloud.points[2].x = coeffs[6];
      model_cloud.points[2].y = coeffs[7];
      model_cloud.points[2].z = coeffs[8];
      node_->publish("parallel_lines_model", model_cloud);
    }
    else {
      // No parallel lines were found.
    }

  }


  /**
   * \brief Compute and publish the visualization info, including the two lines and the model inliers point cloud.
   */
  void visualization(std::vector<double> coeffs, std::vector<int> inliers) {
     // First line:
    visualization_msgs::Marker marker;
    marker.header.frame_id = fixed_frame_;
    marker.header.stamp = cloud_.header.stamp;
    marker.ns = "hallway_tracker";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::LINE_STRIP;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = 0.05;//0.01;
    marker.scale.y = 0.05;//0.1;
    marker.scale.z = 0.05;//0.1;
    marker.color.a = 1.0;
    marker.color.g = 1.0;
    marker.set_points_size(2);

    geometry_msgs::Point32 d;
    d.x = coeffs[3] - coeffs[0];
    d.y = coeffs[4] - coeffs[1];
    d.z = coeffs[5] - coeffs[2];
    double l = sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
    d.x = d.x/l;
    d.y = d.y/l;
    d.z = d.z/l;

    marker.points[0].x = coeffs[0] - 6*d.x;
    marker.points[0].y = coeffs[1] - 6*d.y;
    marker.points[0].z = coeffs[2] - 6*d.z;

    marker.points[1].x = coeffs[0] + 6*d.x;
    marker.points[1].y = coeffs[1] + 6*d.y;
    marker.points[1].z = coeffs[2] + 6*d.z;

    node_->publish( "visualization_marker", marker );

    // Second line:
    marker.id = 1;

    marker.points[0].x = coeffs[6] - 6*d.x;
    marker.points[0].y = coeffs[7] - 6*d.y;
    marker.points[0].z = coeffs[8] - 6*d.z;

    marker.points[1].x = coeffs[6] + 6*d.x;
    marker.points[1].y = coeffs[7] + 6*d.y;
    marker.points[1].z = coeffs[8] + 6*d.z;

    node_->publish( "visualization_marker", marker );

    // Inlier cloud
    sensor_msgs::PointCloud  inlier_cloud;
    inlier_cloud.header.frame_id = fixed_frame_;
    inlier_cloud.header.stamp = cloud_.header.stamp;
    inlier_cloud.points.resize(inliers.size());
    for (unsigned int i=0; i<inliers.size(); ++i) {
      inlier_cloud.points[i]  = cloud_.points[inliers[i]];
    }
    node_->publish("parallel_lines_inliers", inlier_cloud);
  }

};


/* ---[ */
int
main (int argc, char** argv)
{
  ros::init (argc, argv);

  ros::Node n("hallway_tracker");

  HallwayTracker ht;

  n.spin ();
  return (0);
}
/* ]--- */

