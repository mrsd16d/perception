/**
 * @file search_env.cpp
 * @brief Object recognition search environment
 * @author Venkatraman Narayanan
 * Carnegie Mellon University, 2015
 */

#include <sbpl_perception/search_env.h>
#include <sbpl_perception/perception_utils.h>
#include <sbpl_perception/cycleTimer.h>

#include <ros/ros.h>
#include <ros/package.h>

#include <pcl/conversions.h>
#include <pcl/filters/filter.h>
#include <pcl_ros/transforms.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>

#include <pcl/io/io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/png_io.h>
#include <pcl/io/vtk_lib_io.h>
#include <pcl/common/common.h>
#include <pcl/console/print.h>

#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/contrib/contrib.hpp>

#include <boost/lexical_cast.hpp>
#include <mpi.h>
#include <assert.h>

using namespace std;
using namespace perception_utils;
using namespace pcl::simulation;
using namespace Eigen;

const int kICPCostMultiplier = 1000000;
const double kSensorResolution = 0.01 / 2;//0.01
const double kSensorResolutionSqr = kSensorResolution * kSensorResolution;

const string kDebugDir = ros::package::getPath("sbpl_perception") +
                         "/visualization/";

#define PROFILE 0

#if PROFILE
static double diff = 0;
#endif


EnvObjectRecognition::EnvObjectRecognition(int rank, int numproc) : 
  image_debug_(false),
  id(rank),
  num_proc(numproc) {
  
  // OpenGL requires argc and argv
  char **argv;
  argv = new char *[2];
  argv[0] = new char[1];
  argv[1] = new char[1];
  argv[0] = "0";
  argv[1] = "1";

  std::cout << "From Constructor: " <<  id << std::endl;

  env_params_.x_min = -0.3;
  env_params_.x_max = 0.31;
  env_params_.y_min = -0.3;
  env_params_.y_max = 0.31;

  // env_params_.res = 0.05;
  // env_params_.theta_res = M_PI / 10; //8

  env_params_.res = 0.2; //0.2
  const int num_thetas = 16;
  env_params_.theta_res = 2 * M_PI / static_cast<double>(num_thetas); //8

  env_params_.table_height = 0;
  env_params_.img_width = 640;
  env_params_.img_height = 480;
  env_params_.num_models = 0;
  env_params_.num_objects = 0;

  env_params_.observed_max_range = 20000;
  env_params_.observed_min_range = 0;

  Pose fake_pose(0.0, 0.0, 0.0);
  goal_state_.object_ids.push_back(
    -1); // This state should never be generated during the search
  goal_state_.object_poses.push_back(fake_pose);

  // debugging
  // // Pose p1( 0.509746, 0.039520, 0.298403);
  // // Pose p2( 0.550498, -0.348341, 5.665042);
  // Pose p3( 0.355350, -0.002500, 5.472355);
  // Pose p4( 0.139923, -0.028259, 3.270873);
  // Pose p5( -0.137201, -0.057090, 5.188886);
  // // poses.push_back(p1);
  // // poses.push_back(p2);
  // start_state_.object_poses.push_back(p3);
  // start_state_.object_poses.push_back(p4);
  // start_state_.object_poses.push_back(p5);
  // // start_state_.object_ids.push_back(0);
  // // start_state_.object_ids.push_back(1);
  // start_state_.object_ids.push_back(2);
  // start_state_.object_ids.push_back(3);
  // start_state_.object_ids.push_back(4);


  env_params_.goal_state_id = StateToStateID(goal_state_);
  env_params_.start_state_id = StateToStateID(
                                 start_state_); // Start state is the empty state
  minz_map_[env_params_.start_state_id] = 0;
  maxz_map_[env_params_.start_state_id] = 0;

  kinect_simulator_ = SimExample::Ptr(new SimExample(0, argv,
                                                     env_params_.img_height, env_params_.img_width));
  scene_ = kinect_simulator_->scene_;
  observed_cloud_.reset(new PointCloud);
  observed_organized_cloud_.reset(new PointCloud);
  downsampled_observed_cloud_.reset(new PointCloud);

  gl_inverse_transform_ <<
                        0, 0 , -1 , 0,
                        -1, 0 , 0 , 0,
                        0, 1 , 0 , 0,
                        0, 0 , 0 , 1;

  pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);
}

EnvObjectRecognition::EnvObjectRecognition() : 
  image_debug_(false)
 {
  // OpenGL requires argc and argv
  char **argv;
  argv = new char *[2];
  argv[0] = new char[1];
  argv[1] = new char[1];
  argv[0] = "0";
  argv[1] = "1";

  env_params_.x_min = -0.3;
  env_params_.x_max = 0.31;
  env_params_.y_min = -0.3;
  env_params_.y_max = 0.31;

  // env_params_.res = 0.05;
  // env_params_.theta_res = M_PI / 10; //8

  env_params_.res = 0.1; //0.2
  const int num_thetas = 16;
  env_params_.theta_res = 2 * M_PI / static_cast<double>(num_thetas); //8

  env_params_.table_height = 0;
  env_params_.img_width = 640;
  env_params_.img_height = 480;
  env_params_.num_models = 0;
  env_params_.num_objects = 0;

  env_params_.observed_max_range = 20000;
  env_params_.observed_min_range = 0;

  Pose fake_pose(0.0, 0.0, 0.0);
  goal_state_.object_ids.push_back(
    -1); // This state should never be generated during the search
  goal_state_.object_poses.push_back(fake_pose);

  // debugging
  // // Pose p1( 0.509746, 0.039520, 0.298403);
  // // Pose p2( 0.550498, -0.348341, 5.665042);
  // Pose p3( 0.355350, -0.002500, 5.472355);
  // Pose p4( 0.139923, -0.028259, 3.270873);
  // Pose p5( -0.137201, -0.057090, 5.188886);
  // // poses.push_back(p1);
  // // poses.push_back(p2);
  // start_state_.object_poses.push_back(p3);
  // start_state_.object_poses.push_back(p4);
  // start_state_.object_poses.push_back(p5);
  // // start_state_.object_ids.push_back(0);
  // // start_state_.object_ids.push_back(1);
  // start_state_.object_ids.push_back(2);
  // start_state_.object_ids.push_back(3);
  // start_state_.object_ids.push_back(4);


  env_params_.goal_state_id = StateToStateID(goal_state_);
  env_params_.start_state_id = StateToStateID(
                                 start_state_); // Start state is the empty state
  minz_map_[env_params_.start_state_id] = 0;
  maxz_map_[env_params_.start_state_id] = 0;

  kinect_simulator_ = SimExample::Ptr(new SimExample(0, argv,
                                                     env_params_.img_height, env_params_.img_width));
  scene_ = kinect_simulator_->scene_;
  observed_cloud_.reset(new PointCloud);
  observed_organized_cloud_.reset(new PointCloud);
  downsampled_observed_cloud_.reset(new PointCloud);

  gl_inverse_transform_ <<
                        0, 0 , -1 , 0,
                        -1, 0 , 0 , 0,
                        0, 1 , 0 , 0,
                        0, 0 , 0 , 1;

  pcl::console::setVerbosityLevel(pcl::console::L_ALWAYS);
}

EnvObjectRecognition::~EnvObjectRecognition() {
}

void EnvObjectRecognition::LoadObjFiles(const vector<string> &model_files,
                                        const vector<bool> model_symmetric) {

  assert(model_files.size() == model_symmetric.size());
  model_files_ = model_files;
  env_params_.num_models = static_cast<int>(model_files_.size());

  for (size_t ii = 0; ii < model_files.size(); ++ii) {
    ROS_INFO("Object %zu: Symmetry %d", ii, static_cast<int>(model_symmetric[ii]));
  }

  obj_models_.clear();


  for (int ii = 0; ii < env_params_.num_models; ++ii) {
    pcl::PolygonMesh mesh;
    pcl::io::loadPolygonFile (model_files_[ii].c_str(), mesh);

    pcl::PolygonMesh::Ptr mesh_in (new pcl::PolygonMesh(mesh));
    pcl::PolygonMesh::Ptr mesh_out (new pcl::PolygonMesh(mesh));

    PreprocessModel(mesh_in, mesh_in);

    Eigen::Matrix4f transform;
    transform <<
              1, 0 , 0 , 0,
              0, 1 , 0 , 0,
              0, 0 , 1 , 0,
              0, 0 , 0 , 1;
    TransformPolyMesh(mesh_in, mesh_out, 0.001 * transform);

    ObjectModel obj_model(*mesh_out, model_symmetric[ii]);
    obj_models_.push_back(obj_model);

    ROS_INFO("Read %s with %d polygons and %d triangles", model_files_[ii].c_str(),
             static_cast<int>(mesh.polygons.size()),
             static_cast<int>(mesh.cloud.data.size()));
    ROS_INFO("Object dimensions: X: %f %f, Y: %f %f, Z: %f %f, Rad: %f",
             obj_model.min_x(),
             obj_model.max_x(), obj_model.min_y(), obj_model.max_y(), obj_model.min_z(),
             obj_model.max_z(), obj_model.GetCircumscribedRadius());
    ROS_INFO("\n");

  }
}

bool EnvObjectRecognition::IsValidPose(State s, int model_id, Pose p) {

  vector<int> indices;
  vector<float> sqr_dists;
  PointT point;
  // Eigen::Vector3d vec;
  // vec << p.x, p.y, env_params_.table_height;
  // Eigen::Vector3d camera_vec;
  // camera_vec = env_params_.camera_pose.rotation() * vec + env_params_.camera_pose.translation();
  // point.x = camera_vec[0];
  // point.y = camera_vec[1];
  // point.z = camera_vec[2];
  // printf("P: %f %f %f\n", point.x, point.y, point.z);

  point.x = p.x;
  point.y = p.y;
  // point.z = env_params_.table_height;
  point.z = obj_models_[model_id].max_z() / 2.0 + env_params_.table_height;

  // int num_neighbors_found = knn->radiusSearch(point, env_params_.res / 2,
  //                                             indices,
  //                                             sqr_dists, 1); //0.2
  // double obj_rad = 0.15; //0.15
  double search_rad = obj_models_[model_id].GetCircumscribedRadius() + env_params_.res / 2.0;
  int num_neighbors_found = knn->radiusSearch(point, search_rad,
                                              indices,
                                              sqr_dists, 1); //0.2

  if (num_neighbors_found == 0) {
    return false;
  }

  // TODO: revisit this and accomodate for collision model
  double rad_1, rad_2;
  // rad_1 = obj_models_[model_id].GetCircumscribedRadius();
  rad_1 = obj_models_[model_id].GetInscribedRadius();

  for (size_t ii = 0; ii < s.object_ids.size(); ++ii) {
    int obj_id = s.object_ids[ii];
    Pose obj_pose = s.object_poses[ii];

    // if (fabs(p.x - obj_pose.x) < obj_rad &&
    //     fabs(p.y - obj_pose.y) < obj_rad) {
    //   return false;
    // }

    // rad_2 = obj_models_[obj_id].GetCircumscribedRadius();
    rad_2 = obj_models_[obj_id].GetInscribedRadius();

    if ((p.x - obj_pose.x) * (p.x - obj_pose.x) + (p.y - obj_pose.y) *
        (p.y - obj_pose.y) < (rad_1 + rad_2) * (rad_1 + rad_2))  {
      return false;
    }
  }

  return true;
}

