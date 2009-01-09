/*
 * Copyright (c) 2008 Radu Bogdan Rusu <rusu -=- cs.tum.edu>
 *
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
 *
 * $Id$
 *
 */

/**
@mainpage

@htmlinclude manifest.html

\author Radu Bogdan Rusu

@b planar_patch_map transforms a 3D point cloud into a polygonal planar patch map using Sample Consensus techniques and Octrees.

 **/

// ROS core
#include <ros/node.h>
// ROS messages
#include <std_msgs/PointCloud.h>
#include <std_msgs/Polygon3D.h>
#include <std_msgs/PolygonalMap.h>

// Sample Consensus
#include <sample_consensus/sac.h>
#include <sample_consensus/msac.h>
#include <sample_consensus/sac_model_plane.h>

// Cloud geometry
#include <cloud_geometry/point.h>
#include <cloud_geometry/areas.h>
#include <cloud_geometry/nearest.h>
#include <cloud_geometry/intersections.h>

// Cloud Octree
#include <cloud_octree/octree.h>

#include <sys/time.h>

using namespace std;
using namespace std_msgs;

class PlanarPatchMap: public ros::node
{
  public:

    // ROS messages
    PointCloud cloud_, cloud_f_;

    // Octree stuff
    cloud_octree::Octree *octree_;
    float leaf_width_;

