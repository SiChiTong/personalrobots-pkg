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

#ifndef TOPOLOGICAL_MAP_BOTTLENECK_GRAPH_H
#define TOPOLOGICAL_MAP_BOTTLENECK_GRAPH_H

#include <utility>
#include <iostream>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/multi_array.hpp>


namespace topological_map
{


// Vertex descriptions
struct Coords
{
  int first;
  int second;

  Coords(int f=0, int s=0) : first(f), second(s) {}
};


typedef std::set<Coords> Region;
enum VertexType { BOTTLENECK, OPEN };
struct VertexDescription
{
  VertexDescription () { region = new Region(); id=-2; }

  VertexType type;
  Region* region;
  int id;
};


// Now we can define the graph type
// Vertices will be labeled with VertexDescriptions 
struct desc_t 
{
  typedef boost::vertex_property_tag kind;
};
typedef boost::property<desc_t,VertexDescription> desc_property; 
typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS, desc_property> BottleneckGraph; 
typedef boost::property_map<BottleneckGraph, desc_t>::type DescMap;
typedef boost::graph_traits<BottleneckGraph>::vertex_descriptor BottleneckVertex;
typedef boost::graph_traits<BottleneckGraph>::vertex_iterator BottleneckVertexIterator;
typedef boost::graph_traits<BottleneckGraph>::adjacency_iterator BottleneckAdjacencyIterator;

// Typedefs for occupancy grids
typedef boost::multi_array<bool, 2> GridArray;

// Array mapping from grid cell to topological graph node
typedef boost::multi_array<BottleneckVertex, 2> RegionArray;



// Externally used ops
class IndexedBottleneckGraph
{
public:
  IndexedBottleneckGraph () : numRows(-1), numCols(-1) {}

  void initializeFromGrid (GridArray g, int bottleneckSize, int bottleneckSkip, int inflationRadius, int distanceMin, int distanceMax);
  void readFromFile (const char* filename);

  void setDims (int nr, int nc);

  int regionId (int r, int c);
  bool lookupVertex (int r, int c, BottleneckVertex* v);
  VertexDescription vertexDescription (BottleneckVertex v) { return boost::get(desc_t(), graph, v); }


  void printBottleneckGraph (void);
  void printBottlenecks (const char *filename);
  void printBottlenecks (void);

private:
  void writeToStream (std::ostream&);
  void indexRegions (void);

  BottleneckGraph graph;
  RegionArray* regions;
  GridArray* isFree;
  int numRows, numCols;
  
};






} // namespace topological_map





#endif //TOPOLOGICAL_MAP_BOTTLENECK_GRAPH_H