bool EnvObjectRecognition::StatesEqual(const State &s1, const State &s2) {
  if (s1.object_ids.size() != s2.object_ids.size()) {
    return false;
  }

  for (size_t ii = 0; ii < s1.object_ids.size(); ++ii) {
    int idx = -1;

    for (size_t jj = 0; jj < s2.object_ids.size(); ++jj) {
      if (s2.object_ids[jj] == s1.object_ids[ii]) {
        idx = static_cast<int>(jj);
        break;
      }
    }

    // Object not found
    if (idx == -1) {
      return false;
    }

    int model_id = s1.object_ids[ii];
    bool symmetric = false;

    if (model_id != -1) {
      symmetric = obj_models_[model_id].symmetric();
    }

    if (!(s1.object_poses[ii].Equals(s2.object_poses[idx], symmetric))) {
      return false;
    }
  }

  return true;
}

bool EnvObjectRecognition::StatesEqualOrdered(const State &s1,
                                              const State &s2) {
  if (s1.object_ids.size() != s2.object_ids.size()) {
    return false;
  }

  for (size_t ii = 0; ii < s1.object_ids.size(); ++ii) {

    if (s2.object_ids[ii] != s1.object_ids[ii]) {
      return false;
    }


    int model_id = s1.object_ids[ii];
    bool symmetric = false;

    if (model_id != -1) {
      symmetric = obj_models_[model_id].symmetric();
    }

    if (!(s1.object_poses[ii].Equals(s2.object_poses[ii], symmetric))) {
      return false;
    }
  }

  return true;
}

void EnvObjectRecognition::DebugPrint(State s){
  std::cout << "DebugPrint" << std::endl;
  std::cout << "@@@@@@@@@@@@@@@@@@@@@@obj_ids printf for " << id << std::endl;
  for(std::vector<int>::iterator obj_it = s.object_ids.begin(); obj_it != s.object_ids.end(); obj_it++) {
   std::cout << *obj_it << std::endl;
  }

  std::cout << "^^^^^^^^^^^^^^^^^^^^^^disc printf for " << id << std::endl;
  for(std::vector<DiscPose>::iterator obj_it = s.disc_object_poses.begin(); obj_it != s.disc_object_poses.end(); obj_it++) {
    std::cout << "x = " << obj_it->x << "\t";
    std::cout << "y = " << obj_it->y << "\t";
    std::cout << "theta = " << obj_it->theta << std::endl;
  }

  std::cout << "***********************pose printf for " << id << std::endl;
  for(std::vector<Pose>::iterator obj_it = s.object_poses.begin(); obj_it != s.object_poses.end(); obj_it++) {
    std::cout << "x = " << obj_it->x << "\t";
    std::cout << "y = " << obj_it->y << "\t";
    std::cout << "theta = " << obj_it->theta << std::endl;
  }
}

void EnvObjectRecognition::DebugPrintArray(SendMsg* s){
  std::cout << "DebugPrintArray" << std::endl;
  int i = 0;
  while(i < sizeof(SendMsg)) {
   std::cout << *(int*)s << "\t";
   // if ((i%NUM_MODELS) == 0) std::endl;
   s = (SendMsg*)((char*)s + 4);
   i+=4;
  }
  std::cout << "\n";
}

void EnvObjectRecognition::DebugPrintArrayRecv(RecvMsg* s){
  std::cout << "DebugPrintArray" << std::endl;
  int i = 0;
  while(i < sizeof(RecvMsg)) {
   std::cout << *(int*)s << "\t";
   // if ((i%NUM_MODELS) == 0) std::endl;
   s = (RecvMsg*)((char*)s + 4);
   i+=4;
  }
  std::cout << "\n";
}

void EnvObjectRecognition::SendbufPopulate(SendMsg *sendbuf, State s, State p, int sid, int pid) {
  int i = 0;
  // double* temp = (double*)sendbuf;

  // for (i = 0; i < NUM_MODELS*3*2; i++) {
  //  temp[i] = -1.0;
  // }

  // int* temp2 = (int*)((double*)sendbuf + NUM_MODELS * 3 * 2);
  // for(i = 0; i < ((8 * NUM_MODELS )+ 3); i++) {
  //   temp2[i] = -1;
  // }
  // i = 0;

  // memset(sendbuf, 0, sizeof(SendMsg));


  for(std::vector<int>::iterator obj_it = s.object_ids.begin(); obj_it != s.object_ids.end(); obj_it++) {
    sendbuf->source_ids[i++] = *obj_it;
  }
  while(i < NUM_MODELS) {
    sendbuf->source_ids[i++] = -1;
  }

  // std::cout << "size 0: " << i << std::endl;

  i = 0;
  for(std::vector<DiscPose>::iterator obj_it = s.disc_object_poses.begin(); obj_it != s.disc_object_poses.end(); obj_it++) {
    sendbuf->source_disc[i++] = obj_it->x;
    sendbuf->source_disc[i++] = obj_it->y;
    sendbuf->source_disc[i++] = obj_it->theta;
  }
  while(i < 3*NUM_MODELS) {
    sendbuf->source_disc[i++] = -1;
  }

  // std::cout << "size 1: " << i << std::endl;

  i = 0;
  for(std::vector<Pose>::iterator obj_it = s.object_poses.begin(); obj_it != s.object_poses.end(); obj_it++) {
    sendbuf->source_pose[i++] = obj_it->x;
    sendbuf->source_pose[i++] = obj_it->y;
    sendbuf->source_pose[i++] = obj_it->theta;
  }
  while(i < 3*NUM_MODELS) {
    sendbuf->source_pose[i++] = -1.0;
  }

  // std::cout << "size 2: " << i << std::endl;

  i = 0;
  for(std::vector<int>::iterator obj_it = p.object_ids.begin(); obj_it != p.object_ids.end(); obj_it++) {
    sendbuf->cand_ids[i++] = *obj_it;
  }
  while(i < NUM_MODELS) {
    sendbuf->cand_ids[i++] = -1;
  }

  // std::cout << "size 3: " << i << std::endl;

  i = 0;
  for(std::vector<DiscPose>::iterator obj_it = p.disc_object_poses.begin(); obj_it != p.disc_object_poses.end(); obj_it++) {
    sendbuf->cand_disc[i++] = obj_it->x;
    sendbuf->cand_disc[i++] = obj_it->y;
    sendbuf->cand_disc[i++] = obj_it->theta;
  }
  while(i < 3*NUM_MODELS) {
    sendbuf->cand_disc[i++] = -1;
  }

  // std::cout << "size 4: " << i << std::endl;

  i = 0;
  for(std::vector<Pose>::iterator obj_it = p.object_poses.begin(); obj_it != p.object_poses.end(); obj_it++) {
    sendbuf->cand_pose[i++] = obj_it->x;
    sendbuf->cand_pose[i++] = obj_it->y;
    sendbuf->cand_pose[i++] = obj_it->theta;
  }
  while(i < 3*NUM_MODELS) {
    sendbuf->cand_pose[i++] = -1.0;
  }

  // std::cout << "size 5: " << i << std::endl;

  sendbuf->source_id = sid;
  sendbuf->cand_id = pid;
  sendbuf->valid = 1;
}

int EnvObjectRecognition::ExpectedCountScatter(int *expected) {
  int val = 0;
  // std::cout << "Proc: " << id << "reached ExpectedCountScatter" << std::endl;
  MPI_Scatter(expected, 1, MPI_INT, &val, 1, MPI_INT, 0, MPI_COMM_WORLD);
  assert(val > 0);
  // std::cout << "Proc: " << id << "left ExpectedCountScatter" << std::endl;
  return val;
}

void EnvObjectRecognition::DataScatter(SendMsg* sendbuf, SendMsg* getbuf, int expected_count) {
  int nitems = 9;
  int blocklengths[9] = {NUM_MODELS, NUM_MODELS*3, NUM_MODELS*3,
                        NUM_MODELS, NUM_MODELS*3, NUM_MODELS*3,
                        1, 1, 1};
  MPI_Datatype types[9] = {MPI_INT, MPI_INT, MPI_DOUBLE, MPI_INT, MPI_INT, MPI_DOUBLE,
                            MPI_INT, MPI_INT, MPI_INT};

  MPI_Datatype mpi_sendbuf;
  MPI_Aint offset[9];
  offset[0] = offsetof(SendMsg, source_ids);
  offset[1] = offsetof(SendMsg, source_disc);
  offset[2] = offsetof(SendMsg, source_pose);
  offset[3] = offsetof(SendMsg, cand_ids);
  offset[4] = offsetof(SendMsg, cand_disc);
  offset[5] = offsetof(SendMsg, cand_pose);
  offset[6] = offsetof(SendMsg, source_id);
  offset[7] = offsetof(SendMsg, cand_id);
  offset[8] = offsetof(SendMsg, valid);

  MPI_Type_create_struct(nitems, blocklengths, offset, types, &mpi_sendbuf);
  MPI_Type_commit(&mpi_sendbuf);
  std::cout << "Proc: " << id << "going to Scatter" << std::endl;
  MPI_Scatter(sendbuf, expected_count, mpi_sendbuf, 
                getbuf, expected_count, mpi_sendbuf, 0, MPI_COMM_WORLD);
  std::cout << "Proc: " << id << "left Scatter" << std::endl;

}

