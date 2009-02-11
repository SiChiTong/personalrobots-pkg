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

/** \author Radu Bogdan Rusu */

#ifndef _CLOUD_GEOMETRY_DISTANCES_H_
#define _CLOUD_GEOMETRY_DISTANCES_H_

// ROS includes
#include <robot_msgs/Point32.h>

namespace cloud_geometry
{

  namespace distances
  {
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /** \brief Get the distance from a 3D point to another 3D point
      * \param p1 the first point
      * \param p2 the second point
      */
    inline double
      pointToPointDistance (robot_msgs::Point32 *p1, robot_msgs::Point32 *p2)
    {
      return (sqrt ( (p1->x - p2->x) * (p1->x - p2->x) + (p1->y - p2->y) * (p1->y - p2->y) + (p1->z - p2->z) * (p1->z - p2->z) ));
    }

    double pointToLineDistance (robot_msgs::Point32 p, robot_msgs::Point32 q, robot_msgs::Point32 dir);
    double pointToLineDistance (robot_msgs::Point32 p, std::vector<double> line_coefficients);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /** \brief Get the distance from a point to a plane (unsigned) defined by ax+by+cz+d=0
      * \param p a point
      * \param plane_coefficients the normalized coefficients (a, b, c, d) of a plane
      */
    inline double
      pointToPlaneDistance (robot_msgs::Point32 *p, std::vector<double> plane_coefficients)
    {
      return (fabs (plane_coefficients[0]*p->x + plane_coefficients[1]*p->y + plane_coefficients[2]*p->z + plane_coefficients[3]));
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /** \brief Get the distance from a point to a plane (unsigned) defined by ax+by+cz+d=0
      * \param p a point
      * \param a the normalized <i>a</i> coefficient of a plane
      * \param b the normalized <i>b</i> coefficient of a plane
      * \param c the normalized <i>c</i> coefficient of a plane
      * \param d the normalized <i>d</i> coefficient of a plane
      */
    inline double
      pointToPlaneDistance (robot_msgs::Point32 *p, double a, double b, double c, double d)
    {
      return (fabs (a * p->x + b * p->y + c * p->z + d));
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /** \brief Get the distance from a point to a sphere
      * \param p a point
      * \param sphere_coefficients the coefficients (x, y, z, R) of a sphere
      */
    inline double
      pointToSphereDistance (robot_msgs::Point32 p, std::vector<double> sphere_coefficients)
    {
      return (sqrt (
                    (p.x - sphere_coefficients[0]) * (p.x - sphere_coefficients[0]) +
                    (p.y - sphere_coefficients[1]) * (p.y - sphere_coefficients[1]) +
                    (p.z - sphere_coefficients[2]) * (p.z - sphere_coefficients[2])
                   ) - sphere_coefficients[3]);
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /** \brief Get the distance from a point to a sphere
      * \param p a point
      * \param x the x center coefficient of a sphere
      * \param y the y center coefficient of a sphere
      * \param z the z center coefficient of a sphere
      * \param R the radius coefficient of a sphere
      */
    inline double
      pointToSphereDistance (robot_msgs::Point32 p, double x, double y, double z, double r)
    {
      return (sqrt ( (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) + (p.z - z) * (p.z - z) ) - r);
    }


    void lineToLineSegment (std::vector<double> line_a, std::vector<double> line_b, std::vector<double> &segment);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    /** \brief Creates a parallel 2D line at a given distance in an X-Y plane (Z is ignored)
      * \param line_a the 3D coefficients of the input line (point, direction)
      * \param line_b the 3D coefficients of the resultant parallel line (point, direction), with point.z copied and
      *        direction.z = 0
      * \param distance the desired distance between the lines
      */
    inline void
      createParallel2DLine (std::vector<double> line_a, std::vector<double> &line_b, double distance)
    {
      line_b.resize (6);
      // The direction of the resultant line is equal to the first
      line_b[3] = line_a[3];
      line_b[4] = line_a[4];
      line_b[5] = line_a[5];

      // Create a point on an orthogonal line, at the given distance
      double angle = atan2 (line_a[3], -line_a[4]);
      if (angle < 0)
        angle += 2 * M_PI;

      line_b[0] = line_a[0] + distance * cos (angle);
      line_b[1] = line_a[1] + distance * sin (angle);
      line_b[2] = line_a[2];
    }

  }
}

#endif
