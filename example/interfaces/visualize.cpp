#include <boost/shared_ptr.hpp>
#include <Moby/Simulator.h>

#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osg/MatrixTransform>
#include <osg/ShapeDrawable>
#include <osg/PositionAttitudeTransform>
#include <osg/Plane>
#include <osg/LineSegment>
#include <osg/LineWidth>

using namespace Moby;
using namespace Ravelin;
const double VIBRANCY = 1;

/// Draws a ray directed from a contact point along the contact normal
void visualize_ray( const Ravelin::Vector3d& point, const Ravelin::Vector3d& vec, const Ravelin::Vector3d& c,double point_radius, boost::shared_ptr<Simulator> sim ) {
  
  // random color for this contact visualization
  double r = c[0] * VIBRANCY;
  double g = c[1] * VIBRANCY;
  double b = c[2] * VIBRANCY;
  osg::Vec4 color = osg::Vec4( r, g, b, 1.0 );
  
  const double point_scale = point_radius;
  
  // the osg node this event visualization will attach to
  osg::Group* group_root = new osg::Group();
  
  // turn off lighting for this node
  osg::StateSet *point_state = group_root->getOrCreateStateSet();
  point_state->setMode( GL_LIGHTING, osg::StateAttribute::PROTECTED | osg::StateAttribute::OFF );
  
  // a geode for the visualization geometry
  osg::Geode* point_geode = new osg::Geode();
  
  // add some hints to reduce the polygonal complexity of the visualization
  osg::TessellationHints *hints = new osg::TessellationHints();
  hints->setTessellationMode( osg::TessellationHints::USE_TARGET_NUM_FACES );
  hints->setCreateNormals( true );
  hints->setDetailRatio( 0.1 );
  
  osg::Sphere* point_geometry = new osg::Sphere( osg::Vec3( 0,0,0 ), point_radius );
  osg::ShapeDrawable* point_shape = new osg::ShapeDrawable( point_geometry, hints );
  point_shape->setColor( color );
  point_geode->addDrawable( point_shape );
  
  osg::PositionAttitudeTransform* point_transform;
  point_transform = new osg::PositionAttitudeTransform();
  point_transform->setPosition( osg::Vec3( point[0], point[1], point[2] ) );
  point_transform->setScale( osg::Vec3( point_scale, point_scale, point_scale ) );
  
  // add the geode to the transform
  point_transform->addChild( point_geode );
  
  // add the transform to the root
  group_root->addChild( point_transform );
  
  // add the root to the transient data scene graph
  sim->add_transient_vdata( group_root );
  
  // ----- LINE -------
  
  //  osg::Group* vec_root = new osg::Group();
  osg::Geode* vec_geode = new osg::Geode();
  osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
  osg::ref_ptr<osg::DrawArrays> drawArrayLines =
  new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP);
  
  osg::ref_ptr<osg::Vec3Array> vertexData = new osg::Vec3Array;
  
  geom->addPrimitiveSet(drawArrayLines);
  geom->setVertexArray(vertexData);
  
  //loop through points
  vertexData->push_back(osg::Vec3d(point[0],point[1],point[2]));
  vertexData->push_back(osg::Vec3d(vec[0],vec[1],vec[2]));
  
  drawArrayLines->setFirst(0);
  drawArrayLines->setCount(vertexData->size());
  
  // Add the Geometry (Drawable) to a Geode and return the Geode.
  vec_geode->addDrawable( geom.get() );
  // the osg node this event visualization will attach to
  
  // add the root to the transient data scene graph
  // create the visualization transform
  osg::PositionAttitudeTransform* vec_transform;
  vec_transform = new osg::PositionAttitudeTransform();
  
  // add the geode to the transform
  vec_transform->addChild( vec_geode );
  
  // add the transform to the root
  group_root->addChild( vec_transform );
  
  // add the root to the transient data scene graph
  sim->add_transient_vdata( group_root );
}

void visualize_ray( const Ravelin::Vector3d& point, const Ravelin::Vector3d& vec, const Ravelin::Vector3d& c, boost::shared_ptr<Simulator> sim ) {
  visualize_ray(point,vec,c,0.1,sim);
}

void draw_pose(const Ravelin::Pose3d& p, boost::shared_ptr<Simulator> sim ,double lightness, double size){
  Ravelin::Pose3d pose(p);
  assert(lightness >= 0.0 && lightness <= 2.0);
  pose.update_relative_pose(Pacer::GLOBAL);
  Ravelin::Matrix3d Rot(pose.q);
  Rot*= 0.3;
  double alpha = (lightness > 1.0)? 1.0 : lightness,
  beta = (lightness > 1.0)? lightness-1.0 : 0.0;
  
  visualize_ray(pose.x+Ravelin::Vector3d(Rot(0,0),Rot(1,0),Rot(2,0),Pacer::GLOBAL)*size,Ravelin::Vector3d(0,0,0)+pose.x,Ravelin::Vector3d(alpha,beta,beta),size,sim);
  visualize_ray(pose.x+Ravelin::Vector3d(Rot(0,1),Rot(1,1),Rot(2,1),Pacer::GLOBAL)*size,Ravelin::Vector3d(0,0,0)+pose.x,Ravelin::Vector3d(beta,alpha,beta),size,sim);
  visualize_ray(pose.x+Ravelin::Vector3d(Rot(0,2),Rot(1,2),Rot(2,2),Pacer::GLOBAL)*size,Ravelin::Vector3d(0,0,0)+pose.x,Ravelin::Vector3d(beta,beta,alpha),size,sim);
}