int EnvObjectRecognition::GetRecvdState(State *work_source_state, 
                                          State *work_cand_succs,
                                          int *work_source_id,
                                          int *work_cand_id,
                                          SendMsg* dummy,
                                          int val) {
  int count = 0;
  std::cout << "Proc: " << id << "reached start of GetRecvdState" << std::endl;
  for (int i = 0; i < val; i++) {
    // DebugPrintArray(dummy);
    
    if(dummy[i].valid == 1){
      count++;
      for(int j = 0; j < NUM_MODELS; j++){
        if( (dummy[i].source_ids)[j] != -1){
          int int_add = (dummy[i].source_ids)[j];
          work_source_state[i].object_ids.push_back(int_add);
        }
      }

      // std::cout << i << " : " << val << std::endl;
      // std::cout << "Proc: " << id << "reached start of GetRecvdState 1" << std::endl;

      int j;
      for(int k = 0; k < NUM_MODELS; k++){
        j = k * 3;
        if( (dummy[i].source_disc)[j] != -1){
          DiscPose disc(0, 0, 0);
          // std::cout << "Proc: " << id << "reached start of GetRecvdState k1" << std::endl;
          disc.x = (dummy[i].source_disc)[j++];
          // std::cout << "Proc: " << id << "reached start of GetRecvdState k2" << std::endl;
          disc.y = (dummy[i].source_disc)[j++];
          // std::cout << "Proc: " << id << "reached start of GetRecvdState k3" << std::endl;
          disc.theta = (dummy[i].source_disc)[j++];
          // std::cout << "Proc: " << id << "reached start of GetRecvdState k4" << std::endl;
          work_source_state[i].disc_object_poses.push_back(disc);
        }
      }

      // std::cout << "Proc: " << id << "reached start of GetRecvdState 2" << std::endl;

      j = 0;
      for(int k = 0; k < NUM_MODELS; k++){
        j = k * 3;
        if( (dummy[i].source_pose)[j] != -1.0){
          Pose pose;
          // std::cout << "Proc: " << id << "reached start of GetRecvdState k5" << std::endl;
          pose.x = (dummy[i].source_pose)[j++];
          pose.y = (dummy[i].source_pose)[j++];
          pose.theta = (dummy[i].source_pose)[j++];
          work_source_state[i].object_poses.push_back(pose);
        }
      }

      // std::cout << "Proc: " << id << "reached start of GetRecvdState 3" << std::endl;

      //cand

      // DebugPrintArray(&dummy[i]);

      for(int j = 0; j < NUM_MODELS; j++){
        if( (dummy[i].cand_ids)[j] != -1){
          int id_add = (dummy[i].cand_ids)[j];
          // std::cout << "Proc: " << id << "reached start of GetRecvdState k6: " << id_add << std::endl;
          work_cand_succs[i].object_ids.push_back(id_add);
          // std::cout << "Proc: " << id << "reached end of GetRecvdState k6" << std::endl;
        }
      }

      // std::cout << "Proc: " << id << "reached start of GetRecvdState 4" << std::endl;

      for(int k = 0; k < NUM_MODELS; k++){
        j = k * 3;
        if( (dummy[i].cand_disc)[j] != -1){
          DiscPose disc(0, 0, 0);
          // std::cout << "Proc: " << id << "reached start of GetRecvdState k7" << std::endl;
          disc.x = (dummy[i].cand_disc)[j++];
          disc.y = (dummy[i].cand_disc)[j++];
          disc.theta = (dummy[i].cand_disc)[j++];
          work_cand_succs[i].disc_object_poses.push_back(disc);
        }
      }

      // std::cout << "Proc: " << id << "reached start of GetRecvdState 5" << std::endl;

      j = 0;
      for(int k = 0; k < NUM_MODELS; k++){
        j = k * 3;
        if( (dummy[i].cand_pose)[j] != -1.0){
          Pose pose(0, 0, 0);
          // std::cout << "Proc: " << id << "reached start of GetRecvdState k8" << std::endl;
          pose.x = (dummy[i].cand_pose)[j++];
          pose.y = (dummy[i].cand_pose)[j++];
          pose.theta = (dummy[i].cand_pose)[j++];
          // std::cout << "Proc: " << id << "going to push_back GetRecvdState k8" << std::endl;
          work_cand_succs[i].object_poses.push_back(pose);
        }
      }

      // std::cout << "Proc: " << id << "reached start of GetRecvdState 6" << std::endl;

      work_source_id[i] = dummy[i].source_id;
      work_cand_id[i] = dummy[i].cand_id;
    }
  }

  // std::cout << "Proc: " << id << "left GetRecvdState with " << count << std::endl;

  return count;
}

void EnvObjectRecognition::RecvbufPopulate(RecvMsg* sendbuf, 
                                            State& s,
                                            StateProperties& child_properties,
                                            int cost) {

  int i = 0;
  for(std::vector<int>::iterator obj_it = s.object_ids.begin(); obj_it != s.object_ids.end(); obj_it++) {
    sendbuf->child_ids[i++] = *obj_it;
  }
  while(i < NUM_MODELS) {
    sendbuf->child_ids[i++] = -1;
  }

  i = 0;
  for(std::vector<DiscPose>::iterator obj_it = s.disc_object_poses.begin(); obj_it != s.disc_object_poses.end(); obj_it++) {
    sendbuf->child_disc[i++] = obj_it->x;
    sendbuf->child_disc[i++] = obj_it->y;
    sendbuf->child_disc[i++] = obj_it->theta;
  }
  while(i < 3*NUM_MODELS) {
    sendbuf->child_disc[i++] = -1;
  }

  i = 0;
  for(std::vector<Pose>::iterator obj_it = s.object_poses.begin(); obj_it != s.object_poses.end(); obj_it++) {
    sendbuf->child_pose[i++] = obj_it->x;
    sendbuf->child_pose[i++] = obj_it->y;
    sendbuf->child_pose[i++] = obj_it->theta;
  }
  while(i < 3*NUM_MODELS) {
    sendbuf->child_pose[i++] = -1.0;
  }

  sendbuf->last_min_depth = child_properties.last_min_depth;
  sendbuf->last_max_depth = child_properties.last_max_depth;
  sendbuf->cost = cost;
  sendbuf->valid = 1;

}

void EnvObjectRecognition::DataGather(RecvMsg* recvbuf, RecvMsg* getresult, int expected_count) {
  int nitems = 7;
  int blocklengths[7] = {NUM_MODELS, NUM_MODELS*3, NUM_MODELS*3, 1, 1, 1, 1};
  MPI_Datatype types[7] = {MPI_INT, MPI_INT, MPI_DOUBLE, MPI_UNSIGNED_SHORT,
                            MPI_UNSIGNED_SHORT, MPI_INT, MPI_INT};

  MPI_Datatype mpi_recvbuf;
  MPI_Aint offset[7];
  offset[0] = offsetof(RecvMsg, child_ids);
  offset[1] = offsetof(RecvMsg, child_disc);
  offset[2] = offsetof(RecvMsg, child_pose);
  offset[3] = offsetof(RecvMsg, last_min_depth);
  offset[4] = offsetof(RecvMsg, last_max_depth);
  offset[5] = offsetof(RecvMsg, cost);
  offset[6] = offsetof(RecvMsg, valid);

  MPI_Type_create_struct(nitems, blocklengths, offset, types, &mpi_recvbuf);
  MPI_Type_commit(&mpi_recvbuf);
  MPI_Gather(recvbuf, expected_count, mpi_recvbuf, 
                getresult, expected_count, mpi_recvbuf, 0, MPI_COMM_WORLD);
}

int EnvObjectRecognition::GetRecvdResult(State *work_source_state, 
                                          StateProperties *child_properties_result,
                                          int *cost_result,
                                          RecvMsg* dummy,
                                          int tot) {
  int count = 0;
  for (int i = 0; i < tot; i++) {

    if(dummy[i].valid == 1){
      count++;
      for(int j = 0; j < NUM_MODELS; j++){
        if( (dummy[i].child_ids)[j] != -1){
          work_source_state[i].object_ids.push_back( (dummy[i].child_ids)[j] );
        }
      }

      int j;
      for(int k = 0; k < NUM_MODELS; k++){
        j = k * 3;
        if( (dummy[i].child_disc)[j] != -1){
          DiscPose disc(0, 0, 0);
          disc.x = (dummy[i].child_disc)[j++];
          disc.y = (dummy[i].child_disc)[j++];
          disc.theta = (dummy[i].child_disc)[j++];
          work_source_state[i].disc_object_poses.push_back(disc);
        }
      }

      j = 0;
      for(int k = 0; k < NUM_MODELS; k++){
        j = k * 3;
        if( (dummy[i].child_pose)[j] != -1.0){
          Pose pose(0, 0, 0);
          pose.x = (dummy[i].child_pose)[j++];
          pose.y = (dummy[i].child_pose)[j++];
          pose.theta = (dummy[i].child_pose)[j++];
          work_source_state[i].object_poses.push_back(pose);
        }
      }

      child_properties_result[i].last_min_depth = dummy[i].last_min_depth;
      child_properties_result[i].last_max_depth = dummy[i].last_max_depth;

      cost_result[i] = dummy[i].cost;
    }
  }

  return count;
}