    // Parameters
    int sac_min_points_per_cell_;
    double d_min_, d_max_;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    PlanarPatchMap () : ros::node ("planar_patch_map")
    {
      param ("~sac_min_points_per_cell", sac_min_points_per_cell_, 8);

      param ("~distance_min", d_min_, 0.10);
      param ("~distance_max", d_max_, 5.0);

      subscribe ("tilt_laser_cloud", cloud_, &PlanarPatchMap::cloud_cb, 1);
      //advertise<scan_utils::OctreeMsg> ("octree", 1);
      advertise<PolygonalMap> ("planar_map", 1);
//       advertise<PointCloud> ("points_before", 1);
       advertise<PointCloud> ("points_after", 1);

      leaf_width_ = 0.15f;
      octree_ = new cloud_octree::Octree (0.0f, 0.0f, 0.0f, leaf_width_, leaf_width_, leaf_width_, 0);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    virtual ~PlanarPatchMap () { }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    void
      fitSACPlane (PointCloud *points, cloud_octree::Octree *octree, cloud_octree::Leaf* leaf, Polygon3D &poly)
    {
      vector<int> indices = leaf->getIndices ();
      if ((int)indices.size () < sac_min_points_per_cell_)
        return;

      // Create and initialize the SAC model
      sample_consensus::SACModelPlane *model = new sample_consensus::SACModelPlane ();
      sample_consensus::SAC *sac             = new sample_consensus::MSAC (model, 0.02);
      model->setDataSet (points, indices);

      PointCloud pts (*points);
      // Search for the best plane
      if (sac->computeModel ())
      {
        // Obtain the inliers and the planar model coefficients
        std::vector<int> inliers = sac->getInliers ();
        std::vector<double> coeff = sac->computeCoefficients ();
        ///fprintf (stderr, "> Found a model supported by %d inliers: [%g, %g, %g, %g]\n", inliers.size (), coeff[0], coeff[1], coeff[2], coeff[3]);

        // Project the inliers onto the model
        model->projectPointsInPlace (inliers, coeff);

        std::vector<double> cell_bounds (6);
        octree->computeCellBounds (leaf, cell_bounds);

//        std::cerr << leaf->cen_[0] << " " << leaf->cen_[1] << " " << leaf->cen_[2] << std::endl;
        //cloud_geometry::intersections::planeWithCubeIntersection (coeff, cell_bounds, poly);
        //poly.points.push_back (poly.points[0]);
        // Compute a 2D convex hull in 3D
        cloud_geometry::areas::convexHull2D (model->getCloud (), inliers, coeff, poly);

/**
//poly.points.resize (inliers.size ());
        pts.pts.resize (inliers.size ());
        for (int d = 0; d < pts.get_chan_size (); d++)
          pts.chan[d].vals.resize (inliers.size ());
        for (unsigned int i = 0; i < poly.points.size (); i++)
        {
//poly.points[i].x = points->pts[inliers.at (i)].x;
// poly.points[i].y = points->pts[inliers.at (i)].y;
//poly.points[i].z = points->pts[inliers.at (i)].z;
          pts.pts[i].x = poly.points[i].x;
          pts.pts[i].y = poly.points[i].y;
          pts.pts[i].z = poly.points[i].z;
        }*/
/*        std::cerr << "Points: " << poly.points.size () << std::endl;
        for (int x = 0; x < poly.points.size (); x++)
          std::cerr << poly.points[x].x << " " << poly.points[x].y << " " << poly.points[x].z << std::endl;*/
      }
      ///publish ("points_after", pts);
/*      PolygonalMap pmap;
      pmap.polygons.push_back (poly);
      publish ("planar_map", pmap);*/
    }

    void
      filterCloudBasedOnDistance (std_msgs::PointCloud cloud_in, std_msgs::PointCloud &cloud_out,
                                  int d_idx, double d_min, double d_max)
    {
      cloud_out.pts.resize (cloud_in.pts.size ());
      int nr_p = 0;

      for (unsigned int i = 0; i < cloud_out.pts.size (); i++)
      {
        if (cloud_in.chan[d_idx].vals[i] >= d_min && cloud_in.chan[d_idx].vals[i] <= d_max)
        {
          cloud_out.pts[nr_p] = cloud_in.pts[i];
          nr_p++;
        }
      }
      cloud_out.pts.resize (nr_p);
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Callback
    void cloud_cb ()
    {
      ROS_INFO (" Received %d data points.", cloud_.pts.size ());

      int d_idx = cloud_geometry::getChannelIndex (cloud_, "distances");
      if (d_idx != -1)
      {
        filterCloudBasedOnDistance (cloud_, cloud_f_, d_idx, d_min_, d_max_);
        ROS_INFO ("Distance information present. Filtering points between %g and %g : %d / %d left.", d_min_, d_max_,
                  cloud_f_.pts.size (), cloud_.pts.size ());
      }
      else
        cloud_f_ = cloud_;

      timeval t1, t2;
      gettimeofday (&t1, NULL);

      octree_ = new cloud_octree::Octree (0.0f, 0.0f, 0.0f, leaf_width_, leaf_width_, leaf_width_, 0);
      // Insert all data points into the octree
      for (unsigned int i = 0; i < cloud_f_.pts.size (); i++)
      {
/**        // Add the index of the current point to the appropriate cell.
        // If the cell doesn't exist, it gets created
        if (!octree_->testBounds (cloud_.pts[i].x, cloud_.pts[i].y, cloud_.pts[i].z))
        {
          fprintf (stderr, "Point outside bounds, will create new cell.\n");
          //octree_->expandTo (cloud_.pts[i].x, cloud_.pts[i].y, cloud_.pts[i].z);
//          octree_->insert (cloud_.pts[i].x, cloud_.pts[i].y, cloud_.pts[i].z);
        }
        (*octree_)(cloud_.pts[i].x, cloud_.pts[i].y, cloud_.pts[i].z).push_back (i);
        */
///        vector<int> idx = octree_->get (cloud_.pts[i].x, cloud_.pts[i].y, cloud_.pts[i].z);
///        idx.push_back (i);
//        vector <int> idx (1); idx[0] = i;
        octree_->insert (cloud_f_.pts[i].x, cloud_f_.pts[i].y, cloud_f_.pts[i].z, i);
      }

      gettimeofday (&t2, NULL);
      double time_spent = t2.tv_sec + (double)t2.tv_usec / 1000000.0 - (t1.tv_sec + (double)t1.tv_usec / 1000000.0);
      ROS_INFO ("> Created octree with %d leaves (%d ndivs) in %g seconds.", octree_->getNumLeaves (), octree_->getNumCells (), time_spent);

      // Initialize the polygonal map
      PolygonalMap pmap;
      pmap.polygons.resize (octree_->getNumLeaves ());
      int nr_poly = 0;

/**      std::set<cloud_octree::OctreeIndex> occupied_leaves_ = octree_->getOccupiedLeaves ();
    for (std::set<cloud_octree::OctreeIndex>::iterator it = occupied_leaves_.begin (); it != occupied_leaves_.end (); ++it)
    {
      int i, j, k;
      cloud_octree::OctreeIndex oi = *it;
      oi.getIndex (i, j, k);
      std::cerr << "Leaf at " << i << " " << j << " " << k << std::endl;
    }*/

      std::vector<cloud_octree::Leaf*> leaves = octree_->getOccupiedLeaves ();
      int total_pts = 0;

      gettimeofday (&t1, NULL);

      for (std::vector<cloud_octree::Leaf*>::iterator it = leaves.begin (); it != leaves.end (); ++it)
      {
        cloud_octree::Leaf* leaf = *it;
        total_pts += leaf->getIndices ().size ();

        fitSACPlane (&cloud_f_, octree_, leaf, pmap.polygons[nr_poly]);
        nr_poly++;
      }
      pmap.polygons.resize (nr_poly);

      gettimeofday (&t2, NULL);
      time_spent = t2.tv_sec + (double)t2.tv_usec / 1000000.0 - (t1.tv_sec + (double)t1.tv_usec / 1000000.0);
      fprintf (stderr, "> Number of: [total points / points inserted / difference] : [%d / %d / %d] in %g seconds.\n", cloud_f_.pts.size (), total_pts, (int)fabs (cloud_f_.pts.size () - total_pts), time_spent);
      publish ("planar_map", pmap);

/*      int nr_cells = octree_->getNumCells ();
      // Perform planar decomposition in each octree cell
      int total_pts = 0;
      for (int i = 0; i < nr_cells; i++)
      {
        for (int j = 0; j < nr_cells; j++)
        {
          for (int k = 0; k < nr_cells; k++)
          {

            vector<int> idx;
            idx = octree_->cellGet (i, j, k);
            if (idx.size () > 0)
            {
      std::cerr << "Nonempty Leaf at " << i << " " << j << " " << k << " " << idx.size () << std::endl;
              // Get the point indices present in the i-j-k cell
              vector<int> indices = octree_->cellGet (i, j, k);
              fitSACPlane (&cloud_, octree_, indices, pmap.polygons[nr_poly]);
              nr_poly++;
              //cerr << i << "," << j << "," << k << " " << idx.size () << endl;
              total_pts += idx.size ();
            }
          }
        }
      }
      pmap.polygons.resize (nr_poly);
      fprintf (stderr, "> Number of: [total points / points inserted / difference] : [%d / %d / %d]\n", cloud_.pts.size (), total_pts, (int)fabs (cloud_.pts.size () - total_pts));
      publish ("planar_map", pmap);
*/

/*      scan_utils::OctreeMsg msg;
      octree_->getAsMsg (msg);

      Polygon3D p;
      p.points.resize (3);
      p.points[0].x = 1; p.points[0].y = 1; p.points[0].z = 1;
      p.points[1].x = 2; p.points[1].y = 2; p.points[1].z = 2;
      p.points[2].x = 3; p.points[2].y = 3; p.points[2].z = 3;

      publish ("octree", msg);*/
    }
};

/* ---[ */
int
  main (int argc, char** argv)
{
  ros::init (argc, argv);

  PlanarPatchMap p;
  p.spin ();

  ros::fini ();

  return (0);
}
/* ]--- */

