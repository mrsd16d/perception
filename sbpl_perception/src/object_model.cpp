/**
 * @file object_model.cpp
 * @brief Object recognition search environment
 * @author Venkatraman Narayanan
 * Carnegie Mellon University, 2015
 */

#include <sbpl_perception/object_model.h>

#include <kinect_sim/model.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl_ros/transforms.h>

#include <omp.h>
#include <pcl/common/common.h>
#include <pcl/conversions.h>
#include <pcl/filters/filter.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/segmentation/extract_polygonal_prism_data.h>
#include <pcl/surface/vtk_smoothing/vtk_utils.h>
#include <pcl/surface/convex_hull.h>

#include <opencv2/highgui/highgui.hpp>

#include <vtkVersion.h>
#include <vtkPolyData.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>
#include <vtkSelectEnclosedPoints.h>

using namespace std;

namespace {
// If true, mesh is converted from mm to meters while preprocessing, otherwise left as such.
constexpr bool kMeshInMillimeters = false; // true for PERCH experiments  
// Inflate inscribed (and circumscribed) radius of mesh by the following
// additive amount, when checking whether points lie within the convex
// footprint or if they are within the volume of the mesh model.
constexpr double kMeshAdditiveInflation = 0.01; // m
// Resolution for the footprint.
constexpr double kFootprintRes = 0.0005; // m

Eigen::Affine3f PreprocessModel(const pcl::PolygonMesh::Ptr &mesh_in,
                     pcl::PolygonMesh::Ptr &mesh_out, bool mesh_in_mm, bool flipped) {
  pcl::PointCloud<PointT>::Ptr cloud_in (new
                                         pcl::PointCloud<PointT>);
  pcl::PointCloud<PointT>::Ptr cloud_out (new
                                          pcl::PointCloud<PointT>);
  pcl::fromPCLPointCloud2(mesh_in->cloud, *cloud_in);


  Eigen::Vector4f centroid;
  pcl::compute3DCentroid(*cloud_in, centroid);
  double x_translation = centroid[0];
  double y_translation = centroid[1];

  Eigen::Affine3f flipping_transform = Eigen::Affine3f::Identity();
  if (flipped) {
    flipping_transform.matrix()(2, 2) = -1;
    transformPointCloud(*cloud_in, *cloud_in, flipping_transform);
  }

  PointT min_pt, max_pt;
  pcl::getMinMax3D(*cloud_in, min_pt, max_pt);
  double z_translation = min_pt.z;

  // std::cout <<  "Bounds: " << max_pt.x - min_pt.x << endl 
  //   << max_pt.y - min_pt.y << endl
  //   << max_pt.z - min_pt.z << endl;

  // Shift bottom most points to 0-z coordinate
  Eigen::Affine3f transform = Eigen::Affine3f::Identity();
  // By default, assume cad models are in mm.
  if (mesh_in_mm) {
    const double kScale = 0.001;
    // const double kScale = 0.001 * 0.7; // UGH, chess piece models are
    // off-scale. TODO: add scaling paramater for models in config file.
    transform.scale(kScale);
    x_translation *= kScale;
    y_translation *= kScale;
    z_translation *= kScale;
  }

  Eigen::Vector3f translation;
  translation << -x_translation, -y_translation, -z_translation;
  transform.translation() = translation;

  transformPointCloud(*cloud_in, *cloud_out, transform);

  pcl::getMinMax3D(*cloud_out, min_pt, max_pt);
  // std::cout <<  "Bounds: " << max_pt.x - min_pt.x << endl 
  //   << max_pt.y - min_pt.y << endl
  //   << max_pt.z - min_pt.z << endl;

  *mesh_out = *mesh_in;
  pcl::toPCLPointCloud2(*cloud_out, mesh_out->cloud);
  return transform * flipping_transform;
}

// TODO: move geometry methods to utils file.
// http://geomalgorithms.com/a03-_inclusion.html#wn_PnPoly()
// IsLeft(): tests if a point is Left|On|Right of an infinite line.
//  Input:  three points P0, P1, and P2
//  Return: >0 for P2 left of the line through P0 and P1
//          =0 for P2  on the line
//          <0 for P2  right of the line
double IsLeft(pcl::PointXYZ p0, pcl::PointXYZ p1, pcl::PointXYZ p2) {
    return ((p1.x - p0.x) * (p2.y - p0.y) - (p2.x -  p0.x) * (p1.y - p0.y));
}

// WindingNumber(): winding number test for a point in a polygon
// Input:   P = a point,
//          V[] = vertex points of a polygon V[n+1] with V[n]=V[0]
// Return:  wn = the winding number (=0 only when P is outside)
int WindingNumber(pcl::PointXYZ p,
                  const pcl::PointCloud<pcl::PointXYZ> &vertices) {
  int wn = 0;    // the  winding number counter
  // loop through all edges of the polygon
  int num_vertices = vertices.points.size() - 1;

  for (int ii = 0; ii < num_vertices; ++ii) {  // edge from V[i] to  V[i+1]
    if (vertices.points[ii].y <= p.y) {          // start y <= P.y
      if (vertices.points[ii + 1].y  > p.y)    // an upward crossing
        if (IsLeft(vertices.points[ii], vertices.points[ii + 1],
                   p) > 0) { // P left of  edge
          ++wn;  // have  a valid up intersect
        }
    } else {                      // start y > P.y (no test needed)
      if (vertices.points[ii + 1].y  <= p.y)   // a downward crossing
        if (IsLeft(vertices.points[ii], vertices.points[ii + 1],
                    p) < 0) { // P right of  edge
          --wn;  // have  a valid down intersect
        }
    }
  }
  return wn;
}

// Assumes polygon is explicitly closed, with vertices.points.last() ==
// vertices.points[0].
bool IsInPoly(pcl::PointXYZ p,
                    const pcl::PointCloud<pcl::PointXYZ> &vertices) {
  return (WindingNumber(p, vertices) != 0);
}

// This function is copied of PCL 1.8. I have a duplicate here since the implementation
// of this function has changed between PCL 1.7 and 1.8, pertaining to the input polygon
// being closed or implicitly closed. In this version, the polygon is assumed to be implicitly 
// closed (i.e, the last point DOES NOT need to be the same as the first point. 
// An n-vertex polygon should have polygon.points.size() = n).
template <typename PointT> bool
isXYPointIn2DXYPolygonCustom(const PointT &point, const pcl::PointCloud<PointT> &polygon)
{
  bool in_poly = false;
  double x1, x2, y1, y2;

  int nr_poly_points = static_cast<int> (polygon.points.size ());
  double xold = polygon.points[nr_poly_points - 1].x;
  double yold = polygon.points[nr_poly_points - 1].y;
  for (int i = 0; i < nr_poly_points; i++)
  {
    double xnew = polygon.points[i].x;
    double ynew = polygon.points[i].y;
    if (xnew > xold)
    {
      x1 = xold;
      x2 = xnew;
      y1 = yold;
      y2 = ynew;
    }
    else
    {
      x1 = xnew;
      x2 = xold;
      y1 = ynew;
      y2 = yold;
    }

    if ( (xnew < point.x) == (point.x <= xold) && (point.y - y1) * (x2 - x1) < (y2 - y1) * (point.x - x1) )
    {
      in_poly = !in_poly;
    }
    xold = xnew;
    yold = ynew;
  }
  return (in_poly);
}

cv::Point WorldPointToRasterPoint(double x, double y, double half_side) {
  cv::Point cv_point;
  cv_point.x = static_cast<int>(std::round((-y + half_side) /
                                     kFootprintRes));
  cv_point.y = static_cast<int>(std::round((-x + half_side) /
                                     kFootprintRes));
  return cv_point;
}
}