void EnvObjectRecognition::GetSuccs(int source_state_id,
                                    vector<int> *succ_ids, vector<int> *costs) {
  succ_ids->clear();
  costs->clear();

  if (source_state_id == env_params_.goal_state_id) {
    HeuristicMap[source_state_id] = static_cast<int>(0.0);
    return;
  }

  // If in cache, return
  auto it = succ_cache.find(source_state_id);

  if (it !=  succ_cache.end()) {
    *succ_ids = succ_cache[source_state_id];
    *costs = succ_cache[source_state_id];
    return;
  }

  State source_state = StateIDToState(source_state_id);

  ROS_INFO("Expanding state: %d with %zu objects",
           source_state_id,
           source_state.object_ids.size());
  string fname = kDebugDir + "expansion_" + to_string(source_state_id) + ".png";
  PrintState(source_state_id, fname);

  vector<int> candidate_succ_ids, candidate_costs;
  vector<State> candidate_succs;

  if (IsGoalState(source_state)) {
    // NOTE: We shouldn't really get here at all
    int succ_id = StateToStateID(goal_state_);
    succ_ids->push_back(succ_id);
    costs->push_back(0);
    HeuristicMap[succ_id] = static_cast<int>(0.0);
    return;
  }

  for (int ii = 0; ii < env_params_.num_models; ++ii) {

    // Skip object if it has already been assigned
    auto it = std::find(source_state.object_ids.begin(),
                        source_state.object_ids.end(),
                        ii);

    if (it != source_state.object_ids.end()) {
      continue;
    }

    for (double x = env_params_.x_min; x <= env_params_.x_max;
         x += env_params_.res) {
      for (double y = env_params_.y_min; y <= env_params_.y_max;
           y += env_params_.res) {
        for (double theta = 0; theta < 2 * M_PI; theta += env_params_.theta_res) {
          Pose p(x, y, theta);

          if (!IsValidPose(source_state, ii, p)) {
            continue;
          }

          State s = source_state; // Can only add objects, not remove them
          s.object_ids.push_back(ii);
          s.object_poses.push_back(p);
          int succ_id = StateToStateID(s);

          // TODO: simple check to ensure we don't add duplicate children

          candidate_succ_ids.push_back(succ_id);
          candidate_succs.push_back(s);
          HeuristicMap[succ_id] = static_cast<int>(0.0);

          // If symmetric object, don't iterate over all thetas
          if (obj_models_[ii].symmetric()) {
            break;
          }
        }
      }
    }
  }

  
  // Awesome work starts

  // int number = 3343;
  // MPI_Send(&number, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);

  // std::cout << "Proc: " << id << " reached start of awesome code" << std::endl;
  int next_multiple = candidate_succ_ids.size() + num_proc - (candidate_succ_ids.size() % num_proc);
  SendMsg* sendbuf = (SendMsg*) malloc(next_multiple * sizeof(SendMsg));
  for (int i = candidate_succ_ids.size(); i < next_multiple; i++)
    sendbuf[i].valid = -1;
  SendMsg* tempbuf = sendbuf;

  //populate sendbuf buffer

  int sz = candidate_succ_ids.size();
  
  #pragma omp parallel for
  for(unsigned int ii = 0; ii < sz; ++ii)
    SendbufPopulate(&tempbuf[ii], source_state, candidate_succs[ii], source_state_id, candidate_succ_ids[ii]);

  // {
    // DebugPrint(candidate_succs[ii]);
    // if (ii < 8) {
    //   DebugPrint(candidate_succs[ii]);
    //   DebugPrintArray(tempbuf);
    // }
  //   tempbuf++;
  // }

  //count array so workers can allocate appropriately
  int* expected_count = (int *) malloc(num_proc * sizeof(int));
  //split per processor
  int val = next_multiple / num_proc;
  assert((next_multiple % num_proc) == 0);

  for (int i = 0; i < num_proc; i++)
    expected_count[i] = val;

  std::cout << "Proc: " << id << "populated buffer to send " << val << std::endl;

  // Till now master only executes

  //expected_count_scatter
  ExpectedCountScatter(expected_count);
  free(expected_count);

  SendMsg* dummy = (SendMsg*) malloc(val * sizeof(SendMsg));
  DataScatter(sendbuf, dummy, val);
  std::cout << "Proc: " << id << "printing " << std::endl;
  // DebugPrintArray(dummy);

  free(sendbuf);
  
  State* work_source_state = new State[val];

  // State* work_cand_succs = (State*) malloc(val * sizeof(State));
  State* work_cand_succs = new State[val];

  int* work_source_id = (int *) malloc(val * sizeof(int));
  int* work_cand_id = (int *) malloc(val * sizeof(int));

  int count = GetRecvdState(work_source_state, work_cand_succs,
                work_source_id, work_cand_id, dummy, val);

  // MPI_Barrier(MPI_COMM_WORLD);
  // std::cout << "proc "<< id <<": reached MPI_Barrier" << std::endl;
  free(dummy);

  // std::cout << "proc "<< id <<": reached MPI_Barrier" << std::endl;

  State* adjusted_child_state = new State[val];
  StateProperties* child_properties = new StateProperties[val];
  int* cost = (int *) malloc(val * sizeof(int));

  for (int ii = 0; ii < count; ii++) {
    cost[ii] = GetTrueCost(work_source_state[ii], 
                            work_cand_succs[ii],
                            work_source_id[ii],
                            work_cand_id[ii],
                            &adjusted_child_state[ii],
                            &child_properties[ii]);
  }

  // workers result buf
  RecvMsg* recvbuf = (RecvMsg*) malloc(val * sizeof(RecvMsg));
  for (int i = 0; i < val; i++)
    recvbuf[i].valid = -1;

  RecvMsg* recvtemp = recvbuf;

  // #pragma omp parallel for
  for (size_t ii = 0; ii < count; ++ii)
    RecvbufPopulate(&recvtemp[ii], adjusted_child_state[ii], child_properties[ii], cost[ii]);
    // DebugPrintArrayRecv(recvtemp);
    // recvtemp++;

  std::cout << "proc "<< id <<": done RecvbufPopulate" << std::endl;

  // delete adjusted_child_state;
  // delete child_properties;
  // delete cost;

  RecvMsg* getresult = (RecvMsg*) malloc(next_multiple * sizeof(RecvMsg));
  std::cout << "val: " << val << "next_multiple: " << next_multiple << std::endl;
  
  DataGather(recvbuf, getresult, val);
  std::cout << "proc "<< id <<": done DataGather" << std::endl;

  free(recvbuf);

  State* child_result = new State[candidate_succ_ids.size()];
  StateProperties* child_properties_result = new StateProperties[candidate_succ_ids.size()];

  int* cost_result = (int *) malloc(candidate_succ_ids.size() * sizeof(int));

  GetRecvdResult(child_result, 
                child_properties_result,
                cost_result,
                getresult,
                candidate_succ_ids.size());
  std::cout << "proc "<< id <<": done GetRecvdResult" << std::endl;


  //---- PARALLELIZE THIS LOOP-----------//
  for (size_t ii = 0; ii < candidate_succ_ids.size(); ++ii) {

    minz_map_[candidate_succ_ids[ii]] = child_properties_result[ii].last_min_depth;
    maxz_map_[candidate_succ_ids[ii]] = child_properties_result[ii].last_max_depth;

    for (auto it = StateMap.begin(); it != StateMap.end(); ++it) {
      if (it->first == candidate_succ_ids[ii]) {
        continue;  // This is the original state
      }

      if (StatesEqual(child_result[ii], it->second)) {
        cost_result[ii] = -1;
        break;
      }
    }

    if (cost_result[ii] != -1) {
      StateMap[candidate_succ_ids[ii]] = child_result[ii];
    }

    candidate_costs.push_back(cost_result[ii]);
  }

  // MPI_Barrier(MPI_COMM_WORLD);

  //--------------------------------------//

  // Awesome work ends

  for (size_t ii = 0; ii < candidate_succ_ids.size(); ++ii) {
    if (candidate_costs[ii] == -1) {
      continue;  // Invalid successor
    }

    succ_ids->push_back(candidate_succ_ids[ii]);
    costs->push_back(candidate_costs[ii]);
  }

  // cache succs and costs
  succ_cache[source_state_id] = *succ_ids;
  cost_cache[source_state_id] = *costs;


  // ROS_INFO("Expanding state: %d with %d objects and %d successors",
  //          source_state_id,
  //          source_state.object_ids.size(), costs->size());
  // string fname = kDebugDir + "expansion_" + to_string(source_state_id) + ".png";
  // PrintState(source_state_id, fname);
}



void EnvObjectRecognition::GetLazySuccs(int source_state_id,
                                        vector<int> *succ_ids, vector<int> *costs,
                                        vector<bool> *true_costs) {
  succ_ids->clear();
  costs->clear();

  if (true_costs != NULL) {
    true_costs->clear();
  }

  if (source_state_id == env_params_.goal_state_id) {
    HeuristicMap[source_state_id] = static_cast<int>(0.0);
    return;
  }


  // If in cache, return
  auto it = succ_cache.find(source_state_id);

  if (it !=  succ_cache.end()) {
    *succ_ids = succ_cache[source_state_id];
    *costs = succ_cache[source_state_id];
    true_costs->resize(costs->size(), true);
    return;
  }

  State source_state = StateIDToState(source_state_id);
  vector<State> succs;

  if (IsGoalState(source_state)) {
    // NOTE: We shouldn't really get here at all
    succs.push_back(goal_state_);
    int succ_id = StateToStateID(goal_state_);
    succ_ids->push_back(succ_id);
    costs->push_back(0);
    true_costs->push_back(true);
    HeuristicMap[succ_id] = static_cast<int>(0.0);
    return;
  }

  for (int ii = 0; ii < env_params_.num_models; ++ii) {

    // Skip object if it has already been assigned
    auto it = std::find(source_state.object_ids.begin(),
                        source_state.object_ids.end(),
                        ii);

    if (it != source_state.object_ids.end()) {
      continue;
    }

    for (double x = env_params_.x_min; x <= env_params_.x_max;
         x += env_params_.res) {
      for (double y = env_params_.y_min; y <= env_params_.y_max;
           y += env_params_.res) {
        for (double theta = 0; theta < 2 * M_PI; theta += env_params_.theta_res) {
          Pose p(x, y, theta);

          if (!IsValidPose(source_state, ii, p)) {
            continue;
          }

          State s = source_state; // Can only add objects, not remove them
          s.object_ids.push_back(ii);
          s.object_poses.push_back(p);
          int succ_id = StateToStateID(s);

          succs.push_back(s);
          succ_ids->push_back(succ_id);
          costs->push_back(static_cast<int>(0));
          HeuristicMap[succ_id] = static_cast<int>(0.0);
        }
      }
    }
  }

  // cache succs and costs
  succ_cache[source_state_id] = *succ_ids;
  cost_cache[source_state_id] = *costs;

  if (true_costs != NULL) {
    true_costs->resize(costs->size(), false);
  }

  ROS_INFO("Expanded state: %d with %zu objects and %zu successors",
           source_state_id,
           source_state.object_ids.size(), costs->size());
  string fname = kDebugDir + "expansion_" + to_string(source_state_id) + ".png";
  PrintState(source_state_id, fname);
}


int EnvObjectRecognition::GetGoalHeuristic(int state_id) {

  if (state_id == env_params_.goal_state_id) {
    return 0;
  }

  if (state_id == env_params_.start_state_id) {
    return 0;
  }

  auto it = HeuristicMap.find(state_id);

  if (it == HeuristicMap.end()) {
    ROS_ERROR("State %d was not found in heuristic map", state_id);
    return 0;
  }

  int depth_heur;
  State s = StateIDToState(state_id);
  depth_heur = (env_params_.num_objects - s.object_ids.size());
  return depth_heur;

  // return HeuristicMap[state_id];

  /*
  if (s.object_ids.size() == env_params_.num_objects)
  {
    return 0;
  }
  vector<int> pixels;
  const int num_pixels = env_params_.img_width * env_params_.img_height;
  vector<unsigned short> depth_image;
  const float *depth_buffer = GetDepthImage(s, &depth_image);

  for (int ii = 0; ii < num_pixels; ++ii) {
    if (depth_image[ii] == 20000 && observed_depth_image_[ii] != 20000) {
      pixels.push_back(ii);
    }
  }

  heur = ComputeScore(depth_image, pixels);
  */

  //heur = 0;
}

int EnvObjectRecognition::GetGoalHeuristic(int q_id, int state_id) {

  if (state_id == env_params_.goal_state_id) {
    return 0;
  }

  State s = StateIDToState(state_id);

  int num_objects_left = env_params_.num_objects - s.object_ids.size();
  int depth_first_heur = num_objects_left;
  // printf("State %d: %d %d\n", state_id, icp_heur, depth_first_heur);

  switch (q_id) {
  case 0:
    return 0;

  // return depth_first_heur;
  case 1:
    return depth_first_heur;

  case 2:
    return GetICPHeuristic(s);

  default:
    return 0;
  }
}

