/*
 * Copyright (c) 2009, Willow Garage, Inc.
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

#include "tf_conversions/tf_kdl.h"

namespace tf {


void VectorTFToKDL(const tf::Vector3& t, KDL::Vector& k)
{
  k = KDL::Vector(t[0], t[1], t[2]);
}

void RotationTFToKDL(const tf::Quaternion& t, KDL::Rotation& k)
{
  k.Quaternion(t[0], t[1], t[2], t[3]);
}

void TransformTFToKDL(const tf::Transform &t, KDL::Frame &k)
{
  for (unsigned int i = 0; i < 3; ++i)
    k.p.data[i] = t.getOrigin()[i];
  for (unsigned int i = 0; i < 9; ++i)
    k.M.data[i] = t.getBasis()[i/3][i%3];
}

void TransformKDLToTF(const KDL::Frame &k, tf::Transform &t)
{
  t.setOrigin(tf::Vector3(k.p.data[0], k.p.data[1], k.p.data[2]));
  t.setBasis(btMatrix3x3(k.M.data[0], k.M.data[1], k.M.data[2],
                         k.M.data[3], k.M.data[4], k.M.data[5],
                         k.M.data[6], k.M.data[7], k.M.data[8]));
}

}