ObjectModel::ObjectModel(const pcl::PolygonMesh &mesh, const string name, const bool symmetric, const bool flipped) {
  pcl::PolygonMesh::Ptr mesh_in(new pcl::PolygonMesh(mesh));
  pcl::PolygonMesh::Ptr mesh_out(new pcl::PolygonMesh);
  preprocessing_transform_ = PreprocessModel(mesh_in, mesh_out, kMeshInMillimeters, flipped);
  mesh_ = *mesh_out;
  symmetric_ = symmetric;
  name_ = name;
  cloud_.reset(new PointCloud);
  SetObjectProperties();
}

void ObjectModel::TransformPolyMesh(const pcl::PolygonMesh::Ptr
                       &mesh_in, pcl::PolygonMesh::Ptr &mesh_out, Eigen::Matrix4f transform) {
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_in (new
                                                pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_out (new
                                                 pcl::PointCloud<pcl::PointXYZ>);
  pcl::fromPCLPointCloud2(mesh_in->cloud, *cloud_in);

  transformPointCloud(*cloud_in, *cloud_out, transform);

  *mesh_out = *mesh_in;
  pcl::toPCLPointCloud2(*cloud_out, mesh_out->cloud);
  return;
}

void ObjectModel::SetObjectProperties() {
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new
                                             pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr base_cloud (new
                                             pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr projected_cloud (new
                                             pcl::PointCloud<pcl::PointXYZ>);
  pcl::fromPCLPointCloud2(mesh_.cloud, *cloud);

  for (size_t ii = 0; ii < cloud->size(); ++ii) {
    auto point = cloud->points[ii];
    pcl::PointXYZ projected_point = point;
    projected_point.z = 0.0;
    projected_cloud->push_back(projected_point);
    if (point.z < 0.01) {
      base_cloud->push_back(point);
    }
  }
  base_cloud->width = base_cloud->points.size();
  base_cloud->height = 1;

  projected_cloud->width = projected_cloud->points.size();
  projected_cloud->height = 1;

  pcl::PointXYZ min_pt, max_pt;
  getMinMax3D(*base_cloud, min_pt, max_pt);
  min_x_ = min_pt.x;
  min_y_ = min_pt.y;
  min_z_ = min_pt.z;
  max_x_ = max_pt.x;
  max_y_ = max_pt.y;
  max_z_ = max_pt.z;

  // Compute the convex polygonal footprint.
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_hull (new pcl::PointCloud<pcl::PointXYZ>);
  vector<pcl::Vertices> polygons;
  pcl::ConvexHull<pcl::PointXYZ> convex_hull;
  convex_hull.setInputCloud(projected_cloud);
  convex_hull.setDimension(2);
  convex_hull.reconstruct(*cloud_hull, polygons);
  // 2D point set should have only one polygon.
  assert(polygons.size() == 1);
  convex_hull_footprint_.reset(new PointCloud);
  pcl::copyPointCloud(*cloud_hull, *convex_hull_footprint_);

  // Inflate the footprint such that the new footprint's inscribed radius is
  // bigger than the old inscribed radius by 0.5 cm.
  const double inscribed_rad = GetInscribedRadius();
  if (inscribed_rad < 1e-5) {
    printf("Object %s has very small (possibly zero) inscribed radius. Please check that the model is correct\n", name_.c_str());
    inflation_factor_ = 1.0;
  } else {
    inflation_factor_ = 1.0 + (kMeshAdditiveInflation / inscribed_rad);
  }

  // printf("Footprint\n");
  // for(const auto &point : convex_hull_footprint_->points) {
  //   printf("%f %f %f\n,   ", point.x, point.y, point.z);
  // }
  // printf("\n");
  // printf("Polygon size: %d\n", static_cast<int>(polygons[0].vertices.size()));
  //
  // printf("Inflation factor for model %s: %f\n", name_.c_str(), inflation_factor_);
  
  // Rasterize the footprint for fast point-within-footprint checking
  const double half_side = (GetCircumscribedRadius() + kMeshAdditiveInflation);
  const int cv_side = static_cast<int>(2.0 * half_side / kFootprintRes);
  footprint_raster_.create(cv_side, cv_side, CV_8UC1);
  vector<cv::Point> cv_points;
  cv_points.reserve(convex_hull_footprint_->points.size());
  for(const auto &point : convex_hull_footprint_->points) {
    cv::Point cv_point = WorldPointToRasterPoint(point.x, point.y, half_side);
    cv_points.push_back(cv_point);
  }
  footprint_raster_.setTo(0);
  cv::fillConvexPoly(footprint_raster_, cv_points.data(), cv_points.size(), 255);
  cv::imwrite("/usr0/home/venkatrn/hydro_workspace/src/perception/sbpl_perception/visualization/footprint.png", footprint_raster_);
}

void ObjectModel::SetObjectPointCloud(const PointCloudPtr &cloud) {
  transformPointCloud(*cloud, *cloud_, preprocessing_transform_);
}

double ObjectModel::GetInscribedRadius() const {
  return std::min(fabs(max_x_ - min_x_), fabs(max_y_ - min_y_)) / 2.0;
}

double ObjectModel::GetCircumscribedRadius() const {
  return std::max(fabs(max_x_ - min_x_), fabs(max_y_ - min_y_)) / 2.0;
}


pcl::PolygonMeshPtr ObjectModel::GetTransformedMesh(const ContPose &p,
                                                    double table_height) const {
  pcl::PolygonMeshPtr mesh_in(new pcl::PolygonMesh(mesh_));
  pcl::PolygonMeshPtr transformed_mesh(new pcl::PolygonMesh);
  Eigen::Matrix4f transform;
  transform <<
            cos(p.yaw()), -sin(p.yaw()) , 0, p.x(),
                sin(p.yaw()) , cos(p.yaw()) , 0, p.y(),
                0, 0 , 1 , table_height,
                0, 0 , 0 , 1;
  TransformPolyMesh(mesh_in, transformed_mesh, transform);
  return transformed_mesh;
}

pcl::PolygonMeshPtr ObjectModel::GetTransformedMesh(const Eigen::Matrix4f &transform ) const {
  pcl::PolygonMeshPtr mesh_in(new pcl::PolygonMesh(mesh_));
  pcl::PolygonMeshPtr transformed_mesh(new pcl::PolygonMesh);
  TransformPolyMesh(mesh_in, transformed_mesh, transform);
  return transformed_mesh;
}

Eigen::Affine3f ObjectModel::GetRawModelToSceneTransform(const ContPose &p, double table_height) const {
  Eigen::Matrix4f transform;
  transform <<
            cos(p.yaw()), -sin(p.yaw()) , 0, p.x(),
                sin(p.yaw()) , cos(p.yaw()) , 0, p.y(),
                0, 0 , 1 , table_height,
                0, 0 , 0 , 1;
  Eigen::Affine3f model_to_scene_transform;
  model_to_scene_transform.matrix() = transform.matrix() * preprocessing_transform_.matrix();
  return model_to_scene_transform;
}

bool ObjectModel::PointInsideRasterizedFootprint(double x, double y) const {
  const double half_side = (GetCircumscribedRadius() + kMeshAdditiveInflation);
  cv::Point cv_point = WorldPointToRasterPoint(x, y, half_side);
  const double side = footprint_raster_.rows;
  if (cv_point.x < 0 || cv_point.x >= side || cv_point.y < 0 || cv_point.y >= side) {
    return false;
  }
  return (footprint_raster_.at<uchar>(cv_point.y, cv_point.x) == 255);
}

vector<bool> ObjectModel::PointsInsideMesh(const vector<Eigen::Vector3d> &points, const ContPose &pose, double table_height) const {

  // Inflate mesh so that we get the points on the boundaries as well.
  Eigen::Matrix4f transform;
  transform <<
            cos(pose.yaw()), -sin(pose.yaw()) , 0, pose.x(),
                sin(pose.yaw()) , cos(pose.yaw()) , 0, pose.y(),
                0, 0 , 1 , table_height,
                0, 0 , 0 , 1;
  transform.block<3,3>(0,0) = inflation_factor_ * transform.block<3,3>(0,0);
  auto transformed_mesh = GetTransformedMesh(transform);

  vtkSmartPointer<vtkPolyData> vtk_mesh = vtkSmartPointer<vtkPolyData>::New(); 
  pcl::VTKUtils::mesh2vtk(*transformed_mesh,vtk_mesh); 

  vtkSmartPointer<vtkPoints> vtk_points = 
    vtkSmartPointer<vtkPoints>::New();
	
	for (const auto &point : points) {
    vtk_points->InsertNextPoint(point[0], point[1], point[2]);
  }

  vtkSmartPointer<vtkPolyData> points_polydata = 
    vtkSmartPointer<vtkPolyData>::New();
  points_polydata->SetPoints(vtk_points);

  vtkSmartPointer<vtkSelectEnclosedPoints> select_enclosed_points = 
    vtkSmartPointer<vtkSelectEnclosedPoints>::New();
#if VTK_MAJOR_VERSION <= 5
  select_enclosed_points->SetInput(points_polydata);
#else
  select_enclosed_points->SetInputData(points_polydata);
#endif
#if VTK_MAJOR_VERSION <= 5
  select_enclosed_points->SetSurface(vtk_mesh.GetPointer());
#else
  select_enclosed_points->SetSurfaceData(vtk_mesh.GetPointer());
#endif
  select_enclosed_points->Update();
 
  vector<bool> is_inside(points.size(), false);
  for(size_t ii = 0; ii < points.size(); ++ii) {
    is_inside[ii] = static_cast<bool>(select_enclosed_points->IsInside(ii));
  }
  return is_inside;
}

vector<bool> ObjectModel::PointsInsideFootprint(const std::vector<Eigen::Vector2d> &points, const ContPose &pose, double table_height) const {
  Eigen::Affine3f transform;
  transform.matrix() <<
            cos(pose.yaw()), -sin(pose.yaw()) , 0, pose.x(),
                sin(pose.yaw()) , cos(pose.yaw()) , 0, pose.y(),
                0, 0 , 1 , table_height,
                0, 0 , 0 , 1;
  transform.matrix().block<3,3>(0,0) = inflation_factor_ * transform.matrix().block<3,3>(0,0);

  // NOTE: this works only when the transforms are in the XY plane (i.e, we
  // have an implicit 3 DoF assumption for the models).
  // pcl::PointCloud<pcl::PointXYZ>::Ptr transformed_footprint (new
  //                                                pcl::PointCloud<pcl::PointXYZ>);
  // transformPointCloud(*convex_hull_footprint_, *transformed_footprint, transform);

  vector<bool> is_inside(points.size(), false);

  // Uncomment to explicitly close polygon.
  // transformed_footprint->points.push_back(transformed_footprint->points[0]);
  
  Eigen::Affine3f inverse_transform  = transform.inverse();
  vector<Eigen::Vector2d> transformed_points(points.size());
  for(size_t ii = 0; ii < points.size(); ++ii) {
    Eigen::Vector4f homogeneous_point, transfomed_homogeneous_point;
    homogeneous_point << points[ii][0], points[ii][1], 0, 1;
    transfomed_homogeneous_point = inverse_transform * homogeneous_point;
    transformed_points[ii][0] = transfomed_homogeneous_point[0];
    transformed_points[ii][1] = transfomed_homogeneous_point[1];
  }

  // #pragma omp parallel for
  for(size_t ii = 0; ii < transformed_points.size(); ++ii) {
    PointT pcl_point;
    pcl_point.x = transformed_points[ii][0];
    pcl_point.y = transformed_points[ii][1];
    // pcl_point.z = table_height;
    pcl_point.z = 0;

    // NOTE: for this test, transformed_footprint should be an implicitly
    // closed polygon, i.e, transformed_footprint.points.last() !=
    // transformed_footprint.points[0].
    // is_inside[ii] = isXYPointIn2DXYPolygonCustom(pcl_point, *transformed_footprint);
    is_inside[ii] = isXYPointIn2DXYPolygonCustom(pcl_point, *convex_hull_footprint_);
    // is_inside[ii] = PointInsideRasterizedFootprint(pcl_point.x, pcl_point.y);

    // NOTE: for this test, transformed_footprint should be an explicitly
    // closed polygon, i.e, transformed_footprint.points.last() ==
    // transformed_footprint.points[0].
    // is_inside[ii] = IsInPoly(pcl_point, *transformed_footprint);
  }
  return is_inside;
}

PointCloudPtr ObjectModel::GetFootprint(const ContPose &pose, double table_height, bool use_inflation/*=false*/) const {
  Eigen::Affine3f transform;
  transform.matrix() <<
            cos(pose.yaw()), -sin(pose.yaw()) , 0, pose.x(),
                sin(pose.yaw()) , cos(pose.yaw()) , 0, pose.y(),
                0, 0 , 1 , table_height,
                0, 0 , 0 , 1;

  if (use_inflation) {
    transform.matrix().block<3,3>(0,0) = inflation_factor_ * transform.matrix().block<3,3>(0,0);
  }

  PointCloudPtr transformed_footprint (new PointCloud);
  transformPointCloud(*convex_hull_footprint_, *transformed_footprint, transform);
  return transformed_footprint;
}