int EnvObjectRecognition::GetTrueCost(const State source_state,
                                      const State child_state, int parent_id, int child_id,
                                      State *adjusted_child_state, StateProperties *child_properties) {

#if PROFILE
  FILE * pFile;
  pFile = fopen ("/home/namanj/profile.txt","a");
  double startTime = CycleTimer::currentSeconds();
#endif

  assert(child_state.object_ids.size() > 0);

  *adjusted_child_state = child_state;
  child_properties->last_max_depth = 20000;
  child_properties->last_min_depth = 0;

  vector<unsigned short> source_depth_image;
  const float *depth_buffer = GetDepthImage(source_state, &source_depth_image);
  const int num_pixels = env_params_.img_width * env_params_.img_height;

  Pose child_pose = child_state.object_poses.back();
  int last_object_id = child_state.object_ids.back();

  vector<unsigned short> depth_image, new_obj_depth_image;
  const float *succ_depth_buffer;
  Pose pose_in(child_pose.x, child_pose.y, child_pose.theta),
       pose_out(child_pose.x, child_pose.y, child_pose.theta);
  PointCloudPtr cloud_in(new PointCloud);
  PointCloudPtr succ_cloud(new PointCloud);
  PointCloudPtr cloud_out(new PointCloud);

  // Begin ICP Adjustment
  State s_new_obj;
  s_new_obj.object_ids.push_back(last_object_id);
  s_new_obj.object_poses.push_back(child_pose);
  succ_depth_buffer = GetDepthImage(s_new_obj, &new_obj_depth_image);

  // Create new buffer with only new pixels
  float new_pixel_buffer[env_params_.img_width * env_params_.img_height];

  for (int y = 0; y <  env_params_.img_height; ++y) {
    for (int x = 0; x < env_params_.img_width; ++x) {
      int i = y * env_params_.img_width + x ; // depth image index
      int i_in = (env_params_.img_height - 1 - y) * env_params_.img_width + x
                 ; // flip up down (buffer index)

      if (new_obj_depth_image[i] != 20000 && source_depth_image[i] == 20000) {
        new_pixel_buffer[i_in] = succ_depth_buffer[i_in];
      } else {
        new_pixel_buffer[i_in] = 1.0; // max range
      }
    }
  }

  // Align with ICP
  // Only non-occluded points
  kinect_simulator_->rl_->getPointCloudFromBuffer (cloud_in, new_pixel_buffer,
                                                   true,
                                                   env_params_.camera_pose);

  double icp_fitness_score = GetICPAdjustedPose(cloud_in, pose_in, cloud_out,
                                                &pose_out);
  // icp_cost = static_cast<int>(kICPCostMultiplier * icp_fitness_score);
  int last_idx = child_state.object_poses.size() - 1;

  adjusted_child_state->object_poses[last_idx] = pose_out;
  // End ICP Adjustment

  // Check again after icp
  if (!IsValidPose(source_state, last_object_id,
                   adjusted_child_state->object_poses.back())) {
    // printf(" state %d is invalid\n ", child_id);
    return -1;
  }

  succ_depth_buffer = GetDepthImage(*adjusted_child_state, &depth_image);
  // All points
  kinect_simulator_->rl_->getPointCloud(succ_cloud, true,
                                        env_params_.camera_pose);

  unsigned short succ_min_depth, succ_max_depth;
  vector<int> new_pixel_indices;

  if (IsOccluded(source_depth_image, depth_image, &new_pixel_indices,
                 &succ_min_depth,
                 &succ_max_depth)) {
    return -1;
  }

  // Cache the min and max depths
  child_properties->last_min_depth = succ_min_depth;
  child_properties->last_max_depth = succ_max_depth;



  // Must use re-rendered adjusted partial cloud for cost
  for (int y = 0; y <  env_params_.img_height; ++y) {
    for (int x = 0; x < env_params_.img_width; ++x) {
      int i = y * env_params_.img_width + x ; // depth image index
      int i_in = (env_params_.img_height - 1 - y) * env_params_.img_width + x
                 ; // flip up down (buffer index)

      // auto it = find(new_pixel_indices.begin(), new_pixel_indices.end(), i);
      // if (it == new_pixel_indices.end()) continue; //Skip source pixels

      if (depth_image[i] != 20000 && source_depth_image[i] == 20000) {
        new_pixel_buffer[i_in] = succ_depth_buffer[i_in];
      } else {
        new_pixel_buffer[i_in] = 1.0; // max range
      }
    }
  }
  kinect_simulator_->rl_->getPointCloudFromBuffer (cloud_out, new_pixel_buffer,
                                                   true,
                                                   env_params_.camera_pose);


  // Compute costs
  int target_cost = 0, source_cost = 0, total_cost = 0;
  target_cost = GetTargetCost(cloud_out);
  total_cost = source_cost + target_cost;

  if (image_debug_) {
    std::stringstream ss;
    ss.precision(20);
    ss << kDebugDir + "succ_" << child_id << ".png";
    PrintImage(ss.str(), depth_image);
    ROS_INFO("State %d,       %d      %d      %d", child_id,
             target_cost,
             source_cost, total_cost);
  }
#if PROFILE
  double endTime = CycleTimer::currentSeconds();
  diff += (endTime - startTime);
  fprintf(pFile, "Render:   %.4f ms\n", 1000.f * diff);
  fclose(pFile);
#endif
  // if (image_debug_) {
  //   std::stringstream ss1, ss2;
  //   ss1.precision(20);
  //   ss2.precision(20);
  //   ss1 << kDebugDir + "cloud_" << child_id << ".pcd";
  //   ss2 << kDebugDir + "cloud_aligned_" << child_id << ".pcd";
  //   pcl::PCDWriter writer;
  //   writer.writeBinary (ss1.str()  , *cloud_in);
  //   writer.writeBinary (ss2.str()  , *cloud_out);
  // }
  return total_cost;
}

bool EnvObjectRecognition::IsOccluded(const vector<unsigned short>
                                      &parent_depth_image, const vector<unsigned short> &succ_depth_image,
                                      vector<int> *new_pixel_indices, unsigned short *min_succ_depth,
                                      unsigned short *max_succ_depth) {

  const int num_pixels = env_params_.img_width * env_params_.img_height;
  assert(static_cast<int>(parent_depth_image.size()) == num_pixels);
  assert(static_cast<int>(succ_depth_image.size()) == num_pixels);

  new_pixel_indices->clear();
  *min_succ_depth = 20000;
  *max_succ_depth = 0;

  bool is_occluded = false;

  for (int jj = 0; jj < num_pixels; ++jj) {

    if (succ_depth_image[jj] != 20000 &&
        parent_depth_image[jj] == 20000) {
      new_pixel_indices->push_back(jj);

      // Find mininum depth of new pixels
      if (succ_depth_image[jj] != 20000 && succ_depth_image[jj] < *min_succ_depth) {
        *min_succ_depth = succ_depth_image[jj];
      }

      // Find maximum depth of new pixels
      if (succ_depth_image[jj] != 20000 && succ_depth_image[jj] > *max_succ_depth) {
        *max_succ_depth = succ_depth_image[jj];
      }
    }

    // Occlusion
    if (succ_depth_image[jj] != 20000 && parent_depth_image[jj] != 20000 &&
        succ_depth_image[jj] < parent_depth_image[jj]) {
      is_occluded = true;
      break;
    }

    // if (succ_depth_image[jj] == 20000 && observed_depth_image_[jj] != 20000) {
    //   obs_pixels.push_back(jj);
    // }
  }

  if (is_occluded) {
    new_pixel_indices->clear();
    *min_succ_depth = 20000;
    *max_succ_depth = 0;
  }

  return is_occluded;
}

int EnvObjectRecognition::GetTargetCost(const PointCloudPtr
                                        partial_rendered_cloud) {
  // Nearest-neighbor cost
  double nn_score = 0;

  for (size_t ii = 0; ii < partial_rendered_cloud->points.size(); ++ii) {
    vector<int> indices;
    vector<float> sqr_dists;
    PointT point = partial_rendered_cloud->points[ii];
    int num_neighbors_found = knn->radiusSearch(point, kSensorResolution,
                                                indices,
                                                sqr_dists, 1);

    if (num_neighbors_found == 0) {
      // nn_score += kSensorResolutionSqr ;
      // nn_score += kSensorResolutionSqr * 100 ; //TODO: Do something principled
      nn_score += 1.0;
    } else {
      // nn_score += sqr_dists[0];
      nn_score += 0.0;
    }
  }

  int target_cost = static_cast<int>(nn_score);
  return target_cost;
}

int EnvObjectRecognition::GetSourceCost(const PointCloudPtr
                                        full_rendered_cloud, const int parent_id, const int child_id) {

  const int num_pixels = env_params_.img_width * env_params_.img_height;

  // Compute the cost of surely unexplained points in observed point cloud
  pcl::search::KdTree<PointT>::Ptr knn_reverse;
  knn_reverse.reset(new pcl::search::KdTree<PointT>(true));
  knn_reverse->setInputCloud(full_rendered_cloud);

  State child_state = StateIDToState(child_id);
  assert(child_state.object_poses.size() != 0);
  Pose last_obj_pose = child_state.object_poses.back();
  int last_obj_id = child_state.object_ids.back();
  PointT obj_center;
  obj_center.x = last_obj_pose.x;
  obj_center.y = last_obj_pose.y;
  obj_center.z = env_params_.table_height;

  double nn_score = 0.0;

  // TODO: Move this to a better place
  if (counted_pixels_map_[parent_id].size() != 0) {
    counted_pixels_map_[child_id] = counted_pixels_map_[parent_id];
  }

  for (int ii = 0; ii < num_pixels; ++ii) {

    // Skip if empty pixel
    if (observed_depth_image_[ii] == 20000) {
      continue;
    }

    // Skip if already accounted for
    vector<int> counted_pixels = counted_pixels_map_[child_id];
    auto it = find(counted_pixels.begin(),
                   counted_pixels.end(), ii);

    if (it != counted_pixels.end()) {
      continue;
    }

    // Skip if out-of-slice
    // const double depth_inflation = 0.1;
    // if (observed_depth_image_[ii] < source_max_depth
    //      || observed_depth_image_[ii] >= max_succ_depth + depth_inflation) {
    //   continue;
    // }

    vector<int> indices;
    vector<float> sqr_dists;
    PointT point;


    int u = ii / env_params_.img_width;
    int v = ii % env_params_.img_width;
    // point = observed_organized_cloud_->at(v, u);

    Eigen::Vector3f point_eig;
    kinect_simulator_->rl_->getGlobalPoint(v, u,
                                           static_cast<float>(observed_depth_image_[ii]) / 1000.0, cam_to_world_,
                                           point_eig);
    point.x = point_eig[0];
    point.y = point_eig[1];
    point.z = point_eig[2];


    const double kSensorResolution = 0.01 / 2;
    const double kSensorResolutionSqr = kSensorResolution * kSensorResolution;
    const double kCollisionRadThresh = 0.05;
    const int kCollisionPointsThresh = 5;
    int num_neighbors_found = knn_reverse->radiusSearch(point, kCollisionRadThresh,
                                                        indices,
                                                        sqr_dists, kCollisionPointsThresh);
    bool point_unexplained = (num_neighbors_found == 0 ||
                              static_cast<double>(sqr_dists[0]) > kSensorResolutionSqr);

    PointT projected_point;
    projected_point.x = point.x;
    projected_point.y = point.y;
    projected_point.z = env_params_.table_height;
    float dist = pcl::euclideanDistance(obj_center, projected_point);

    // bool point_in_collision = dist <= obj_models_[last_obj_id].inscribed_rad();
    bool point_in_collision = dist <= 3.0 * obj_models_[last_obj_id].GetCircumscribedRadius();
    // bool point_in_collision = num_neighbors_found >= kCollisionPointsThresh;


    bool too_far_in_front = false;

    const unsigned short min_succ_depth = minz_map_[child_id];

    if (observed_depth_image_[ii] < min_succ_depth) {
      too_far_in_front = true;
    }


    // Skip if not in collision (i.e, might be explained by a future object) or
    // if its not too far in front
    if (point_unexplained) {
      if (point_in_collision || too_far_in_front) {
        nn_score += 1.0 ; //TODO: Do something principled
        counted_pixels_map_[child_id].push_back(ii);
      }
    } else {
      counted_pixels_map_[child_id].push_back(ii);
    }
  }

  // int icp_cost = static_cast<int>(kICPCostMultiplier * nn_score);
  int source_cost = static_cast<int>(nn_score);
  return source_cost;
}

