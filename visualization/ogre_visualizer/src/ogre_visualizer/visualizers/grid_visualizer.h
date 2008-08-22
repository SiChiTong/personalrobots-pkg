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

#ifndef OGRE_VISUALIZER_GRID_VISUALIZER_H
#define OGRE_VISUALIZER_GRID_VISUALIZER_H

#include "../visualizer_base.h"

namespace ogre_tools
{
class Grid;
}

class wxFocusEvent;
class wxCommandEvent;

class GridOptionsPanel;

namespace ogre_vis
{

class GridVisualizer : public VisualizerBase
{
public:
  GridVisualizer( Ogre::SceneManager* sceneManager, ros::node* node, rosTFClient* tfClient, const std::string& name, bool enabled );
  virtual ~GridVisualizer();

  uint32_t getCellCount() { return cell_count_; }
  float getCellSize() { return cell_size_; }
  void getColor( float& r, float& g, float& b );

  void set( uint32_t cellCount, float cellSize, float r, float g, float b );
  void setCellCount( uint32_t cellCount );
  void setCellSize( float cellSize );
  void setColor( float r, float g, float b );

  // Overrides from VisualizerBase
  virtual void fillPropertyGrid( wxPropertyGrid* propertyGrid );
  virtual void propertyChanged( wxPropertyGridEvent& event );

protected:
  void create();

  // overrides from VisualizerBase
  virtual void onEnable();
  virtual void onDisable();

  float cell_size_;
  uint32_t cell_count_;
  float r_;
  float g_;
  float b_;
  ogre_tools::Grid* grid_;
};

} // namespace ogre_vis

 #endif
