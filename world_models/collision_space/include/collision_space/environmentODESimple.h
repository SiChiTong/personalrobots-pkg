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

/** \author Ioan Sucan */

#ifndef COLLISION_SPACE_ENVIRONMENT_MODEL_ODE_SIMPLE_
#define COLLISION_SPACE_ENVIRONMENT_MODEL_ODE_SIMPLE_

#include "collision_space/environment.h"
#include <ode/ode.h>

/** @htmlinclude ../../manifest.html

    A class describing an environment for a kinematic robot using ODE */

namespace collision_space
{
    
    class EnvironmentModelODESimple : public EnvironmentModel
    {
    public:
		
        EnvironmentModelODESimple(void) : EnvironmentModel()
	{
	    dInitODE2(0);
	    m_space = dHashSpaceCreate(0);
	    m_spaceBasicGeoms = dHashSpaceCreate(0);
	}
	
	virtual ~EnvironmentModelODESimple(void)
	{
	    freeMemory();
	    dCloseODE();
	}
	
	/** The space ID for the objects that can be changed in the
	    map. clearObstacles will invalidate this ID. Collision
	    checking on this space is optimized for many small
	    objects. */
	dSpaceID getODESpace(void) const;

	/** Return the space ID for the space in which static objects are added */
	dSpaceID getODEBasicGeomSpace(void) const;

	/** Return the space ID for the space in which the robot model is instanciated */
	dSpaceID getModelODESpace(void) const;
	
	/** Get the list of contacts (collisions) */
	virtual bool getCollisionContacts(std::vector<Contact> &contacts, unsigned int max_count = 1);

	/** Check if a model is in collision */
	virtual bool isCollision(void);
	
	/** Remove all obstacles from collision model */
	virtual void clearObstacles(void);

	/** Add a point cloud to the collision space */
	virtual void addPointCloud(unsigned int n, const double *points); 

	/** Add a plane to the collision space. Equation it satisfies is a*x+b*y+c*z = d*/
	virtual void addStaticPlane(double a, double b, double c, double d);

	/** Add a robot model. Ignore robot links if their name is not
	    specified in the string vector. The scale argument can be
	    used to increase or decrease the size of the robot's
	    bodies (multiplicative factor). The padding can be used to
	    increase or decrease the robot's bodies with by an
	    additive term */
	virtual void addRobotModel(const boost::shared_ptr<planning_models::KinematicModel> &model, const std::vector<std::string> &links, double scale = 1.0, double padding = 0.0);

	/** Update the positions of the geometry used in collision detection */
	virtual void updateRobotModel(void);

	/** Update the set of bodies that are attached to the robot (re-creates them) */
	virtual void updateAttachedBodies(void);

	/** Add a group of links to be checked for self collision */
	virtual void addSelfCollisionGroup(std::vector<std::string> &links);

	/** Enable/Disable collision checking for specific links. Return the previous value of the state (1 or 0) if succesful; -1 otherwise */
	virtual int setCollisionCheck(const std::string &link, bool state);
	
    protected:
	
	/** Internal function for collision detection */
	void testCollision(void *data);
	void testSelfCollision(void *data);
	void testStaticBodyCollision(void *data);
	void testDynamicBodyCollision(void *data);

	struct kGeom
	{
	    std::vector<dGeomID>                   geom;
	    bool                                   enabled;
	    planning_models::KinematicModel::Link *link;
	};
	
	struct ModelInfo
	{
	    std::vector< kGeom* >                    linkGeom;
	    double                                   scale;
	    double                                   padding;
	    dSpaceID                                 space;
	    std::vector< std::vector<unsigned int> > selfCollision;
	};
	
	dGeomID createODEGeom(dSpaceID space, planning_models::shapes::Shape *shape, double scale, double padding) const;
	void    updateGeom(dGeomID geom, btTransform &pose) const;	
	void    freeMemory(void);	
	
	ModelInfo            m_modelGeom;
	dSpaceID             m_space;
	dSpaceID             m_spaceBasicGeoms;
	
	/* This is where static geoms from the world (that are not cleared) are added; the space for this is m_spaceBasicGeoms */
	std::vector<dGeomID> m_basicGeoms;
	
    };
}

#endif