void EnvObjectRecognition::PrintState(int state_id, string fname) {

  State s = StateIDToState(state_id);
  PrintState(s, fname);
  return;
}

void EnvObjectRecognition::PrintState(State s, string fname) {

  printf("Num objects: %zu\n", s.object_ids.size());

  for (size_t ii = 0; ii < s.object_ids.size(); ++ii) {

    printf("Obj: %d, Pose: %f %f %f\n", s.object_ids[ii], s.object_poses[ii].x,
           s.object_poses[ii].y, s.object_poses[ii].theta);
  }

  vector<unsigned short> depth_image;
  const float *depth_buffer = GetDepthImage(s, &depth_image);
  // kinect_simulator_->write_depth_image(depth_buffer, fname);
  PrintImage(fname, depth_image);
  return;
}

void EnvObjectRecognition::PrintImage(string fname,
                                      const vector<unsigned short> &depth_image) {
  assert(depth_image.size() != 0);
  cv::Mat image(env_params_.img_height, env_params_.img_width, CV_8UC1);
  unsigned short max_depth = 0, min_depth = 20000;

  for (int ii = 0; ii < env_params_.img_height; ++ii) {
    for (int jj = 0; jj < env_params_.img_width; ++jj) {
      int idx = ii * env_params_.img_width + jj;

      if (observed_depth_image_[idx] == 20000) {
        continue;
      }

      if (max_depth < observed_depth_image_[idx]) {
        max_depth = observed_depth_image_[idx];
      }

      if (min_depth > observed_depth_image_[idx]) {
        min_depth = observed_depth_image_[idx];
      }
    }
  }

  // for (int ii = 0; ii < env_params_.img_height; ++ii) {
  //   for (int jj = 0; jj < env_params_.img_width; ++jj) {
  //     int idx = ii * env_params_.img_width + jj;
  //
  //     if (depth_image[idx] == 20000) {
  //       continue;
  //     }
  //
  //     if (max_depth < depth_image[idx]) {
  //       max_depth = depth_image[idx];
  //     }
  //
  //     if (min_depth > depth_image[idx]) {
  //       min_depth = depth_image[idx];
  //     }
  //   }
  // }

  // ROS_INFO("Observed Image: Min z: %d, Max z: %d", min_depth, max_depth);

  // max_depth = 12000;
  // min_depth = 5000;

  const double range = double(max_depth - min_depth);

  for (int ii = 0; ii < env_params_.img_height; ++ii) {
    for (int jj = 0; jj < env_params_.img_width; ++jj) {
      int idx = ii * env_params_.img_width + jj;

      if (depth_image[idx] > max_depth || depth_image[idx] == 20000) {
        image.at<uchar>(ii, jj) = 0;
      } else if (depth_image[idx] < min_depth) {
        image.at<uchar>(ii, jj) = 255;
      } else {
        image.at<uchar>(ii, jj) = static_cast<uchar>(255.0 - double(
                                                       depth_image[idx] - min_depth) * 255.0 / range);
      }
    }
  }

  cv::Mat c_image;
  cv::applyColorMap(image, c_image, cv::COLORMAP_JET);
  cv::imwrite(fname.c_str(), c_image);
  //http://docs.opencv.org/modules/contrib/doc/facerec/colormaps.html
}

bool EnvObjectRecognition::IsGoalState(State state) {
  if (static_cast<int>(state.object_ids.size()) ==  env_params_.num_objects) {
    return true;
  }

  return false;
}


const float *EnvObjectRecognition::GetDepthImage(State s,
                                                 vector<unsigned short> *depth_image) {
  if (scene_ == NULL) {
    ROS_ERROR("Scene is not set");
  }

  scene_->clear();

  assert(s.object_ids.size() == s.object_poses.size());

  for (size_t ii = 0; ii < s.object_ids.size(); ++ii) {
    ObjectModel obj_model = obj_models_[s.object_ids[ii]];
    pcl::PolygonMesh::Ptr cloud (new pcl::PolygonMesh (
                                   obj_model.mesh()));
    Pose p = s.object_poses[ii];

    Eigen::Matrix4f transform;
    transform <<
              cos(p.theta), -sin(p.theta) , 0, p.x,
                  sin(p.theta) , cos(p.theta) , 0, p.y,
                  0, 0 , 1 , env_params_.table_height,
                  0, 0 , 0 , 1;
    TransformPolyMesh(cloud, cloud, transform);

    PolygonMeshModel::Ptr model = PolygonMeshModel::Ptr (new PolygonMeshModel (
                                                           GL_POLYGON, cloud));
    scene_->add (model);
  }

  kinect_simulator_->doSim(env_params_.camera_pose);
  const float *depth_buffer = kinect_simulator_->rl_->getDepthBuffer();
  kinect_simulator_->get_depth_image_uint(depth_buffer, depth_image);
  return depth_buffer;
};


void EnvObjectRecognition::TransformPolyMesh(const pcl::PolygonMesh::Ptr
                                             mesh_in, pcl::PolygonMesh::Ptr mesh_out, Eigen::Matrix4f transform) {
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_in (new
                                                pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_out (new
                                                 pcl::PointCloud<pcl::PointXYZ>);
  //   pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_in_xyz (new
  //                                                     pcl::PointCloud<pcl::PointXYZ>);
  //
  //   pcl::fromPCLPointCloud2(mesh->cloud, *cloud_in_xyz);
  //   copyPointCloud(*cloud_in_xyz, *cloud_in);
  pcl::fromPCLPointCloud2(mesh_in->cloud, *cloud_in);

  transformPointCloud(*cloud_in, *cloud_out, transform);

  *mesh_out = *mesh_in;
  pcl::toPCLPointCloud2(*cloud_out, mesh_out->cloud);
  return;
}


void EnvObjectRecognition::PreprocessModel(const pcl::PolygonMesh::Ptr mesh_in,
                                           pcl::PolygonMesh::Ptr mesh_out) {
  pcl::PointCloud<PointT>::Ptr cloud_in (new
                                         pcl::PointCloud<PointT>);
  pcl::PointCloud<PointT>::Ptr cloud_out (new
                                          pcl::PointCloud<PointT>);
  pcl::fromPCLPointCloud2(mesh_in->cloud, *cloud_in);

  PointT min_pt, max_pt;
  pcl::getMinMax3D(*cloud_in, min_pt, max_pt);
  // Shift bottom most points to 0-z coordinate
  Eigen::Matrix4f transform;
  transform << 1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, -min_pt.z,
            0, 0 , 0, 1;

  transformPointCloud(*cloud_in, *cloud_out, transform);

  *mesh_out = *mesh_in;
  pcl::toPCLPointCloud2(*cloud_out, mesh_out->cloud);
  return;
}

void EnvObjectRecognition::SetScene() {
  if (scene_ == NULL) {
    ROS_ERROR("Scene is not set");
  }

  for (int ii = 0; ii < env_params_.num_models; ++ii) {
    ObjectModel obj_model = obj_models_[ii];
    pcl::PolygonMesh::Ptr cloud (new pcl::PolygonMesh (
                                   obj_model.mesh()));

    PolygonMeshModel::Ptr model = PolygonMeshModel::Ptr (new PolygonMeshModel (
                                                           GL_POLYGON, cloud));
    scene_->add (model);
  }
}


// A 'halo' camera - a circular ring of poses all pointing at a center point
// @param: focus_center: the center points
// @param: halo_r: radius of the ring
// @param: halo_dz: elevation of the camera above/below focus_center's z value
// @param: n_poses: number of generated poses
void EnvObjectRecognition::GenerateHalo(
  std::vector<Eigen::Isometry3d, Eigen::aligned_allocator<Eigen::Isometry3d>>
  &poses, Eigen::Vector3d focus_center, double halo_r, double halo_dz,
  int n_poses) {

  for (double t = 0; t < (2 * M_PI); t = t + (2 * M_PI) / ((double) n_poses) ) {
    double x = halo_r * cos(t);
    double y = halo_r * sin(t);
    double z = halo_dz;
    double pitch = atan2( halo_dz, halo_r);
    double yaw = atan2(-y, -x);

    Eigen::Isometry3d pose;
    pose.setIdentity();
    Eigen::Matrix3d m;
    m = AngleAxisd(yaw, Eigen::Vector3d::UnitZ())
        * AngleAxisd(pitch, Eigen::Vector3d::UnitY())
        * AngleAxisd(0, Eigen::Vector3d::UnitZ());

    pose *= m;
    Vector3d v(x, y, z);
    v += focus_center;
    pose.translation() = v;
    poses.push_back(pose);
  }

  return ;
}

void EnvObjectRecognition::SetCameraPose(Eigen::Isometry3d camera_pose) {
  env_params_.camera_pose = camera_pose;
  cam_to_world_ = camera_pose;
  return;
}

void EnvObjectRecognition::SetTableHeight(double height) {
  env_params_.table_height = height;
}


void EnvObjectRecognition::SetBounds(double x_min, double x_max, double y_min,
                                     double y_max) {
  env_params_.x_min = x_min;
  env_params_.x_max = x_max;
  env_params_.y_min = y_min;
  env_params_.y_max = y_max;
}


void EnvObjectRecognition::PrecomputeHeuristics() {
  ROS_INFO("Precomputing heuristics.........");
  State greedy_state = ComputeGreedyICPPoses();
  ROS_INFO("Finished precomputing heuristics");
}

void EnvObjectRecognition::SetObservation(int num_objects,
                                          const vector<unsigned short> observed_depth_image,
                                          const PointCloudPtr observed_organized_cloud) {
  observed_depth_image_.clear();
  observed_depth_image_ = observed_depth_image;
  env_params_.num_objects = num_objects;

  const int num_pixels = env_params_.img_width * env_params_.img_height;
  // Compute the range in observed image
  unsigned short observed_min_depth = 20000;
  unsigned short observed_max_depth = 0;

  for (int ii = 0; ii < num_pixels; ++ii) {
    if (observed_depth_image_[ii] < observed_min_depth) {
      observed_min_depth = observed_depth_image_[ii];
    }

    if (observed_depth_image_[ii] != 20000 &&
        observed_depth_image_[ii] > observed_max_depth) {
      observed_max_depth = observed_depth_image_[ii];
    }
  }

  env_params_.observed_max_range = observed_max_depth;
  env_params_.observed_max_range = static_cast<unsigned short>(20000);
  env_params_.observed_min_range = observed_min_depth;


  *observed_cloud_  = *observed_organized_cloud;
  *observed_organized_cloud_  = *observed_organized_cloud;
  downsampled_observed_cloud_ = DownsamplePointCloud(observed_cloud_);


  empty_range_image_.setDepthImage(&observed_depth_image_[0],
                                   env_params_.img_width, env_params_.img_height, 321.06398107f, 242.97676897f,
                                   576.09757860f, 576.09757860f);

  knn.reset(new pcl::search::KdTree<PointT>(true));
  knn->setInputCloud(observed_cloud_);

  std::stringstream ss;
  ss.precision(20);
  ss << kDebugDir + "obs_cloud" << ".pcd";
  pcl::PCDWriter writer;
  writer.writeBinary (ss.str()  , *observed_cloud_);
  PrintImage(kDebugDir + string("ground_truth.png"), observed_depth_image_);
}

void EnvObjectRecognition::SetObservation(int num_objects,
                                          const unsigned short *observed_depth_image) {
  const int num_pixels = env_params_.img_width * env_params_.img_height;
  observed_depth_image_.clear();
  observed_depth_image_.resize(num_pixels);

  for (int ii = 0; ii < num_pixels; ++ii) {
    observed_depth_image_[ii] = observed_depth_image[ii];
  }

  env_params_.num_objects = num_objects;
}

void EnvObjectRecognition::SetObservation(vector<int> object_ids,
                                          vector<Pose> object_poses) {
  assert(object_ids.size() == object_poses.size());

  State s;

  for (size_t ii = 0; ii < object_ids.size(); ++ii) {
    if (object_ids[ii] >= env_params_.num_models) {
      ROS_ERROR("Invalid object ID %d with (%d) when setting ground truth", object_ids[ii],
                      env_params_.num_models);
    }

    s.object_ids.push_back(object_ids[ii]);
    s.object_poses.push_back(object_poses[ii]);
  }

  env_params_.num_objects = object_ids.size();
  vector<unsigned short> depth_image;
  const float *depth_buffer = GetDepthImage(s, &observed_depth_image_);
  const int num_pixels = env_params_.img_width * env_params_.img_height;


  // Compute the range in observed image
  unsigned short observed_min_depth = 20000;
  unsigned short observed_max_depth = 0;

  for (int ii = 0; ii < num_pixels; ++ii) {
    if (observed_depth_image_[ii] < observed_min_depth) {
      observed_min_depth = observed_depth_image_[ii];
    }

    if (observed_depth_image_[ii] != 20000 &&
        observed_depth_image_[ii] > observed_max_depth) {
      observed_max_depth = observed_depth_image_[ii];
    }
  }

  env_params_.observed_max_range = observed_max_depth;
  // env_params_.observed_max_range = std::max(static_cast<unsigned short>(15000),
  //                                           observed_max_depth);
  // env_params_.observed_max_range = static_cast<unsigned short>(double(observed_max_depth + observed_min_depth)/2.0);

  env_params_.observed_max_range = static_cast<unsigned short>(20000);
  env_params_.observed_min_range = observed_min_depth;


  kinect_simulator_->rl_->getOrganizedPointCloud (observed_organized_cloud_,
                                                  true,
                                                  env_params_.camera_pose);
  // kinect_simulator_->rl_->getPointCloud (observed_cloud_, true,
  //                                                 kinect_simulator_->camera_->getPose ());
  kinect_simulator_->rl_->getPointCloud (observed_cloud_, true,
                                         env_params_.camera_pose); //GLOBAL
  downsampled_observed_cloud_ = DownsamplePointCloud(observed_cloud_);


  empty_range_image_.setDepthImage(&observed_depth_image_[0],
                                   env_params_.img_width, env_params_.img_height, 321.06398107f, 242.97676897f,
                                   576.09757860f, 576.09757860f);

  // knn.reset(new pcl::search::OrganizedNeighbor<PointT>(true, 1e-4));
  knn.reset(new pcl::search::KdTree<PointT>(true));
  knn->setInputCloud(observed_cloud_);

  if (id == 0) {
    std::stringstream ss;
    ss.precision(20);
    ss << kDebugDir + "obs_cloud" << ".pcd";
    pcl::PCDWriter writer;
    writer.writeBinary (ss.str()  , *observed_cloud_);
    PrintImage(kDebugDir + string("ground_truth.png"), observed_depth_image_);
  }
}

double EnvObjectRecognition::GetICPAdjustedPose(const PointCloudPtr cloud_in,
                                                const Pose &pose_in, PointCloudPtr cloud_out, Pose *pose_out) {
  *pose_out = pose_in;


  pcl::IterativeClosestPointNonLinear<PointT, PointT> icp;

  int num_points_original = cloud_in->points.size();

  // if (cloud_in->points.size() > 2000) { //TODO: Fix it
  if (false) {
    PointCloudPtr cloud_in_downsampled = DownsamplePointCloud(cloud_in);
    icp.setInputCloud(cloud_in_downsampled);
  } else {
    icp.setInputCloud(cloud_in);
  }

  icp.setInputTarget(downsampled_observed_cloud_);
  // icp.setInputTarget(observed_cloud_);

  pcl::registration::TransformationEstimation2D<PointT, PointT>::Ptr est;
  est.reset(new pcl::registration::TransformationEstimation2D<PointT, PointT>);
  // pcl::registration::TransformationEstimationSVD<PointT, PointT>::Ptr est;
  // est.reset(new pcl::registration::TransformationEstimationSVD<PointT, PointT>);
  icp.setTransformationEstimation(est);

  /*
  boost::shared_ptr<pcl::registration::WarpPointRigid3D<PointT, PointT> > warp_fcn
          (new pcl::registration::WarpPointRigid3D<PointT, PointT>);

      // Create a TransformationEstimationLM object, and set the warp to it
           boost::shared_ptr<pcl::registration::TransformationEstimationLM<PointT, PointT> > te (new
           pcl::registration::TransformationEstimationLM<PointT, PointT>);
               te->setWarpFunction (warp_fcn);
  icp.setTransformationEstimation(te);
  */

  // Set the max correspondence distance to 5cm (e.g., correspondences with higher distances will be ignored)
  icp.setMaxCorrespondenceDistance (env_params_.res / 2); //TODO: properly
  // icp.setMaxCorrespondenceDistance (0.5); //TODO: properly
  // Set the maximum number of iterations (criterion 1)
  icp.setMaximumIterations (50);
  // Set the transformation epsilon (criterion 2)
  // icp.setTransformationEpsilon (1e-8);
  // Set the euclidean distance difference epsilon (criterion 3)
  icp.setEuclideanFitnessEpsilon (1e-5);

  icp.align(*cloud_out);
  double score = 100.0;//TODO

  if (icp.hasConverged()) {
    score = icp.getFitnessScore();
    // std::cout << "has converged:" << icp.hasConverged() << " score: " <<
    //           score << std::endl;
    // std::cout << icp.getFinalTransformation() << std::endl;
    Eigen::Matrix4f transformation = icp.getFinalTransformation();
    Eigen::Vector4f vec_in, vec_out;
    vec_in << pose_in.x, pose_in.y, env_params_.table_height, 1.0;
    vec_out = transformation * vec_in;
    double yaw = atan2(transformation(1, 0), transformation(0, 0));
    (*pose_out).x = vec_out[0];
    (*pose_out).y = vec_out[1];

    double yaw1 = pose_in.theta;
    double yaw2 = yaw;
    double cos_term = cos(yaw1) * cos(yaw2) - sin(yaw1) * sin(yaw2);
    double sin_term = sin(yaw1) * cos(yaw2) + cos(yaw1) * sin(yaw2);
    double total_yaw = atan2(sin_term, cos_term);

    // (*pose_out).theta = WrapAngle(pose_in.theta + yaw);
    (*pose_out).theta = total_yaw;
    // printf("Old yaw: %f, New yaw: %f\n", pose_in.theta, pose_out->theta);
    // printf("Old xy: %f %f, New xy: %f %f\n", pose_in.x, pose_in.y, pose_out->x, pose_out->y);


    // std::stringstream ss1, ss2;
    // ss1.precision(20);
    // ss2.precision(20);
    // ss1 << "sim_cloud_" << i << ".pcd";
    // ss2 << "sim_cloud_aligned_" << i << ".pcd";
    // pcl::PCDWriter writer;
    // writer.writeBinary (ss1.str()  , *cloud);
    // writer.writeBinary (ss2.str()  , aligned_cloud);
  }

  return score;
}

double EnvObjectRecognition::ComputeScore(const PointCloudPtr cloud) {
  pcl::IterativeClosestPointNonLinear<PointT, PointT> icp;
  icp.setInputCloud(cloud);
  icp.setInputTarget(observed_cloud_);
  pcl::PointCloud<PointT> aligned_cloud;

  //pcl::registration::TransformationEstimation2D<PointT, PointT>::Ptr est;
  //est.reset(new pcl::registration::TransformationEstimation2D<PointT, PointT>);
  pcl::registration::TransformationEstimationSVD<PointT, PointT>::Ptr est;
  est.reset(new pcl::registration::TransformationEstimationSVD<PointT, PointT>);
  icp.setTransformationEstimation(est);

  /*
  boost::shared_ptr<pcl::registration::WarpPointRigid3D<PointT, PointT> > warp_fcn
          (new pcl::registration::WarpPointRigid3D<PointT, PointT>);

      // Create a TransformationEstimationLM object, and set the warp to it
           boost::shared_ptr<pcl::registration::TransformationEstimationLM<PointT, PointT> > te (new
           pcl::registration::TransformationEstimationLM<PointT, PointT>);
               te->setWarpFunction (warp_fcn);
  icp.setTransformationEstimation(te);
  */

  // Set the max correspondence distance to 5cm (e.g., correspondences with higher distances will be ignored)
  icp.setMaxCorrespondenceDistance (env_params_.res * 2);
  // Set the maximum number of iterations (criterion 1)
  icp.setMaximumIterations (50);
  // Set the transformation epsilon (criterion 2)
  // icp.setTransformationEpsilon (1e-8);
  // Set the euclidean distance difference epsilon (criterion 3)
  icp.setEuclideanFitnessEpsilon (1e-5);

  icp.align(aligned_cloud);
  double score = icp.getFitnessScore();

  /*
  if (icp.hasConverged()) {
    std::cout << "has converged:" << icp.hasConverged() << " score: " <<
              score << std::endl;
    std::cout << icp.getFinalTransformation() << std::endl;

    std::stringstream ss1, ss2;
    ss1.precision(20);
    ss2.precision(20);
    ss1 << "sim_cloud_" << i << ".pcd";
    ss2 << "sim_cloud_aligned_" << i << ".pcd";
    pcl::PCDWriter writer;
    writer.writeBinary (ss1.str()  , *cloud);
    writer.writeBinary (ss2.str()  , aligned_cloud);
  }
  */

  return score;
}

void EnvObjectRecognition::WriteSimOutput(string fname_root) {
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr pc_out (new
                                                 pcl::PointCloud<pcl::PointXYZRGB>);
  bool write_cloud = true;
  bool demo_other_stuff = true;

  if (write_cloud) {
    // Read Color Buffer from the GPU before creating PointCloud:
    // By default the buffers are not read back from the GPU
    kinect_simulator_->rl_->getColorBuffer ();
    kinect_simulator_->rl_->getDepthBuffer ();
    // Add noise directly to the CPU depth buffer
    kinect_simulator_->rl_->addNoise ();

    // Optional argument to save point cloud in global frame:
    // Save camera relative:
    //kinect_simulator_->rl_->getPointCloud(pc_out);
    // Save in global frame - applying the camera frame:
    //kinect_simulator_->rl_->getPointCloud(pc_out,true,kinect_simulator_->camera_->getPose());
    // Save in local frame
    kinect_simulator_->rl_->getPointCloud (pc_out, false,
                                           kinect_simulator_->camera_->getPose ());
    // TODO: what to do when there are more than one simulated view?

    if (pc_out->points.size() > 0) {
      //std::cout << pc_out->points.size() << " points written to file\n";

      pcl::PCDWriter writer;
      //writer.write ( string (fname_root + ".pcd"), *pc_out,	false);  /// ASCII
      writer.writeBinary (  string (fname_root + ".pcd")  , *pc_out);
      //cout << "finished writing file\n";
    } else {
      std::cout << pc_out->points.size() << " points in cloud, not written\n";
    }
  }

  if (demo_other_stuff && write_cloud) {
    //kinect_simulator_->write_score_image (kinect_simulator_->rl_->getScoreBuffer (),
    //   		   string (fname_root + "_score.png") );
    kinect_simulator_->write_rgb_image (kinect_simulator_->rl_->getColorBuffer (),
                                        string (fname_root + "_rgb.png") );
    kinect_simulator_->write_depth_image (
      kinect_simulator_->rl_->getDepthBuffer (),
      string (fname_root + "_depth.png") );
    //kinect_simulator_->write_depth_image_uint (kinect_simulator_->rl_->getDepthBuffer (),
    //                                string (fname_root + "_depth_uint.png") );

    // Demo interacton with RangeImage:
    pcl::RangeImagePlanar rangeImage;
    kinect_simulator_->rl_->getRangeImagePlanar (rangeImage);
  }
}

int EnvObjectRecognition::StateToStateID(State &s) {

  // If state has already been created, return ID from hash map
  for (auto it = StateMap.begin(); it != StateMap.end(); ++it) {
    if (StatesEqual(s, it->second)) {
      return it->first;
    }
  }

  // Otherwise, create state, add to hash map, and return ID
  int new_id = int(StateMap.size());
  StateMap[new_id] = s;
  return new_id;
}

State EnvObjectRecognition::StateIDToState(int state_id) {
  auto it = StateMap.find(state_id);

  if (it != StateMap.end()) {
    return it->second;
  } else {
    ROS_ERROR("DModel: Error. Requested State ID does not exist. Will return empty state.\n");
  }

  State empty_state;
  return empty_state;
}


int EnvObjectRecognition::GetICPHeuristic(State s) {

  double heuristic = 0;
  int num_objects_assigned = env_params_.num_objects - s.object_ids.size();
  assert(num_objects_assigned <= env_params_.num_objects);

  for (int ii = 0; ii < env_params_.num_models; ++ii) {

    int object_id = sorted_greedy_icp_ids_[ii];


    // Skip object if it has already been assigned
    auto it = std::find(s.object_ids.begin(),
                        s.object_ids.end(),
                        object_id);

    if (it != s.object_ids.end()) {
      continue;
    }

    heuristic += sorted_greedy_icp_scores_[ii];
    num_objects_assigned += 1;

    if (num_objects_assigned == env_params_.num_objects) {
      break;
    }

  }

  return static_cast<int>(kICPCostMultiplier * heuristic);
}


// Feature-based and ICP Planners
State EnvObjectRecognition::ComputeGreedyICPPoses() {

  // We will slide the 'n' models in the database over the scene, and take the 'k' best matches.
  // The order of objects matters for collision checking--we will 'commit' to the best pose
  // for an object and disallow it for future objects.
  // ICP error is computed over full model (not just the non-occluded points)--this means that the
  // final score is always an upper bound

  vector<double> icp_scores; //smaller, the better
  vector<Pose> icp_adjusted_poses;
  // icp_scores.resize(env_params_.num_models, numeric_limits<double>::max());
  icp_scores.resize(env_params_.num_models, 100.0);
  icp_adjusted_poses.resize(env_params_.num_models);



  int succ_id = 0;
  State empty_state;
  State committed_state;

  for (int ii = 0; ii < env_params_.num_models; ++ii) {
    for (double x = env_params_.x_min; x <= env_params_.x_max;
         x += env_params_.res) {
      for (double y = env_params_.y_min; y <= env_params_.y_max;
           y += env_params_.res) {
        for (double theta = 0; theta < 2 * M_PI; theta += env_params_.theta_res) {
          Pose p_in(x, y, theta);
          Pose p_out = p_in;

          State succ_state;
          succ_state.object_ids.push_back(ii);
          succ_state.object_poses.push_back(p_in);

          // if (!IsValidPose(committed_state, ii, p_in)) {
          //   continue;
          // }

          // pcl::PolygonMesh::Ptr mesh (new pcl::PolygonMesh (
          //                               obj_models_[ii].mesh()));
          // PointCloudPtr cloud_in(new PointCloud);
          // PointCloudPtr cloud_out(new PointCloud);
          // PointCloudPtr cloud_aligned(new PointCloud);
          // pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_in_xyz (new
          //                                                   pcl::PointCloud<pcl::PointXYZ>);
          //
          // pcl::fromPCLPointCloud2(mesh->cloud, *cloud_in_xyz);
          // copyPointCloud(*cloud_in_xyz, *cloud_in);
          //
          // Eigen::Matrix4f transform;
          // transform <<
          //           cos(p_in.theta), -sin(p_in.theta) , 0, p_in.x,
          //               sin(p_in.theta) , cos(p_in.theta) , 0, p_in.y,
          //               0, 0 , 1 , env_params_.table_height,
          //               0, 0 , 0 , 1;
          //
          //
          // transformPointCloud(*cloud_in, *cloud_out, transform);
          // double icp_fitness_score = GetICPAdjustedPose(cloud_out, p_in,
          //                                               cloud_aligned, &p_out);


          PointCloudPtr cloud_in(new PointCloud);
          PointCloudPtr cloud_out(new PointCloud);
          vector<unsigned short> succ_depth_image;
          const float *succ_depth_buffer;
          succ_depth_buffer = GetDepthImage(succ_state, &succ_depth_image);
          kinect_simulator_->rl_->getPointCloud (cloud_in, true,
                                                 env_params_.camera_pose);

          double icp_fitness_score = GetICPAdjustedPose(cloud_in, p_in, cloud_out,
                                                        &p_out);

          // Check *after* icp alignment
          if (!IsValidPose(committed_state, ii, p_out)) {
            continue;
          }


          // double icp_fitness_score = GetICPAdjustedPose(cloud_out, p_in,
          //                                               cloud_aligned, &p_out) / double(cloud_out->points.size());

          succ_state.object_poses[0] = p_out;

          if (image_debug_) {
            string fname = kDebugDir + "succ_" + to_string(succ_id) + ".png";
            PrintState(succ_state, fname);
            printf("%d: %f\n", succ_id, icp_fitness_score);
          }

          if (icp_fitness_score < icp_scores[ii]) {
            icp_scores[ii] = icp_fitness_score;
            icp_adjusted_poses[ii] = p_out;
          }

          succ_id++;

          // Skip multiple orientations for symmetric objects
          if (obj_models_[ii].symmetric()) {
            break;
          }

        }
      }
    }

    committed_state.object_ids.push_back(ii);
    committed_state.object_poses.push_back(icp_adjusted_poses[ii]);
  }


  vector<int> sorted_indices(env_params_.num_models);

  for (int ii = 0; ii < env_params_.num_models; ++ii) {
    sorted_indices[ii] = ii;
  }

  // sort indexes based on comparing values in icp_scores
  sort(sorted_indices.begin(), sorted_indices.end(),
  [&icp_scores](int idx1, int idx2) {
    return icp_scores[idx1] < icp_scores[idx2];
  });

  for (int ii = 0; ii < env_params_.num_models; ++ii) {
    ROS_INFO("ICP Score for Object %d: %f", ii, icp_scores[ii]);
  }

  ROS_INFO("Sorted scores:");

  for (int ii = 0; ii < env_params_.num_models; ++ii) {
    printf("%f ", icp_scores[sorted_indices[ii]]);
  }


  // Store for future use
  sorted_greedy_icp_ids_ = sorted_indices;
  sorted_greedy_icp_scores_.resize(env_params_.num_models);

  for (int ii = 0; ii < env_params_.num_models; ++ ii) {
    sorted_greedy_icp_scores_[ii] = icp_scores[sorted_indices[ii]];
  }


  // Take the first 'k'
  State greedy_state;

  for (int ii = 0; ii < env_params_.num_objects; ++ii) {
    int object_id = sorted_indices[ii];
    greedy_state.object_ids.push_back(object_id);
    greedy_state.object_poses.push_back(icp_adjusted_poses[object_id]);
  }

  string fname = kDebugDir + "greedy_state.png";
  PrintState(greedy_state, fname);
  return greedy_state;
}

State EnvObjectRecognition::ComputeVFHPoses() {
  vector<PointCloudPtr> cluster_clouds;
  DoEuclideanClustering(observed_cloud_, &cluster_clouds);
  const size_t num_clusters = cluster_clouds.size();

  for (size_t ii = 0; ii < num_clusters; ++ii) {
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud (new
                                               pcl::PointCloud<pcl::PointXYZ>);
    copyPointCloud(*cluster_clouds[ii], *cloud);

    // Eigen::Matrix4f world_to_cam = cam_to_world_.matrix().cast<float>();
    // Eigen::Vector4f centroid;
    // compute3DCentroid(*cloud, centroid);
    // demeanPointCloud(*cloud, centroid, *cloud);
    // Eigen::Matrix4f cam_to_world;
    // Eigen::Matrix4f transform;
    // transform <<  1,  0,  0, 0,
    //           0, -1,  0, 0,
    //           0,  0, -1, 0,
    //           0,  0,  0, 1;


    // transformPointCloud(*cloud, *cloud, transform);



    pcl::PCDWriter writer;
    stringstream ss;
    ss.precision(20);
    ss << kDebugDir + "cluster_" << ii << ".pcd";
    writer.writeBinary (ss.str()  , *cloud);

    float roll, pitch, yaw;
    vfh_pose_estimator_.getPose(cloud, roll, pitch, yaw, true);
    std::cout << roll << " " << pitch << " " << yaw << std::endl;
  }

  State vfh_state;
  return vfh_state;
}

void EnvObjectRecognition::SetDebugOptions(bool image_debug) {
  image_debug_ = image_debug;
}

